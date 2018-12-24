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
#include <string.h>
#include <vmkapi.h>
#include <getopt.h>
#include <malloc.h>
#include <ctype.h>
#include <errno.h>

#include "sf_firmware.h"

/******************************************************************************/

static VMK_ReturnStatus
sfvmk_getAllFWVer(sfvmk_masterDevNode_t *pMsNode)
{
  sfvmk_versionInfo_t verInfo;
  sfvmk_vpdInfo_t vpdInfo;
  vmk_Name *pIfaceName;
  VMK_ReturnStatus status;

  pIfaceName = &pMsNode->pIfaceHead->ifaceName;

  memset(&verInfo, 0, sizeof(verInfo));
  verInfo.type = SFVMK_GET_FW_VERSION;
  status = sfvmk_getFWVersion(pIfaceName->string, &verInfo);
  if (status != VMK_OK)
    goto end;

  strcpy(pMsNode->mcVer.string, verInfo.version.string);

  memset(&verInfo, 0, sizeof(verInfo));
  verInfo.type = SFVMK_GET_ROM_VERSION;
  status = sfvmk_getFWVersion(pIfaceName->string, &verInfo);
  if (status != VMK_OK)
    goto end;

  strcpy(pMsNode->bootromVer.string, verInfo.version.string);

  memset(&verInfo, 0, sizeof(verInfo));
  verInfo.type = SFVMK_GET_UEFI_VERSION;
  status = sfvmk_getFWVersion(pIfaceName->string, &verInfo);
  if (status != VMK_OK)
    goto end;

  strcpy(pMsNode->uefiromVer.string, verInfo.version.string);

  memset(&verInfo, 0, sizeof(verInfo));
  verInfo.type = SFVMK_GET_SUC_VERSION;
  status = sfvmk_getFWVersion(pIfaceName->string, &verInfo);
  if (status != VMK_OK)
    goto end;

  strcpy(pMsNode->sucVer.string, verInfo.version.string);

  /* Read VPD tag from static area to get product information */
  status = sfvmk_getVpdByTag(pIfaceName->string, &vpdInfo, 0x02, 0x0);
  if (status != VMK_OK)
    goto end;

  strcpy(pMsNode->nicModel, vpdInfo.vpdPayload);
  status = 0;

end:
  return status;
}

static VMK_ReturnStatus
sfvmk_printFirmwareVer(sfvmk_masterDevNode_t *pMsNode, sfvmk_firmwareType_t fwType)
{

  printf("NIC model: %s\n", pMsNode->nicModel);

  if (fwType & SFVMK_FIRMWARE_MC)
    printf("Controller version: %s\n", pMsNode->mcVer.string);

  if (fwType & SFVMK_FIRMWARE_BOOTROM)
    printf("BOOTROM version:    %s\n", pMsNode->bootromVer.string);

  if (fwType & SFVMK_FIRMWARE_UEFI)
    printf("UEFI version:       %s\n", pMsNode->uefiromVer.string);

  if (fwType & SFVMK_FIRMWARE_SUC)
    printf("SUC version:        %s\n", pMsNode->sucVer.string);

  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_printFwInfo(sfvmk_firmwareCtx_t *pfwCtx)
{
  sfvmk_masterDevNode_t *pMsNode;
  sfvmk_ifaceNode_t *pIfaceList;

  assert(pfwCtx);
  assert(pfwCtx->pMasters);
  assert(pfwCtx->pMasters->pIfaceHead);

  pMsNode = pfwCtx->pMasters;
  pIfaceList = pMsNode->pIfaceHead;

  printf("%s - MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
         pIfaceList->ifaceName.string,
         pIfaceList->macAddress[0], pIfaceList->macAddress[1],
         pIfaceList->macAddress[2], pIfaceList->macAddress[3],
         pIfaceList->macAddress[4], pIfaceList->macAddress[5]);

  sfvmk_printFirmwareVer(pMsNode, SFVMK_FIRMWARE_ALL);

  return VMK_OK;
}

void sfvmk_freeSlaves(sfvmk_ifaceNode_t *pIfNodeHead)
{
    free(pIfNodeHead);
}

void sfvmk_freeMasters(sfvmk_firmwareCtx_t *pfwCtx)
{
  sfvmk_masterDevNode_t *pMsNode;

  pMsNode = pfwCtx->pMasters;
  sfvmk_freeSlaves(pMsNode->pIfaceHead);
  free(pMsNode);
}

static VMK_ReturnStatus
sfvmk_createNicList(sfvmk_firmwareCtx_t *pfwCtx)
{
  sfvmk_masterDevNode_t *pMsNodeHead = NULL;
  sfvmk_ifaceNode_t *pIfNodeHead = NULL;
  VMK_ReturnStatus status;

  assert(pfwCtx);

  pIfNodeHead = (sfvmk_ifaceNode_t *)calloc(1, sizeof(*pIfNodeHead));
  if (!pIfNodeHead) {
    status = VMK_NO_MEMORY;
    sprintf(pfwCtx->errorMsg, "Failed to allocate memory with error 0x%x", status);
    goto end;
  }

  strcpy(pIfNodeHead->ifaceName.string, pfwCtx->ifaceName.string);
  status = sfvmk_getPCIAddress(pIfNodeHead->ifaceName.string, &pIfNodeHead->pciBDF);
  if (status != VMK_OK) {
    sprintf(pfwCtx->errorMsg, "PCI address get failed with error 0x%x", status);
    goto free_slaves;
  }

  status = sfvmk_getMACAddress(pIfNodeHead->ifaceName.string, pIfNodeHead->macAddress);
  if (status != VMK_OK) {
    sprintf(pfwCtx->errorMsg, "MAC address get failed with error 0x%x", status);
    goto free_slaves;
  }

  pMsNodeHead = (sfvmk_masterDevNode_t *)calloc(1, sizeof(*pMsNodeHead));
  if (!pMsNodeHead) {
    status = VMK_NO_MEMORY;
    sprintf(pfwCtx->errorMsg, "Failed to allocate memory with error 0x%x", status);
    goto free_masters;
  }

  pMsNodeHead->pIfaceHead = pIfNodeHead;
  pfwCtx->pMasters = pMsNodeHead;

  status = sfvmk_getAllFWVer(pMsNodeHead);
  if (status != VMK_OK) {
    sprintf(pfwCtx->errorMsg, "Firmware get failed with error 0x%x", status);
    goto free_masters;
  }

  status = VMK_OK;
  goto end;

free_masters:
  if (pfwCtx->pMasters) {
    sfvmk_freeMasters(pfwCtx);
    /* Interface list is already freed within sfvmk_freeMasters() */
    pIfNodeHead = NULL;
  }

free_slaves:
  if (pIfNodeHead)
    sfvmk_freeSlaves(pIfNodeHead);

end:
  return status;
}

VMK_ReturnStatus
sfvmk_runFirmwareOps(int opType, sfvmk_firmwareCtx_t *pfwCtx)
{
  VMK_ReturnStatus status = VMK_FAILURE;

  if (!pfwCtx)
    return status;

  status = sfvmk_createNicList(pfwCtx);
  if (status != VMK_OK)
    goto end;

  switch (opType) {
    case SFVMK_MGMT_DEV_OPS_GET:
      status = sfvmk_printFwInfo(pfwCtx);
      break;

    case SFVMK_MGMT_DEV_OPS_SET:
    default:
      status = VMK_BAD_PARAM;
  }

  sfvmk_freeMasters(pfwCtx);

end:
  if (status != VMK_OK)
    printf("ERROR: %s\n", pfwCtx->errorMsg);

  return status;
}
