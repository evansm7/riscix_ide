#ifndef ECIDE_IO_H
#define ECIDE_IO_H

#include "types.h"

typedef volatile unsigned char* regs_t;

typedef struct {
        unsigned int present;
        /* Note limit is 4G*512 = 2TB */
        unsigned int total_sectors;
        unsigned int cyl;               /* Iff CHS */
        u16 heads;
        u16 sec_per_track;
        unsigned char lba_supported;    /* LBA, else CHS */
} disc_info_t;

typedef struct {
        regs_t          regs;
        disc_info_t     discs[2];
} ide_host_t;


int     ide_init(ide_host_t *ih);
int     ide_read_one(ide_host_t *ih, unsigned int drive,
                     unsigned int sector, unsigned char *dest);
int     ide_write_one(ide_host_t *ih, unsigned int drive,
                      unsigned int sector, unsigned char *src);

#endif
