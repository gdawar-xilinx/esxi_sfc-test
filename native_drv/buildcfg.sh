#!/bin/bash -eu
#############

#---------------------------------------------------------------------------#
#              Script to support multi os compilation
#---------------------------------------------------------------------------#

#---------------------------------------------------------------------------#
# Supported build platforms :
#   ESXI 6.0/6.5/6.7
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
BUILD_NUM=$(echo $NDDK_DIR | cut -c 17-23)

# Perform sanity Checks
sanityChecks () {
    # Check supported NDDK version
    if [ "$NDDK_VER" != "6.0" ] && [ "$NDDK_VER" != "6.5" ] && [ "$NDDK_VER" != "6.7" ]; then
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

cfg_sfvmksc () {
    if [ "$NDDK_VER" == "6.0" ]; then
       cp 6.0-sfvmk sfvmk.sc
    elif [ "$NDDK_VER" == "6.5" ] || [ "$NDDK_VER" == "6.7" ]; then
       # Same build parameters are being used for 6.5 and 6.7
       cp 6.5-sfvmk sfvmk.sc
    fi
}

cfg_toolscfg () {
    if [ "$NDDK_VER" == "6.0" ]; then
       cp 6.0-tool-defs.inc tool-defs.inc
    elif [ "$NDDK_VER" == "6.5" ] || [ "$NDDK_VER" == "6.7" ]; then
       # Same tool versions are being used for 6.5 and 6.7
       cp 6.5-tool-defs.inc tool-defs.inc
    fi
}


sanityChecks
case "$1" in
    CFG_SFVMKBUILD)
        cfg_toolscfg
        cfg_sfvmksc
        break
        ;;
    GET_NDDKDIR)
        get_ndddir
        break
        ;;
    GET_NDDKVER)
        get_nddkver
        break
        ;;
    GET_NDDKBUILDNUM)
        get_buildnum
        break
        ;;
    *)
        break
        ;;
esac
