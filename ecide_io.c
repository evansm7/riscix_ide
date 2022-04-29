/* ecide_io.c
 *
 * PIO IDE probing/identification and data transfer routines.
 *
 * Polled, currently; IRQs TODO.
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


#include <string.h>
#ifndef _KERNEL
#include <stdio.h>
#endif
#include "ecide.h"
#include "ecide_io.h"
#include "ecide_ataregs.h"


extern void DELAY_(int);

#define DELAYUS DELAY_


/* Returns -1 on timeout, else 0 */
int     ide_wait_nbsy(regs_t regs)
{
        int timeout = 1000*1000; /* 1000ms */
        unsigned int s;

        /* Burn 400ns  after drive select */
        s = read_reg8(regs, wd_status);
        s = read_reg8(regs, wd_status);
        s = read_reg8(regs, wd_status);

        do {
                s = read_reg8(regs, wd_status);
                if (!(s & WDCS_BUSY))
                        return 0;
                DELAYUS(1);
        } while(--timeout > 0);
        return -1;
}

/* Slight variation: wait for !Busy and DRQ, but also
 * check for error.  Fold in the mystical 400ns delay plus
 * "ignore error for first 4 reads".
 *
 * Returns -1 on timeout, 0 on success, or contents of error
 * register + status register if ERR/DF bits set in status.
 */
int     ide_wait_drq(regs_t regs)
{
        int timeout = 1000*1000; /* 1000ms */
        unsigned int s;

        /* Wait for status to "settle", in particular legend has it that
         * ERR/DF bits will take some time to update after a command.
         */
        s = read_reg8(regs, wd_status);
        s = read_reg8(regs, wd_status);
        s = read_reg8(regs, wd_status);
        s = read_reg8(regs, wd_status);

        do {
                /* Look for:
                 * BSY=0, DRQ=1 (yay, carry on)
                 * ERR=1 or DF=1 (d'oh, return error)
                 */
                s = read_reg8(regs, wd_status);
                if (!(s & WDCS_BUSY)) {
                        if ((s & WDCS_ERR) || (s & WDCS_DRVFLT))
                                return (s << 8) | read_reg8(regs, wd_error);
                        if (s & WDCS_DRQ)
                                return 0;
                }

                DELAYUS(1);
        } while(--timeout > 0);
        return -1;
}

extern void     ide_read_data(regs_t regs, unsigned char *dest);
extern void     ide_write_data(regs_t regs, unsigned char *src);
extern void     ide_read_data8(regs_t regs, regs_t hbl, unsigned char *dest);
extern void     ide_write_data8(regs_t regs, regs_t hbl, unsigned char *src);

#ifdef GENERIC_C_PIO_TRANSFERS
/* Read a sector-sized chunk (512B) */
static void     ide_read_data(regs_t regs, unsigned char *dest)
{
        int i;
        unsigned int *d = (unsigned int *)dest;

        for (i = 0; i < 512/4; i++) {
                unsigned int w = read_reg16(regs, wd_data);
                w |= ((unsigned int)read_reg16(regs, wd_data)) << 16;
                *d++ = w;
        }
}

static void     ide_write_data(regs_t regs, unsigned char *src)
{
        int i;
        unsigned int *d = (unsigned int *)src;

        for (i = 0; i < 512/4; i++) {
                unsigned int w = *d++;
                write_reg16(regs, wd_data, w & 0xffff);
                write_reg16(regs, wd_data, (w >> 16));
        }
}

/* Read a sector-sized chunk (512B), with a high-byte latch */
static void     ide_read_data8(regs_t regs, regs_t hbl, unsigned char *dest)
{
        int i;
        unsigned int *dp = (unsigned int *)dest;

        for (i = 0; i < 512/4; i++) {
                /* Read of the data reg latches data in the high-byte latch: */
                unsigned int w, x, y, z;
                w = read_reg8(regs, wd_data);
                x = read_reg8(hbl, 0);
                y = read_reg8(regs, wd_data);
                z = read_reg8(hbl, 0);
                w |= x << 8;
                w |= y << 16;
                w |= z << 24;
                *dp++ = w;
        }
}

static void     ide_write_data8(regs_t regs, regs_t hbl, unsigned char *src)
{
        int i;
        unsigned int *d = (unsigned int *)src;

        for (i = 0; i < 512/4; i++) {
                unsigned int w = *d++;
                /* Writing the data reg combines with previous high-byte value */
                write_reg8(hbl, 0, (w >> 8) & 0xff);
                write_reg8(regs, wd_data, w & 0xff);
                write_reg8(hbl, 0, (w >> 24) & 0xff);
                write_reg8(regs, wd_data, (w >> 16) & 0xff);
        }
}
#endif

static void ide_copy_string(char *dst, u16 *src, int num_hwords)
{
        int i;
        int cidx = 0;
        /* Cuts out space-padding at end */
        int last_printable = 0;
        for (i = 0; i < num_hwords; i++) {
                u16 d = *src++;
                dst[cidx] = d >> 8;
                if (dst[cidx] > ' ')
                        last_printable = cidx;
                cidx++;
                dst[cidx] = d & 0xff;
                if (dst[cidx] > ' ')
                        last_printable = cidx;
                cidx++;
        }
        if ((last_printable + 1) < num_hwords*2) {
                dst[last_printable + 1] = 0;
        }
        dst[num_hwords*2] = 0;
}

static void ide_parse_identify(u16 *buff, drive_info_t *di, int card)
{
        char id_strb[41];
        char fw_strb[9];
        u16 cyl = buff[1];
        u16 heads = buff[3];
        u16 lsplt = buff[6];
        u16 caps = buff[49];
        u32 lba_sectors = buff[60] | (unsigned int)buff[61] << 16;

        di->cyl = cyl;
        di->heads = heads;
        di->sec_per_track = lsplt;

        if (caps & (1<<9)) {
                di->total_sectors = lba_sectors;
                di->lba_supported = 1;
                if (di->heads != 16 || di->sec_per_track != 63 ||
                    di->total_sectors != cyl*heads*lsplt) {
                        DBG("*** LBA CHS info mismatch ***\n");
                        /* Can't trust CHS stuff, try to recreate it. */
                        di->heads = 16;
                        di->sec_per_track = 63;
                        di->cyl = di->total_sectors/(63*16);
                }
        } else {
                di->total_sectors = cyl*heads*lsplt;
                di->lba_supported = 0;
        }

        ide_copy_string(id_strb, &buff[27], 40/2);
        ide_copy_string(fw_strb, &buff[23], 8/2);

        printf("ecide%d: '%s', %dMB (%ld sectors, CHS %d/%d/%d)\n"
               "        [revision '%s', caps %04x (%sLBA)]\n", card,
               id_strb, di->total_sectors/2048, di->total_sectors, cyl, heads, lsplt,
               fw_strb, caps, di->lba_supported ? "" : "no ");
}

static void     ide_select_drive(ide_host_t *ih, unsigned int drive)
{
        write_reg8(ih->regs, wd_sdh, DRVHD(drive, 0));
}

/* Reset drives?
 * Identify devices
 */
int     ide_init(ide_host_t *ih, int card, u8 *scratch_buffer)
{
        int i;
        int td = 0;

        /* Can regs be accessed? */
        write_reg8(ih->regs, wd_cyl_lo, 0xaa);
        write_reg8(ih->regs, wd_cyl_hi, 0x55);
        if (read_reg8(ih->regs, wd_cyl_lo) != 0xaa ||
            read_reg8(ih->regs, wd_cyl_hi) != 0x55) {
                DBG("ide_init: Can't access regs\n");
                return -1;
        }
        /* Now want to probe whether drive 0/1 are present. */

        /* OK, do an IDENTIFY on each drive: */
        for (i = 0; i < 2; i++) {
                int r;
                ih->drives[i].present = 0;
                ih->drives[i].lba_supported = 0;
                ih->drives[i].total_sectors = 0;
                ih->drives[i].cyl = 0;
                ih->drives[i].heads = 0;
                ih->drives[i].sec_per_track = 0;

                ide_select_drive(ih, i);
                r = ide_wait_nbsy(ih->regs);
                if (r) {
                        DBG("ide_init: Timeout on nbusy for drive %d\n", i);
                        continue;
                }

                write_reg8(ih->regs, wd_command, WDCC_IDENTIFY);

                r = ide_wait_drq(ih->regs);
                if (r != 0) {
                        if (r < 0)
                                DBG("ide_init: Timeout on DRQ for IDENTITY on drive %d\n", i);
                        else
                                DBG("ide_init: Error for IDENTITY on drive %d: %04x\n", i, r);
                        continue;
                }
                ih->drives[i].present = 1;
                td++;
                if (!ih->hi_latch)
                        ide_read_data(ih->regs, scratch_buffer);
                else
                        ide_read_data8(ih->regs, ih->hi_latch, scratch_buffer);

                ide_parse_identify((u16 *)scratch_buffer, &ih->drives[i], card);
        }

        return td;
}

static void     ide_setup_address(ide_host_t *ih, unsigned int drive, unsigned int sector)
{
        /* Calculate address... */
        if (ih->drives[drive].lba_supported) {
                write_reg8(ih->regs, wd_lba_lo, sector & 0xff);
                write_reg8(ih->regs, wd_lba_mid, (sector >> 8) & 0xff);
                write_reg8(ih->regs, wd_lba_hi, (sector >> 16) & 0xff);
                write_reg8(ih->regs, wd_sdh, DRVBLK_LBA(drive, sector >> 24));
        } else {
                unsigned int s = (sector % ih->drives[drive].sec_per_track) + 1;
                unsigned int c = (sector / ih->drives[drive].sec_per_track) / ih->drives[drive].heads;
                unsigned int h = (sector / ih->drives[drive].sec_per_track) % ih->drives[drive].heads;
                /* NOTE: Sector starts at one! */
                write_reg8(ih->regs, wd_sector, s);
                write_reg8(ih->regs, wd_cyl_lo, c & 0xff);
                write_reg8(ih->regs, wd_cyl_hi, (c >> 8) & 0xff);
                write_reg8(ih->regs, wd_sdh, DRVHD(drive, h));
        }
}

#define SECTOR_LIMIT    128     /* Quirks? Standard? */
/* Read sectors, without IRQs.  Returns 0 for success, else error code.
 */
int     ide_read_some(ide_host_t *ih, unsigned int drive,
                      unsigned int sector, unsigned int count,
                      unsigned char *dest)
{
        int r;
        unsigned int done_sectors;
        unsigned int sectors_this_time;
        unsigned int s;

        ide_select_drive(ih, drive);
        if (ide_wait_nbsy(ih->regs)) {
                DBG("ide_read_some: Timeout on nBSY\n");
                return 1;
        }

        write_reg8(ih->regs, wd_precomp, 0);

#ifdef SUPER_VERBOSE
        DBG("ide_read_some(sector %d, count %d)\n", sector, count);
#endif
        done_sectors = 0;
        do {
                /* How many sectors are left? */
                if ((count - done_sectors) > SECTOR_LIMIT)
                        sectors_this_time = SECTOR_LIMIT;
                else
                        sectors_this_time = count - done_sectors;

#ifdef SUPER_VERBOSE
                DBG("   ide_read_some(sector %d, sectorcount %d)\n",
                    sector + done_sectors,
                    sectors_this_time);
#endif
                write_reg8(ih->regs, wd_seccnt, sectors_this_time);
                ide_setup_address(ih, drive, sector + done_sectors);
                write_reg8(ih->regs, wd_command, WDCC_READ);

                for (s = 0; s < sectors_this_time; s++) {
                        r = ide_wait_drq(ih->regs);
                        if (r != 0) {
                                if (r < 0)
                                        DBG("ide_read_some: Timeout on DRQ\n");
                                else
                                        DBG("ide_read_some: Error %04x\n", r);
                                return 1;
                        }
                        if (!ih->hi_latch)
                                ide_read_data(ih->regs, dest + ((done_sectors + s) * 512));
                        else
                                ide_read_data8(ih->regs, ih->hi_latch,
                                               dest + ((done_sectors + s) * 512));
                        /* Loop and wait for DRQ between each sector! */
                }
                if (ide_wait_nbsy(ih->regs)) {
                        DBG("ide_read_some: Timeout on post-block nBSY\n");
                        return 1;
                }
                done_sectors += sectors_this_time;
        } while(done_sectors < count);

        return 0;
}

int     ide_read_one(ide_host_t *ih, unsigned int drive,
                     unsigned int sector, unsigned char *dest)
{
        return ide_read_some(ih, drive, sector, 1, dest);
}

int     ide_write_some(ide_host_t *ih, unsigned int drive,
                       unsigned int sector, unsigned int count,
                       unsigned char *src)
{
        int r;
        unsigned int done_sectors;
        unsigned int sectors_this_time;
        unsigned int s;

        ide_select_drive(ih, drive);
        if (ide_wait_nbsy(ih->regs)) {
                DBG("ide_write_some: Timeout on nbusy\n");
                return 1;
        }

        write_reg8(ih->regs, wd_precomp, 0);

#ifdef SUPER_VERBOSE
        DBG("ide_write_some(sector %d, count %d)\n", sector, count);
#endif
        done_sectors = 0;
        do {
                /* How many sectors are left? */
                if ((count - done_sectors) > SECTOR_LIMIT)
                        sectors_this_time = SECTOR_LIMIT;
                else
                        sectors_this_time = count - done_sectors;
#ifdef SUPER_VERBOSE
                DBG("   ide_write_some(sector %d, sectorcount %d)\n",
                    sector + done_sectors,
                    sectors_this_time);
#endif
                write_reg8(ih->regs, wd_seccnt, sectors_this_time);
                ide_setup_address(ih, drive, sector + done_sectors);
                write_reg8(ih->regs, wd_command, WDCC_WRITE);

                for (s = 0; s < sectors_this_time; s++) {
                        r = ide_wait_drq(ih->regs);
                        if (r != 0) {
                                if (r < 0)
                                        DBG("ide_write_some: Timeout on DRQ\n");
                                else
                                        DBG("ide_write_some: Error %04x\n", r);
                                return 1;
                        }
                        if (!ih->hi_latch)
                                ide_write_data(ih->regs, src + ((done_sectors + s) * 512));
                        else
                                ide_write_data8(ih->regs, ih->hi_latch,
                                                src + ((done_sectors + s) * 512));
                }
                if (ide_wait_nbsy(ih->regs)) {
                        DBG("ide_write_some: Timeout on post-block nBSY\n");
                        return 1;
                }
                done_sectors += sectors_this_time;
        } while(done_sectors < count);

        return 0;
}

int     ide_write_one(ide_host_t *ih, unsigned int drive,
                      unsigned int sector, unsigned char *src)
{
        return ide_write_some(ih, drive, sector, 1, src);
}
