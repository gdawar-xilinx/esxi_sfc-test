# Copyright (c) 2017 - 2018, Solarflare Communications Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
# EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Makefile
#
# This builds the Userworld Tool binary for the example being packaged here.
#
# type:
#	make
# to build the Userworld Tool.
#
# Expected usage is to combine this Makefile with Makefile.user, then
# customize the resulting file to meet the specific needs.

# The UWTOOL is changed here to accommodate the UWTOOL_VERSION in produced
# VIB name. The logical name would be 'demo-tool-user-to-kernel', but when
# UWTOOL_VERSION gets appended, the total length exceeds the maximum of 35
# characters allowed in a VIB name.
UWTOOL = libsfvmk_mc_api.so
UWTOOL_VERSION = 1.0

# mgmtInterface.c is in DEV_COMMON_DIR, the build system looks
# there for sources as well as here.
UWTOOL_SOURCES = sfvmk_mgmt_interface.c mcdi.c
DEV_COMMON_DIR = ../native_drv

DESC_VENDOR_EMAIL=support@solarflare.com
DESC_VERSION=$(UWTOOL_VERSION)-03
DESC_VENDOR=Solarflare
DESC_SUMMARY=Management-to-driver module interface library
DESC_DESCRIPTION=User library to communicate with a SFVMK driver module
DESC_URLS=descriptor-urls.xml
DESC_DEPENDS=descriptor-depends.xml
DESC_CONFLICTS=
DESC_REPLACES=
DESC_PROVIDES=
DESC_MAINT=1
DESC_LIVE_INSTALL_ALLOWED=true
DESC_LIVE_REMOVE_ALLOWED=true
DESC_STATELESS_READY=true

DESC_ACCEPTANCE_LEVEL=partner

# When this is not empty, the infrastructure will look for the files
# $(DEV_SIGNING_PREFIX).key and $(DEV_SIGNING_PREFIX).cert.  If the
# prefix contains a slash, it is assumed to be a full path to the
# directory containing the key/cert files, and that simply appending
# ".key" and ".cert" will give the actual file names.  Otherwise, it
# looks in the vib-suite installation directory's "testcerts"
# directory for the files.
DEV_SIGNING_PREFIX=vmpartner

BULL_KBURL=http://www.solarflare.com
BULL_PLATFORMS=bulletin-platforms.url
BULL_VENDOR_CODE=solarflare

# Remove the below include statement when appending Makefile.user
include Makefile.user
