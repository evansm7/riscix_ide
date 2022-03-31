/* file: ecide.c */

/*
 * Driver for Expansion Card IDE
 *
 * Based heavily on iecd.{c,h} which are:
 *
 * Copyright (C) 1989,1991 Acorn Computers Ltd.
 *
 * And ME!
 */


/*
 * The following set of #includes is typical for a block
 * device driver.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/file.h>
#include <sys/user.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/uio.h>

/* error logging stuff */
#include <sys/syslog.h>

/* busy-wait delay support */
#include <arm/delay.h>

/* includes defining the expansion bus and interrupt interfaces */
#include <arm/int_hndlr.h>
#include <arm/xcb.h>

/* Include general specs for device driver interfaces */
#include <arm/drivers.h>

/* finally the include file defining our own device */
#include "ecide.h"
#include "ecide_io.h"
#include "ecide_defs.h"

/*
 * Memory scavenging support.  If no expansion card for this device is
 * found at system boot time, then the XCB manager will attempt to
 * reclaim the memory (both code and data) used by this driver for
 * re-allocation via the permalloc() routine.  Scavenging is
 * controlled by declaring an *initialised* variable (initialised so
 * it is placed in the data segment rather than in bss), and a short
 * function that returns a TRUE/FALSE answer as to whether the driver
 * can be scavenged.  (Since the XCB manager will not bother trying to
 * scavenge if any cards are found, for simple XCB device drivers like
 * this one the function can always return TRUE.)  Further
 * declarations at the end of this file complete the definition of the
 * scavenging structure. 
 *
 * N.B. Any variables which are NOT to be scavenged (e.g. driver
 * configuration variables, potentially examined by other kernel code
 * or by system status programs via /dev/kmem) must appear either
 * before the {code,data}_start declarations, or after the
 * {code,data}_end declarations (see end of file).
 */

static char ecide_data_base[] = "ecide";
static int ecide_code_base() { return 1; }

/*
 * The following contains the actual number of drives present,
 * up to a limit of MAX_DRIVE - this is determined at boot 
 * time in the expansion card initialisation sequence. 
 * Drives are always numbered in the range 0..(n_drive-1),
 * in the order their controller cards were found in the XCB
 * manager's scan of the expansion card bus (which is done in
 * physical slot order).
 */
static int n_drive;

static struct drive
{
    /* flags for access to the drive */
    int d_flags;
    /* values of d_flags bits */
#define DF_OPENED       (1 << 0)
#define DF_OPENING      (1 << 1) /* open sequence interlocks */
#define DF_OPENWAIT     (1 << 2)
    int d_open_status;
    /*
     * partition table for a drive - this is read in on first
     * open of any partition of that drive.
     */
    struct part d_part[MAX_PART];
    unsigned char d_slot;       /* physical expansion card slot */
    unsigned char d_cmd;        /* operation drive is currently doing */
    long *d_mem;                /* memory address for data transfer */
    int d_sectors;              /* how many sectors remain to be done */
    int d_cur_cyl;              /* current drive head position */
    int d_next_cyl;             /* target drive head position (for seek) */
    int d_last_cyl;             /* head position after a data transfer */
    int d_retries;              /* counts error recovery attempts */
    struct devqueue d_ioq;      /* header record for I/O operations queue */
} *drive_info[MAX_DRIVE];

static int n_card;
static ide_host_t       ide_card[MAX_CARD];

#define MAX_RETRIES     5       /* number of attempts on an operation */

/*
 * Various forward references for static (i.e. local) functions 
 * used before they are defined.
 */
static int open_drive();
static void start_drive();
static void drive_int();

/*
 * memory allocation routine
 */
extern caddr_t permalloc();


/*
 * Free list of raw I/O buffers: initially empty, but
 * added to by ecide_init_hi for each card found.
 */
static struct buf *free_raw_buf = NULL;
static int need_raw_buf;

/*
 * Trivial functions to maintain raw I/O buf free list.
 * Note: these are only ever called in foreground
 * process context by ecide_{read,write}(), so there is
 * no need for splbio()/splx() protection: interrupts
 * are not involved (processes in kernel mode are not
 * pre-emptable).
 */
static struct buf *acquire_raw_buf() /* Defined in st506 driver? use that iff it matches? */
{
        struct buf *bp;
        while (free_raw_buf == NULL)
        {
                need_raw_buf = 1;
                sleep ((caddr_t)&need_raw_buf, PRIBIO);
        }
        bp = free_raw_buf;
        free_raw_buf = bp->av_forw;
        return bp;
}

static void release_raw_buf (struct buf *bp)
{
        /*
         * First process to release a buffer when there has
         * been a shortage will awake any process which was
         * in need.  If more than one, scheduling decides
         * who gets the buffer (i.e. who runs first after
         * being woken up).
         */
        if (free_raw_buf == NULL && need_raw_buf)
        {
                wakeup ((caddr_t)&need_raw_buf);
                need_raw_buf = 0;
        }
        bp->av_forw = free_raw_buf;
        free_raw_buf = bp;
}

/* Expansion Card Bus manager interface code */

void ecide_init_high(int slot)
{
        int card;
        int i;
        struct buf *rbp;
        ide_host_t *ih;

        /* Rules:
         * This is called with spl high, i.e. no IRQs.  Also no
         * sleep/wakeup, so busy-wait only.
         */

        /* An ecide card has been found in the specified slot */
        if (n_card == MAX_CARD) {
                printf("Ignoring ecide card in slot %d\n", slot);
                return;
        }
        card = n_card++;

        /* FIXME: use permalloc for ide_card */
        ih = &ide_card[card];
        ih->slot = slot;
        ih->card_num = card;
        ih->regs = (regs_t)ECIDE_SLOT_REGS_BASE(slot) + 0x3000 /* For ICS/IS/ZIDEFS podule! */;
        ih->type = HOST_ZIDEFS; /* FIXME: take from XCB probe type */

        i = ide_init(ih);   /* Probes presence for 2x drives underneath */

        /* ME card probing notes:
           - card should fill in struct drive for the card itself, and also the drives under it
           - 2 drives
           - separated by minor number innit
           - always use 2 minor (chunks) per card, even if drives not present
           - 8 chunks total (4 cards, 2 drives per card)
           So, 4x ide_t with 2x drives within
        */

        if (i > 0) {
                printf("ecide%d: %d drives found, slot %d\n", card, i, slot);
        } else {
                printf("ecide%d, slot %d: no drives found\n", card, slot);
                return;
        }

        /* When the time comes for interrupts, register them as follows: */
#if 0
        ih->d_ih.ih_fn = ecide_irq_handler;
        ih->d_ih.ih_farg = foo_argument;
        decl_xcb_interrupt(slot, &ih->d_ih, PRIO_BIO); /* Normal BIO priority */
#endif

        /*
         * As a final touch, allocate some raw I/O buffers: for each card
         * found, add some buffers into a local pool used for raw I/O.
         */
        rbp = (struct buf *)permalloc(sizeof (struct buf) * RBUFS_PER_CARD);
        for (i = 0; i < RBUFS_PER_CARD; ++i) {
                rbp->av_forw = free_raw_buf;
                free_raw_buf = rbp++;
        }
}

ecide_init_low(int slot, int irqs)
{
        /*
         * This is the low-priority initialisation routine - as it happens
         * we don't need to do anything here, but we might have chosen to
         * do things like read partition tables etc off the drive at this
         * point, rather than as part of the device open sequence.  Note
         * that the system is not yet running in full multi-processing
         * context and use of the sleep/wakeup mechanism is therefore
         * impossible, and fatal if tried!
         *
         * As documented in "xcb.h", this function will be called twice
         * for each slot containing an ecide device, once with XCB
         * interrupts disabled (marked by irqs == 0), and the second time
         * with them enabled (irqs == 1).  On the second call, therefore,
         * we could do some work in conjunction with our interrupt
         * handler, waiting in a polling loop in the main code, if this
         * were desired.
         */
        if (irqs) {
                int i;
                ide_host_t *ih = 0;

                /* Find interface for slot: */
                for (i = 0; i < n_card; i++) {
                        if (ide_card[i].slot == slot) {
                                ih = &ide_card[i];
                                break;
                        }
                }
                if (ih == 0) {
                        printf("ecide, slot %d: can't find interface!\n", slot);
                        return;
                }
                for (i = 0; i < 2; i++) {
                        if (ih->drives[i].present)
                                ide_probe_partitions(ih, i);
                }
                for (i = 0; i < 2; i++) {
                        if (ih->drives[i].present)
                                ide_dump_partitions(ih, i);
                }
        }
}


/*
 * ecide_shutdown arranges that the ecide card in the given slot
 * is in a stable, non-interrupting state, as if it had been
 * given a hardware reset.  This is very easy.  Note that we can
 * be called even for cards which have failed their hardware test
 * on initialisation... beware.
 */
ecide_shutdown (slot)
  int slot;
{
    /* The following should be sufficient in our case... */
/*    struct ecideregs *regs = ECIDE_REGS(slot); */
    /*
     * The controller reset command clears everything to 
     * a suitable state.
     */
}


/* Main block device interface follows */

int ecide_open (dev, flag)
  dev_t dev;
  int flag;
{
    int mindev = minor(dev);
    int drive = DRIVENO(mindev);
    struct drive *di;
    struct part *pt;

    /* check for valid drive number */
    if (drive >= n_drive)
        return ENXIO;

    /* address drive information record */
    di = drive_info[drive];

    /* check whether drive has been successfully opened */
    if (di->d_open_status != 0)
        return di->d_open_status; /* no - give up */

    /* address the relevant partition description */
    pt = &di->d_part[PARTNO(mindev)];

    /* a partition of size 0 is undefined and inaccessible */
    if (pt->p_size == 0)
        return ENXIO;

    /* check for read-only flag on partition */
    if ((flag & FWRITE) && pt->p_rdonly)
        return EROFS;

    /* all seems to be in order */
    return 0;
}

int ecide_close (dev, flag)
  dev_t dev;
  int flag;
{
    /* We don't need to do anything special here */
    return 0;
}


/*
 * ecide_strategy - where I/O requests are processed.
 */
ecide_strategy (bp)
  struct buf *bp;
{
    int mindev = minor(bp->b_dev);
    int drive = DRIVENO(mindev);
    struct drive *di;
    struct part *pt;
    int nblks, s;

    /* Set up for the specific drive and partition involved */
    di = drive_info[drive];
    pt = &di->d_part[PARTNO(mindev)];

    /*
     * We permit block size-multiple transfers only, starting on
     * a word boundary in memory.
     */
    if (((unsigned int)bp->b_bcount % DEV_BSIZE) != 0 ||
        ((long)bp->b_un.b_addr & (sizeof(int)-1)) != 0)
    {
        bp->b_flags |= B_ERROR; /* flag an error */
        bp->b_error = EINVAL;   /* set the error code in */
        bp->b_resid = bp->b_bcount; /* no data moved */
        biodone (bp);           /* pass buffer back to kernel control */
    }

    /* work out size of partition in system device block units */
    nblks = pt->p_size/SECS_PER_BLK;

    /* check for transfer outside partition bounds */
    if (bp->b_blkno + (bp->b_bcount / DEV_BSIZE) > nblks)
    {
        /*
         * don't complain too hard if exactly at end of partition
         * - this helps read-ahead handling.
         */
        if (bp->b_blkno != nblks)
        {
            /* quite unacceptable request */
            bp->b_flags |= B_ERROR;
            bp->b_error = ENXIO;        /* set the error code in */
        }
        bp->b_resid = bp->b_bcount; /* no data moved */
        biodone (bp);           /* pass buffer back to kernel control */
    }
    /*
     * Everything seems OK - now queue and possibly start the transfer.
     *
     * First we ensure that queue manipulation is not messed up by
     * interrupts from the controller
     */
    s = splbio();

    di->d_ioq.dq_qcnt++;         /* one more item in queue now */

    bp->av_forw = NULL;         /* clear forward link, for queuing */

    if (di->d_ioq.dq_actf == NULL)
    {
        /* There was nothing happening: install buffer on empty Q */
        di->d_ioq.dq_actf = bp;  /* sits at head of queue */
        di->d_ioq.dq_actl = bp;  /* and also at tail */
        /*
         * Start up the operation, since the controller must be
         * idle; first set retries on operation to 0.
         */
        di->d_retries = 0;
        start_drive (drive);
    }
    else
    {
        /*
	 * There is already at least one entry on the queue, which
	 * will be being processed at the moment.  For simplicity, we
	 * just place this request at the end of the current queue
	 * (first come first served ordering).  It might be useful at
	 * some stage to attempt some sorting on the queue entries.
	 * On the other hand, the Berkeley Fast File System does most
	 * of the work for us, meaning that the extra gain from
	 * sorting is likely to be small.
	 */
        di->d_ioq.dq_actl->av_forw = bp;
        bp->av_back = di->d_ioq.dq_actl;
        di->d_ioq.dq_actl = bp;
    }
    splx (s);                   /* restore SPL */
}


/* ecide_size returns the size of the specified device */
int ecide_size (dev_t dev)
{
    int mindev = minor(dev);
    int drive = DRIVENO(mindev);

    /*
     * Be very careful - under NFS 4.0 it is possible for the size
     * routine to be called before the device has been opened.  If
     * this is the case, the only option open is to return -1.
     *
     * If device opened, get the size from the appropriate partition
     * of the appropriate drive, these being determined from the minor
     * device number.  We return the size in units of DEV_BSIZE: the
     * macro BLKS_PER_CYL gives the number of these per cylinder on
     * our disc.
     */
    if (drive >= MAX_DRIVE || drive_info[drive] == (struct drive *)0)
            return(-1);
    else
            return (drive_info[drive]->d_part[PARTNO(mindev)].p_size/SECS_PER_BLK);
}


/* ecide_dump is the normal trivial implementation for now */
int ecide_dump (dev)
  dev_t dev;
{
    return ENXIO;
}


static void command_drive (di, bp)
  struct drive *di;
  struct buf *bp;
{
#if 0
    int mindev = minor(bp->b_dev);
    struct ecideregs *regs = di->d_regs;
    unsigned int start_block = (bp->b_blkno + 
                                (di->d_part[PARTNO(mindev)].p_start * 128
                                 /* BLKS_PER_CYL */));
    unsigned int start_cyl = start_block / 128/* BLKS_PER_CYL */;
    /*
     * We must be located on the right cylinder before we start a read
     * or write operation, however access to subsequent cylinders is
     * handled by the controller, since we have set the AUTOSTEP bit
     * in its control register...
     */
    {
        /* remember what we are up to, for the next interrupt */
        di->d_cmd = CMD_SEEK;
        /* and where we are going to... */
        di->d_next_cyl = start_cyl;
        /*
         * We're in the right place, set up for the data transfer.
         * For impenetrable reasons, we have to remind the 
         * controller which cylinder it's on, besides specifying
         * the head and sector to start with.  Thanks to the 
         * HEADSWITCH and AUTOSTEP functions (aren't Imaginary devices
         * wonderful!) which we always configure in the controller,
         * we don't have much else to worry about....
         */
        unsigned int cblock = start_block % BLKS_PER_CYL; /* block no in cylinder */
        unsigned int track = cblock / BLKS_PER_TRACK; /* which track to start on */
        /* compute start sector in physical device units */
        unsigned int sector = (cblock % BLKS_PER_TRACK) * SECS_PER_BLK;
        unsigned int blocks;
        /*
         * Note that all the parameters to the controller
         * are offset by one, as for seek.
         */
#if 0
        write_reg (regs->c_param, start_cyl+1); 
        /*
         * The head (track) and start sector get combined in the second 
         * 16-bit parameter.
         */
        write_reg (regs->c_param, (track+1) << 12 | (sector + 1));
#endif
        /*
         * Third and final parameter is the sector count, which we
         * compute from the transfer byte count.  We record this and
         * the next memory address in the drive structure, for use
         * on data traffic interrupts.
         */

        /* The byte count is already known to be a block-size multiple */
        blocks = bp->b_bcount / DEV_BSIZE;
        di->d_sectors = blocks * SECS_PER_BLK;

        /* And the memory address to be word aligned... */
        di->d_mem = (long *)bp->b_un.b_addr;

        /* Set the last parameter in place */
        write_reg (regs->c_param, di->d_sectors);

        /*
         * Now start it off by giving it the appropriate command, 
         * which we record for testing on the next interrupt.
         */
        di->d_cmd = (bp->b_flags & B_READ) ? CMD_READ : CMD_WRITE;
//        write_reg (regs->c_command, di->d_cmd);

        /*
         * Compute which cylinder the last block of the transfer
         * will be transferred to/from, so we can track where we
         * are.
         */
        di->d_last_cyl = (start_block + blocks - 1) / BLKS_PER_CYL;
    }
#endif
}
  
static void start_drive (drive)
  int drive;
{
    struct drive *di = drive_info[drive];
    struct buf *bp;
    /* see if there is anything on the queue to be processed */
    if ((bp = di->d_ioq.dq_actf) == NULL)
        return;                 /* nothing to do */
    command_drive (di, bp);
}

/*
 * drive_int is the function called when an ecide drive
 * controller card interrupts.  We have arranged (in 
 * ecide_init_hi()) that the argument passed in is the 
 * logical drive number concerned.
 */
static void drive_int (int drive) /* fixme: IRQ given controller number! */
{
    struct drive *di = drive_info[drive];
    struct buf *bp;
/*    struct ecideregs *regs = di->d_regs;
    int status;
*/

    /*
     * Get the head of queue, which is the transfer we are
     * processing at the moment.
     */
     
    if ((bp = di->d_ioq.dq_actf) == NULL)
    {
        /* eh? nothing going on - must be a spurious interrupt */
        log (LOG_NOTICE, "ecide%d: spurious int\n", drive);
        return;                                 /* just ignore it */
    }

    /*********************************************************************************/
#if 0
        
    /* first get drive status */
    status = read_reg (regs->c_status);

    /* check for errors */
    if (status & (STAT_DRVERR))
    {
        /*
         * Here we read the error code from the controller
         * and handle it appropriately.  If possible, we
         * retry the operation, unless the retry count has
         * reached its limit, when we log the error and
         * abandon the operation.
         */
        int code = read_reg (regs->c_data);
        if (++di->d_retries >= MAX_RETRIES)
        {
            log (LOG_ERR, "ecide%d: drive error %04x\n", drive, code);

            /* move on to the next item in the queue */
            di->d_ioq.dq_actf = bp->av_forw;
            di->d_retries = 0;  /* for next operation */
            di->d_ioq.dq_qcnt--; /* one less item in Q */

            /* flag the error */
            bp->b_flags |= B_ERROR;
            bp->b_error = EIO; 
#ifndef CALC_RESID
            /*
             * For simplicity, ignore what data may have been
             * successfully transferred (it might be tricky to work
             * out the right numbers) and say that nothing was moved.
             * This is normally quite acceptable.
             */
            bp->b_resid = bp->b_bcount;
#else   CALC_RESID
            /*
             * Attempt to compute how much data was transferred.  We
             * assume that for a read operation, everything the drive
             * actually fed us was valid data, hence the residual
             * count is just the data in the sectors not yet moved
             * (held in the drive record).  For a write, we assume
             * here that the error meant that the last sector we sent
             * to the drive was NOT successfully written to the disc
             * (since the controller always has to get a complete
             * physical sector from us before it can attempt to write
             * it to the disc) so the sectors count will be down by 
             * one.  It is possible for some error cases not to fit 
             * this pattern, but we won't attempt to cope with those 
             * for the moment.
             */
            if (di->d_cmd == CMD_WRITE)
                bp->b_resid = (di->d_sectors + 1) * D_SECSIZE;
            else if (di->d_cmd == CMD_READ)
                bp->b_resid = di->d_sectors * D_SECSIZE;
            else
                /*
                 * An error on the initial seek operation, we assume.
                 * No data will have moved at all; note that d_sectors
                 * will not have been computed, so we just use the
                 * original byte count.
                 */
                bp->b_resid = bp->b_bcount;
#endif CALC_RESID
            /* operations on this buffer now abandoned */
            biodone (bp);
            /* go do next item on queue, if any */
            start_drive (drive);
            return;
        }
        else
        {
            /*
             * Retry it - just calling command_drive is enough,
             * but first we set the "current cylinder" flag to
             * an impossible value, to force a re-seek attempt.
             */
            di->d_cur_cyl = -1; /* invalid */
            command_drive (di, bp);
            return;
        }
     }
     /* OK, no problem: now what was going on? */
     if (di->d_cmd == CMD_SEEK)
     { 
        /* got where we were going OK */
        di->d_cur_cyl = di->d_next_cyl;
        /* start data transfer operation */
        command_drive (di, bp);
     }
     else
     {
        /* must be read/write - check for data traffic */
        if (status == (STAT_BUSY|STAT_DATA))
        {
            /*
             * It wants a (256 byte) sector to be moved (in or
             * out): check which way we're going on this operation.
             *
             * The following code MIGHT go faster if we wrote it
             * in assembler, but there's not much in it.  If our
             * device allowed data transfers in several (half-)word
             * chunks at a time, via sequential I/O space addresses,
             * we would definitely want to go to assembler, in 
             * order to use ARM's ldm/stm instructions for maximum 
             * speed.
             *
             * First set up pointers for one sector in memory.
             * The d_mem field of the drive record tells us where to
             * start.  We also compute where we will end up, for a
             * convenient loop-termination test.
             */
            register long *data = di->d_mem;
            register long *limit = data + (D_SECSIZE/sizeof(int));
            /*
             * The following coding explicitly handles the 16 bit 
             * shifts normally treated by read/write_reg().
             */
            if (di->d_cmd == CMD_READ)
            {
                do
                {
		    /*
		     * Read in 32 bits per loop (as two half words).
		     * Note: to ensure the correct sequencing, the two
		     * reads are done using separate statements, in
		     * conjunction with the use of "volatile" in the
		     * definition of the device register structure
		     * (see ecide.h).
		     *
		     * If we had instead used something like:
                     *    *data++ = (regs->c_data & 0xFFFF) |
                     *               regs->c_data << 16;
		     * we would be likely to get garbage results.
		     * This is because C does not define the order in
		     * which the accesses of regs->c_data are done, or
		     * whether the register is read twice at all!
		     */
                    register long d;
                    /*
                     * First get low 16-bits (half-word) and mask the 
                     * undefined top half of the data. 
                     */
                    d = regs->c_data & 0xFFFF;
                    /*
                     * Then combine this with the high half-word shifted
                     * into place, and store it out. 
                     */
                    *data++ = d | regs->c_data << 16;
                } while (data < limit);
            }
            else 
            {
                /* must be a write op */
                do
                {
                    int word = *data++;
                    /*
                     * First write low half, shifted up as required
                     * for I/O space write operations. 
                     */
                    regs->c_data = word << 16;
                    /*
                     * Then write high half, which is in the right 
                     * place in the word already. 
                     */
                    regs->c_data = word;
                } while (data < limit);
            }
            /* update the memory pointer */
            di->d_mem = limit;
            --di->d_sectors;    /* count down how many remain */
        }
        else if (status == STAT_DONE)
        {
            /*
             * Command complete: move on to the next item 
             * in the queue.  First record where we have
             * now reached in terms of cylinders. 
             */
            di->d_cur_cyl = di->d_last_cyl;

            di->d_ioq.dq_actf = bp->av_forw;
            di->d_ioq.dq_qcnt--; /* one less item in Q */

            di->d_retries = 0;  /* for next operation */

            /* operations on this buffer now finished */
            biodone (bp);

            /* start processing next item on queue, if any */
            start_drive (drive);
        }
    }
#endif
     /*********************************************************************************/
}


static int open_drive (drive)
  int drive;
{
    /*
     * Code here should arrange to read and validate the partition 
     * table stored at a fixed position on the drive, and do any
     * one-time setting up of the drive.
     */
        /* FIXME: reset
         * FIXME: read partition table
         *      find RISC OS, find offset to RISCiX table
         * FIXME: somehow update disklabel from partition table
         */
}


/* The remaining code handles raw I/O */

/*
 * iocheck ensures that raw I/O transfer requests satisfy
 * certain constraints imposed by the hardware & software.
 */
static int iocheck (struct uio *uio)
{
    struct iovec *iov;
    int i;
    /*
     * Raw I/O transfer requests must start on a 512-byte boundary on
     * the logical disc, since physio() does its computations of block
     * number on this basis.  The uio_offset field of a uio structure
     * gives the byte position that the user process has got to on the
     * associated file-descriptor (by means of previous read, write or
     * lseek calls) - we use this as the byte offset from the start of
     * the partition.
     */
    if ((uio->uio_offset & (DEV_BSIZE-1)) != 0)
        return EINVAL;
    /*
     * Now, for each segment of the transfer (there may be more than
     * one, if a process uses the "writev" system call), check that it
     * is a multiple of 512 bytes in size (since we can only request
     * whole sectors from the disc) and starts in memory on a whole
     * word (integer, 4 bytes) boundary, since this is all that our
     * transfer code will cope with,
     */
    iov = uio->uio_iov; 
    for (i = uio->uio_iovcnt; i > 0; --i, ++iov)
        if ((iov->iov_len & (DEV_BSIZE-1)) != 0 ||
            ((int)iov->iov_base & ((sizeof(int))-1)) != 0)
            return EINVAL;
    /* No segment failed the test - we can proceed */
    return 0;
}


/*
 * ecide_read processes read requests coming through the
 * raw interface.  Most of the work is done for us by
 * physio().
 */
int ecide_read (dev, uio)
  dev_t dev;
  struct uio *uio;
{
    int  status;
    struct buf *rb;

    if ((status = iocheck (uio)) != 0)
        return status;

    /*
     * The routine physio() does all the main work for us, including
     * organising the user virtual memory to be read into or written
     * from, ensuring that it is all resident and contiguous in
     * memory.  It also breaks up large requests into pieces of a size
     * determined by the routine we pass to it (we use the standard
     * "minphys", and don't worry about the details).  It calls the
     * strategy routine we pass to it with the address of the buffer
     * which it has set up with the details of the transfer or each
     * part of it.  Physio waits for each part of the transfer to
     * complete (or an error to occur) before it starts the next part
     * or returns.  It returns a status value (0 -> success, else
     * error code), which we simply pass back to our caller.
     */
    rb = acquire_raw_buf ();
    status = physio (ecide_strategy, rb, dev, B_READ, minphys, uio);
    release_raw_buf (rb);

    return status;
}


/*
 * ecide_write is exactly like ecide_read with the exception of the
 * direction of transfer.
 */
int ecide_write (dev, uio)
  dev_t dev;
  struct uio *uio;
{
    int  status;
    struct buf *rb;

    if ((status = iocheck (uio)) != 0)
        return status;

    rb = acquire_raw_buf ();
    status = physio (ecide_strategy, rb, dev, B_WRITE, minphys, uio);
    release_raw_buf (rb);

    return status;
}


int ecide_sectorsize (dev)
  dev_t dev;
{
    /* For simplicity, we use DEV_BSIZE as our minimum sector size */
    return DEV_BSIZE;
}

/*
 * Memory scavenging support.  See the comments in <sys/conf.h> for
 * further information.
 */
static char ecide_data_end[] = { 1 };
static int ecide_code_end() {  }

struct scavenge ecide_scavenge =
{
    ecide_code_base, ecide_code_end,
    ecide_data_base, ecide_data_end,
    ecide_open, ecide_open,
    NULL
};

/* EOF ecide.c */


/*
  Matt misc notes:

  Need to support multiple types of podule, so in xcbconf.c match vendor/product codes and
  invoke multiple _init_high/low() entry points, specialising on:
  - IDE register base addresses
  - IRQ mask/unmask function pointers
  - Presence of IRQs at all!
  - 8b/16b data transfer width, and access method

  Also, want to get a callback to probe for A5000/A3020/A4000 onboard IDE.  This'll need some
  cleverness:
  - Detect a compatible machine type (might be best done from RISC OS & passed in bootparams)
  - Find some way to get code executed on boot that is NOT via XCB callbacks
   -- could binary-patch machdep_init to call init on the driver
   -- could do some kind of obscene "init on open()" business
   -- could hijack ist_init (though that's only called for particular machine types, e.g. Arc)

 */
