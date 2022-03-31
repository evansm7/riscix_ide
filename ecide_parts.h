#ifndef ECIDE_PARTS_H
#define ECIDE_PARTS_H

#include "types.h"
#include "ecide_defs.h"


void    ide_probe_partitions(ide_host_t *ih, unsigned int drive);
void    ide_dump_partitions(ide_host_t *ih, unsigned int drive);


/******************************************************************************/

typedef struct {
        u32     magique;
        struct {
                u32     dr_start;
                u32     len;
        } pt[4];
} zidefs_ptab_t;

#define PARTITION_TYPE_RISCIX_MFM   1
#define PARTITION_TYPE_RISCIX_SCSI  2

/* ****************************** From NetBSD: ****************************** */

#define FILECORE_BOOT_SECTOR 6

/* Stuff to deal with RISCiX partitions */

#define NRISCIX_PARTITIONS 8

struct riscix_partition {
        u32        rp_start;
        u32        rp_length;
        u32        rp_type;
        i8         rp_name[16];
};

struct riscix_scsi_partition_table {
        u32        magic;  /* ME: not just padding! */
        u32        date;
        struct riscix_partition partitions[NRISCIX_PARTITIONS];
};

struct riscix_ide_partition_table { /* ME: matches that of ST506 */
        u32        magic;
        struct riscix_partition partitions[NRISCIX_PARTITIONS];
};

struct filecore_bootblock {
        u8         padding0[0x1c0];
        u8         log2secsize;
        u8         secspertrack;
        u8         heads;
        u8         density;
        u8         idlen;
        u8         log2bpmb;
        u8         skew;
        u8         bootoption;
        u8         lowsector;
        u8         nzones;
        u16        zone_spare;
        u32        root;
        u32        disc_size;
        u16        disc_id;
        u8         disc_name[10];
        u32        disc_type;

        u8         padding1[24];

        u8         partition_type;
        u8         partition_cyl_low;
        u8         partition_cyl_high;
        u8         checksum;
};
/* ****************************** /NetBSD ****************************** */
#define RISCIX_MAGIC    0x70617274      /* ST506 magic */

#endif
