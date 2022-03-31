VERBOSE ?= 1
DEBUG ?= 0

ACC = $(GCCSDK_INSTALL_CROSSBIN)/arm-unknown-riscos-gcc
AOC = $(GCCSDK_INSTALL_CROSSBIN)/arm-unknown-riscos-objcopy
ACFLAGS = -mlibscl
#-ffreestanding -Wl,--gc-sections -nostdlib -Wl,-Ttext=0x8000
ACLINKFLAGS =
#-static

BUILD_DATE=$(shell date)

ACFLAGS += -DBUILD_DATE="\"$(BUILD_DATE)\""
ACFLAGS += -DDEBUG=$(DEBUG)

ifeq ($(VERBOSE), 1)
        ACFLAGS += -DVERBOSE
endif

OBJECTS = test_ide.o
OBJECTS += ecide_io.o
OBJECTS += ecide_parts.o

TEST_TARGET = IDETest,ff8

all:    $(TEST_TARGET)

$(TEST_TARGET):	$(OBJECTS)
	$(ACC) $(ACFLAGS) $(ACLINKFLAGS) $^ -o $@

%.o:	%.S
	$(ACC) $(ACFLAGS) -c $^ -o $@

%.o:	%.c
	$(ACC) $(ACFLAGS) -c $^ -o $@

clean:
	rm -f $(TEST_TARGET) $(OBJECTS) *~
