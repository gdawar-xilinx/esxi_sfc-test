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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vmkapi.h>
#include <ctype.h>

#include "sf_utils.h"

extern vmk_MgmtUserHandle mgmtHandle;

/*
 * Request and return VPD information based on the VPD tag and keyword.
 */
VMK_ReturnStatus
sfvmk_getVpdByTag(const char *pIfaceName, sfvmk_vpdInfo_t *pVpdInfo,
                      vmk_uint8 tag, vmk_uint16 keyword)
{
  sfvmk_mgmtDevInfo_t mgmtParm;
  int status;

  if (!pVpdInfo || !pIfaceName)
    return VMK_BAD_PARAM;

  memset(&mgmtParm, 0, sizeof(mgmtParm));
  strcpy(mgmtParm.deviceName, pIfaceName);

  memset(pVpdInfo, 0, sizeof(*pVpdInfo));
  pVpdInfo->vpdOp = SFVMK_MGMT_DEV_OPS_GET;
  pVpdInfo->vpdTag = tag;
  pVpdInfo->vpdKeyword = keyword;
  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_VPD_REQUEST, &mgmtParm, pVpdInfo);
  if (status != VMK_OK)
    return VMK_NO_CONNECT;

  return mgmtParm.status;
}

/*
 * Get PCI address of a vmnic.
 */
VMK_ReturnStatus
sfvmk_getPCIAddress(const char *pIfaceName, vmk_Name *pPCIAddr)
{
  sfvmk_mgmtDevInfo_t mgmtParm;
  sfvmk_pciInfo_t pciInfo;
  int status;

  if (!pPCIAddr || !pIfaceName)
    return VMK_BAD_PARAM;

  memset(&mgmtParm, 0, sizeof(mgmtParm));
  strcpy(mgmtParm.deviceName, pIfaceName);

  memset(&pciInfo, 0, sizeof(pciInfo));
  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_PCI_INFO_GET, &mgmtParm,
                                      &pciInfo);
  if (status != VMK_OK)
    return VMK_NO_CONNECT;

  if (mgmtParm.status == VMK_OK)
    strcpy(pPCIAddr->string, pciInfo.pciBDF.string);

  return mgmtParm.status;
}

/*
 *  Get installed firmware version of Controller, UefiRom, BootRom and Suc.
 */
VMK_ReturnStatus
sfvmk_getFWVersion(const char *pIfaceName, sfvmk_versionInfo_t *pVerInfo)
{
  sfvmk_mgmtDevInfo_t mgmtParm;
  int status;

  if (!pVerInfo || !pIfaceName)
    return VMK_BAD_PARAM;

  memset(&mgmtParm, 0, sizeof(mgmtParm));
  strcpy(mgmtParm.deviceName, pIfaceName);

  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_VERINFO_GET, &mgmtParm, pVerInfo);
  if (status != VMK_OK)
    return VMK_NO_CONNECT;

  return mgmtParm.status;
}

/*
 * Get MAC address of a vmnic.
 */
VMK_ReturnStatus
sfvmk_getMACAddress(const char *pIfaceName, vmk_uint8 *pMacAddr)
{
  sfvmk_mgmtDevInfo_t mgmtParm;
  sfvmk_macAddress_t mac;
  int status;

  if (!pMacAddr || !pIfaceName)
    return VMK_BAD_PARAM;

  memset(&mgmtParm, 0, sizeof(mgmtParm));
  strcpy(mgmtParm.deviceName, pIfaceName);

  memset(&mac, 0, sizeof(mac));

  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_MAC_ADDRESS_GET, &mgmtParm,
                                      &mac);
  if (status != VMK_OK)
    return VMK_NO_CONNECT;

  if (mgmtParm.status == VMK_OK)
    memcpy(pMacAddr, mac.macAddress, 6);

  return mgmtParm.status;
}

/*
 * Get vmnic name of all solarflare adapters.
 */
VMK_ReturnStatus
sfvmk_getNicList(sfvmk_ifaceList_t *pNicList)
{
  sfvmk_mgmtDevInfo_t mgmtParm;
  int status;

  if (!pNicList)
    return VMK_BAD_PARAM;

  memset(pNicList, 0, sizeof(*pNicList));
  memset(&mgmtParm, 0, sizeof(mgmtParm));

  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_IFACE_LIST_GET, &mgmtParm,
                                      pNicList);
  if (status != VMK_OK)
    return VMK_NO_CONNECT;

  if (mgmtParm.status != VMK_OK)
    return mgmtParm.status;

  if (pNicList->ifaceCount == 0)
    return VMK_BAD_PARAM_COUNT;

  if (pNicList->ifaceCount > SFVMK_MAX_INTERFACE)
    return VMK_BAD_PARAM_COUNT;

  return VMK_OK;
}

/*
 * Read firmware file content, returns the buffer filled with file
 * contents and the size of the buffer.
 */
VMK_ReturnStatus
sfvmk_readFileContent(char *pFileName, char **ppBuf, int *pFileSize, char errMsg[])
{
  FILE *pFile = NULL;
  char *pBuf = NULL;
  int fileSize = 0;
  VMK_ReturnStatus status = VMK_FAILURE;

  assert(pFileName);

  pFile = fopen(pFileName, "r");
  if (pFile == NULL) {
    status = VMK_INVALID_NAME;
    sprintf(errMsg, "Unable to open file '%s' with error 0x%x", pFileName, status);
    goto end;
  }

  status = fseek(pFile, 0, SEEK_END);
  if (status != 0) {
    status = VMK_READ_ERROR;
    sprintf(errMsg, "Unable to open file '%s' with error 0x%x", pFileName, status);
    goto end;
  }

  fileSize = ftell(pFile);
  if (fileSize <= 0) {
    status = VMK_READ_ERROR;
    sprintf(errMsg, "Empty file '%s' with error 0x%x", pFileName, status);
    goto end;
  }

  rewind(pFile);

  pBuf = (char*) malloc(fileSize);
  if (pBuf == NULL) {
    status = VMK_NO_MEMORY;
    sprintf(errMsg, "Failed to allocate memmory for '%s'"
                    " file buffer with error 0x%x", pFileName, status);
    goto close_file;
  }

  memset(pBuf, 0, fileSize);
  if (fread(pBuf, 1, fileSize, pFile) != fileSize) {
    status = VMK_READ_ERROR;
    sprintf(errMsg, "Unable to read file '%s' with error 0x%x", pFileName, status);
    goto free_buffer;
  }

  *pFileSize = fileSize;
  *ppBuf = pBuf;
  status = VMK_OK;
  goto close_file;

free_buffer:
  free(pBuf);
  pBuf = NULL;

close_file:
  fclose(pFile);

end:
  return status;
}

/*
 * Post NVRAM request to driver.
 */
static VMK_ReturnStatus
sfvmk_postNVRequest(sfvmk_mgmtDevInfo_t *pMgmtParm, sfvmk_nvramCmdV2_t *pNvram)
{
  VMK_ReturnStatus status;

  if (!pMgmtParm || !pNvram)
    return VMK_BAD_PARAM;

  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_NVRAM_REQUEST_V2, pMgmtParm,
                                      pNvram);
  if (status != VMK_OK)
    return VMK_NO_CONNECT;

  return VMK_OK;
}

/*
 * Take NVRAM partition type as an input and return subtype.
 */
VMK_ReturnStatus
sfvmk_getFWPartSubtype(const char *pIfaceName, sfvmk_nvramType_t type, vmk_uint32 *pSubtype)
{
  sfvmk_mgmtDevInfo_t mgmtParm;
  sfvmk_nvramCmdV2_t nvram;
  VMK_ReturnStatus status;

  if (!pSubtype || !pIfaceName)
    return VMK_BAD_PARAM;

  memset(&mgmtParm, 0, sizeof(mgmtParm));
  strcpy(mgmtParm.deviceName, pIfaceName);

  memset(&nvram, 0, sizeof(nvram));

  nvram.type = type;
  nvram.op = SFVMK_NVRAM_OP_GET_VER;

  if ((status = sfvmk_postNVRequest(&mgmtParm, &nvram)) != VMK_OK)
    return status;

  if (mgmtParm.status == VMK_OK)
    *pSubtype = nvram.subtype;

  return mgmtParm.status;
}

/*
 * Take NVRAM partition type as an input and return flag.
 */
VMK_ReturnStatus
sfvmk_getFWPartFlag(const char *pIfaceName, sfvmk_nvramType_t type, vmk_uint32 *pFlag)
{
  sfvmk_mgmtDevInfo_t mgmtParm;
  sfvmk_nvramCmdV2_t nvram;
  VMK_ReturnStatus status;

  if (!pFlag || !pIfaceName)
    return VMK_BAD_PARAM;

  memset(&mgmtParm, 0, sizeof(mgmtParm));
  strcpy(mgmtParm.deviceName, pIfaceName);

  memset(&nvram, 0, sizeof(nvram));

  nvram.type = type;
  nvram.op = SFVMK_NVRAM_OP_SIZE;

  if ((status = sfvmk_postNVRequest(&mgmtParm, &nvram)) != VMK_OK)
    return status;

  /* This is get around for the JIRA ticket - EFX-16, This JIRA ticket tracks
   * common code change to return not supported error code. The get around
   * is valid till the bug fix tracked by this JIRA ticket. */
  if ((mgmtParm.status != VMK_OK) || (nvram.size == 0))
    mgmtParm.status = VMK_NOT_SUPPORTED;
  else
    *pFlag = nvram.flags;

  return mgmtParm.status;
}

/*
 * Update various firmware type (Controller, UefiRom, BootRom and SuC)
 */
VMK_ReturnStatus
sfvmk_setNicFirmware(const char *pIfaceName, sfvmk_imgUpdateV2_t *pImgUpdateV2)
{
  sfvmk_mgmtDevInfo_t mgmtParm;
  int status;

  if (!pImgUpdateV2 || !pIfaceName)
    return VMK_BAD_PARAM;

  memset(&mgmtParm, 0, sizeof(mgmtParm));
  strcpy(mgmtParm.deviceName, pIfaceName);

  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_IMG_UPDATE_V2, &mgmtParm, pImgUpdateV2);
  if (status != VMK_OK)
    return VMK_NO_CONNECT;

  return mgmtParm.status;
}

/*
 * Post fec request to get or set the applied fec settings
 */
VMK_ReturnStatus
sfvmk_postFecReq(const char *pIfaceName, sfvmk_fecMode_t *pFecMode)
{
  sfvmk_mgmtDevInfo_t mgmtParm;
  int status;

  if (!pFecMode || !pIfaceName)
    return VMK_BAD_PARAM;

  memset(&mgmtParm, 0, sizeof(mgmtParm));
  strcpy(mgmtParm.deviceName, pIfaceName);

  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_FEC_MODE_REQUEST, &mgmtParm,
                                      pFecMode);
  if (status != VMK_OK)
    return VMK_NO_CONNECT;

  return mgmtParm.status;
}
