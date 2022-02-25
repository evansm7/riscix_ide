/*
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
#ifndef ECIDE_IO_H
#define ECIDE_IO_H

#include "ecide.h"

int     ide_init(ide_host_t *ih, int card, u8 *scratch_buffer);
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
