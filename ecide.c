/* ecide.c
 *
 * RISC iX block device driver for Expansion Card IDE
 *
 * ME, March/April 2022
 *
 * The structure is heavily inspired by Acorn's iecd.{c,h} examples, which are:
 *   Copyright (C) 1989,1991 Acorn Computers Ltd.
 *
 * The rest of the code is...
 *
 * Copyright (c) 2022 Matt Evans
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Matt's notes:
 *
 * Supporting multiple types of podule is achieved using several specialised
 * entry points that are invoked from XCB's podule probing, which in turn init
 * via a common point.
 *
 * FIXME/TODO:
 * - Support IRQs on interfaces that have IRQs (e.g. Castle)
 *   - This would include specialisations for card-specific masking
 * - Detect and support 8b interfaces
 * - Find a way to be initialised without a podule being probed, for A5000-like HW
 *
 * On the last point, I'd like a callback to probe for
 * A5000/A3020/A4000 onboard IDE.  This'll need some cleverness:
 *
 * - Detect a compatible machine type (might be best done from RISC OS &
 *   passed in bootparams)
 * - Find some way to get code executed on boot that is NOT via XCB callbacks,
 *   such as:
 *   - could binary-patch machdep_init to call init on the driver
 *   - could do some kind of "init/probe on first open()" business
 *   - could hijack ist_init (though beware that's only called for particular
 *     machine types, e.g. Arc)
 *
 *
 * Probing:
 * There are 4 possible concurrent IDE podules/cards (each of which could be a
 * different specific type).  This driver supports one IDE bus (of a primary +
 * secondary drive) per card.
 *
 * The block device's major number selects this driver; the minor number selects
 * partition/drive/card as follows:
 * - Part = minor[2:0]
 * - Drive = minor[3]
 * - Card = minor[4:5]
 *
 * The ide_t type represents a controller; this embeds 2x drive_info_t structs
 * that contain info about each drive, and each drive's partition table.
 *
 * Partitions:
 * The partitions are stored using IDE (512B) sector addressing, abstracted
 * away from whatever CHS/LBA cylinder size a drive might use.  ide_probe_partitions
 * will try to figure out which proprietary partition format a given controller
 * will be using.  For example, a true ADFS IDE drive will have only one ADFS
 * partition, followed by a RISCiX section.  But, a ZIDEFS drive will have 1-4 ZIDEFS
 * partitions (each of which is checked for a RISCiX section).
 * The partition probe code looks for *one* RISCiX section (containing up to 8 RISCiX
 * partitions) -- it stops looking once it finds a ZIDEFS part containing this, and
 * doesn't support multiple RISCiX sections per drive.  Got all that? ;'(
 *
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

#define PIO_POLLED yes

char *ecide_ident = "ecide IDE driver v0.1, (c) 2022 Matt Evans";

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
 *
 * ME: The registration of these symbols in conf.c has been disabled
 * because there won't be a 1:1 relationship between podules and this
 * code (i.e. it's only unused if all IDE podules don't match, which
 * I don't think the XCB manager can support).
 */

static char ecide_data_base[] = "ecide";
static int ecide_code_base() { return 1; }

/*
 * The following contains the actual number of cards present,
 * up to a limit of MAX_CARD - this is determined at boot 
 * time in the expansion card initialisation sequence. 
 * Cards are always numbered in the range 0..(n_card-1) from the XCB
 * manager's scan of the expansion card bus (which is done in
 * physical slot order).
 */

static int n_card;
static ide_host_t       ide_card[MAX_CARD];

/*
 * memory allocation routine
 */
extern caddr_t permalloc();

/* Scratch buffer for the various identification/partition probing */
static u8 *sector_scratch;

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

static void ecide_init_high(int slot, regs_t regs, host_type_t host_type)
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

        /* FIXME: use permalloc for ide_card, save about 1KB */
        ih = &ide_card[card];
        ih->slot = slot;
        ih->card_num = card;
        ih->regs = regs;
        ih->type = host_type;

        sector_scratch = (u8 *)permalloc(512);

        i = ide_init(ih, card, sector_scratch);   /* Probes presence for 2x drives underneath */

        if (i > 0) {
                printf("ecide%d: %d drive%s found, slot %d\n", card, i,
                       i > 1 ? "s" : "", slot);
        } else {
                printf("ecide%d, slot %d: no drives found\n", card, slot);
                return;
        }

        /* When the time comes for interrupts, register them as follows: */
#ifdef SUPPORT_IRQS /* Not defined! */
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

/* Probe entrypoint for ICS/IanS ZIDEFS podule:
 * No IRQs, ZIDEFS partitions
 */
void ecide_init_zidefs(int slot)
{
        regs_t ide_regs = (regs_t)(XCB_ADDRESS(FAST, slot) + 0x3000);

        ecide_init_high(slot, ide_regs, HOST_ZIDEFS);
}

/* Probe entrypoint for Castle IDE podule:
 * No IRQs yet (though TBC, hardware supports IRQs), ADFS partition.
 */
void ecide_init_castle(int slot)
{
        regs_t ide_regs = (regs_t)(XCB_ADDRESS(SYNC, slot) + 0x1000);

        write_reg8(XCB_ADDRESS(SYNC, slot) + 0x3000, 0, 0x00);  /* Disable IRQ (for now */

        ecide_init_high(slot, ide_regs, HOST_CASTLE);
}


int ecide_init_low(int slot, int irqs)
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
                        return 0;
                }
                for (i = 0; i < 2; i++) {
                        if (ih->drives[i].present)
                                ide_probe_partitions(ih, i, sector_scratch);
                }
                for (i = 0; i < 2; i++) {
                        if (ih->drives[i].present)
                                ide_dump_partitions(ih, i, sector_scratch);
                }
        }
        return 0;
}


/*
 * ecide_shutdown arranges that the ecide card in the given slot
 * is in a stable, non-interrupting state, as if it had been
 * given a hardware reset.  This is very easy.  Note that we can
 * be called even for cards which have failed their hardware test
 * on initialisation... beware.
 */
void ecide_shutdown (int slot)
{
        /* FIXME: We can't do a drive reset on all cards (e.g. ZIDEFS card) */
}


/* Main block device interface follows */

int ecide_open(dev_t dev, int flag)
{
        int mindev = minor(dev);
        int drive = DRIVENO(mindev);
        int card = CARDNO(mindev);
        ide_host_t *ih;
        drive_info_t *di;
        struct part *pt;

        /* Card & drive valid? */
        if (card >= n_card || !ide_card[card].drives[drive].present)
                return ENXIO;

        ih = &ide_card[card];
        di = &ih->drives[drive];

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

int ecide_close (dev_t dev, int flag)
{
        /* We don't need to do anything special here */
        return 0;
}

static void ecide_do_immediate(ide_host_t *ih, struct buf *bp)
{
        /* Transfer bp->b_bcount/DEV_BSIZE blocks from bp->b_blkno
         * (within partition given by bp->b_dev) to/from bp->b_un.b_addr
         */
        int mindev = minor(bp->b_dev);
        int drive = DRIVENO(mindev);
        drive_info_t *di;
        struct part *pt;
        unsigned int start_sector;
        unsigned int total_sectors;
        unsigned char *start_addr;
        unsigned int s;
        int r;

        di = &ih->drives[drive];
        pt = &di->d_part[PARTNO(mindev)];
        start_sector = (bp->b_blkno*SECS_PER_BLK) + pt->p_start;
        total_sectors = (bp->b_bcount / DEV_BSIZE)*SECS_PER_BLK;
        start_addr = (unsigned char *)bp->b_un.b_addr;
#ifdef SUPER_VERBOSE
        DBG("ecide_do_immediate(card %d, min %d, dr %d) start %d, total %d, %s\n",
            ih->card_num, mindev, drive, start_sector, total_sectors,
            bp->b_flags & B_READ ? "RD" : "WR");
#endif
        r = 0;
        s = 0;
        if (bp->b_flags & B_READ) {
                r = ide_read_some(ih, drive, start_sector, total_sectors, start_addr);
                if (r)
                        goto err;
        } else {
                r = ide_write_some(ih, drive, start_sector, total_sectors, start_addr);
                if (r)
                        goto err;
        }

        return;
err:
        DBG("ecide_do_immediate(card %d, min %d, dr %d) Transfer error %04x, sector %d\n",
            ih->card_num, mindev, drive, r, s);
        bp->b_flags |= B_ERROR;
        bp->b_error = EIO;
        bp->b_resid = bp->b_bcount - ((s - start_sector)*D_SECSIZE);
}

/*
 * ecide_strategy - where I/O requests are processed.
 */
int ecide_strategy(struct buf *bp)
{
        int mindev = minor(bp->b_dev);
        int drive = DRIVENO(mindev);
        int card = CARDNO(mindev);
        ide_host_t *ih;
        drive_info_t *di;
        struct part *pt;
        int nblks, s;

        /* Set up for the specific drive and partition involved */
        ih = &ide_card[card];
        di = &ih->drives[drive];
        pt = &di->d_part[PARTNO(mindev)];

        /*
         * We permit block size-multiple transfers only, starting on
         * a word boundary in memory.
         */
        if (((unsigned int)bp->b_bcount % DEV_BSIZE) != 0 ||
            ((long)bp->b_un.b_addr & (sizeof(int)-1)) != 0) {
                bp->b_flags |= B_ERROR; /* flag an error */
                bp->b_error = EINVAL;   /* set the error code in */
                bp->b_resid = bp->b_bcount; /* no data moved */
                biodone (bp);           /* pass buffer back to kernel control */
        }

        /* work out size of partition in system device block units */
        nblks = pt->p_size/SECS_PER_BLK;

        /* check for transfer outside partition bounds */
        if (bp->b_blkno + (bp->b_bcount / DEV_BSIZE) > nblks) {
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


#ifdef PIO_POLLED
        s = splbio();
        ecide_do_immediate(ih, bp);
        splx(s);
        biodone(bp);
#else
        /* When we support interrupts, the queue becomes useful! */

        /*
         * Everything seems OK - now queue and possibly start the transfer.
         *
         * First we ensure that queue manipulation is not messed up by
         * interrupts from the controller
         */
        s = splbio();

        ih->d_ioq.dq_qcnt++;         /* one more item in queue now */

        bp->av_forw = NULL;         /* clear forward link, for queuing */

        if (ih->d_ioq.dq_actf == NULL) {
                /* There was nothing happening: install buffer on empty Q */
                ih->d_ioq.dq_actf = bp;  /* sits at head of queue */
                ih->d_ioq.dq_actl = bp;  /* and also at tail */
                /*
                 * Start up the operation, since the controller must be
                 * idle; first set retries on operation to 0.
                 */
                ih->d_retries = 0;
                start_drive (ih);
        } else {
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
                ih->d_ioq.dq_actl->av_forw = bp;
                bp->av_back = ih->d_ioq.dq_actl;
                ih->d_ioq.dq_actl = bp;
        }
        splx (s);                   /* restore SPL */
#endif
        return 0;
}


/* ecide_size returns the size of the specified device */
int ecide_size (dev_t dev)
{
        int mindev = minor(dev);
        int drive = DRIVENO(mindev);
        int card = CARDNO(mindev);
        ide_host_t *ih;
        drive_info_t *di;
        int r;

        /* Set up for the specific drive and partition involved */
        ih = &ide_card[card];
        di = &ih->drives[drive];

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
        if (card >= n_card || !ide_card[card].drives[drive].present)
                r = -1;
        else
                r = di->d_part[PARTNO(mindev)].p_size/SECS_PER_BLK;
#ifdef SUPER_VERBOSE
        DBG("ecide_size(min %d/dr %d/c %d) = %d\n", mindev, drive, card, r);
#endif
        return r;
}


/* ecide_dump is the normal trivial implementation for now */
int ecide_dump (dev_t dev)
{
        return ENXIO;
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
int ecide_read (dev_t dev, struct uio *uio)
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
int ecide_write (dev_t dev, struct uio *uio)
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


int ecide_sectorsize (dev_t dev)
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
