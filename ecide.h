/*
 * RISC iX expansion card IDE driver
 *
 * Some commends and #defines are derived from "Imaginary Expansion Card - Disc"
 * which is:
 *
 * Copyright (C) 1989,1991 Acorn Computers Ltd.
 *
 * For the remainder,
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

#ifndef ECIDE_H
#define ECIDE_H

#ifdef _KERNEL
#include <arm/int_hndlr.h>
#include <sys/buf.h>
#endif

#ifdef DEBUG
#define DBG     printf
#else
static void __debug_nothing(char *fmt, ...) { }
#define DBG     __debug_nothing
#endif

/****************************** Defs/constants ********************************/

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


/****************************** Types *****************************************/

#ifdef USE_STD_INTTYPES
#include <inttypes.h>
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint8_t u8;
typedef int8_t i8;
#else
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned char u8;
typedef char i8;
#endif

typedef volatile unsigned char* regs_t;

struct part {
        int p_start;            /* Start sector of partition */
        int p_size;             /* Sector size of partition */
        int p_rdonly;           /* */
};

typedef struct {
        unsigned int present;
        /* Note limit is 4G*512 = 2TB */
        unsigned int total_sectors;
        unsigned int cyl;               /* Iff CHS */
        u16 heads;
        u16 sec_per_track;
        unsigned char lba_supported;    /* LBA, else CHS */

        struct part d_part[MAX_PART];
} drive_info_t;

typedef enum {
        HOST_ZIDEFS,
        HOST_CASTLE,
        HOST_HCCS
} host_type_t;

typedef struct {
        int                     slot;
        regs_t                  regs;
        regs_t                  hi_latch_write;   /* If zero, 16b access is supported */
        regs_t                  hi_latch_read;    /* these two latches may be the same */
        host_type_t             type;
        drive_info_t            drives[2];
        int                     card_num;

#ifdef _KERNEL
        struct devqueue         d_ioq;      /* I/O operations queue */
        struct int_hndlr        d_ih;
        unsigned int            d_retries;
#endif
} ide_host_t;


/****************************** Macros ****************************************/

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
