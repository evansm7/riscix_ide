/* file: iecd.h */

/*
 * Header for Imaginary Expansion Card - Disc
 *
 * Copyright (C) 1989,1991 Acorn Computers Ltd.
 *
 */

/* Description of the imaginary expansion card disc (iecd) */

/* physical drive organisation */

#define D_SECTORS       56      /* device sectors per track */
#define D_HEADS         4       /* = tracks/cylinder */
#define D_CYLS          2310    /* total cylinders/drive */
#define D_SECSIZE       256     /* note: small sectors */

/* Define the ratio of standard device block size to our 
 * physical sector size.
 */
#define SECS_PER_BLK    (DEV_BSIZE/D_SECSIZE)

/*
 * Number of standard-sized (512-byte) blocks per track - note that 
 * there is an exact number of these, so no fiddling around with
 * odd 256-byte sectors is needed.
 */
#define BLKS_PER_TRACK  ((D_SECTORS*D_SECSIZE)/DEV_BSIZE)
/* and per complete cylinder */
#define BLKS_PER_CYL    (BLKS_PER_TRACK*D_HEADS)

/*
 * Definition of the registers on the imaginary expansion card disc
 * controller.  Note that because of the way ARM and the expansion
 * card bus hardware work we have to fiddle this a bit.  When we write
 * a 16-bit quantity to the controller, we actually write a whole word
 * (since ARM does not support half-word operations on memory), and
 * the data must be placed in the top half of the word written (the
 * low 16 bits are ignored). When reading from a 16-bit register we
 * must get the data out of the low 16-bits of the word we read.
 *
 * The ANSI C "volatile" qualifier is applied to all the fields to
 * ensure that the compiler doesn't pull any smart stunts over use of
 * the device registers (which otherwise it is quite entitled - and
 * likely - to do).
 */
struct iecdregs
{
    /*
     * The first word in the register block is dual-purpose: it is the
     * the control register if we are writing to it, or the status
     * register when we read from it.  For simplicity (although we
     * might think of using a union) we just use a macro to get the
     * two names.
     */
    volatile unsigned int c_control;
#define c_status c_control
    /*
     * The next register is for sending commands to the controller, and
     * is write-only.
     */
    volatile unsigned int c_command;
    /*
     * Then follows the param register (used for setting in such things
     * as cylinder number, head number, sector number/count, etc).  Again
     * this is write-only.
     */
    volatile unsigned int c_param;
    /*
     * Finally the data register - this is bidirectional - we write to
     * it data to be put onto the disc, and read from it data we have
     * requested from the disc.  It is also used when the controller is
     * telling us about error cases.
     */       
    volatile unsigned int c_data;
};

/*
 * The following gives the address of the disc controller registers
 * in expansion card memory space.  REGS_LOC is the byte offset of the
 * base of the register set, within the normal 8Kb expansion card 
 * address space for a given I/O cycle-speed and slot-number.
 * The iecd card is designed to be accessed with FAST I/O cycle
 * timings.
 */
#define REGS_LOC        0x800
#define IECD_REGS(slot) \
    ((struct iecdregs *)(XCB_ADDRESS(FAST,slot) + REGS_LOC))

/*
 * Define macros to handle the 16-bit shifts
 * typical use would be -
 *
 *      write_reg (regs->c_command, C_WRITE);
 *
 *      status = read_reg(regs->c_status);
 */
#define write_reg(reg, value)   reg = (value) << 16
#define read_reg(reg)           ((reg) & 0xFFFF)

/*
 * Definitions of commands, status etc. Much simplified from
 * the average real disc controller, and much more convenient!
 */

/* Bits in the control register */
#define CON_TWO_BUFF    (1 << 0) /* use internal double buffering */
#define CON_ECC         (1 << 1) /* do automatic ECC */
#define CON_FASTSEEK    (1 << 2) /* if set, use high-speed seek */
#define CON_HEADSWITCH  (1 << 3) /* do multi-head operations */
#define CON_AUTOSTEP    (1 << 4) /* do multi-cylinder ops */

/*
 * Command values written to the command reg (after all
 * parameters have been sent via the param reg).
 */
#define CMD_RESET       0x00    /* clears controller state */
#define CMD_READ        0x22    /* read sectors */
#define CMD_WRITE       0x24    /* write sectors */
#define CMD_SEEK        0x35    /* seek to cylinder */
#define CMD_TEST        0x41    /* controller internal test */

/* Bits in the status register */
#define STAT_BUSY       (1 << 0) /* executing a command */
#define STAT_CMDERR     (1 << 1) /* bad command */
#define STAT_PARMERR    (1 << 2) /* bad parameter to command */
#define STAT_DRVERR     (1 << 3) /* error from drive */
#define STAT_DATA       (1 << 4) /* ready for data transfer */
#define STAT_DONE       (1 << 5) /* command completed OK */

/* EOF iecd.h */
