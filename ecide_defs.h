#ifndef ECIDE_DEFS_H
#define ECIDE_DEFS_H

#ifdef _KERNEL
#include <arm/int_hndlr.h>
#include <sys/buf.h>
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
        HOST_ZIDEFS
} host_type_t;

typedef struct {
        int                     slot;
        regs_t                  regs;
        host_type_t             type;
        drive_info_t            drives[2];
        int                     card_num;

#ifdef _KERNEL
        struct devqueue         d_ioq;      /* I/O operations queue */
        struct int_hndlr        d_ih;
#endif
} ide_host_t;


#endif
