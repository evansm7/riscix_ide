#ifndef ECIDE_IO_H
#define ECIDE_IO_H

#include "types.h"
#include "ecide.h"
#include "ecide_defs.h"

int     ide_init(ide_host_t *ih);
int     ide_read_one(ide_host_t *ih, unsigned int drive,
                     unsigned int sector, unsigned char *dest);
int     ide_write_one(ide_host_t *ih, unsigned int drive,
                      unsigned int sector, unsigned char *src);
int     ide_read_some(ide_host_t *ih, unsigned int drive,
                      unsigned int sector, unsigned int count,
                      unsigned char *dest);
int     ide_write_some(ide_host_t *ih, unsigned int drive,
                       unsigned int sector, unsigned int count,
                       unsigned char *src);

#endif
