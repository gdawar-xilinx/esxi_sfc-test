/*
 * Copyright (c) 2017-2018, Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SF_UTILS_H
#define _SF_UTILS_H

#include "sfvmk_mgmt_interface.h"

#define SFVMK_VIB_PLUGIN_NAME "SFC-ESX-sfvmkcli"

#define SFVMK_MAX_DIR_PATH_LENGTH 128
#define SFVMK_MAX_FILENAME_LENGTH 64

#define SFVMK_MAX_FW_RDWR_ERR_LENGTH 128

#define SFVMK_DEFAULT_FIRMWARE_LOC "/opt/sfc/"
#define SFVMK_FIRMWARE_METADATA_FILE "firmware/FirmwareMetadata.json"

typedef enum sfvmk_firmwareType_e {
  SFVMK_FIRMWARE_ANY     =  0,
  SFVMK_FIRMWARE_MC      = (1 << 0),
  SFVMK_FIRMWARE_BOOTROM = (1 << 1),
  SFVMK_FIRMWARE_UEFI    = (1 << 2),
  SFVMK_FIRMWARE_SUC     = (1 << 3),
#define SFVMK_MAX_FWTYPE_SUPPORTED     4
  SFVMK_FIRMWARE_ALL     = (SFVMK_FIRMWARE_MC |      \
                            SFVMK_FIRMWARE_BOOTROM | \
                            SFVMK_FIRMWARE_UEFI | \
                            SFVMK_FIRMWARE_SUC),
  SFVMK_FIRMWARE_INVALID = 0xffff
} sfvmk_firmwareType_t;

VMK_ReturnStatus sfvmk_getVpdByTag(const char *pIfaceName, sfvmk_vpdInfo_t *pVpdInfo,
                      vmk_uint8 tag, vmk_uint16 keyword);

VMK_ReturnStatus sfvmk_getPCIAddress(const char *pIfaceName, vmk_Name *pPCIAddr);

VMK_ReturnStatus sfvmk_getFWVersion(const char *pIfaceName, sfvmk_versionInfo_t *pVerInfo);

VMK_ReturnStatus sfvmk_getMACAddress(const char *pIfaceName, vmk_uint8 *pMacAddr);

VMK_ReturnStatus sfvmk_getNicList(sfvmk_ifaceList_t *pNicList);

VMK_ReturnStatus sfvmk_setNicFirmware(const char *pIfaceName, sfvmk_imgUpdateV2_t *pImgUpdateV2);

VMK_ReturnStatus sfvmk_postFecReq(const char *pIfaceName, sfvmk_fecMode_t *pFecMode);

VMK_ReturnStatus sfvmk_getFWPartSubtype(const char *pIfaceName, sfvmk_nvramType_t type, vmk_uint32 *pSubtype);

VMK_ReturnStatus sfvmk_getFWPartFlag(const char *pIfaceName, sfvmk_nvramType_t type, vmk_uint32 *pFlag);

VMK_ReturnStatus sfvmk_readFileContent(char *pFileName, char **ppBuf, int *pFileSize, char errMsg[]);
#endif
