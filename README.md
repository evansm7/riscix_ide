# Acorn RISC iX IDE disc driver (ecide)

v0.1 4 April 2022


A very simple, very PIO IDE disc driver for Acorn's RISC iX operating system.  It's a present for RISC iX 1.21's 30th birthday which happens to be right about now.  :-)

Using this driver, an Acorn Archimedes can boot RISC iX from a regular IDE disc instead of a grim old ST-506 disc, or a SCSI disc that has to be attached only one particular (and expensive) Acorn SCSI card.  In theory (currently untested O:-) ) this unlocks use of CompactFlash storage and some very reasonable modern-day IDE card products.

There is also a path to enjoying an ancient 4.3 BSD on machines such as the A3000, and in future A5000/A4000/A3020.

Huge thanks to Ian Stocks for providing a ZIDEFS IDE card to enable this work.


## Status

For basic usage, it works well.  Surprisingly enough, I haven't experienced any massive filesystem corruption yet with it.  However, this has received very little "variety testing" with different drives.  I've used it with a flash Disc-On-Module with success, but have not attempted CF cards yet.  This is 'in plan' ;-)

(It's laughable to use words like "production" or "commercial" in conjunction with this driver or OS, but please consider this driver inappropriate for serious usage or in contexts where data matters!  Your choice, no warranty is provided.  Thanks!)

Here's the dmesg from my Acorn R140, whose ST-506 drive has long since died, and now enjoys running RISC iX once again:

~~~
RISC iX (M) ME ecide kernel root #10 special: made Fri Jan  1 19:24:04 1999
ARM3 processor, cache enabled
real mem  = 4194304
avail mem = 3080192
30 buffers (240 Kbytes)
st[0-1]: internal controller
ecide0: Disk ID '1GB ATA Flash Disk', 977MB (2001888 sectors, CHS 1986/16/63)
        [revision 'AD B61FK', caps 0b00 (LBA)]
ecide0: 1 drive found, slot 1
ecide1, slot 2: no drives found
ecide0:0 Found RISCiX partition table at cyl 50 (abs sector 1072512)
ecide0:0 partitions:
  0:  1073520-1867824 (size 794304) 
  1:  1868832-2000880 (size 132048) 
  6:  1022112-1072512 (size 50400) 
  7:  1022112-2001888 (size 979776) 
et0: slot 3: iss 1, address 00:00:a4:00:09:33
Swap size = 66.0 Mb
root fstype  4.3, name /dev/id0a
swap fstype spec, name /dev/id0S
~~~

(The boot messages are a little verbose, but it's been helpful.)

And, FWIW:

~~~
# df
Filesystem            kbytes    used   avail capacity  Mounted on
/dev/id0a             372302  119963  215108    36%    /
~~~


### Features

   - Supports 16-bit PIO accesses to both LBA and non-LBA/CHS drives
   - Supports the "Ian Stocks ZIDEFS podule"
   - Supports RISC iX sections embedded in any of the ZIDEFS partition (one RISC iX partition table per physical drive)
   - Supports the Castle 16-bit IDE podule _in principle_ (but doesn't work yet -- see ecide1 in the dmesg above!)
   - Easily extensible to support other interfaces
   - Supports the ST-506/ADFS/non-SCSI RISCiX partitioning scheme, so interacts well with an ADFS slice of the drive
     - You can use up to 512MB of a drive for RISC OS, the bootloader, etc., and use the rest for RISC iX.


### Issues/FIXME/Todo

(In order of most to least conspicuous)

   - Interrupts!  Well, on cards that support them.  RISC OS doesn't get much benefit from IRQs, but RISC iX spends a lot of system time on polled transfers which could be spent elsewhere.
   - Support more IDE podules
   - Castle 16-bit IDE is a WIP/needs debugging
   - Support the 8-bit/A30x0 versions of Ian Stocks IDE and Castle IDE podules
   - Support A5000/A4000/A3020 native/82c711 IDE
   - Support character device access


## Getting started

Ideally, grab a pre-built kernel (or better) disc image from *TBD*.

The overall procedure is currently quite involved.  You may benefit from at least a 6-pack of beer and possibly some aspirin:

   - Build/acquire a RISC iX kernel containing this driver (see below for build advice)
   - "Partition a disc appropriately" :grimace:
   - Create a BSD filesystem
   - Create new block device nodes
   - Install/copy a RISC iX root filesystem from another disc, with new kernel
   - Patch RISCiXFS (if necessary) for non-ADFS IDE filesystems

(I'll start a thread on <https://stardot.org.uk> to try to capture the partitioning side of things!)


## Block device nodes

The driver identifies using the "id" string, so conventionally block device names are of the form `/dev/idXXXyyy`.

The default configuration (see the patch to `conf.c`) uses major number 40.  All "id" drives use this major.  The minor number is encoded as:

   - [2:0] = partition (0-7)
   - [3] = drive (0-1)
   - [5:4] = card (0-3)

Each card supports 2 drives.  The primary is always drive 0 of a card.  The cards are numbered in the order they are discovered -- this is NOT the slot number, so that one card with one drive will always appear at 'id0' no matter which slot it's in.

If a card only has 1 drive (assuming this is the primary...) the nodes corresponding to drive 1 are unused.

For example, a machine has 2 IDE podules in slots 0 and 3, called A and B respectively.  A has 1 drive attached, X, and B has 2 drives, Y & Z.  X would appear as id0, Y as id2 and Z as id3.  Note id1* is unused because card A's drive 1 is not present.

The following will create the correct block device nodes.  (Note:  The character "raw" device nodes are not yet supported.)

~~~
mknod id0a b 40 0
mknod id0S b 40 1
mknod id0h b 40 7

mknod id1a b 40 8
mknod id1S b 40 9
mknod id1h b 40 15

# Card 1
mknod id2a b 40 16
mknod id2S b 40 17
mknod id2h b 40 23

mknod id3a b 40 24
mknod id3S b 40 25
mknod id3h b 40 31

# Card 2
mknod id4a b 40 32
mknod id4S b 40 33
mknod id4h b 40 39

mknod id5a b 40 40
mknod id5S b 40 41
mknod id5h b 40 47

# Card 3
mknod id6a b 40 48
mknod id6S b 40 49
mknod id6h b 40 55

mknod id7a b 40 56
mknod id7S b 40 57
mknod id7h b 40 63
~~~


## Booting

The RISCiXFS module needs to be tricked into accessing an IDE disc's partition.  The module pre-dates any IDE on Arc machines, so assumes that any non-SCSI ADFS disc is an "st" disc (ST-506).  This is good for us, because ADFS can support IDE...!  It can also be patched (see below) to access other filesystems.

This tells RISCiXFS to find the kernel on a filesystem via "ADFS", on the disc equivalent to ADFS::4 (:5 would be Unit 1), and partition 0:

~~~
 *configure Device st0
 *configure Unit 0
 *configure Partition 0
~~~

The 'boot' command then reads the named kernel file from the BSD filesystem on that drive.

The following command boots the kernel, instructing it to mount partition 0 on the first-found IDE card's drive 0 as root, and partition 1 on the same drive as swap:

~~~
 *boot vmunix id0(0,0) id0(0,1)
~~~

Note the slight inconsistency:  we lie to RISCiXFS saying it's an ST-506 drive (really an ADFS IDE drive substituted for this), but tell the kernel the truth.  The kernel matches "id" to the `ecide` driver.


## RISCiXFS patching for other filesystems

On RISCiXFS v1.22, two instructions at offsets 0x10ce0 and 0x10ce4 construct the value "0x40240" in R0.  This number is SWI "ADFS_DiscOp", and can be changed to another filesystem's _DiscOp SWI.  For example, 'ZIDEFS\_DiscOp' works well here.


# A short primer on building a kernel on RISC iX

I appreciate the chicken & egg inherent in these directions.  If you have an Ether1 or Ether2 card, setting up an NFS-booting environment is VERY useful for development.  See stardot.org.uk

Make a backup of `/usr/src/sys`!

Read the README.  In particular, don't forget to update your system `/usr/include` with newer headers in `/usr/src/sys/include` as instructed.

I found building as a non-root user a little frustrating permissions-wise (though using NFS root may have complicated things).

## Create and set up build directory:

Each machine/config gets built in its own subdirectory, modelled after the `/usr/src/sys/SYSTEM` directory.  Strongly advise to not build in the `SYSTEM` directory itself, and to protect that as a template.

~~~
# mkdir /usr/src/sys/M
# cd /usr/src/sys/M
# cp ../SYSTEM/KERNCOMP* ../SYSTEM/*.c ../SYSTEM/*.h ../SYSTEM/Makefile .
# ln -s ../SHARED .
~~~

### Copy misc scripts:

~~~
# cd /usr/src/sys/conf
# cp SYSTEMdevconf.h Mdevconf.h
# cp SYSTEMlinkopts Mlinkopts
# cp SYSTEMsqueezecmd Msqueezecmd
~~~

(Optional) edit ../conf/{conf.c, xcbconf.c} to add device drivers (see below).  Recommended to get accustomed to the build process first, then add drivers second.


## Tune kernel config:

In `/usr/src/sys/M/Makefile`, search for the string "RISC iX%s test kernel" and modify to taste (be descriptive, this is the banner printed on boot.  Remove `$S/iecd.o` and `$S/iecs.o` objects from the `DRIVER_OBJS` variable -- they don't do anything except waste space.

Add or remove drivers by setting their `#define` appropriately in `/usr/src/sys/conf/Mdevconf.h`.  (0 = not present.)  Fewer drivers = smaller kernel = more memory = better chance of it being less painfully slow.  I mean, running RISC iX on a 4MB machine will let you experience "make you want to weep" levels of performance, but if you can save a couple of pages that's cool.


## Build:

```
# make
Fri Jan  1 17:43:14 GMT0BST 1999
/usr/lib/cc -Zi./KERNCOMP ./KERNCOMP.h -c -Fn -R -D_KERNEL -j/usr/include,. -DQUOTA -DSYSACCT -DINET -DISO -DNFSSERVER -DNFSCLIENT -DUFS -DGATEWAY -DCHECK    ../conf/param.c
rm -f devconf.h
cp ../conf/`basename \`pwd\``devconf.h devconf.h 
/usr/lib/cc -Zi./KERNCOMP ./KERNCOMP.h -c -Fn -R -D_KERNEL -j/usr/include,. -DQUOTA -DSYSACCT -DINET -DISO -DNFSSERVER -DNFSCLIENT -DUFS -DGATEWAY -DCHECK    ../conf/conf.c
/usr/lib/cc -Zi./KERNCOMP ./KERNCOMP.h -c -Fn -R -D_KERNEL -j/usr/include,. -DQUOTA -DSYSACCT -DINET -DISO -DNFSSERVER -DNFSCLIENT -DUFS -DGATEWAY -DCHECK    ../conf/md_conf.c
rm -f lineconf.h
cp ../conf/lineconf.h .
/usr/lib/cc -Zi./KERNCOMP ./KERNCOMP.h -c -Fn -R -D_KERNEL -j/usr/include,. -DQUOTA -DSYSACCT -DINET -DISO -DNFSSERVER -DNFSCLIENT -DUFS -DGATEWAY -DCHECK    ../sys/tty_conf.c
/usr/lib/cc -Zi./KERNCOMP ./KERNCOMP.h -c -Fn -R -D_KERNEL -j/usr/include,. -DQUOTA -DSYSACCT -DINET -DISO -DNFSSERVER -DNFSCLIENT -DUFS -DGATEWAY -DCHECK    ../conf/xcbconf.c
/usr/lib/cc -Zi./KERNCOMP ./KERNCOMP.h -c -Fn -R -D_KERNEL -j/usr/include,. -DQUOTA -DSYSACCT -DINET -DISO -DNFSSERVER -DNFSCLIENT -DUFS -DGATEWAY -DCHECK    -o ps_conf.o ps_conf.c
/usr/lib/cc -Zi./KERNCOMP ./KERNCOMP.h -c -Fn -R -D_KERNEL -j/usr/include,. -DQUOTA -DSYSACCT -DINET -DISO -DNFSSERVER -DNFSCLIENT -DUFS -DGATEWAY -DCHECK    -o SHARED/iecd.o ../dev/iec/iecd.c
/usr/lib/cc -Zi./KERNCOMP ./KERNCOMP.h -c -Fn -R -D_KERNEL -j/usr/include,. -DQUOTA -DSYSACCT -DINET -DISO -DNFSSERVER -DNFSCLIENT -DUFS -DGATEWAY -DCHECK    -o SHARED/iecs.o ../dev/iec/iecs.c
/usr/lib/cc -Zi./KERNCOMP ./KERNCOMP.h -c -Fn -R -D_KERNEL -j/usr/include,. -DQUOTA -DSYSACCT -DINET -DISO -DNFSSERVER -DNFSCLIENT -DUFS -DGATEWAY -DCHECK    -o SHARED/md.o ../dev/md/md.c
rm -f SHARED/driverlib.a
ar qc SHARED/driverlib.a SHARED/badblk.o  SHARED/cent.o  SHARED/dk.o  SHARED/econet.o  SHARED/econetmc.o  SHARED/en_io.o  SHARED/es.o  SHARED/fdc.o  SHARED/fdc_fiq.o  SHARED/hawk.o  SHARED/hawkv12.o  SHARED/hawk_io.o  SHARED/iecd.o  SHARED/iecs.o  SHARED/if_ea.o  SHARED/if_ea_move.o  SHARED/if_en.o  SHARED/if_et.o  SHARED/if_ppp.o  SHARED/if_sl.o  SHARED/lbp_dev.o  SHARED/lbp_fiq.o  SHARED/md.o  SHARED/nym.o  SHARED/partition.o  SHARED/ps_copy.o  SHARED/ps_direct.o  SHARED/ps_dmac.o  SHARED/ps_driver.o  SHARED/ps_irq.o  SHARED/ps_scsi.o  SHARED/ps_sequential.o  SHARED/ps_soft_irq.o  SHARED/ps_xfer.o  SHARED/scsi.o  SHARED/tty_fifo.o  SHARED/tty_mki.o  SHARED/tty_pty.o  SHARED/tty_tb.o  SHARED/winc.o  SHARED/zilog.o
ranlib SHARED/driverlib.a
/usr/lib/cc -Zi./KERNCOMP ./KERNCOMP.h -c -Fn -R -D_KERNEL -j/usr/include,. -DQUOTA -DSYSACCT -DINET -DISO -DNFSSERVER -DNFSCLIENT -DUFS -DGATEWAY -DCHECK    ../sys/pty_data.c
/usr/lib/cc -Zi./KERNCOMP ./KERNCOMP.h -c -Fn -R -D_KERNEL -j/usr/include,. -DQUOTA -DSYSACCT -DINET -DISO -DNFSSERVER -DNFSCLIENT -DUFS -DGATEWAY -DCHECK    ../net/ppp_data.c
/usr/lib/cc -Zi./KERNCOMP ./KERNCOMP.h -c -Fn -R -D_KERNEL -j/usr/include,. -DQUOTA -DSYSACCT -DINET -DISO -DNFSSERVER -DNFSCLIENT -DUFS -DGATEWAY -DCHECK    ../net/sl_data.c
rm -f datalib.a
ar qc datalib.a pty_data.o  ppp_data.o  sl_data.o
ranlib datalib.a
/usr/ucb/cc -c -I/usr/include -o SHARED/compileversion.o SHARED/compileversion.c
ld -o SHARED/compileversion -L/usr/lib /usr/lib/crt0.o SHARED/compileversion.o -lc_n
/usr/ucb/cc -c -I/usr/include -o SHARED/copyversion.o SHARED/copyversion.c
ld -o SHARED/copyversion -L/usr/lib /usr/lib/crt0.o SHARED/copyversion.o -lc_n
rm -f linkopts
cp ../conf/`basename \`pwd\``linkopts linkopts
rm -f squeezecmd
cp ../conf/`basename \`pwd\``squeezecmd squeezecmd
chmod a+x squeezecmd
grep -v '^#' ../conf/symbols.raw | sed 's/^     //' | sort -u > SHARED/symbols.sort
Loading...
root #1 special:                                 0/1      vmunix
text    data    bss     dec     hex
688128  65536   20848   774512  bd170
Fri Jan  1 17:45:38 GMT0BST 1999
```

The result is in `M/vmunix`.  Copy this to `/vmunix` (having made a backup of the original!).

## Adding new drivers (such as ecide) to a kernel build

Add drivers as a subdirectory to `/usr/src/sys/dev`, for example:

~~~
# mkdir /usr/src/sys/dev/ecide
# cp -R .../riscix_ide/ecide* /usr/src/sys/dev/ecide/
~~~

There are various configuration changes required, in the Makefile and C source, to instruct the kernel to invoke the driver:

   - Modify/patch `conf/xcbconf.c` and `conf/conf.c`, to add callbacks into the driver,
   - Create definitions in `conf/Mdevconf.h` which build the callbacks in,
   - Add the driver objects to the Makefile's `DRIVER_OBJS` variable, and rules to build them.

The following do this for the ecide driver:

~~~
# cd /usr/src/sys
# patch -p0 < .../riscix_ide/rix_kern_build.patch
~~~

Then, rockin' the `make clean all` will bring you a new vmunix containing the driver.


# License

The file `ecide.c` was initially based on the `iecd.c` example block device supplied in the "RISC iX 1.2 kernel using the kernel binary distribution".  That work is:

Copyright (C) 1989,1991 Acorn Computers Ltd.

The source dates from a time before absolute licence clarity, but is offered with the statement that "the sources may be freely used as a basis for the construction of real device drivers, if desired."

`ecide_parts.h` borrows a struct, `ecide_parts.c` a routine, and `ecide_ataregs.h` some register #defines from NetBSD, and have copyrights as outlined in those files.

The remainder of the code is released under the MIT licence:

 Copyright (c) 2022 Matt Evans

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
