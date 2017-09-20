# Copyright (c) 2013-2016 VMware, Inc.  All rights reserved.
# --VMware Confidential

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
UWTOOL_SOURCES = sfvmk_mgmtInterface.c mcdi.c
DEV_COMMON_DIR = ../native_drv

DESC_VENDOR_EMAIL=skaushik@solarflare.com
DESC_VERSION=$(UWTOOL_VERSION)-03
DESC_VENDOR=SolarFlare
DESC_SUMMARY=Management-to-driver module sample
DESC_DESCRIPTION=Sample Userworld Tool to communicate with a VMKAPI driver module
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
BULL_VENDOR_CODE=solarfalre

# Remove the below include statement when appending Makefile.user
include Makefile.user
