# ***********************************************************************
# Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
# Use is subject to license terms.
#
# -- Solarflare Confidential
# **********************************************************************/

#-------------------------------------------------------------------------------
# Makefile
#
#    Provides a wrapper to compile drivers in the VMKAPI DDK devkit.
#
#    This is a generic Makefile and no changes are required to compile
#    drivers provided that a single .sc file exists in the current directory.
#-------------------------------------------------------------------------------

#-------------------------------------------------------------------------------
# Tools
#-------------------------------------------------------------------------------
$(shell sh buildcfg.sh CFG_SFVMKBUILD)
NDDK_VER = $(shell sh buildcfg.sh GET_NDDKVER)
NDDK_DIR = $(shell sh buildcfg.sh GET_NDDKDIR)
NDDK_BUILDNUM = $(shell sh buildcfg.sh GET_NDDKBUILDNUM)

include tool-defs.inc

#-------------------------------------------------------------------------------
# Parameters to the scons command
#-------------------------------------------------------------------------------
PRODUCT       = nativeddk

VMKAPI_DIR    = /opt/vmware/$(NDDK_DIR)
SRC           = $(VMKAPI_DIR)/src/
FIND_RPM      = $(VMKAPI_DIR)/tools/find_rpm_install_dir.sh
VIRTUAL_GOBUILD = $(VMKAPI_DIR)/tools/gobuild/nativeddk.cache

VIBTOOLS      = $(shell $(FIND_RPM) vmware-esx-vib-suite)

SCONS         = cd $(SRC); $(SRC)/scons/bin/scons
#FULL_PWD      = $(shell pwd)
REL_PWD       = $(shell pwd | $(SED) -e 's,$(SRC),,')
SC            = $(shell echo *.sc | $(AWK) '{print $$1}')
MANIFEST      = $(REL_PWD)/$(SC)
BUILDTYPE     = release
BUILD_NUMBER  = $(NDDK_BUILDNUM)
TARGET        = $(CURDIR)/build
SCONS_TARGET  = $(CURDIR)/.build
IMPORTED_CODE = $(CURDIR)/imported
DOXYGEN       = $(CURDIR)/../doxygen
DOXYGEN_TARGET= $(DOXYGEN)/doc
ESXI_DOXY_CFG = $(DOXYGEN)/esxi_doxy.cfg


# Map VIB acceptance level to canonical default signing-key file base name
ACCEPT_LEVEL = $(shell grep acceptance-level $(SRC)/$(MANIFEST))
ifneq ($(findstring accepted,$(ACCEPT_LEVEL)),)
    VMW_KC_NAME = accepted
else
ifneq ($(findstring partner,$(ACCEPT_LEVEL)),)
    VMW_KC_NAME = vmpartner
else
ifneq ($(findstring certified,$(ACCEPT_LEVEL)),)
    VMW_KC_NAME = vmware
else
ifeq ($(findstring community,$(ACCEPT_LEVEL)),)
    $(error invalid acceptance level $(ACCEPT_LEVEL))
endif
endif
endif
endif
ifneq ($(VMW_KC_NAME),)
    KEYPATH?=$(VIBTOOLS)/testcerts/$(VMW_KC_NAME).key
    CERTPATH?=$(VIBTOOLS)/testcerts/$(VMW_KC_NAME).cert
endif

# Driver requires common code files, import it from v5 repo
# Common code would be used from v5 tag for the released version of sfvmk
# or from the tip of v5 branch or tip of default branch for beta version
V5_TAG          = v4_13_2_1007
V5_BRANCH       = v4_13_2_sfvmk

ifeq ($(BUILDTYPE), beta)
V5_TARGET       = $(V5_BRANCH)
else
V5_TARGET       = $(V5_TAG)
endif

V5_URL          = http://source.uk.solarflarecom.com/hg/incoming/v5/rawfile/${V5_TARGET}
FILE            = export.files-common
IMPORTED_FILES  = $(shell cat ${FILE})
MAKEFILE_LIST   = $(IMPORTED_FILES)

all: doc vib

imported/%:
	@wget -q -O $@ $(V5_URL)/src/driver/common/$(@F)
	@chmod a-w $@

doc:
	@$(RM) -rf $(DOXYGEN_TARGET)
	@mkdir -p $(DOXYGEN_TARGET)
	@cd $(DOXYGEN_TARGET)
	@doxygen $(ESXI_DOXY_CFG)

imported_dir:
	@mkdir -p $(IMPORTED_CODE)

imported_code:	imported_dir $(IMPORTED_FILES)

vib:	imported_code
	@$(SCONS) native-driver-modules native-driver-vibs native-driver-bundle \
	       RELEASE_PACKAGES_DIR=$(TARGET) \
	       TOOLKIT_MANIFEST=$(MANIFEST) \
	       PRODUCT=$(PRODUCT) \
	       BUILDTYPE=$(BUILDTYPE) \
	       BUILD_NUMBER=$(BUILD_NUMBER) \
	       ESX_SIGN_BINARIES=0 \
	       VIBPATH=$(VIBTOOLS) \
	       BUILDROOT=$(SCONS_TARGET) \
	       VIRTUAL_GOBUILD=$(VIRTUAL_GOBUILD) \
               KEYPATH=$(KEYPATH) \
               CERTPATH=$(CERTPATH)

clean_imported:
	@echo Cleaning out imported common code files ...
	@$(RM) -rf $(IMPORTED_CODE)
	@echo Done.

clean_doxygen:
	@echo Cleaning out doxygen files ...
	@$(RM) -rf $(DOXYGEN_TARGET)
	@echo Done.

clean_cfg:
	@echo Cleaning out *.sc and tool-defs.inc files ...
	@$(RM) -rf *.sc
	@$(RM) -rf tool-defs.inc
	@echo Done.

clean_targets: clean_doxygen
	@echo Cleaning out build directories ...
	@$(RM) -rf $(TARGET) $(SCONS_TARGET)
	@echo Done.

clean: clean_targets clean_cfg

cleanall: clean_imported clean_targets clean_cfg
