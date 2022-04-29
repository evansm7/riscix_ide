/*
 * Acorn IDE partition-detection routines
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
 *
 *
 * Uses filecore_checksum() from NetBSD's disksubr_acorn.c, which is:
 *
 * Copyright (c) 1995 Mark Brinicombe
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _KERNEL
#include <stdio.h>
#endif
#include <string.h>
#include "ecide.h"
#include "ecide_parts.h"


static unsigned int     sector_from_cyl(drive_info_t *di, unsigned int cyl)
{
        return di->sec_per_track*di->heads*cyl;
}

/* From NetBSD: */
static unsigned int     filecore_checksum(u8 *bootblock)
{
        unsigned int sum;
        unsigned int loop;
    
        sum = 0;

        for (loop = 0; loop < 512; ++loop)
                sum += bootblock[loop];

        if (sum == 0) return(0xffff);

        sum = 0;
    
        for (loop = 0; loop < 511; ++loop) {
                sum += bootblock[loop];
                if (sum > 255)
                        sum -= 255;
        }

        return(sum);
}

static int      ide_probe_adfs_parts(ide_host_t *ih, unsigned int drive,
                                     unsigned int sector_offset, u8 *scratch)
{
        int r;
        int p;
        struct filecore_bootblock *bb;
        struct riscix_ide_partition_table *rpt;
        unsigned int rix_pt_cyl, rix_pt_sector;

        r = ide_read_one(ih, drive, FILECORE_BOOT_SECTOR + sector_offset, scratch);
        if (r) {
                DBG("ide_probe_adfs_parts(ecide%d:%d): can't read part table\n",
                    ih->card_num, drive);
                return 1;
        }

        bb = (struct filecore_bootblock *)scratch;
        /* 1.  Is it a valid ADFS boot sector? */
        if (bb->checksum != filecore_checksum((u8 *)bb)) {
                DBG("ide_probe_adfs_parts(ecide%d:%d): BB checksum bad\n",
                    ih->card_num, drive);
                return 1;
        }
        /* 2.  Print some debug info */
        DBG("ecide%d:%d:  ADFS: ssl2 %d, s/t %d, heads %d, size %d, type %d, name '%s'\n",
            ih->card_num, drive,
            bb->log2secsize, bb->secspertrack, bb->heads, bb->disc_size, bb->disc_type,
            &bb->disc_name[0]);
        /* 3.  Check for RISCiX partition table */
        if (bb->partition_type != PARTITION_TYPE_RISCIX_MFM) {
                return 1;
        }
        /* Sweet! Found RISCiX section, finally.... */
        rix_pt_cyl = bb->partition_cyl_low | (bb->partition_cyl_high << 8);
        if (rix_pt_cyl & 1) {
                printf("ecide%d:%d RISCiX partition cyl offset %d is not even!\n",
                       ih->card_num, drive, rix_pt_cyl);
                return 1;
        }
        rix_pt_cyl /= 2; /* specified as though 256B sectors, but we have 512B! */
        rix_pt_sector = sector_offset + sector_from_cyl(&ih->drives[drive], rix_pt_cyl);
        printf("ecide%d:%d Found RISCiX partition table at cyl %d (abs sector %d)\n",
               ih->card_num, drive, rix_pt_cyl, rix_pt_sector);

        /* 4. Read the table */
        r = ide_read_one(ih, drive, rix_pt_sector, scratch);
        if (r) {
                DBG("ide_probe_adfs_parts(ecide%d:%d): can't read RISCiX part table\n",
                    ih->card_num, drive);
                return 1;
        }
        rpt = (struct riscix_ide_partition_table *)scratch;

        /* 5. Check magic */
        if (rpt->magic != RISCIX_MAGIC) {
                printf("ecide%d:%d RISCiX partition table magic %08x not understood!\n",
                       ih->card_num, drive, rpt->magic);
                return 1;
        }
        /* 6. Dump/log table */
        for (p = 0; p < NRISCIX_PARTITIONS; p++) {
                /* HDForm specifies partition sizes in units of cylinders
                 * of 256B sectors, so that the RISCiX partition table cylinder
                 * offset is in the units expected by RISCiXfs.
                 *
                 * We need to divide everything by two and complain bitterly
                 * if the partitions were created on "half sector"/odd boundaries.
                 */
                if (rpt->partitions[p].rp_length)
                        DBG(" %d: '%s' %d +%d type %d\n", p, rpt->partitions[p].rp_name,
                            rpt->partitions[p].rp_start, rpt->partitions[p].rp_length,
                            rpt->partitions[p].rp_type);
                /* What is type used for?  Auto-probe of swap/root, sure... do we need it? */

                ih->drives[drive].d_part[p].p_size = 0;         /* Default not present */

                if (rpt->partitions[p].rp_type) {
                        /* We store the partitions in units of sector (not CHS faff)
                         * to make the runtime partition maths easier.  The RISCiX partition
                         * table stores them in units of (drive) cylinders; convert.
                         *
                         * In addition, the start is absolute (not relative to a possible
                         * container partition, e.g. in ZIDEFS), so add on the offset.
                         */
                        if ((rpt->partitions[p].rp_start & 1) || (rpt->partitions[p].rp_length & 1)) {
                                printf("ecide%d:%d Ignoring mis-aligned partition %d (start %d, len %d)\n",
                                       ih->card_num, drive, p,
                                       rpt->partitions[p].rp_start,
                                       rpt->partitions[p].rp_length);
                                continue;
                        }

                        ih->drives[drive].d_part[p].p_start = sector_offset +
                                sector_from_cyl(&ih->drives[drive], rpt->partitions[p].rp_start/2);
                        ih->drives[drive].d_part[p].p_size = sector_from_cyl(&ih->drives[drive],
                                                                             rpt->partitions[p].rp_length/2);
                        ih->drives[drive].d_part[p].p_rdonly = 0;       /* FIXME: get this from type? */
                }
        }
        return 0;
}

static int      ide_probe_zidefs_parts(ide_host_t *ih, unsigned int drive, u8 *scratch)
{
        /* The partition table appears to live (only?) on drive 0.  Fetch
         * that.  (Is secondary-without-primary ever possible?  Assume PT
         * comes from 1st if present else 2nd.)
         */
        int real_dr = 0;
        int r, i;
        zidefs_ptab_t zpt;

        if (!ih->drives[real_dr].present)
                real_dr = 1;

        r = ide_read_one(ih, real_dr, 0, scratch);
        if (r) {
                DBG("ide_probe_zidefs_parts(ecide%d:%d): can't read part table of drive %d\n",
                    ih->card_num, drive, real_dr);
                return 1;
        }

        memcpy(&zpt, scratch, sizeof(zidefs_ptab_t));

        if (zpt.magique != 0x32454449 /* IDE2 */) {
                DBG("ide_probe_zidefs_parts(ecide%d:%d): bad magic 0x%x\n",
                    ih->card_num, drive, zpt.magique);
                return 1;
        }
        DBG("ide_probe_zidefs_parts(ecide%d:%d): Good ZIDEFS partition magic, scanning\n",
            ih->card_num, drive);
        /* Find partitions that match the requested drive */
        for (i = 0; i < 4; i++) {
                if (zpt.pt[i].dr_start != 0xffffffff &&
                    ((zpt.pt[i].dr_start >> 31) == (drive & 1))) {
                        unsigned int start_cyl = zpt.pt[i].dr_start & 0x7fffffff;
                        /* Convert from cylinders to sectors kthx */

                        DBG("ide_probe_zidefs_parts(ecide%d:%d): Checking p%d (start cyl %d)\n",
                            ih->card_num, drive, i, start_cyl);

                        /* We STOP after the first match -- only one RISCiX section
                         * per physical drive!
                         */
                        if (!ide_probe_adfs_parts(ih, drive,
                                                  sector_from_cyl(&ih->drives[drive],
                                                                  start_cyl),
                                                  scratch))
                                return 0;
                }
        }
        return 1; /* Nothing found */
}

/* Find RISCiX partitions, possibly within some other kind of RISC OS
 * partitioning scheme.  Update ih->discs[disc]->d_part table.
 */
void    ide_probe_partitions(ide_host_t *ih, unsigned int drive, u8 *scratch)
{
        if (!ih->drives[drive].present)
                return;

        /* First up, approach depends on host controller type.
         *
         * For instance, a drive on an ADFS IDE card (or A5000-like machine)
         * contains one ADFS partition, after which a RISCiX table lives.
         *
         * But, an ICS/ZIDEFS card has a partitioning scheme that might
         * contain a RISCiX 'partition' (containing the real RISCiX partitions
         * within.
         */
        switch (ih->type) {
        case HOST_ZIDEFS:
                ide_probe_zidefs_parts(ih, drive, scratch);
                break;
        case HOST_CASTLE:
                /* No exotic partitioning scheme, just ADFS-then-stuff-afterwards */
                ide_probe_adfs_parts(ih, drive, 0, scratch);
                break;
        default:
                printf("ecide%d: Cannot probe partitions, unknown controller %d\n",
                       ih->card_num, ih->type);
        }
}

void    ide_dump_partitions(ide_host_t *ih, unsigned int drive)
{
        int p;
        printf("ecide%d:%d partitions:\n", ih->card_num, drive);
        for (p = 0; p < MAX_PART; p++) {
                if (ih->drives[drive].d_part[p].p_size) {
                        printf("  %d:  %d-%d (size %d) %s\n", p,
                               ih->drives[drive].d_part[p].p_start,
                               ih->drives[drive].d_part[p].p_start +
                               ih->drives[drive].d_part[p].p_size,
                               ih->drives[drive].d_part[p].p_size,
                               ih->drives[drive].d_part[p].p_rdonly ? "R" : "");
                }
        }
}
