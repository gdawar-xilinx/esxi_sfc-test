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
#include <unistd.h>
#include <string.h>
#include <vmkapi.h>
#include <getopt.h>
#include <malloc.h>
#include <ctype.h>
#include <errno.h>

#include "sf_firmware.h"
#include "sf_jlib/sf_jlib.h"

#define MIN_LEN(a, b) (a < b ? a : b)
#define FW_INDEX_TO_TYPE(_i) (1 << _i)

const char * supportedFWTypes[] = {
  "controller",
  "bootrom",
  "uefirom",
  "suc",
  "bundle"
};

const char * fwTypeName[] = {
  "Controller version: ",
  "BOOTROM version:    ",
  "UEFIROM version:    ",
  "SUC version:        ",
  "BUNDLE version:     "
};

const vmk_uint32 fwVersionGetCmdType[] = {
  SFVMK_GET_FW_VERSION,
  SFVMK_GET_ROM_VERSION,
  SFVMK_GET_UEFI_VERSION,
  SFVMK_GET_SUC_VERSION,
  SFVMK_GET_BUNDLE_VERSION
};

static sfvmk_nvramType_t sfvmk_getNvramType(sfvmk_firmwareType_t fwType);

/******************************************************************************/

inline int sfvmk_fwtypeToIndex(sfvmk_firmwareType_t fwType)
{
  int i = 0;

  while (i < SFVMK_MAX_FWTYPE_SUPPORTED) {
    if (fwType & FW_INDEX_TO_TYPE(i))
      return i;

    i++;
  }

  return SFVMK_MAX_FWTYPE_SUPPORTED;
}

static vmk_Bool
sfvmk_doWaitAfterUpdate(sfvmk_firmwareType_t fwType)
{
  if (fwType == SFVMK_FIRMWARE_MC ||
      fwType == SFVMK_FIRMWARE_SUC ||
      fwType == SFVMK_FIRMWARE_BUNDLE ||
      fwType == SFVMK_FIRMWARE_ANY)
    return VMK_TRUE;

  return VMK_FALSE;
}

static VMK_ReturnStatus
sfvmk_getFwVerForType(sfvmk_masterDevNode_t *pMsNode, vmk_uint32 fwIndex)
{
  sfvmk_versionInfo_t verInfo;
  vmk_Name *pIfaceName;
  VMK_ReturnStatus status;

  pIfaceName = &pMsNode->pMsIfaceNode->ifaceName;

  assert(fwIndex < SFVMK_MAX_FWTYPE_SUPPORTED);

  memset(&verInfo, 0, sizeof(verInfo));

  verInfo.type = fwVersionGetCmdType[fwIndex];

  status = sfvmk_getFWVersion(pIfaceName->string, &verInfo);
  if (status != VMK_OK)
    return status;

  strcpy(pMsNode->fwVer[fwIndex].string, verInfo.version.string);

  return VMK_OK;
}

static sfvmk_nvramType_t
sfvmk_getNvramType(sfvmk_firmwareType_t fwType)
{
  sfvmk_nvramType_t type;

  switch(fwType) {
    case SFVMK_FIRMWARE_MC:
      type = SFVMK_NVRAM_MC;
      break;

    case SFVMK_FIRMWARE_BOOTROM:
      type = SFVMK_NVRAM_BOOTROM;
      break;

    case SFVMK_FIRMWARE_UEFI:
      type = SFVMK_NVRAM_UEFIROM;
      break;

    case SFVMK_FIRMWARE_SUC:
      type = SFVMK_NVRAM_MUM;
      break;

    case SFVMK_FIRMWARE_BUNDLE:
      type = SFVMK_NVRAM_BUNDLE;
      break;

    case SFVMK_FIRMWARE_ANY:
      type = SFVMK_NVRAM_INVALID;
      break;

    default:
      type = SFVMK_NVRAM_NTYPE;
  }

  return type;
}

static sf_image_type_t
sfvmk_getJLIBImgType(sfvmk_firmwareType_t fwType)
{
  sf_image_type_t imageType = max_images;

  switch (fwType) {
    case SFVMK_FIRMWARE_MC:
      imageType = controller;
      break;
    case SFVMK_FIRMWARE_BOOTROM:
      imageType = bootrom;
      break;
    case SFVMK_FIRMWARE_UEFI:
      imageType = uefirom;
      break;
    case SFVMK_FIRMWARE_SUC:
      imageType = sucfw;
      break;
    case SFVMK_FIRMWARE_BUNDLE:
      imageType = bundle;
      break;
    default:
      imageType = max_images;
  }

  return imageType;
}

static int
sfvmk_getImgSubtype(sfvmk_masterDevNode_t *pMsNode, vmk_uint32 fwIndex)
{
  int subtype = 0xffff;

  assert(fwIndex < SFVMK_MAX_FWTYPE_SUPPORTED);

  subtype = pMsNode->boardSubtype[fwIndex];

  return subtype;
}

static vmk_Bool
sfvmk_matchFWVersion(sfvmk_masterDevNode_t *pMsNode,
                     const char *pImgVersion, vmk_uint32 fwIndex)
{
  char *pFwVerStr;
  int len;
  vmk_Bool foundMatch = VMK_FALSE;

  assert(fwIndex < SFVMK_MAX_FWTYPE_SUPPORTED);

  if (pMsNode->notSupported & FW_INDEX_TO_TYPE(fwIndex))
    return foundMatch;

  pFwVerStr = pMsNode->fwVer[fwIndex].string;

  /* Version string given in *.json file and returned from MC firmware may be of
   * different length. For example the version returned by MC firmware includes (rx0 tx0)
   * at the end. Here we are getting MIN length and then comparing. */
  len = MIN_LEN(strlen(pImgVersion), strlen(pFwVerStr));

  if (strncmp(pFwVerStr, pImgVersion, len) == 0)
    foundMatch = VMK_TRUE;

  return foundMatch;
}

static VMK_ReturnStatus
sfvmk_getAllFWSubtype(sfvmk_masterDevNode_t *pMsNode)
{
  vmk_Name *pIfaceName;
  sfvmk_nvramType_t type;
  vmk_uint32 i;
  vmk_uint32 fwTypeIter = 0;
  VMK_ReturnStatus status;

  pIfaceName = &pMsNode->pMsIfaceNode->ifaceName;

  for (i = 0; i < SFVMK_MAX_FWTYPE_SUPPORTED; i++) {
    fwTypeIter = FW_INDEX_TO_TYPE(i);

    if (pMsNode->notSupported & fwTypeIter)
      continue;

    type = sfvmk_getNvramType(fwTypeIter);
    status = sfvmk_getFWPartSubtype(pIfaceName->string, type,
                                    &pMsNode->boardSubtype[i]);
    if (status != VMK_OK)
      goto end;
  }

  status = VMK_OK;

end:
  return status;
}

static VMK_ReturnStatus
sfvmk_getAllNVPartInfo(sfvmk_masterDevNode_t *pMsNode)
{
  vmk_Name *pIfaceName;
  vmk_uint32 flags = 0;
  vmk_uint32 i;
  vmk_uint32 fwTypeIter = 0;
  sfvmk_nvramType_t type;
  VMK_ReturnStatus status;

  pIfaceName = &pMsNode->pMsIfaceNode->ifaceName;

  for (i = 0; i < SFVMK_MAX_FWTYPE_SUPPORTED; i++) {
    fwTypeIter = FW_INDEX_TO_TYPE(i);
    type = sfvmk_getNvramType(fwTypeIter);
    status = sfvmk_getFWPartFlag(pIfaceName->string, type, &flags);
    if (status == VMK_NOT_SUPPORTED) {
      pMsNode->notSupported |= fwTypeIter;
      continue;
    } else if (status != VMK_OK) {
      goto end;
    }

    if (flags & SFVMK_NVRAM_PART_READONLY_FLAG) {
      pMsNode->readOnly |= fwTypeIter;
    }
  }

  status = VMK_OK;

end:
  return status;
}

static VMK_ReturnStatus
sfvmk_getAllFWVer(sfvmk_masterDevNode_t *pMsNode)
{
  vmk_Name *pIfaceName;
  vmk_uint32 i;
  vmk_uint32 fwTypeIter = 0;
  sfvmk_vpdInfo_t vpdInfo;
  VMK_ReturnStatus status;

  pIfaceName = &pMsNode->pMsIfaceNode->ifaceName;

  for (i = 0; i < SFVMK_MAX_FWTYPE_SUPPORTED; i++) {
    fwTypeIter = FW_INDEX_TO_TYPE(i);

    if (pMsNode->notSupported & fwTypeIter) {
      strcpy(pMsNode->fwVer[i].string, "N/A");
      continue;
    }

    status = sfvmk_getFwVerForType(pMsNode, i);
    if (status != VMK_OK)
      goto end;
  }

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
  vmk_uint32 i;
  vmk_uint32 fwTypeIter = 0;

  for (i = 0; i < SFVMK_MAX_FWTYPE_SUPPORTED; i++) {
    fwTypeIter = FW_INDEX_TO_TYPE(i);

    if (pMsNode->notSupported & fwTypeIter)
      continue;

    if (fwType & fwTypeIter) {
      printf("  %s%s\n", fwTypeName[i], pMsNode->fwVer[i].string);
    }
  }

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

    printf("NIC model: %s\n", pMsNode->nicModel);

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

static VMK_ReturnStatus
sfvmk_updateVPDTag(sfvmk_masterDevNode_t *pMsNode)
{
  vmk_Name *pIfaceName;
  char verString[SF_JLIB_MAX_VER_STRING_LENGTH];
  vmk_uint8 vpdData[SFVMK_VPD_MAX_PAYLOAD];
  vmk_uint32 fwIndex = SFVMK_MAX_FWTYPE_SUPPORTED;
  vmk_uint32 vpdWriteableLen = 0;
  vmk_uint32 i;
  int subtype;
  int ret;
  VMK_ReturnStatus status;

  assert(pMsNode);
  assert(pMsNode->pMsIfaceNode);

  memset(verString, 0, SF_JLIB_MAX_VER_STRING_LENGTH);
  memset(vpdData, 0, SFVMK_VPD_MAX_PAYLOAD);

  /* Clear VPD if bundle partition is supported */
  if (!(pMsNode->notSupported & SFVMK_FIRMWARE_BUNDLE)) {
    /* Setting the maximum length to 255. This is get around
     * till Bug86414 get fixed. */
    vpdWriteableLen = SFVMK_VPD_MAX_PAYLOAD - 1;
  } else {
    fwIndex = sfvmk_fwtypeToIndex(SFVMK_FIRMWARE_MC);
    assert(fwIndex < SFVMK_MAX_FWTYPE_SUPPORTED);

    subtype = sfvmk_getImgSubtype(pMsNode, fwIndex);

    if ((ret = sf_jlib_get_fwfamily_ver(subtype, verString)) < 0)
      return VMK_FAILURE;

    vpdWriteableLen = strlen(verString);

    /* Remove 'v' or 'V' prefix from the version string */
    if (verString[0] == 'v' || verString[0] == 'V')
      memcpy(vpdData, &verString[1], (vpdWriteableLen - 1));
    else
      memcpy(vpdData, verString, vpdWriteableLen);

    i = vpdWriteableLen = strlen(vpdData);
    while (vpdData[i] != '.') {
      vpdData[i] = '\0';
      i--;
    }

    vpdData[i] = '\0';
    vpdWriteableLen = i;
  }

  pIfaceName = &pMsNode->pMsIfaceNode->ifaceName;

  /* Update VPD Keyword VD, V0 */
  status = sfvmk_setVpdByTag(pIfaceName->string, vpdData,
                             vpdWriteableLen,
                             SFVMK_VPD_READONLY_TAG,
                             SFVMK_VPD_FAMILY_KEYWORD_V0);
  if (status != VMK_OK)
    return status;

  status = sfvmk_setVpdByTag(pIfaceName->string, vpdData,
                             vpdWriteableLen,
                             SFVMK_VPD_READONLY_TAG,
                             SFVMK_VPD_FAMILY_KEYWORD_VD);
  if (status != VMK_OK)
    return status;

  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_updateFromFile(sfvmk_masterDevNode_t *pMsNode,
                     sfvmk_firmwareType_t fwType,
                     char *pFileName, char *pFwVerMatchStr, char errMsg[])
{
  sfvmk_ifaceNode_t *pIfaceNode;
  sfvmk_imgUpdateV2_t imgUpdateV2;
  const char *pMsPortName;
  char *pBuf;
  vmk_uint32 fwIndex = 0;
  int fileSize;
  VMK_ReturnStatus status = VMK_FAILURE;

  assert(pMsNode);
  assert(pMsNode->pMsIfaceNode);
  assert(pFileName);

  pIfaceNode = pMsNode->pMsIfaceNode;
  pMsPortName = pIfaceNode->ifaceName.string;

  if (fwType == SFVMK_FIRMWARE_INVALID || fwType == SFVMK_FIRMWARE_ALL)
    return status;

  sfvmk_printIfaceList(pMsNode, 0, VMK_TRUE);
  printf("Previous firmware version:\n");
  sfvmk_printFirmwareVer(pMsNode, (fwType == SFVMK_FIRMWARE_ANY) ?
                                   SFVMK_FIRMWARE_ALL : fwType);

  /* No need for sprintf here. errMsg is filled within sfvmk_readFileContent() */
  status = sfvmk_readFileContent(pFileName, &pBuf, &fileSize, errMsg);
  if (status != VMK_OK)
    return status;

  if (fwType != SFVMK_FIRMWARE_ANY) {
    fwIndex = sfvmk_fwtypeToIndex(fwType);
    assert(fwIndex < SFVMK_MAX_FWTYPE_SUPPORTED);
  }

  if (pFwVerMatchStr) {
    if (sfvmk_matchFWVersion(pMsNode, pFwVerMatchStr, fwIndex) == VMK_TRUE) {
      printf("Skipped %s firmware update as version is same for",
             supportedFWTypes[fwIndex]);
      while(pIfaceNode) {
        printf(" %s", pIfaceNode->ifaceName.string);
        pIfaceNode = pIfaceNode->pNext;
      }
      printf("...\n");

      free(pBuf);
      return VMK_OK;
    }
  }

  memset(&imgUpdateV2, 0, sizeof(imgUpdateV2));
  imgUpdateV2.pFileBuffer = (vmk_uint64)((vmk_uint32)pBuf);
  imgUpdateV2.size = fileSize;
  imgUpdateV2.type = sfvmk_getNvramType(fwType);

  status = sfvmk_setNicFirmware(pMsPortName, &imgUpdateV2);
  if (status != VMK_OK) {
    if (status == VMK_NO_PERMISSION)
      sprintf(errMsg, "Firmware --type %s mismatch with firmware image, error 0x%x",
              supportedFWTypes[fwIndex], status);
    else if (status == VMK_INVALID_METADATA)
      sprintf(errMsg, "Image board type mismatch, error 0x%x", status);
    else
      sprintf(errMsg, "Firmware update failed, error 0x%x", status);
  } else {
    /* At least one image updated is need to set
     * this flag true. This is needed for vpd update */
    if (!pMsNode->vpdUpdateNeeded)
      pMsNode->vpdUpdateNeeded = VMK_TRUE;

    if (sfvmk_doWaitAfterUpdate(fwType) == VMK_TRUE)
      sleep(30);

    if (fwType == SFVMK_FIRMWARE_ANY)
      status = sfvmk_getAllFWVer(pMsNode);
    else
      status = sfvmk_getFwVerForType(pMsNode, fwIndex);

    if (status != VMK_OK) {
      sprintf(errMsg, "Unable to get current firmware, error 0x%x", status);
      free(pBuf);
      return status;
    }

    printf("Updated firmware successfully for");
    while(pIfaceNode) {
      printf(" %s", pIfaceNode->ifaceName.string);
      pIfaceNode = pIfaceNode->pNext;
    }
    printf("...\n");

    printf("Current firmware version:\n");
    sfvmk_printFirmwareVer(pMsNode, (fwType == SFVMK_FIRMWARE_ANY) ?
                                     SFVMK_FIRMWARE_ALL : fwType);
  }

  free(pBuf);
  return status;
}

static VMK_ReturnStatus
sfvmk_updateFromDefault(sfvmk_masterDevNode_t *pMsNode,
                        sfvmk_firmwareType_t fwType,
                        vmk_Bool overwrite, char errMsg[])
{
  sf_image_type_t imageType;
  vmk_uint32 fwTypeIter = 0;
  char fwFilePath[SFVMK_MAX_DIR_PATH_LENGTH];
  char fwFileName[SFVMK_MAX_FILENAME_LENGTH];
  char fwImgVer[SF_JLIB_MAX_VER_STRING_LENGTH];
  int subtype;
  VMK_ReturnStatus status = VMK_FAILURE;
  int i, ret;
  vmk_Bool updatePassed = VMK_TRUE;

  assert(pMsNode);

  for (i = (SFVMK_MAX_FWTYPE_SUPPORTED - 1); i >= 0; i--) {
    fwTypeIter = FW_INDEX_TO_TYPE(i);

    if (!(fwType & fwTypeIter))
      continue;

    if (pMsNode->notSupported & fwTypeIter) {
      printf("%s - %s\n%s firmware not supported on this board\n",
             pMsNode->pMsIfaceNode->ifaceName.string, pMsNode->nicModel,
             supportedFWTypes[i]);
      continue;
    }

    if (pMsNode->readOnly & fwTypeIter)
      continue;

    memset(fwFilePath, 0, SFVMK_MAX_DIR_PATH_LENGTH);
    memset(fwFileName, 0, SFVMK_MAX_FILENAME_LENGTH);
    memset(fwImgVer, 0, SF_JLIB_MAX_VER_STRING_LENGTH);

    strcpy(fwFilePath, SFVMK_DEFAULT_FIRMWARE_LOC);
    imageType = sfvmk_getJLIBImgType(fwTypeIter);
    subtype = sfvmk_getImgSubtype(pMsNode, i);

    /* Do not return and continue for next partition type. It is not
     * necessary that a firmware vib always has binaries for all
     * component firmware. */
    ret = sf_jlib_find_image(imageType, subtype, fwImgVer, fwFileName);
    if (ret < 0)
      continue;

    strcat(fwFilePath, fwFileName);

    /* No need for sprintf here. errMsg is filled within sfvmk_updateFromFile */
    status = sfvmk_updateFromFile(pMsNode, fwTypeIter, fwFilePath,
                                  (overwrite ? NULL : fwImgVer), errMsg);
    if (status != VMK_OK) {
      /* Print the error message and continue updating firmware
       * for next NV partition. */
      printf("ERROR: %s\n", errMsg);
      memset(errMsg, 0, SFVMK_MAX_FW_RDWR_ERR_LENGTH);
      if (updatePassed)
        updatePassed = VMK_FALSE;
    }
  }

  if (pMsNode->vpdUpdateNeeded &&
      updatePassed             &&
      (fwType == SFVMK_FIRMWARE_ALL)) {
    if ((status = sfvmk_updateVPDTag(pMsNode)) != VMK_OK)
      printf("ERROR: VPD update failed, error 0x%x\n", status);
  }

  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_updateFirmware(sfvmk_firmwareCtx_t *pfwCtx)
{
  sfvmk_masterDevNode_t *pMsNode;
  char fwFamilyPath[64] = SFVMK_DEFAULT_FIRMWARE_LOC;
  char jsonFile[32] = SFVMK_FIRMWARE_METADATA_FILE;
  int ret;
  vmk_uint32 fwIndex = SFVMK_MAX_FWTYPE_SUPPORTED;
  VMK_ReturnStatus status = VMK_FAILURE;

  assert(pfwCtx);
  assert(pfwCtx->pMasters);

  pMsNode = pfwCtx->pMasters;

  if (pfwCtx->fwFileSet) {
    if (pfwCtx->fwType != SFVMK_FIRMWARE_ANY) {
      fwIndex = sfvmk_fwtypeToIndex(pfwCtx->fwType);
      if (fwIndex == SFVMK_MAX_FWTYPE_SUPPORTED) {
        sprintf(pfwCtx->errorMsg, "Invalid firmware index");
        return VMK_BAD_PARAM;
      }
    }

    if (pMsNode->notSupported & pfwCtx->fwType) {
      printf("%s - %s\n%s firmware not supported on this board\n",
             pMsNode->pMsIfaceNode->ifaceName.string, pMsNode->nicModel,
             supportedFWTypes[fwIndex]);
      return VMK_OK;
    }

    /* No need for sprintf here. errMsg is filled within sfvmk_updateFromFile */
    status = sfvmk_updateFromFile(pMsNode, pfwCtx->fwType,
                                  pfwCtx->fwFileName, NULL, pfwCtx->errorMsg);
    return status;
  }

  if (!pfwCtx->updateDefault)
    return VMK_FAILURE;

  strcat(fwFamilyPath, jsonFile);
  ret = sf_jlib_init(fwFamilyPath);
  if (ret < 0) {
    if (ret == -ENOENT)
      sprintf(pfwCtx->errorMsg, "SF JLIB could not find %s file,"
                                " please check if firmware vib is installed", fwFamilyPath);
    else if (ret == -EIO)
      sprintf(pfwCtx->errorMsg, "SF JLIB failed to read %s file", fwFamilyPath);
    else if (ret == -EPERM)
      sprintf(pfwCtx->errorMsg, "SF JLIB failed to parse %s file", fwFamilyPath);
    else
      sprintf(pfwCtx->errorMsg, "SF JLIB init failed, error %d", ret);

    return VMK_FAILURE;
  }

  while (pMsNode) {
    status = sfvmk_updateFromDefault(pMsNode, pfwCtx->fwType, pfwCtx->isForce, pfwCtx->errorMsg);
    if (status != VMK_OK) {
      sf_jlib_exit();
      return status;
    }

    pMsNode = pMsNode->pNext;
  }

  sf_jlib_exit();
  return status;
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

    status = sfvmk_getAllNVPartInfo(pMsNode);
    if (status != VMK_OK) {
      sprintf(pfwCtx->errorMsg, "NVRAM info get failed with error 0x%x", status);
      goto free_masters;
    }

    status = sfvmk_getAllFWVer(pMsNode);
    if (status != VMK_OK) {
      sprintf(pfwCtx->errorMsg, "Firmware version get failed with error 0x%x", status);
      goto free_masters;
    }

    status = sfvmk_getAllFWSubtype(pMsNode);
    if (status != VMK_OK) {
      sprintf(pfwCtx->errorMsg, "Board subtype get failed with error 0x%x", status);
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
      status = sfvmk_updateFirmware(pfwCtx);
      break;

    default:
      status = VMK_BAD_PARAM;
  }

  sfvmk_freeMasters(pfwCtx);

end:
  if (status != VMK_OK)
    printf("ERROR: %s\n", pfwCtx->errorMsg);

  return status;
}
