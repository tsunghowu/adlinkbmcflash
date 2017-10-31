#
# This file is part of the flashrom project.
#
# Copyright (C) 2005 coresystems GmbH <stepan@coresystems.de>
# Copyright (C) 2009,2010,2012 Carl-Daniel Hailfinger
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
#

PROGRAM = bmcflash

###############################################################################
# Defaults for the toolchain.

# If you want to cross-compile, just run e.g.
# make CC=i586-pc-msdosdjgpp-gcc
# You may have to specify STRIP/AR/RANLIB as well.
#
# Note for anyone editing this Makefile: gnumake will happily ignore any
# changes in this Makefile to variables set on the command line.
STRIP   ?= strip
INSTALL = install
DIFF    = diff
PREFIX  ?= /usr/local
MANDIR  ?= $(PREFIX)/share/man
CFLAGS  ?= -Os -Wall -Wshadow
EXPORTDIR ?= .
RANLIB  ?= ranlib
PKG_CONFIG ?= pkg-config
BUILD_DETAILS_FILE ?= build_details.txt

# The following parameter changes the default programmer that will be used if there is no -p/--programmer
# argument given when running flashrom. The predefined setting does not enable any default so that every
# user has to declare the programmer he wants to use on every run. The rationale for this to be not set
# (to e.g. the internal programmer) is that forgetting to specify this when working with another programmer
# easily puts the system attached to the default programmer at risk (e.g. you want to flash coreboot to another
# system attached to an external programmer while the default programmer is set to the internal programmer, and
# you forget to use the -p parameter. This would (try to) overwrite the existing firmware of the computer
# running flashrom). Please do not enable this without thinking about the possible consequences. Possible
# values are those specified in enum programmer in programmer.h (which depend on other CONFIG_* options
# evaluated below, namely those that enable/disable the various programmers).
# Compilation will fail for unspecified values.
CONFIG_DEFAULT_PROGRAMMER ?= PROGRAMMER_INVALID
# The following adds a default parameter for the default programmer set above (only).
CONFIG_DEFAULT_PROGRAMMER_ARGS ?= ''
# Example: compiling with
#   make CONFIG_DEFAULT_PROGRAMMER=PROGRAMMER_SERPROG CONFIG_DEFAULT_PROGRAMMER_ARGS="dev=/dev/ttyUSB0:1500000"
# would make executing './flashrom' (almost) equivialent to './flashrom -p serprog:dev=/dev/ttyUSB0:1500000'.

# If your compiler spits out excessive warnings, run make WARNERROR=no
# You shouldn't have to change this flag.
WARNERROR ?= yes

ifeq ($(WARNERROR), yes)
CFLAGS += -Werror
endif

ifdef LIBS_BASE
PKG_CONFIG_LIBDIR ?= $(LIBS_BASE)/lib/pkgconfig
override CPPFLAGS += -I$(LIBS_BASE)/include
override LDFLAGS += -L$(LIBS_BASE)/lib -Wl,-rpath -Wl,$(LIBS_BASE)/lib
endif

ifeq ($(CONFIG_STATIC),yes)
override PKG_CONFIG += --static
override LDFLAGS += -static
endif

# Set LC_ALL=C to minimize influences of the locale.
# However, this won't work for the majority of relevant commands because they use the $(shell) function and
# GNU make does not relay variables exported within the makefile to their evironment.
LC_ALL=C
export LC_ALL

dummy_for_make_3_80:=$(shell printf "Build started on %s\n\n" "$$(date)" >$(BUILD_DETAILS_FILE))

# Provide an easy way to execute a command, print its output to stdout and capture any error message on stderr
# in the build details file together with the original stdout output.
debug_shell = $(shell export LC_ALL=C ; { echo 'exec: export LC_ALL=C ; { $(1) ; }' >&2;  { $(1) ; } | tee -a $(BUILD_DETAILS_FILE) ; echo >&2 ; } 2>>$(BUILD_DETAILS_FILE))

###############################################################################
# General OS-specific settings.
# 1. Prepare for later by gathering information about host and target OS
# 2. Set compiler flags and parameters according to OSes
# 3. Likewise verify user-supplied CONFIG_* variables.

# HOST_OS is only used to work around local toolchain issues.
HOST_OS ?= $(shell uname)
ifeq ($(HOST_OS), MINGW32_NT-5.1)
# Explicitly set CC = gcc on MinGW, otherwise: "cc: command not found".
CC = gcc
endif
ifneq ($(HOST_OS), SunOS)
STRIP_ARGS = -s
endif

# Determine the destination OS.
# IMPORTANT: The following line must be placed before TARGET_OS is ever used
# (of course), but should come after any lines setting CC because the line
# below uses CC itself.
override TARGET_OS := $(strip $(call debug_shell,$(CC) $(CPPFLAGS) -E os.h 2>/dev/null | grep -v '^\#' | grep '"' | cut -f 2 -d'"'))

ifeq ($(TARGET_OS), libpayload)
ifeq ($(MAKECMDGOALS),)
.DEFAULT_GOAL := libflashrom.a
$(info Setting default goal to libflashrom.a)
endif
FLASHROM_CFLAGS += -DSTANDALONE
ifeq ($(CONFIG_DUMMY), yes)
UNSUPPORTED_FEATURES += CONFIG_DUMMY=yes
else
override CONFIG_DUMMY = no
endif
endif

###############################################################################
# General architecture-specific settings.
# Like above for the OS, below we verify user-supplied options depending on the target architecture.

# Determine the destination processor architecture.
# IMPORTANT: The following line must be placed before ARCH is ever used
# (of course), but should come after any lines setting CC because the line
# below uses CC itself.
override ARCH := $(strip $(call debug_shell,$(CC) $(CPPFLAGS) -E archtest.c 2>/dev/null | grep -v '^\#' | grep '"' | cut -f 2 -d'"'))

FEATURE_CFLAGS += $(call debug_shell,grep -q "LINUX_I2C_SUPPORT := yes" .features && printf "%s" "-D'CONFIG_MSTARDDC_SPI=1'")
NEED_LINUX_I2C += CONFIG_MSTARDDC_SPI
PROGRAMMER_OBJS += cli_classic.o cli_output.o udelay.o bmc_update_lib.o ad_bmc_updater.o 

FEATURE_CFLAGS += $(call debug_shell,grep -q "UTSNAME := yes" .features && printf "%s" "-D'HAVE_UTSNAME=1'")

# We could use PULLED_IN_LIBS, but that would be ugly.
FEATURE_LIBS += $(call debug_shell,grep -q "NEEDLIBZ := yes" .libdeps && printf "%s" "-lz")

LIBFLASHROM_OBJS = $(PROGRAMMER_OBJS)
OBJS = $(LIBFLASHROM_OBJS)

all: features $(PROGRAM)$(EXEC_SUFFIX)

$(PROGRAM)$(EXEC_SUFFIX): $(OBJS)
	$(CC) $(LDFLAGS) -o $(PROGRAM)$(EXEC_SUFFIX) $(OBJS) $(LIBS) $(FEATURE_LIBS)

libflashrom.a: $(LIBFLASHROM_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

# TAROPTIONS reduces information leakage from the packager's system.
# If other tar programs support command line arguments for setting uid/gid of
# stored files, they can be handled here as well.
TAROPTIONS = $(shell LC_ALL=C tar --version|grep -q GNU && echo "--owner=root --group=root")

%.o: %.c
	$(CC) -MMD $(CFLAGS) $(CPPFLAGS) $(FLASHROM_CFLAGS) $(FEATURE_CFLAGS) $(SVNDEF) -o $@ -c $<

# Make sure to add all names of generated binaries here.
# This includes all frontends and libflashrom.
# We don't use EXEC_SUFFIX here because we want to clean everything.
clean:
	rm -f $(PROGRAM) $(PROGRAM).exe libflashrom.a *.o *.d $(PROGRAM).8 $(PROGRAM).8.html $(BUILD_DETAILS_FILE)

distclean: clean
	rm -f .features .libdeps

strip: $(PROGRAM)$(EXEC_SUFFIX)
	$(STRIP) $(STRIP_ARGS) $(PROGRAM)$(EXEC_SUFFIX)

# to define test programs we use verbatim variables, which get exported
# to environment variables and are referenced with $$<varname> later

define COMPILER_TEST
int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	return 0;
}
endef
export COMPILER_TEST

compiler: featuresavailable
	@printf "Checking for a C compiler... " | tee -a $(BUILD_DETAILS_FILE)
	@echo "$$COMPILER_TEST" > .test.c
	@printf "\nexec: %s\n" "$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) .test.c -o .test$(EXEC_SUFFIX)" >>$(BUILD_DETAILS_FILE)
	@{ { { { { $(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) .test.c -o .test$(EXEC_SUFFIX) >&2 && \
		echo "found." || { echo "not found."; \
		rm -f .test.c .test$(EXEC_SUFFIX); exit 1; }; } 2>>$(BUILD_DETAILS_FILE); echo $? >&3 ; } | tee -a $(BUILD_DETAILS_FILE) >&4; } 3>&1;} | { read rc ; exit ${rc}; } } 4>&1
	@rm -f .test.c .test$(EXEC_SUFFIX)
	@printf "Target arch is "
	@# FreeBSD wc will output extraneous whitespace.
	@echo $(ARCH)|wc -w|grep -q '^[[:blank:]]*1[[:blank:]]*$$' ||	\
		( echo "unknown. Aborting."; exit 1)
	@printf "%s\n" '$(ARCH)'
	@printf "Target OS is "
	@# FreeBSD wc will output extraneous whitespace.
	@echo $(TARGET_OS)|wc -w|grep -q '^[[:blank:]]*1[[:blank:]]*$$' ||	\
		( echo "unknown. Aborting."; exit 1)
	@printf "%s\n" '$(TARGET_OS)'

.features: features

# If a user does not explicitly request a non-working feature, we should
# silently disable it. However, if a non-working (does not compile) feature
# is explicitly requested, we should bail out with a descriptive error message.
# We also have to check that at least one programmer driver is enabled.
featuresavailable:
ifeq ($(PROGRAMMER_OBJS),)
	@echo "You have to enable at least one programmer driver!"
	@false
endif
ifneq ($(UNSUPPORTED_FEATURES), )
	@echo "The following features are unavailable on your machine: $(UNSUPPORTED_FEATURES)"
	@false
endif

define LINUX_I2C_TEST
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

int main(int argc, char **argv)
{
	(void) argc;
	(void) argv;
	return 0;
}
endef
export LINUX_I2C_TEST

features: compiler
	@echo "FEATURES := yes" > .features.tmp
ifneq ($(NEED_LINUX_I2C), )
	@printf "Checking if Linux I2C headers are present... " | tee -a $(BUILD_DETAILS_FILE)
	@echo "$$LINUX_I2C_TEST" > .featuretest.c
	@printf "\nexec: %s\n" "$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) .featuretest.c -o .featuretest$(EXEC_SUFFIX)" >>$(BUILD_DETAILS_FILE)
	@ { $(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) .featuretest.c -o .featuretest$(EXEC_SUFFIX) >&2 && \
		( echo "yes."; echo "LINUX_I2C_SUPPORT := yes" >> .features.tmp ) ||	\
		( echo "no."; echo "LINUX_I2C_SUPPORT := no" >> .features.tmp ) } \
		2>>$(BUILD_DETAILS_FILE) | tee -a $(BUILD_DETAILS_FILE)
endif
	@printf "Checking for utsname support... " | tee -a $(BUILD_DETAILS_FILE)
	@echo "$$UTSNAME_TEST" > .featuretest.c
	@printf "\nexec: %s\n" "$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) .featuretest.c -o .featuretest$(EXEC_SUFFIX)" >>$(BUILD_DETAILS_FILE)
	@ { $(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) .featuretest.c -o .featuretest$(EXEC_SUFFIX) >&2 && \
		( echo "found."; echo "UTSNAME := yes" >> .features.tmp ) ||	\
		( echo "not found."; echo "UTSNAME := no" >> .features.tmp ) } 2>>$(BUILD_DETAILS_FILE) | tee -a $(BUILD_DETAILS_FILE)
	@$(DIFF) -q .features.tmp .features >/dev/null 2>&1 && rm .features.tmp || mv .features.tmp .features
	@rm -f .featuretest.c .featuretest$(EXEC_SUFFIX)


.PHONY: all install clean distclean 

# Disable implicit suffixes and built-in rules (for performance and profit)
.SUFFIXES:

-include $(OBJS:.o=.d)
