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

CAYMAN_ESX_GDB_DIR = $(shell /bin/rpm -q --qf "%{instprefixes}\n" vmware-esx-cayman_esx_gdb-24f65a7191349603bb2c712d51114d75e92e5065-toolchain)
export GOBUILD_CAYMAN_ESX_GDB_ROOT = $(CAYMAN_ESX_GDB_DIR)
CAYMAN_ESX_TOOLCHAIN_DIR = $(shell /bin/rpm -q --qf "%{instprefixes}\n" vmware-esx-cayman_esx_toolchain-9307983f3985bde80570de52ee3b8b1e0b7b6c85-toolchain)
export GOBUILD_CAYMAN_ESX_TOOLCHAIN_ROOT = $(CAYMAN_ESX_TOOLCHAIN_DIR)
CAYMAN_ESX_PYCPARSER_DIR = $(shell /bin/rpm -q --qf "%{instprefixes}\n" vmware-esx-cayman_esx_pycparser-a27d07068e84310e52d3473c7267a700ac4d4d3f-toolchain)
export GOBUILD_CAYMAN_ESX_PYCPARSER_ROOT = $(CAYMAN_ESX_PYCPARSER_DIR)
CAYMAN_ESX_NCURSES_DIR = $(shell /bin/rpm -q --qf "%{instprefixes}\n" vmware-esx-cayman_esx_ncurses-a09432d1ad9e9327bda85fa9293f8d2f980dedd6-toolchain)
export GOBUILD_CAYMAN_ESX_NCURSES_ROOT = $(CAYMAN_ESX_NCURSES_DIR)
CAYMAN_ESX_GLIBC_DIR = $(shell /bin/rpm -q --qf "%{instprefixes}\n" vmware-esx-cayman_esx_glibc-7fc626201c38c3a7e2472179c969a565b101f7ba-toolchain)
export GOBUILD_CAYMAN_ESX_GLIBC_ROOT = $(CAYMAN_ESX_GLIBC_DIR)
CAYMAN_PYTHON_DIR = $(shell /bin/rpm -q --qf "%{instprefixes}\n" vmware-esx-cayman_python-bc9711fc81a21c5629bda33c48e1f301ff43cbae-toolchain)
export GOBUILD_CAYMAN_PYTHON_ROOT = $(CAYMAN_PYTHON_DIR)
VMW_COMPONENTS = cayman_esx_gdb cayman_esx_toolchain cayman_esx_pycparser cayman_esx_ncurses cayman_esx_glibc cayman_python
VMW_COMPONENT_DIRS = $(CAYMAN_ESX_GDB_DIR) $(CAYMAN_ESX_TOOLCHAIN_DIR) $(CAYMAN_ESX_PYCPARSER_DIR) $(CAYMAN_ESX_NCURSES_DIR) $(CAYMAN_ESX_GLIBC_DIR) $(CAYMAN_PYTHON_DIR)

AWK = /build/toolchain/lin32/gawk-3.1.6/bin/awk
BASH = /build/toolchain/lin32/bash-3.2/bin/bash
CP = /build/toolchain/lin32/coreutils-5.97/bin/cp
ECHO = /build/toolchain/lin32/coreutils-5.97/bin/echo
GREP = /build/toolchain/lin32/grep-2.5.1a/bin/grep
GZIP = /build/toolchain/lin32/gzip-1.3.5/bin/gzip
MAKE = /build/toolchain/lin32/make-3.81/bin/make
MKDIR = /build/toolchain/lin32/coreutils-5.97/bin/mkdir
MV = /build/toolchain/lin32/coreutils-5.97/bin/mv
PERL = /build/toolchain/lin32/perl-5.8.8/bin/perl
RM = /build/toolchain/lin32/coreutils-5.97/bin/rm
SED = /build/toolchain/lin32/sed-4.1.5/bin/sed
TR = /build/toolchain/lin32/coreutils-8.6/bin/tr

