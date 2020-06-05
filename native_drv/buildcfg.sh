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

#!/bin/bash -eu
#############

#---------------------------------------------------------------------------#
#              Script to support multi os compilation
#---------------------------------------------------------------------------#

#---------------------------------------------------------------------------#
# Supported build platforms :
#   ESXI 6.0/6.5/6.7/7.0
#
# This script selects appropiate build files [sfvmk.sc and tool-defs.inc]
# for the selected build platform and it extracts following informations
# which would be used in the Makefile.
#
#   NDDK_VER  : Version of installed native DDK
#   NDDK_DIR  : NDDK installation dir name
#   BUILD_NUM : Build number of native DDK
#
#---------------------------------------------------------------------------#

# Check NDDK Version
PWD=$(pwd)

# Retrieve NDDK directory name
NDDK_DIR=$(echo ${PWD} | sed 's/\//\ /g' | awk '{print $3}')

# Retrieve NDDK version
NDDK_VER=$(echo $NDDK_DIR | cut -c 11-13)


# Retrieve NDDK build number
BUILD_NUM=$(echo $NDDK_DIR | cut -c 17-24)

# Retrieve NDDK build number
BUILD_VER=$(echo $NDDK_DIR | cut -c 11-24)

# Perform sanity Checks
sanityChecks () {
    # Check supported NDDK version
    if [ "$NDDK_VER" != "6.0" ] && [ "$NDDK_VER" != "6.5" ] && [ "$NDDK_VER" != "6.7" ] && [ "$NDDK_VER" != "7.0" ]; then
      echo "UNSUPPORTED NDDK version '${NDDK_VER}' found"
    fi
}

get_ndddir () {
    echo ${NDDK_DIR} | sed -e 's/^[ \t]*//' -e 's/[ \t]*$//'
}

get_nddkver () {
    echo ${NDDK_VER} | sed -e 's/^[ \t]*//' -e 's/[ \t]*$//'
}

get_buildnum () {
    echo ${BUILD_NUM} | sed -e 's/^[ \t]*//' -e 's/[ \t]*$//'
}

get_buildver () {
    echo ${BUILD_VER} | sed -e 's/-/./g'
}

cfg_sfvmksc () {
    if [ "$NDDK_VER" == "6.0" ]; then
       cp 6.0-sfvmk sfvmk.sc
    elif [ "$NDDK_VER" == "6.5" ] || [ "$NDDK_VER" == "6.7" ]; then
       # Same build parameters are being used for 6.5 and 6.7
       cp 6.5-sfvmk sfvmk.sc
    elif [ "$NDDK_VER" == "7.0" ]; then
       cp 7.0-sfvmk sfvmk.sc
    fi
}

cfg_toolscfg () {
    if [ "$NDDK_VER" == "6.0" ]; then
       cp 6.0-tool-defs.inc tool-defs.inc
    elif [ "$NDDK_VER" == "6.5" ] || [ "$NDDK_VER" == "6.7" ]; then
       # Same tool versions are being used for 6.5 and 6.7
       cp 6.5-tool-defs.inc tool-defs.inc
    elif [ "$NDDK_VER" == "7.0" ]; then
       cp 7.0-tool-defs.inc tool-defs.inc
    fi
}

sanityChecks
case "$1" in
    CFG_SFVMKBUILD)
        cfg_toolscfg
        cfg_sfvmksc
        ;;
    GET_NDDKDIR)
        get_ndddir
        ;;
    GET_NDDKVER)
        get_nddkver
        ;;
    GET_NDDKBUILDNUM)
        get_buildnum
        ;;
    GET_NDDKBUILDVER)
        get_buildver
        ;;
    *)
        ;;
esac
