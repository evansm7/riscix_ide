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

#endif
