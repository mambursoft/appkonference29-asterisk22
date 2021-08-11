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

ifndef	ASTERISK_SRC_DIR
  $(warning Asterisk source directory is not set)
  $(error Modify the source directory variable in the Makefile or set it on the command line)
endif

# asterisk version
ASTERISK_SRC_VERSION = $(shell if [ -e $(ASTERISK_SRC_DIR)/.version ] ; then cat $(ASTERISK_SRC_DIR)/.version | awk -F. '{printf "%01d%02d",$$1,$$2}' ; fi)

ifeq	($(ASTERISK_SRC_VERSION),)
  $(warning Asterisk version is not set)
  $(error Modify the version variable in the Makefile or set it on the command line)
endif

AST_MODULE_SELF_SYM = $(shell if [ $(ASTERISK_SRC_VERSION) -ge 1400 ] ; then echo __internal_app_konference_self ; fi)

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

# score board ( 0 == OFF, 1 == ON )
SPEAKER_SCOREBOARD ?= 1

# score board table size
SPEAKER_SCOREBOARD_SIZE ?= 4096

# channel table size
CHANNEL_TABLE_SIZE ?= 997

# conference table size
CONFERENCE_TABLE_SIZE ?= 199

# silence detection ( 0 = OFF 2 = libspeex )
SILDET := 2

# video ( 0 = OFF 1 = ON)
VIDEO := 0

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
#CFLAGS += -std=gnu89 

#
# preprocessor flags
#

CPPFLAGS = $(INCLUDE) -D_REENTRANT -D_GNU_SOURCE
CPPFLAGS += -DREVISION=\"$(REVISION)\"
CPPFLAGS += -DASTERISK_SRC_VERSION=$(ASTERISK_SRC_VERSION)
CPPFLAGS += -DCHANNEL_TABLE_SIZE=$(CHANNEL_TABLE_SIZE)
CPPFLAGS += -DCONFERENCE_TABLE_SIZE=$(CONFERENCE_TABLE_SIZE)
CPPFLAGS += -DCACHE_CONF_FRAMES
#CPPFLAGS += -DCHECK_THREAD_FREQUENCY
CPPFLAGS += -DAC_USE_ULAW

ifneq	($(AST_MODULE_SELF_SYM),)
CPPFLAGS += -DAST_MODULE_SELF_SYM=$(AST_MODULE_SELF_SYM)
endif

#
#
# Uncomment this if you want to mix frames using gcc vector extensions
#
# CPPFLAGS += -DVECTORS
#

#
# Uncomment this if you want to use linux timerfd for timing
#
# CPPFLAGS += -DTIMERFD
#

#
# Uncomment this if you want to use *bsd kqueue for timing
#
# CPPFLAGS += -DKQUEUE
#

#
# Uncomment this if you want ALAW
#
# CPPFLAGS += -DAC_USE_ALAW
#

#
# Uncomment this if you want GSM
#
# CPPFLAGS += -DAC_USE_GSM
#

#
# Uncomment this if you want SPEEX (need to have the actual codec installed)
#
# CPPFLAGS += -DAC_USE_SPEEX
#

#
# Uncomment this if you want G.729A (need to have the actual codec installed)
#
# CPPFLAGS += -DAC_USE_G729A
#

#
# Uncomment this if you want G.722 (requires asterisk 1.6+)
#
# CPPFLAGS += -DAC_USE_G722
#

ifeq ($(CACHE_CONTROL_BLOCKS), 1)
CPPFLAGS += -DCACHE_CONTROL_BLOCKS
ifeq ($(SPEAKER_SCOREBOARD), 1)
CPPFLAGS += -DSPEAKER_SCOREBOARD -DSPEAKER_SCOREBOARD_SIZE=$(SPEAKER_SCOREBOARD_SIZE)
endif
endif

#
# additional flag values for silence detection
#

ifeq ($(SILDET), 2)
OBJS += libspeex/preprocess.o libspeex/misc.o libspeex/smallft.o
INCS += libspeex/speex_preprocess.h libspeex/smallft.h libspeex/misc.h
CPPFLAGS += -Ilibspeex -DSILDET=2
LIBS = -lm
endif

#
# additional flag values for video
#

ifeq ($(VIDEO), 1)
CPPFLAGS += -DVIDEO
INCLUDE += -I$(ASTERISK_SRC_DIR)/third-party/pjproject/source/pjlib/include
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
	$(CC) $(SOLINK) -o $@ $(OBJS) $(LIBS)

install:
	if [ -f $(TARGET) ]; then $(INSTALL) -m 755 $(TARGET) $(INSTALL_MODULES_DIR); fi
