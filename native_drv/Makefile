
# Copyright 2017-2018 Solarflare Inc.
# All rights reserved. -- Solarflare Confidential

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
include tool-defs.inc

#-------------------------------------------------------------------------------
# Parameters to the scons command
#-------------------------------------------------------------------------------
PRODUCT      = nativeddk

VMKAPI_DIR   = $(shell /bin/rpm -q --qf '%{INSTPREFIXES}\n' vmware-esx-nativeddk-devtools-6.5.0-0.0.4598673)
SRC          = $(VMKAPI_DIR)/src/
FIND_RPM     = $(VMKAPI_DIR)/tools/find_rpm_install_dir.sh
VIRTUAL_GOBUILD = $(VMKAPI_DIR)/tools/gobuild/nativeddk.cache

VIBTOOLS     = $(shell $(FIND_RPM) vmware-esx-vib-suite)

SCONS        = cd $(SRC); $(SRC)/scons/bin/scons
FULL_PWD     = $(shell pwd)
REL_PWD      = $(shell pwd | $(SED) -e 's,$(SRC),,')
SC           = $(shell echo *.sc | $(AWK) '{print $$1}')
MANIFEST     = $(REL_PWD)/$(SC)
BUILDTYPE    = release
BUILD_NUMBER = 4598673
BUILD        = build
SCONS_BUILD  = .build
TARGET       = $(FULL_PWD)/$(BUILD)
SCONS_TARGET = $(FULL_PWD)/$(SCONS_BUILD)

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

all vib:
	@$(SCONS) native-driver-modules native-driver-vibs native-driver-bundle \
	       RELEASE_PACKAGES_DIR=$(TARGET) \
	       TOOLKIT_MANIFEST=$(MANIFEST) \
	       PRODUCT=$(PRODUCT) \
	       BUILDTYPE=$(BUILDTYPE) \
	       BUILD_NUMBER=$(BUILD_NUMBER) \
	       ESX_SIGN_BINARIES=0 \
	       SKIP_BIG_BROTHER=1 \
	       VIBPATH=$(VIBTOOLS) \
	       BUILDROOT=$(SCONS_TARGET) \
	       VIRTUAL_GOBUILD=$(VIRTUAL_GOBUILD) \
               KEYPATH=$(KEYPATH) \
               CERTPATH=$(CERTPATH)

clean:
	@echo Cleaning out build directories ...
	@$(RM) -rf $(TARGET) $(SCONS_TARGET)
	@echo Done.
