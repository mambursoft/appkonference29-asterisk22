#
# Makefile, based on the Asterisk Makefile, Coypright (C) 1999, Mark Spencer
#
# Copyright (C) 2002,2003 Junghanns.NET GmbH
#
# Klaus-Peter Junghanns <kapejod@ns1.jnetdns.de>
#
# This program is free software and may be modified and
# distributed under the terms of the GNU Public License.
#

# asterisk source directory
ASTERISK_SRC_DIR =

# asterisk release or subversion working copy
ASTERISK_SRC_VERSION = $(shell if [ -e $(ASTERISK_SRC_DIR)/.version ] ; then echo release; elif [ -e $(ASTERISK_SRC_DIR)/.svn ] ; then echo subversion; else echo wtf; fi)

# asterisk release version or subversion branch
ifeq ($(ASTERISK_SRC_VERSION),release)
 ASTERISK = $(shell cat $(ASTERISK_SRC_DIR)/.version | cut -d "." -f1,2 | sed -e 's/\.//')
else
 ifeq ($(ASTERISK_SRC_VERSION),subversion)
  ASTERISK = $(shell svn info $(ASTERISK_SRC_DIR) | grep URL | sed -e 's:^.*/::' | cut -d "." -f 1,2 | sed -e 's/\.//')
 else
  $(warning Asterisk source not found in directory: $(ASTERISK_SRC_DIR).)
  $(error Set your asterisk source directory in the Makefile or on the command line.)
 endif
endif

# asterisk include directory
ASTERISK_INCLUDE_DIR = $(ASTERISK_SRC_DIR)/include

# asterisk module directory
INSTALL_MODULES_DIR = /usr/lib/asterisk/modules

# module revision
REVISION = $(shell svnversion -n .)

#
# defines which can be passed on the command-line
#

# turn app_konference debugging on or off ( 0 == OFF, 1 == ON )
APP_KONFERENCE_DEBUG ?= 0

# turn cache_control_blocks on or off ( 0 == OFF, 1 == ON )
CACHE_CONTROL_BLOCKS ?= 1

# channel table size default
CHANNEL_TABLE_SIZE ?= 997

# conference table size default
CONFERENCE_TABLE_SIZE ?= 199

# schedule conference threads fifo ( 0 == OFF, 1 == ON )
REALTIME ?= 1

# turn app_konference texting on of off ( 0 == OFF, 1 == ON )
TEXT ?= 0

# turn app_konference video on of off ( 0 == OFF, 1 == ON )
VIDEO ?= 0

# turn app_konference dtmf on of off ( 0 == OFF, 1 == ON )
DTMF ?= 0

# 0 = OFF 1 = astdsp 2 = speex
SILDET := 2

#
# objects to build
#

OBJS = app_conference.o conference.o member.o frame.o cli.o
INCS = app_conference.h  cli.h  conf_frame.h  conference.h  frame.h  member.h
TARGET = app_konference.so

#
# compiler settings
#

#PROC = $(shell uname -m)
INSTALL = install

INCLUDE = -I$(ASTERISK_INCLUDE_DIR)
DEBUG := -g

#
# compiler flags
#

CFLAGS = -pipe -Wall -Wmissing-prototypes -Wmissing-declarations -MD -MP $(DEBUG)
#CFLAGS += -O3 -march=pentium3 -msse -mfpmath=sse,387 -ffast-math
# PERF: below is 10% faster than -O2 or -O3 alone.
#CFLAGS += -O3 -ffast-math -funroll-loops
# below is another 5% faster or so.
CFLAGS += -O3 -ffast-math -funroll-all-loops -fsingle-precision-constant
#CFLAGS += -mcpu=7450 -faltivec -mabi=altivec -mdynamic-no-pic
# adding -msse -mfpmath=sse has little effect.
#CFLAGS += -O3 -msse -mfpmath=sse
#CFLAGS += $(shell if $(CC) -march=$(PROC) -S -o /dev/null -xc /dev/null >/dev/null 2>&1; then echo "-march=$(PROC)"; fi)
CFLAGS += $(shell if uname -m | grep -q ppc; then echo "-fsigned-char"; fi)
CFLAGS += -fPIC

#
# preprocessor flags
#

CPPFLAGS = $(INCLUDE) -D_REENTRANT -D_GNU_SOURCE
CPPFLAGS += -DREVISION=\"$(REVISION)\"
CPPFLAGS += -DASTERISK=$(ASTERISK)
#CPPFLAGS += -DCRYPTO
CPPFLAGS += -DCHANNEL_TABLE_SIZE=$(CHANNEL_TABLE_SIZE)
CPPFLAGS += -DCONFERENCE_TABLE_SIZE=$(CONFERENCE_TABLE_SIZE)
CPPFLAGS += -DONEMIXTHREAD

#
# Uncomment this if you want G.729A support (need to have the actual codec installed)
#
# CPPFLAGS += -DAC_USE_G729A
#

#
# Uncomment this if you want G.722 support (requires asterisk 1.6 or 1.8)
#
# CPPFLAGS += -DAC_USE_G722
#

#
# Uncomment this if you want SPEEX support (need to have the actual codec installed)
#
# CPPFLAGS += -DAC_USE_SPEEX
#

ifeq ($(APP_KONFERENCE_DEBUG), 1)
CPPFLAGS += -DAPP_KONFERENCE_DEBUG
endif

ifeq ($(CACHE_CONTROL_BLOCKS), 1)
CPPFLAGS += -DCACHE_CONTROL_BLOCKS
endif

ifeq ($(REALTIME), 1)
CPPFLAGS += -DREALTIME
endif

ifeq ($(TEXT), 1)
CPPFLAGS += -DTEXT
endif

ifeq ($(VIDEO), 1)
CPPFLAGS += -DVIDEO
endif

ifeq ($(DTMF), 1)
CPPFLAGS += -DDTMF
endif

#
# additional flag values for silence detection
#

ifeq ($(SILDET), 2)
OBJS += libspeex/preprocess.o libspeex/misc.o libspeex/smallft.o
INCS += libspeex/speex_preprocess.h libspeex/smallft.h libspeex/misc.h
CPPFLAGS += -Ilibspeex -DSILDET=2
endif

ifeq ($(SILDET), 1)
CPPFLAGS += -DSILDET=1
endif

OSARCH=$(shell uname -s)
ifeq (${OSARCH},Darwin)
SOLINK=-dynamic -bundle -undefined suppress -force_flat_namespace
else
SOLINK=-shared -Xlinker -x
endif

DEPS += $(subst .o,.d,$(OBJS))

#
# targets
#

all: $(TARGET)

.PHONY: clean
clean:
	$(RM) $(TARGET) $(OBJS) $(DEPS)

$(OBJS): $(INCS)

$(TARGET): $(OBJS)
	$(CC) -pg $(SOLINK) -o $@ $(OBJS)

install:
	$(INSTALL) -m 755 $(TARGET) $(INSTALL_MODULES_DIR)
