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

  pIfaceName = &pMsNode->pMsIfaceNode->ifaceName;

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
  status = VMK_OK;

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

static void
sfvmk_printIfaceList(sfvmk_masterDevNode_t *pMsNode,
                     sfvmk_firmwareType_t fwType, vmk_Bool printMasterOnly)
{
  sfvmk_ifaceNode_t *pIfaceNode;

  assert(pMsNode->pMsIfaceNode);

  pIfaceNode = pMsNode->pMsIfaceNode;

  do {
    printf("\n%s - MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           pIfaceNode->ifaceName.string,
           pIfaceNode->macAddress[0], pIfaceNode->macAddress[1],
           pIfaceNode->macAddress[2], pIfaceNode->macAddress[3],
           pIfaceNode->macAddress[4], pIfaceNode->macAddress[5]);

    sfvmk_printFirmwareVer(pMsNode, fwType);
    if (printMasterOnly)
      break;

    pIfaceNode = pIfaceNode->pNext;
  } while (pIfaceNode);

  return;
}

static VMK_ReturnStatus
sfvmk_printFwInfo(sfvmk_firmwareCtx_t *pfwCtx)
{
  sfvmk_masterDevNode_t *pMsNode;

  assert(pfwCtx);
  assert(pfwCtx->pMasters);

  pMsNode = pfwCtx->pMasters;

  while (pMsNode) {
    sfvmk_printIfaceList(pMsNode, SFVMK_FIRMWARE_ALL, !pfwCtx->applyAllNic);
    pMsNode = pMsNode->pNext;
  }

  return VMK_OK;
}

void sfvmk_freeSlaves(sfvmk_ifaceNode_t *pIfaceNodeHead)
{
  sfvmk_ifaceNode_t *pIfaceNodeLast;

  pIfaceNodeLast = pIfaceNodeHead;
  while(pIfaceNodeLast) {
    pIfaceNodeHead = pIfaceNodeLast->pNext;
    free(pIfaceNodeLast);
    pIfaceNodeLast = pIfaceNodeHead;
  }

  return;
}

void sfvmk_freeMasters(sfvmk_firmwareCtx_t *pfwCtx)
{
  sfvmk_masterDevNode_t *pMsNode;
  sfvmk_masterDevNode_t *pMsNodeLast;

  pMsNode = pMsNodeLast = pfwCtx->pMasters;
  while (pMsNode) {
    pMsNodeLast = pMsNode->pNext;
    sfvmk_freeSlaves(pMsNode->pMsIfaceNode);
    free(pMsNode);
    pMsNode = pMsNodeLast;
  }

  return;
}

static sfvmk_ifaceNode_t *
sfvmk_updateSlaves(sfvmk_masterDevNode_t *pMsNode, sfvmk_ifaceNode_t *pIfaceNodeHead)
{
  sfvmk_ifaceNode_t *pMsIfaceNodeLast;
  sfvmk_ifaceNode_t *pIfaceNode;
  int pciBDFStrLen = 0;

  assert(pMsNode);
  assert(pMsNode->pMsIfaceNode);

  pIfaceNode = pIfaceNodeHead;
  pMsIfaceNodeLast = pMsNode->pMsIfaceNode;
  pciBDFStrLen = strcspn(pMsIfaceNodeLast->pciBDF.string, ".");

  while (pIfaceNode) {
    if (strncmp(pMsNode->pMsIfaceNode->pciBDF.string,
                pIfaceNode->pciBDF.string, (pciBDFStrLen - 1)) == 0) {
      pMsIfaceNodeLast->pNext = pIfaceNode;
      pMsIfaceNodeLast = pIfaceNode;

      if (pMsIfaceNodeLast == pIfaceNodeHead)
        pIfaceNodeHead = pIfaceNode->pNext;

      if (pIfaceNode->pNext != NULL)
        pIfaceNode->pNext->pPrev = pIfaceNode->pPrev;
      if (pIfaceNode->pPrev != NULL)
        pIfaceNode->pPrev->pNext = pIfaceNode->pNext;

      pIfaceNode = pIfaceNode->pNext;
      pMsIfaceNodeLast->pNext = NULL;
      pMsIfaceNodeLast->pPrev = NULL;
      continue;
    }

    pIfaceNode = pIfaceNode->pNext;
  }

  return pIfaceNodeHead;
}

static VMK_ReturnStatus
sfvmk_createNicList(sfvmk_firmwareCtx_t *pfwCtx)
{
  sfvmk_masterDevNode_t *pMsNode = NULL;
  sfvmk_masterDevNode_t *pMsNodeLast = NULL;
  sfvmk_ifaceNode_t *pIfaceNodeHead = NULL;
  sfvmk_ifaceNode_t *pIfaceNode = NULL;
  sfvmk_ifaceNode_t *pIfaceNodeLast = NULL;
  sfvmk_ifaceList_t nicList;
  VMK_ReturnStatus status;
  int i;

  assert(pfwCtx);

  status = sfvmk_getNicList(&nicList);
  if (status != VMK_OK) {
    sprintf(pfwCtx->errorMsg, "NIC list get failed with error 0x%x", status);
    goto end;
  }

  /* Create an interface list for all solarflare adapters */
  for (i = 0; i < nicList.ifaceCount; i++) {
    pIfaceNode = (sfvmk_ifaceNode_t *)calloc(1, sizeof(*pIfaceNode));
    if (!pIfaceNode) {
      status = VMK_NO_MEMORY;
      sprintf(pfwCtx->errorMsg, "Failed to allocate memory with error 0x%x", status);
      goto free_slaves;
    }

    if (!pIfaceNodeHead)
      pIfaceNodeLast = pIfaceNodeHead = pIfaceNode;
    else {
      pIfaceNodeLast->pNext = pIfaceNode;
      pIfaceNode->pPrev = pIfaceNodeLast;
      pIfaceNodeLast = pIfaceNode;
    }

    strcpy(pIfaceNode->ifaceName.string, nicList.ifaceArray[i].string);
    status = sfvmk_getPCIAddress(pIfaceNode->ifaceName.string, &pIfaceNode->pciBDF);
    if (status != VMK_OK) {
      sprintf(pfwCtx->errorMsg, "PCI address get failed with error 0x%x", status);
      goto free_slaves;
    }

    status = sfvmk_getMACAddress(pIfaceNode->ifaceName.string, pIfaceNode->macAddress);
    if (status != VMK_OK) {
      sprintf(pfwCtx->errorMsg, "MAC address get failed with error 0x%x", status);
      goto free_slaves;
    }
  }

  /* If firmware update request is only for single NIC then make
   * that particular NIC as list head. */
  if (!pfwCtx->applyAllNic) {
    pIfaceNode = pIfaceNodeHead;
    while (strcmp(pIfaceNode->ifaceName.string, pfwCtx->ifaceName.string) != 0)
      pIfaceNode = pIfaceNode->pNext;

    if (pIfaceNode != pIfaceNodeHead) {
      if (pIfaceNode->pNext == NULL) {
        pIfaceNode->pPrev->pNext = NULL;
      } else {
        pIfaceNode->pPrev->pNext = pIfaceNode->pNext;
        pIfaceNode->pNext->pPrev = pIfaceNode->pPrev;
      }

      pIfaceNode->pNext = pIfaceNodeHead;
      pIfaceNodeHead->pPrev = pIfaceNode;
      pIfaceNodeHead = pIfaceNode;
      pIfaceNodeHead->pPrev = NULL;
    }
  }

  /* Find and create a master/ slave list from interface list */
  while (pIfaceNodeHead) {
    pMsNode = (sfvmk_masterDevNode_t *)calloc(1, sizeof(*pMsNode));
    if (!pMsNode) {
      status = VMK_NO_MEMORY;
      sprintf(pfwCtx->errorMsg, "Failed to allocate memory with error 0x%x", status);
      goto free_masters;
    }

    pMsNode->pMsIfaceNode = pIfaceNodeHead;
    pIfaceNodeLast = pIfaceNodeHead->pNext;
    pMsNode->pMsIfaceNode->pNext = NULL;

    status = sfvmk_getAllFWVer(pMsNode);
    if (status != VMK_OK) {
      sprintf(pfwCtx->errorMsg, "Firmware get failed with error 0x%x", status);
      goto free_masters;
    }

    if (!pfwCtx->pMasters)
      pfwCtx->pMasters = pMsNodeLast = pMsNode;
    else {
      /* Update master device list pointers */
      pMsNodeLast->pNext = pMsNode;
      pMsNodeLast = pMsNode;
    }

    if (pIfaceNodeLast)
      pIfaceNodeLast->pPrev = NULL;

    /* Find and attach slaves interface nodes to master dev nodes and
     * Update the new interface list head */
    pIfaceNodeHead = sfvmk_updateSlaves(pMsNode, pIfaceNodeLast);

    /* If firmware get/ set request is only for single nic then we already
     * have a sorted list of master and slaves and do not wish to process
     * further. */
    if (!pfwCtx->applyAllNic) {
      goto free_slaves;
    }
  }

  status = VMK_OK;
  goto end;

free_masters:
  if (pfwCtx->pMasters) {
    sfvmk_freeMasters(pfwCtx);
    /* Interface list is already freed within sfvmk_freeMasters() */
    pIfaceNodeHead = NULL;
  }

free_slaves:
  if (pIfaceNodeHead)
    sfvmk_freeSlaves(pIfaceNodeHead);

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
