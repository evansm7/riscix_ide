#include <string.h>
#ifndef _KERNEL
#include <stdio.h>
#endif
#include "types.h"
#include "ecide.h"
#include "ecide_io.h"
#include "ataregs.h"


extern void DELAY_(int);

#define DELAYUS DELAY_

/* Returns 1 on timeout, else 0 */
int     ide_wait_nbsy(regs_t regs)
{
        int timeout = 10*100; /* 10ms */

        do {
                unsigned int s = read_reg8(regs, wd_status);
                if (!(s & WDCS_BUSY))
                        return 0;
                DELAYUS(10);
        } while(--timeout > 0);
        return 1;
}

int     ide_wait_drq(regs_t regs)
{
        int timeout = 1000*10; /* 1000ms */

        do {
                unsigned int s = read_reg8(regs, wd_status);
                if (s & WDCS_DRQ)
                        return 0;
                DELAYUS(100);
        } while(--timeout > 0);
        return 1;
}

static unsigned char ide_id_buffer[512];

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

static void ide_parse_identify(u16 *buff, disc_info_t *disc)
{
        char id_strb[41];
        char fw_strb[9];
        u16 cyl = buff[1];
        u16 heads = buff[3];
        u16 lsplt = buff[6];
        u16 caps = buff[49];
        u32 lba_sectors = buff[60] | (unsigned int)buff[61] << 16;

        disc->cyl = cyl;
        disc->heads = heads;
        disc->sec_per_track = lsplt;

        if (caps & (1<<9)) {
                disc->total_sectors = lba_sectors;
                disc->lba_supported = 1;
                if (disc->heads != 16 || disc->sec_per_track != 63 ||
                    disc->total_sectors != cyl*heads*lsplt) {
                        DBG("*** LBA CHS info mismatch ***\n");
                        /* Can't trust CHS stuff, try to recreate it. */
                        disc->heads = 16;
                        disc->sec_per_track = 63;
                        disc->cyl = disc->total_sectors/(63*16);
                }
        } else {
                disc->total_sectors = cyl*heads*lsplt;
                disc->lba_supported = 0;
        }

        ide_copy_string(id_strb, &buff[27], 40/2);
        ide_copy_string(fw_strb, &buff[23], 8/2);

        printf("ecide: Disk ID '%s', %dMB (%ld sectors, CHS %d/%d/%d)\n"
               "       [revision '%s', caps %04x (%sLBA)]\n",
               id_strb, disc->total_sectors/2048, disc->total_sectors, cyl, heads, lsplt,
               fw_strb, caps, disc->lba_supported ? "" : "no ");
}

/* Reset drives?
 * Identify device
 */
int     ide_init(ide_host_t *ih)
{
        int i;
        int td = 0;

        /* Can regs be accessed? */
        write_reg8(ih->regs, wd_cyl_lo, 0xaa);
        write_reg8(ih->regs, wd_cyl_hi, 0x55);
        if (read_reg8(ih->regs, wd_cyl_lo) != 0xaa ||
            read_reg8(ih->regs, wd_cyl_hi) != 0x55) {
                DBG("ide_init: Can't access regs\n");
                return 1;
        }
        /* Now want to probe whether drive 0/1 are present. */

        /* OK, do an IDENTIFY on each drive: */
        for (i = 0; i < 2; i++) {
                int r;
                ih->discs[i].present = 0;
                ih->discs[i].lba_supported = 0;
                ih->discs[i].total_sectors = 0;
                ih->discs[i].cyl = 0;
                ih->discs[i].heads = 0;
                ih->discs[i].sec_per_track = 0;

                /* Select drive */
                write_reg8(ih->regs, wd_sdh, DRVHD(i, 0));
                r = ide_wait_nbsy(ih->regs);
                if (r) {
                        DBG("ide_init: Timeout on nbusy for drive %d\n", i);
                        continue;
                }

                write_reg8(ih->regs, wd_command, WDCC_IDENTIFY);

                r = ide_wait_drq(ih->regs);
                if (r) {
                        DBG("ide_init: Timeout on DRQ for IDENTITY on drive %d\n", i);
                        continue;
                }
                ih->discs[i].present = 1;
                td++;
                ide_read_data(ih->regs, ide_id_buffer);

                ide_parse_identify((u16 *)ide_id_buffer, &ih->discs[i]);
        }
        DBG("ide_init: Found %d discs\n", td);

        return 0;
}

static void     ide_setup_address(ide_host_t *ih, unsigned int drive, unsigned int sector)
{
        /* Calculate address... */
        if (ih->discs[drive].lba_supported) {
                write_reg8(ih->regs, wd_lba_lo, sector & 0xff);
                write_reg8(ih->regs, wd_lba_mid, (sector >> 8) & 0xff);
                write_reg8(ih->regs, wd_lba_hi, (sector >> 16) & 0xff);
                write_reg8(ih->regs, wd_sdh, DRVBLK_LBA(drive, sector >> 24));
        } else {
                unsigned int s = (sector % ih->discs[drive].sec_per_track) + 1;
                unsigned int c = (sector / ih->discs[drive].sec_per_track) / ih->discs[drive].heads;
                unsigned int h = (sector / ih->discs[drive].sec_per_track) % ih->discs[drive].heads;
                /* NOTE: Sector starts at one! */
                write_reg8(ih->regs, wd_sector, s);
                write_reg8(ih->regs, wd_cyl_lo, c & 0xff);
                write_reg8(ih->regs, wd_cyl_hi, (c >> 8) & 0xff);
                write_reg8(ih->regs, wd_sdh, DRVHD(drive, h));
        }
}

/* Read a sector, without IRQs.  Returns 0 for success, else error code.
 * Only supports LBA28 drives.
 */
int     ide_read_one(ide_host_t *ih, unsigned int drive,
                     unsigned int sector, unsigned char *dest)
{
        if (ide_wait_nbsy(ih->regs)) {
                DBG("ide_read_one: Timeout on nbusy\n");
                return 1;
        }

        write_reg8(ih->regs, wd_precomp, 0);
        write_reg8(ih->regs, wd_seccnt, 1);         /* Bigger blocks later */
        ide_setup_address(ih, drive, sector);

        write_reg8(ih->regs, wd_command, WDCC_READ);

        if (ide_wait_drq(ih->regs)) {
                DBG("ide_read_one: Timeout on DRQ\n");
                return 1;
        }

        ide_read_data(ih->regs, dest);
        return 0;
}

int     ide_write_one(ide_host_t *ih, unsigned int drive,
                      unsigned int sector, unsigned char *src)
{
        if (ide_wait_nbsy(ih->regs)) {
                DBG("ide_write_one: Timeout on nbusy\n");
                return 1;
        }

        write_reg8(ih->regs, wd_precomp, 0);
        write_reg8(ih->regs, wd_seccnt, 1);         /* Bigger blocks later */
        ide_setup_address(ih, drive, sector);

        write_reg8(ih->regs, wd_command, WDCC_WRITE);

        if (ide_wait_drq(ih->regs)) {
                DBG("ide_write_one: Timeout on DRQ\n");
                return 1;
        }

        ide_write_data(ih->regs, src);
        return 0;
}
