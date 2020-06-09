# Copyright (c) 2017-2020 Xilinx, Inc.
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


# UWTOOL_VERSION gets appended, the total length exceeds the maximum of 35
# characters allowed in a VIB name.
UWTOOL = sfvmkcli
UWTOOL_VERSION = 2.4.0.0012

NDDK_VER  := $(shell sh ../native_drv/buildcfg.sh GET_NDDKVER)
BUILD_VER  := $(shell sh ../native_drv/buildcfg.sh GET_NDDKBUILDVER)

# mgmtInterface.c is in DEV_COMMON_DIR, the build system looks
# there for sources as well as here.
UWTOOL_SOURCES = main.c sfvmk_mgmt_interface.c sf_utils.c sf_firmware.c \
		sf_jlib/sf_jlib_parser.c sf_jlib/json.c

DEV_COMMON_DIR = ../native_drv

PLUGIN_XML = esxcli-sfvmk.xml

DESC_VENDOR_EMAIL=support@solarflare.com
DESC_VERSION=$(UWTOOL_VERSION)-06
ifeq ($(shell expr $(NDDK_VER) \>= 7.0), 1)
DESC_VENDOR=Solarflare
else
DESC_VENDOR=SFC
endif
DESC_SUMMARY=esxcli extension for SFVMK driver
DESC_DESCRIPTION=Solarflare Userworld Tool to communicate with a VMKAPI driver module
DESC_URLS=descriptor-urls.xml
DESC_DEPENDS=$(NDDK_VER)-descriptor-depends.xml
DESC_CONFLICTS=
DESC_REPLACES=
DESC_PROVIDES=
DESC_MAINT=1
DESC_LIVE_INSTALL_ALLOWED=true
DESC_LIVE_REMOVE_ALLOWED=true
DESC_STATELESS_READY=true

DESC_ACCEPTANCE_LEVEL=partner

ifeq ($(shell expr $(NDDK_VER) \>= 7.0), 1)
KEYPATH=/opt/vmware/vibtools/testcerts/vmpartner.key
CERTPATH=/opt/vmware/vibtools/testcerts/vmpartner.cert
endif

DEV_SIGNING_PREFIX=vmpartner

DESC_PAYLOAD_NAME=sfvmk-esxcli
BULL_KBURL=http://www.solarflare.com
BULL_PLATFORMS=bulletin-platforms.url
BULL_VENDOR_CODE=SFC

ifeq ($(shell expr $(NDDK_VER) \>= 7.0), 1)
BULL_CNS_NAME=SFC-ESXCLI
BULL_CNS_STRING=$(DESC_DESCRIPTION)
BULL_CVS_VERSION=$(UWTOOL_VERSION)-$(BUILD_VER)
BULL_CVS_STRING=$(UWTOOL_VERSION)
endif

DEV_beta_COMPILEFLAGS = -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast
DEV_release_COMPILEFLAGS = -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast

# Remove the below include statement when appending Makefile.user
include $(NDDK_VER)-Makefile.user
