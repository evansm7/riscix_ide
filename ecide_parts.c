#ifndef _KERNEL
#include <stdio.h>
#endif
#include <string.h>
#include "types.h"
#include "ecide.h"
#include "ecide_defs.h"
#include "ecide_parts.h"

static unsigned char ide_pt_buffer[512];        /* FIXME: make dynamic */


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
                                     unsigned int sector_offset)
{
        int r;
        int p;
        struct filecore_bootblock *bb;
        struct riscix_ide_partition_table *rpt;
        unsigned int rix_pt_cyl, rix_pt_sector;

        r = ide_read_one(ih, drive, FILECORE_BOOT_SECTOR + sector_offset, ide_pt_buffer);
        if (r) {
                DBG("ide_probe_adfs_parts(ecide%d:%d): can't read part table\n",
                    ih->card_num, drive);
                return 1;
        }

        bb = (struct filecore_bootblock *)ide_pt_buffer;
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
        rix_pt_sector = sector_offset + sector_from_cyl(&ih->drives[drive], rix_pt_cyl);
        printf("ecide%d:%d Found RISCiX partition table at cyl %d (abs sector %d)\n",
               ih->card_num, drive, rix_pt_cyl, rix_pt_sector);

        /* 4. Read the table */
        r = ide_read_one(ih, drive, rix_pt_sector, ide_pt_buffer);
        if (r) {
                DBG("ide_probe_adfs_parts(ecide%d:%d): can't read RISCiX part table\n",
                    ih->card_num, drive);
                return 1;
        }
        rpt = (struct riscix_ide_partition_table *)ide_pt_buffer;

        /* 5. Check magic */
        if (rpt->magic != RISCIX_MAGIC) {
                printf("ecide%d:%d RISCiX partition table magic %08x not understood!\n",
                       ih->card_num, drive, rpt->magic);
                return 1;
        }
        /* 6. Dump/log table */
        for (p = 0; p < NRISCIX_PARTITIONS; p++) {
                if (rpt->partitions[p].rp_length)
                        printf(" %d: '%s' %d +%d type %d\n", p, rpt->partitions[p].rp_name,
                               rpt->partitions[p].rp_start, rpt->partitions[p].rp_length,
                               rpt->partitions[p].rp_type);
                /* What is type used for?  Auto-probe of swap/root, sure... do we need it? */

                if (rpt->partitions[p].rp_type) {
                        /* We store the partitions in units of sector (not CHS faff)
                         * to make the runtime partition maths easier.  The RISCiX partition
                         * table stores them in units of (drive) cylinders; convert.
                         *
                         * In addition, the start is absolute (not relative to a possible
                         * container partition, e.g. in ZIDEFS), so add on the offset.
                         */
                        ih->drives[drive].d_part[p].p_start = sector_offset +
                                sector_from_cyl(&ih->drives[drive], rpt->partitions[p].rp_start);
                        ih->drives[drive].d_part[p].p_size = sector_from_cyl(&ih->drives[drive],
                                                                             rpt->partitions[p].rp_length);
                        ih->drives[drive].d_part[p].p_rdonly = 0;       /* FIXME: get this from type? */
                } else {
                        ih->drives[drive].d_part[p].p_size = 0;         /* Not present */
                }
        }
        return 0;
}

static int      ide_probe_zidefs_parts(ide_host_t *ih, unsigned int drive)
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

        r = ide_read_one(ih, real_dr, 0, ide_pt_buffer);
        if (r) {
                DBG("ide_probe_zidefs_parts(ecide%d:%d): can't read part table of drive %d\n",
                    ih->card_num, drive, real_dr);
                return 1;
        }

        memcpy(&zpt, ide_pt_buffer, sizeof(zidefs_ptab_t));

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
                    (zpt.pt[i].dr_start & (1<<31)) == (drive << 31)) {
                        unsigned int start_cyl = zpt.pt[i].dr_start & 0x7fffffff;
                        /* Convert from cylinders to sectors kthx */

                        DBG("ide_probe_zidefs_parts(ecide%d:%d): Checking p%d (start cyl %d)\n",
                            ih->card_num, drive, i, start_cyl);

                        /* We STOP after the first match -- only one RISCiX section
                         * per physical drive!
                         */
                        if (!ide_probe_adfs_parts(ih, drive,
                                                  sector_from_cyl(&ih->drives[drive],
                                                                  start_cyl)))
                                return 0;
                }
        }
        return 1; /* Nothing found */
}

/* Find RISCiX partitions, possibly within some other kind of RISC OS
 * partitioning scheme.  Update ih->discs[disc]->d_part table.
 */
void    ide_probe_partitions(ide_host_t *ih, unsigned int drive)
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
                ide_probe_zidefs_parts(ih, drive);
                break;
        default:
                printf("ecide%d: Cannot probe partitions, unknown controller %d\n",
                       ih->card_num, ih->type);
        }
}

void    ide_dump_partitions(ide_host_t *ih, unsigned int drive)
{
        int p;
        printf("ecide%d: Partitions on drive %d:\n", ih->card_num, drive);
        for (p = 0; p < MAX_PART; p++) {
                if (ih->drives[drive].d_part[p].p_size) {
                        printf("%d:  %d-%d (size %d) %s\n", p,
                               ih->drives[drive].d_part[p].p_start,
                               ih->drives[drive].d_part[p].p_start +
                               ih->drives[drive].d_part[p].p_size,
                               ih->drives[drive].d_part[p].p_size,
                               ih->drives[drive].d_part[p].p_rdonly ? "R" : "");
                }
        }
}
