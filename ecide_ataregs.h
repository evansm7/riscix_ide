/*
 * IDE register definitions from BSD's wd.h:
 * Copyright (c) 1991, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
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

#ifndef ATAREGS_H
#define ATAREGS_H

/*
 * Disk Controller register definitions.
 */
#define wd_data         0x0             /* data register (R/W - 16 bits) */
#define wd_error        0x1             /* error register (R) */
#define wd_precomp      wd_error        /* write precompensation (W) */
#define wd_seccnt       0x2             /* sector count (R/W) */
#define wd_sector       0x3             /* first sector number (R/W) */
#define wd_lba_lo       wd_sector
#define wd_cyl_lo       0x4             /* cylinder address, low byte (R/W) */
#define wd_lba_mid      wd_cyl_lo
#define wd_cyl_hi       0x5             /* cylinder address, high byte (R/W)*/
#define wd_lba_hi       wd_cyl_hi
#define wd_sdh          0x6             /* sector size/drive/head (R/W)*/
#define wd_command      0x7             /* command register (W)  */
#define wd_status wd_command            /* immediate status (R)  */

#define wd_altsts       0x206           /* alternate fixed disk status(via 1015) (R)*/
#define wd_ctlr         0x206           /* fixed disk controller control(via 1015) (W)*/
#define wd_digin        0x207           /* disk controller input(via 1015) (R)*/

/*
 * Status Bits.
 */
#define WDCS_BUSY       0x80            /* Controller busy bit. */
#define WDCS_READY      0x40            /* Selected drive is ready */
#define WDCS_DRVFLT     0x20            /* Drive fault */
#define WDCS_SEEKCMPLT  0x10            /* Seek complete */
#define WDCS_DRQ        0x08            /* Data request bit. */
#define WDCS_ECCCOR     0x04            /* ECC correction made in data */
#define WDCS_INDEX      0x02            /* Index pulse from selected drive */
#define WDCS_ERR        0x01            /* Error detect bit. */

/*
 * Commands for Disk Controller.
 */
#define WDCC_READ       0x20            /* disk read code */
#define WDCC_WRITE      0x30            /* disk write code */
#define WDCC_RESTORE    0x10            /* disk restore code -- resets cntlr */
#define WDCC_IDENTIFY   0xec            /* Identify device */

#define DRVHD(drive, head)      (0xa0 | ((!!(drive)) << 4) | ((head) & 0xf))
#define DRVBLK_LBA(drive, blk)  (0xe0 | ((!!(drive)) << 4) | ((blk) & 0xf))

#endif
