#
# app_konference
#
# A channel independent conference application for Asterisk
#
# Copyright (C) 2002,2003 Junghanns.NET GmbH
#
# This program may be modified and distributed under the
# terms of the GNU General Public License. You should have received
# a copy of the GNU General Public License along with this
# program; if not, write to the Free Software Foundation, Inc.
# 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#

# asterisk source directory
ASTERISK_SRC_DIR =

# asterisk release or subversion working copy
ASTERISK_SRC_VERSION = $(shell if [ -e $(ASTERISK_SRC_DIR)/.version ] ; then echo release; elif [ -e $(ASTERISK_SRC_DIR)/.svn ] ; then echo subversion; else echo unknown; fi)

# asterisk release version or subversion branch
ifeq ($(ASTERISK_SRC_VERSION),release)
 ASTERISK = $(shell cat $(ASTERISK_SRC_DIR)/.version | cut -d "." -f1,2 | sed -e 's/\.//')
else
 ifeq ($(ASTERISK_SRC_VERSION),subversion)
  ASTERISK = $(shell svn info $(ASTERISK_SRC_DIR) | grep URL | sed -e 's:^.*/::' | cut -d "." -f 1,2 | sed -e 's/\.//')
 else
  $(warning Asterisk source version is $(ASTERISK_SRC_VERSION))
  $(error Modify the source directory variable in the Makefile or set it on the command line)
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

# cache_control_blocks ( 0 == OFF, 1 == ON )
CACHE_CONTROL_BLOCKS ?= 1

# channel table size
CHANNEL_TABLE_SIZE ?= 997

# conference table size
CONFERENCE_TABLE_SIZE ?= 199

# schedule conference threads fifo ( 0 == OFF, 1 == ON )
REALTIME ?= 1

# silence detection ( 0 = OFF 1 = libwebrtc 2 = libspeex )
SILDET := 1

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
#DEBUG := -g

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
CPPFLAGS += -DCHANNEL_TABLE_SIZE=$(CHANNEL_TABLE_SIZE)
CPPFLAGS += -DCONFERENCE_TABLE_SIZE=$(CONFERENCE_TABLE_SIZE)
CPPFLAGS += -DONEMIXTHREAD

#
#
# Uncomment this if you want to mix frames using gcc vector extensions
#
# CPPFLAGS += -DVECTORS
#

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

ifeq ($(CACHE_CONTROL_BLOCKS), 1)
CPPFLAGS += -DCACHE_CONTROL_BLOCKS
endif

ifeq ($(REALTIME), 1)
CPPFLAGS += -DREALTIME
endif

#
# additional flag values for silence detection
#

ifeq ($(SILDET), 1)
OBJS += libwebrtc/get_scaling_square.o libwebrtc/division_operations.o libwebrtc/energy.o libwebrtc/vad_core.o libwebrtc/vad_filterbank.o libwebrtc/vad_gmm.o libwebrtc/vad_sp.o libwebrtc/webrtc_vad.o
INCS += libwebrtc/signal_processing_library.h libwebrtc/spl_inl.h libwebrtc/webrtc_vad.h libwebrtc/vad_core.h libwebrtc/vad_defines.h libwebrtc/vad_filterbank.h libwebrtc/vad_gmm.h libwebrtc/vad_sp.h
CPPFLAGS += -Ilibwebrtc -DSILDET=1
else ifeq ($(SILDET), 2)
OBJS += libspeex/preprocess.o libspeex/misc.o libspeex/smallft.o
INCS += libspeex/speex_preprocess.h libspeex/smallft.h libspeex/misc.h
CPPFLAGS += -Ilibspeex -DSILDET=2
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
	$(CC) $(SOLINK) -o $@ $(OBJS)

install:
	$(INSTALL) -m 755 $(TARGET) $(INSTALL_MODULES_DIR)
