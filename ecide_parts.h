/*
 * As indicated below, portions of this file are borrowed from NetBSD.
 *
 * For the remainder,
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

#ifndef ECIDE_PARTS_H
#define ECIDE_PARTS_H

#include "ecide.h"


void    ide_probe_partitions(ide_host_t *ih, unsigned int drive, u8 *scratch);
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
/* These structs are borrowed from NetBSD's sys/disklabel_acorn.h, and are
 *
 * Copyright (c) 1994 Mark Brinicombe.
 * Copyright (c) 1994 Brini.
 * All rights reserved.
 *
 * This code is derived from software written for Brini by Mark Brinicombe
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
 *      This product includes software developed by Brini.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY BRINI ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL BRINI OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
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
