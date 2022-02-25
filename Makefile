# Makefile for RISC OS !TestIDE -- this does not build the
# RISCiX driver!
#
# Needs GCCSDK -- set GCCSDK_INSTALL_CROSSBIN path to tools
#
# Apr 2022
#
# Copyright (c) 2022 Matt Evans
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

VERBOSE ?= 1
DEBUG ?= 0

ACC = $(GCCSDK_INSTALL_CROSSBIN)/arm-unknown-riscos-gcc
AOC = $(GCCSDK_INSTALL_CROSSBIN)/arm-unknown-riscos-objcopy
ACFLAGS = -mlibscl -I.
#-ffreestanding -Wl,--gc-sections -nostdlib -Wl,-Ttext=0x8000
ACLINKFLAGS =
#-static

BUILD_DATE=$(shell date)

ACFLAGS += -DBUILD_DATE="\"$(BUILD_DATE)\""
ACFLAGS += -DDEBUG=$(DEBUG)

ifeq ($(VERBOSE), 1)
        ACFLAGS += -DVERBOSE
endif

OBJECTS = test/test_ide.o
OBJECTS += ecide_io.o
OBJECTS += ecide_io_asm.o
OBJECTS += ecide_parts.o

TEST_TARGET = test/\!TestIDE/\!RunImage,ff8

all:    $(TEST_TARGET)

$(TEST_TARGET):	$(OBJECTS)
	$(ACC) $(ACFLAGS) $(ACLINKFLAGS) $^ -o $@

%.o:	%.S
	$(ACC) $(ACFLAGS) -c $^ -o $@

%.o:	%.s
	$(ACC) $(ACFLAGS) -c $^ -o $@

%.o:	%.c
	$(ACC) $(ACFLAGS) -c $^ -o $@

clean:
	rm -f $(TEST_TARGET) $(OBJECTS) *~
