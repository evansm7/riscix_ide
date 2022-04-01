/* file: ecide.h */

/*
 * Header for expansion card IDE driver
 *
 * Derived from "Imaginary Expansion Card - Disc" which is:
 *
 * Copyright (C) 1989,1991 Acorn Computers Ltd.
 *
 * Remainder (C) ME
 */

#ifndef ECIDE_H
#define ECIDE_H

#ifdef DEBUG
#define DBG     printf
#else
#define DBG(x)  do { } while(0)
#endif

/* physical drive organisation */

#define D_SECSIZE       512

/* Define the ratio of standard device block size to our 
 * physical sector size.
 */
#define SECS_PER_BLK    (DEV_BSIZE/D_SECSIZE)

/*
 * For each card in the machine, add some raw I/O buffers.
 * RBUFS_PER_CARD gives a clearer definition of `some'.
 */
#define RBUFS_PER_CARD	2

/*
 * Maximum number of drives (2 per card) we will handle -
 * since the control structures are dynamically allocated,
 * there is very little space wasted in providing support
 * for the theoretical maximum of four cards.
 */
#define MAX_DRIVE       8
#define MAX_CARD        4

/* macros for handling minor device number */

#define CARDNO(mindev)  (((mindev) >> 4) & 3)
#define DRIVENO(mindev) (((mindev) >> 3) & 1)
#define PARTNO(mindev)  ((mindev) & 7)
#define MAX_PART        8

/*
 * The following gives the address of the disc controller registers
 * in expansion card memory space.  REGS_LOC is the byte offset of the
 * base of the register set, within the normal 8Kb expansion card 
 * address space for a given I/O cycle-speed and slot-number.
 * The ecide card is designed to be accessed with FAST I/O cycle
 * timings.
 */
#define REGS_LOC        0x3000
#define ECIDE_REGS(slot) \
    ((struct ecideregs *)(XCB_ADDRESS(FAST,slot) + REGS_LOC))

#define ECIDE_SLOT_REGS_BASE(slot)      XCB_ADDRESS(FAST, slot)

/*
 * Define macros to handle the 16-bit shifts
 * typical use would be -
 *
 *      write_reg (regs, wd_sector, 123);
 *
 *      status = read_reg(regs, wd_status);
 */
#define REG_ADDR(base, reg)             (volatile unsigned int *)((base)+((reg) << 2))
#define write_reg8(base, reg, value)    do { *(volatile unsigned char *)REG_ADDR(base, reg) = (value); } while(0)
#define read_reg8(base, reg)            (*(volatile unsigned char *)REG_ADDR(base, reg))
#define write_reg16(base, reg, value)   do { *REG_ADDR(base, reg) = (value) << 16; } while(0)
#define read_reg16(base, reg)           (*REG_ADDR(base, reg) & 0xffff)


#endif
