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

#include <stdio.h>
#include <string.h>
#include <vmkapi.h>
#include <getopt.h>
#include <malloc.h>
#include <ctype.h>

#include "sf_utils.h"
#include "sf_firmware.h"
#include "sfvmk_mgmt_interface.h"

/*
 * General coding convention followed in SFVMK esxcli extension code:
 *   1. Use VMK_ReturnStatus for returning error or success case.
 *   2. If same management API invocation happens at multiple places,
 *      move the API to sf_utils.c as helper function.
 *   3. Handle command line parsing within main() function.
 *   4. Print all command line parsing error within main() function.
 *   5. main() should print an error only if there are input parameter error
 *      and the individual action handler can't be invoked. Error and success
 *      messages for an action should be printed by action handler only.
 */

extern const vmk_MgmtApiSignature sfvmk_mgmtSig;
vmk_MgmtUserHandle mgmtHandle;

#define OBJECT_NAME_LEN  16

typedef enum sfvmk_objectType_e {
  SFVMK_OBJECT_MCLOG = 0,
  SFVMK_OBJECT_STATS,
  SFVMK_OBJECT_VPD,
  SFVMK_OBJECT_FIRMWARE,
  SFVMK_OBJECT_FEC,
  SFVMK_OBJECT_SENSOR,
  SFVMK_OBJECT_MAX
} sfvmk_objectType_t;

static const char * supportedObjects[] = {
  "mclog",
  "stats",
  "vpd",
  "firmware",
  "fec",
  "sensor"
};

/*
 * Functions to handle request for each object
 */
static VMK_ReturnStatus sfvmk_mcLog(int opType, sfvmk_mgmtDevInfo_t *pMgmtParm, char cmdOption);
static VMK_ReturnStatus sfvmk_hwQueueStats(int opType, sfvmk_mgmtDevInfo_t *pMgmtParm);
static VMK_ReturnStatus sfvmk_vpdGet(int opType, sfvmk_mgmtDevInfo_t *pMgmtParm);
static VMK_ReturnStatus sfvmk_fwUpdate(sfvmk_mgmtDevInfo_t *pMgmtParm,
                           char *pFileName, sfvmk_firmwareType_t fwType, char *pFwTypeName);
static VMK_ReturnStatus sfvmk_fwVersion(sfvmk_mgmtDevInfo_t *pMgmtParm, sfvmk_firmwareType_t fwType);
static int sfvmk_getSiblingPorts(sfvmk_mgmtDevInfo_t *pMgmtParm, vmk_Name siblingPorts[]);
static vmk_uint32 sfvmk_fecModeTypeGet(const char *pFecModeName);
static vmk_uint32 sfvmk_fecModeParse(const char *pFecModeName);
static VMK_ReturnStatus sfvmk_fecModeSet(sfvmk_mgmtDevInfo_t *pMgmtParm, vmk_uint32 fec);
static VMK_ReturnStatus sfvmk_fecModeGet(sfvmk_mgmtDevInfo_t *pMgmtParm);
static VMK_ReturnStatus sfvmk_hwSensorGet(sfvmk_mgmtDevInfo_t *pMgmtParm, int opType);

static inline void sfvmk_strLwr(char string[])
{
  int i;

  for(i = 0; string[i]; i++)
    string[i] = tolower(string[i]);
}

static int sfvmk_findObjectType(const char *obj)
{
  int i;
  for (i = 0; i < SFVMK_OBJECT_MAX; i++) {
    if (!strncmp(obj, supportedObjects[i], strlen(supportedObjects[i]))) {
      return i;
    }
  }

  return SFVMK_OBJECT_MAX;
}

static vmk_Bool sfvmk_isSFNic(sfvmk_mgmtDevInfo_t *pMgmtParm)
{
  vmk_Name pciBDF;
  int status = 0;

  status = sfvmk_getPCIAddress(pMgmtParm->deviceName, &pciBDF);
  if (status == VMK_OK)
    return VMK_TRUE;

  if (status == VMK_NOT_FOUND)
    printf("ERROR: Unable to find Solarflare NIC %s\n", pMgmtParm->deviceName);

  return VMK_FALSE;
}

int
main(int argc, char **argv)
{
  int c;
  int opType = SFVMK_MGMT_DEV_OPS_INVALID;
  int stringLength = 0;
  vmk_Bool optionEnable = VMK_FALSE;
  vmk_Bool nicNameSet = VMK_FALSE;
  char mclogOption[10];
  char objectName[16];
  char fwTypeName[16];
  char fileName[128];
  char fecModeName[11];
  vmk_uint32 fecMode = SFVMK_MGMT_FEC_NONE_MASK;
  sfvmk_firmwareType_t fwType = SFVMK_FIRMWARE_ANY;
  sfvmk_mgmtDevInfo_t mgmtParm;
  sfvmk_firmwareCtx_t fwCtx;
  VMK_ReturnStatus status;

  printf("<?xml version=\"1.0\"?><output xmlns:esxcli=\"sfvmk\">\n");
  printf("<string>");

  memset(&mgmtParm, 0, sizeof(mgmtParm));

  while (1) {
    int option_index = 0;

    static struct option long_options[] =
    {
      {"get",        no_argument,       0, 'g'},
      {"set",        no_argument,       0, 's'},
      {"object",     required_argument, 0, 'o'},
      {"nic-name",   required_argument, 0, 'n'},
      {"enable",     required_argument, 0, 'e'},
      {"file-name",  required_argument, 0, 'f'},
      {"type",       required_argument, 0, 't'},
      {"mode",       required_argument, 0, 'm'},
      {0, 0, 0, 0}
    };

    c = getopt_long(argc, argv, "::gson:e:", long_options, &option_index);

    if (c == -1)
      break;

    switch (c) {
      case 'g':
        if (opType == SFVMK_MGMT_DEV_OPS_SET) {
          printf("ERROR: Cannot provide both set and get\n");
          goto end;
        }

        opType = SFVMK_MGMT_DEV_OPS_GET;
        break;

      case 's':
        if (opType == SFVMK_MGMT_DEV_OPS_GET) {
          printf("ERROR: Cannot provide both set and get\n");
          goto end;
        }

        opType = SFVMK_MGMT_DEV_OPS_SET;
        break;

      case 'o':
        if (!optarg) {
          printf("ERROR: Object name is not provided\n");
          goto end;
        }

        stringLength = strlen(optarg);
        if (stringLength >= OBJECT_NAME_LEN) {
          printf("ERROR: Invalid object name\n");
          goto end;
        }

        strcpy(objectName, optarg);
        break;

      case 'n':
        if (!optarg) {
          printf("ERROR: nic-name name is not provided\n");
          goto end;
        }

        stringLength = strlen(optarg);
        if (stringLength >= SFVMK_DEV_NAME_LEN) {
          printf("ERROR: Invalid nic-name name\n");
          goto end;
        }

        nicNameSet = VMK_TRUE;
        strcpy((char *) mgmtParm.deviceName, optarg);
        break;

      case 'e':
        if (!optarg) {
          printf("ERROR: --enable option args not provided\n");
          goto end;
        }

        memset(&mclogOption, 0, 10);
        strcpy(mclogOption, optarg);
        sfvmk_strLwr(mclogOption);
        break;

      case 'f':
        if (!optarg) {
          printf("ERROR: File name is not provided\n");
          goto end;
        }

        strcpy(fileName, optarg);
        break;
      case 't':
        if (!optarg) {
          printf("ERROR: Firmware type is not provided\n");
          goto end;
        }

        memset(&fwTypeName, 0, 16);
        strcpy(fwTypeName, optarg);
        sfvmk_strLwr(fwTypeName);

        if (!strcmp(fwTypeName, "controller"))
          fwType = SFVMK_FIRMWARE_MC;
        else if (!strcmp(fwTypeName, "bootrom"))
          fwType = SFVMK_FIRMWARE_BOOTROM;
        else if (!strcmp(fwTypeName, "uefirom"))
          fwType = SFVMK_FIRMWARE_UEFI;
        else if (!strcmp(fwTypeName, "suc"))
          fwType = SFVMK_FIRMWARE_SUC;
        else
          fwType = SFVMK_FIRMWARE_INVALID;

        break;
      case 'm':
        if (!optarg) {
          printf("ERROR: FEC mode is not provided\n");
          goto end;
        }

        if (strlen(optarg) > 10) {
          printf("ERROR: FEC mode name is invalid\n");
          goto end;
        }

        memset(&fecModeName, 0, 10);
        strcpy(fecModeName, optarg);

        fecMode = sfvmk_fecModeParse(fecModeName);
        if (fecMode == SFVMK_MGMT_FEC_NONE_MASK) {
          printf("ERROR: FEC mode name is invalid\n");
          goto end;
        }

        break;
      case '?':
      default:
        break;
    }
  }

  if (opType == SFVMK_MGMT_DEV_OPS_INVALID) {
    printf("ERROR: Neither get nor set is provided\n");
    goto end;
  }

  if (!nicNameSet) {
    printf("ERROR: Missing required parameter -n|--nic-name\n");
    goto end;
  }

  status = vmk_MgmtUserInit((vmk_MgmtApiSignature *)&sfvmk_mgmtSig, 0, &mgmtHandle);
  if (status != 0) {
    printf("ERROR: Unable to connect to the backend, error 0x%x\n", status);
    goto end;
  }

  /* Confirm if NIC name is a Solarflare NIC or not */
  if (!sfvmk_isSFNic(&mgmtParm))
    goto destroy_handle;

  switch (sfvmk_findObjectType(objectName)) {
    case SFVMK_OBJECT_MCLOG:
      if (opType == SFVMK_MGMT_DEV_OPS_SET) {
        if (!strcmp(mclogOption, "true"))
          optionEnable = VMK_TRUE;
        else if (!strcmp(mclogOption, "false"))
          optionEnable = VMK_FALSE;
        else
          goto destroy_handle;
      }

      sfvmk_mcLog(opType, &mgmtParm, optionEnable);
      break;

    case SFVMK_OBJECT_STATS:
      sfvmk_hwQueueStats(opType, &mgmtParm);
      break;

    case SFVMK_OBJECT_VPD:
      sfvmk_vpdGet(opType, &mgmtParm);
      break;

    case SFVMK_OBJECT_FIRMWARE:
      if (opType == SFVMK_MGMT_DEV_OPS_SET) {
        sfvmk_fwUpdate(&mgmtParm, fileName, fwType, fwTypeName);
      } else {
        memset(&fwCtx, 0, sizeof(fwCtx));
        fwCtx.isForce = VMK_FALSE;
        fwCtx.updateAllFirmware = VMK_FALSE;
        fwCtx.applyAllNic = VMK_FALSE;
        fwCtx.fwType = fwType;
        strcpy(fwCtx.ifaceName.string, mgmtParm.deviceName);

        sfvmk_runFirmwareOps(SFVMK_MGMT_DEV_OPS_GET, &fwCtx);
      }

      break;
    case SFVMK_OBJECT_FEC:
      if (opType == SFVMK_MGMT_DEV_OPS_SET) {
        if (fecMode == SFVMK_MGMT_FEC_NONE_MASK) {
          printf("ERROR: Invalid FEC mode settings\n");
          goto destroy_handle;
        }

        sfvmk_fecModeSet(&mgmtParm, fecMode);
      } else {
        sfvmk_fecModeGet(&mgmtParm);
      }
      break;

    case SFVMK_OBJECT_SENSOR:
      sfvmk_hwSensorGet(&mgmtParm, opType);
      break;

    default:
      printf("ERROR: Unknown object - %s\n", objectName);
  }

destroy_handle:
  vmk_MgmtUserDestroy(mgmtHandle);

end:
  printf("</string>");
  printf("</output>\n");
  return 0;
}

static VMK_ReturnStatus
sfvmk_mcLog(int opType, sfvmk_mgmtDevInfo_t *pMgmtParm, vmk_Bool cmdOption)
{
  sfvmk_mcdiLogging_t mcLog;
  int status;

  memset(&mcLog, 0, sizeof(mcLog));
  mcLog.mcLoggingOp = opType;
  mcLog.state =  cmdOption;

  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_MC_LOGGING_REQUEST, pMgmtParm, &mcLog);
  if (status != VMK_OK) {
    printf("ERROR: unable to connect to driver\n");
    return status;
  }

  if (pMgmtParm->status != VMK_OK) {
    printf("ERROR: MC logging state %s failed, error 0x%x\n",
           (opType == SFVMK_MGMT_DEV_OPS_GET) ? "get" : "set", pMgmtParm->status);
    return pMgmtParm->status;
  }

  printf("%s", mcLog.state ? "Enabled" : "Disabled");

  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_hwQueueStats(int opType, sfvmk_mgmtDevInfo_t *pMgmtParm)
{
  sfvmk_hwQueueStats_t stats;
  char *pBuffer;
  int status;

  if (opType != SFVMK_MGMT_DEV_OPS_GET) {
    status = VMK_BAD_PARAM;
    printf("ERROR: Set operation not supported\n");
    goto end;
  }

  stats.subCmd = SFVMK_MGMT_STATS_GET_SIZE;
  stats.size = 0;
  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_HW_QUEUE_STATS_GET, pMgmtParm, &stats);
  if (status != VMK_OK) {
    status = VMK_NO_CONNECT;
    printf("ERROR: Unable to connect to the backend, error 0x%x\n", status);
    goto end;
  }

  if (pMgmtParm->status != VMK_OK) {
    status = pMgmtParm->status;
    printf("ERROR: Hardware queue statistics get size failed, error 0x%x\n", status);
    goto end;
  }

  if (!stats.size) {
    status = VMK_BAD_PARAM;
    printf("ERROR: Invalid statistics buffer size\n");
    goto end;
  }

  pBuffer = malloc(stats.size);
  if (!pBuffer) {
    status = VMK_NO_MEMORY;
    printf("ERROR: Unable to allocate memmory for queue statistics buffer\n");
    goto end;
  }

  memset(pBuffer, 0, stats.size);
  stats.statsBuffer = (vmk_uint64)((vmk_uint32)pBuffer);
  stats.subCmd = SFVMK_MGMT_STATS_GET;
  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_HW_QUEUE_STATS_GET, pMgmtParm, &stats);
  if (status != VMK_OK) {
    status = VMK_NO_CONNECT;
    printf("ERROR: Unable to connect to the backend, error 0x%x\n", status);
    goto free_buffer;
  }

  if (pMgmtParm->status != VMK_OK) {
    status = pMgmtParm->status;
    printf("ERROR: Hardware queue statistics get failed, error 0x%x\n", status);
    goto free_buffer;
  }

  printf("%s\n", pBuffer);
  status = VMK_OK;

free_buffer:
  free(pBuffer);

end:
  return status;
}


static VMK_ReturnStatus
sfvmk_printVpd(const char *pIfaceName, vmk_uint8 tag, vmk_uint16 keyword)
{
  sfvmk_vpdInfo_t vpdInfo;
  VMK_ReturnStatus status;

  status = sfvmk_getVpdByTag(pIfaceName, &vpdInfo, tag, keyword);
  if (status == VMK_NOT_FOUND) {
    printf("[missing]\n");
    status = VMK_OK;
  } else if (status == VMK_OK)
    printf("%s\n", vpdInfo.vpdPayload);

  return status;
}

static VMK_ReturnStatus
sfvmk_vpdGet(int opType, sfvmk_mgmtDevInfo_t *pMgmtParm)
{
  VMK_ReturnStatus status;

  if (opType != SFVMK_MGMT_DEV_OPS_GET) {
    printf("ERROR: Set operation not supported\n");
    return VMK_BAD_PARAM;
  }

  printf("Product Name: ");
  if ((status = sfvmk_printVpd(pMgmtParm->deviceName, 0x02, 0x0)) != VMK_OK)
    goto end;

  printf("[PN] Part number: ");
  if ((status = sfvmk_printVpd(pMgmtParm->deviceName, 0x10, ('P' | 'N' << 8))) != VMK_OK)
    goto end;

  printf("[SN] Serial number: ");
  if ((status = sfvmk_printVpd(pMgmtParm->deviceName, 0x10, ('S' | 'N' << 8))) != VMK_OK)
    goto end;

  printf("[EC] Engineering changes: ");
  if ((status = sfvmk_printVpd(pMgmtParm->deviceName, 0x10, ('E' | 'C' << 8))) != VMK_OK)
    goto end;

  printf("[VD] Version: ");
  if ((status = sfvmk_printVpd(pMgmtParm->deviceName, 0x10, ('V' | 'D' << 8))) != VMK_OK)
    goto end;

end:
  if (status != VMK_OK)
    printf("ERROR: VPD get failed with error 0x%x\n", status);

  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_fwVersion(sfvmk_mgmtDevInfo_t *pMgmtParm, sfvmk_firmwareType_t fwType)
{
  sfvmk_versionInfo_t verInfo;
  vmk_uint8  macAddress[6];
  sfvmk_vpdInfo_t vpdInfo;
  char outBuffer[256];
  VMK_ReturnStatus ret = VMK_FAILURE;

  if (fwType > SFVMK_FIRMWARE_ALL)
    return VMK_BAD_PARAM;

  ret = sfvmk_getMACAddress(pMgmtParm->deviceName, macAddress);
  if (ret != VMK_OK)
    return ret;

  printf("%s - MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
         pMgmtParm->deviceName,
         macAddress[0], macAddress[1], macAddress[2],
         macAddress[3], macAddress[4], macAddress[5]);

  ret = sfvmk_getVpdByTag(pMgmtParm->deviceName, &vpdInfo, 0x02, 0x0);
  if (ret != VMK_OK)
    return ret;

  printf("NIC model: %s\n", vpdInfo.vpdPayload);

  memset(outBuffer, 0, sizeof(outBuffer));

  if (fwType & SFVMK_FIRMWARE_MC) {
    memset(&verInfo, 0, sizeof(verInfo));
    verInfo.type = SFVMK_GET_FW_VERSION;
    ret = sfvmk_getFWVersion(pMgmtParm->deviceName, &verInfo);
    if (ret != VMK_OK)
      goto end;

    printf("Controller version: %s\n", verInfo.version.string);
  }

  if (fwType & SFVMK_FIRMWARE_BOOTROM) {
    memset(&verInfo, 0, sizeof(verInfo));
    verInfo.type = SFVMK_GET_ROM_VERSION;
    ret = sfvmk_getFWVersion(pMgmtParm->deviceName, &verInfo);
    if (ret != VMK_OK)
      goto end;

    printf("BOOTROM version:    %s\n", verInfo.version.string);
  }

  if (fwType & SFVMK_FIRMWARE_UEFI) {
    memset(&verInfo, 0, sizeof(verInfo));
    verInfo.type = SFVMK_GET_UEFI_VERSION;
    ret = sfvmk_getFWVersion(pMgmtParm->deviceName, &verInfo);
    if (ret != VMK_OK)
      goto end;

    printf("UEFI version:       %s\n", verInfo.version.string);
  }

  if (fwType & SFVMK_FIRMWARE_SUC) {
    memset(&verInfo, 0, sizeof(verInfo));
    verInfo.type = SFVMK_GET_SUC_VERSION;
    ret = sfvmk_getFWVersion(pMgmtParm->deviceName, &verInfo);
    if (ret != VMK_OK)
      goto end;

    printf("SUC version:        %s\n", verInfo.version.string);
  }

  ret = VMK_OK;

end:
  return ret;
}

static sfvmk_nvramType_t sfvmk_getNvramType(sfvmk_firmwareType_t fwType)
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

    case SFVMK_FIRMWARE_ANY:
      type = SFVMK_NVRAM_INVALID;
      break;

    default:
      type = SFVMK_NVRAM_NTYPE;
  }

  return type;
}

static VMK_ReturnStatus
sfvmk_fwUpdate(sfvmk_mgmtDevInfo_t *pMgmtParm,
               char *pFileName, sfvmk_firmwareType_t fwType, char *pFwTypeName)
{
  sfvmk_imgUpdateV2_t imgUpdateV2;
  FILE *pFile = NULL;
  vmk_Name siblingPorts[SFVMK_MAX_INTERFACE];
  char *pBuf = NULL;
  int fileSize = 0;
  int portCount = 0;
  int status = 0;
  int i;

  pFile = fopen(pFileName, "r");
  if (pFile == NULL) {
    status = VMK_INVALID_NAME;
    goto end;
  }

  status = fseek(pFile, 0, SEEK_END);
  if (status != 0) {
    status = VMK_READ_ERROR;
    goto end;
  }

  fileSize = ftell(pFile);
  if (fileSize <= 0) {
    status = VMK_READ_ERROR;
    goto end;
  }

  rewind(pFile);

  pBuf = (char*) malloc(fileSize);
  if (pBuf == NULL) {
    status = VMK_NO_MEMORY;
    goto close_file;
  }

  memset(pBuf, 0, fileSize);
  if (fread(pBuf, 1, fileSize, pFile) != fileSize) {
    status = VMK_READ_ERROR;
    goto free_buffer;
  }

  status = sfvmk_fwVersion(pMgmtParm, fwType);
  if (status != VMK_OK)
    goto free_buffer;

  printf("Updating firmware...\n");

  portCount = sfvmk_getSiblingPorts(pMgmtParm, siblingPorts);
  if (portCount <= 0) {
    status = VMK_BAD_PARAM_COUNT;
    goto free_buffer;
  }

  if (fwType != SFVMK_FIRMWARE_ANY)
    printf("Updating %s firmware for", pFwTypeName);
  else
    printf("Updating firmware for");

  for (i = 0; i < portCount; i++)
    printf(" %s", siblingPorts[i].string);
  printf("...\n");

  memset(&imgUpdateV2, 0, sizeof(imgUpdateV2));
  imgUpdateV2.pFileBuffer = (vmk_uint64)((vmk_uint32)pBuf);
  imgUpdateV2.size = fileSize;
  imgUpdateV2.type = sfvmk_getNvramType(fwType);

  status = sfvmk_setNicFirmware(pMgmtParm->deviceName, &imgUpdateV2);
  if (status != VMK_OK)
    goto free_buffer;

  printf("Firmware was successfully updated!\n");

free_buffer:
  free(pBuf);

close_file:
  fclose(pFile);

end:
  return status;
}

static int
sfvmk_getSiblingPorts(sfvmk_mgmtDevInfo_t *pMgmtParm, vmk_Name siblingPorts[])
{
  sfvmk_ifaceList_t ifaceList;
  vmk_Name pciBDF;
  vmk_Name originPortInfo;
  int portCount = 0;
  int pciBDFStrLen = 0;
  int status;
  int i;

  status = sfvmk_getNicList(&ifaceList);
  if (status != VMK_OK)
    return -1;

  status = sfvmk_getPCIAddress(pMgmtParm->deviceName, &pciBDF);
  if (status != VMK_OK)
    return -1;

  strcpy(originPortInfo.string, pciBDF.string);
  pciBDFStrLen = strcspn(pciBDF.string, ".");

  for (i = 0; i < ifaceList.ifaceCount; i++) {
    status = sfvmk_getPCIAddress(ifaceList.ifaceArray[i].string, &pciBDF);
    if (status != VMK_OK)
      return -1;

    if (strncmp(originPortInfo.string,
                pciBDF.string, (pciBDFStrLen - 1)) == 0) {
       strcpy(siblingPorts[portCount].string, ifaceList.ifaceArray[i].string);
       portCount++;
    }
  }

  return portCount;
}

static vmk_uint32 sfvmk_fecModeTypeGet(const char *pFecModeName)
{
  if (!strcasecmp(pFecModeName, "auto"))
    return SFVMK_MGMT_FEC_AUTO_MASK;
  if (!strcasecmp(pFecModeName, "off"))
    return SFVMK_MGMT_FEC_OFF_MASK;
  if (!strcasecmp(pFecModeName, "rs"))
    return SFVMK_MGMT_FEC_RS_MASK;
  if (!strcasecmp(pFecModeName, "baser"))
    return SFVMK_MGMT_FEC_BASER_MASK;

  return 0;
}

static vmk_uint32 sfvmk_fecModeParse(const char *pFecModeName)
{
  vmk_uint32 fecMode = 0;
  char buf[6];

  if (!pFecModeName)
    return SFVMK_MGMT_FEC_NONE_MASK;

  while (*pFecModeName) {
    vmk_uint32 fecModeStrSz = 0;
    int mode;

    fecModeStrSz = strcspn(pFecModeName, ",");
    if (fecModeStrSz >= 6)
      return SFVMK_MGMT_FEC_NONE_MASK;

    memcpy(buf, pFecModeName, fecModeStrSz);
    buf[fecModeStrSz] = '\0';
    mode = sfvmk_fecModeTypeGet(buf);
    if (!mode)
      return SFVMK_MGMT_FEC_NONE_MASK;

    fecMode |= mode;
    pFecModeName += fecModeStrSz;

    if (*pFecModeName)
      pFecModeName++;
  }

  return fecMode;
}

static void sfvmk_printFecMode(vmk_uint32 fec)
{
  if (fec & SFVMK_MGMT_FEC_NONE_MASK)
    printf(" None");
  if (fec & SFVMK_MGMT_FEC_AUTO_MASK)
    printf(" Auto");
  if (fec & SFVMK_MGMT_FEC_OFF_MASK)
    printf(" Off");
  if (fec & SFVMK_MGMT_FEC_RS_MASK)
    printf(" RS");
  if (fec & SFVMK_MGMT_FEC_BASER_MASK)
    printf(" BaseR");
}

static VMK_ReturnStatus
sfvmk_fecModeSet(sfvmk_mgmtDevInfo_t *pMgmtParm, vmk_uint32 fec)
{
  sfvmk_fecMode_t fecMode;
  VMK_ReturnStatus status;

  memset(&fecMode, 0, sizeof(fecMode));

  fecMode.type = SFVMK_MGMT_DEV_OPS_SET;
  fecMode.fec = fec;
  status = sfvmk_postFecReq(pMgmtParm->deviceName, &fecMode);
  if (status != VMK_OK) {
    printf("ERROR: FEC mode set failed with error 0x%x\n", status);
    return status;
  }

  printf("FEC parameters for %s applied\n", pMgmtParm->deviceName);
  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_fecModeGet(sfvmk_mgmtDevInfo_t *pMgmtParm)
{
  sfvmk_fecMode_t fecMode;
  VMK_ReturnStatus status;

  memset(&fecMode, 0, sizeof(fecMode));

  fecMode.type = SFVMK_MGMT_DEV_OPS_GET;
  status = sfvmk_postFecReq(pMgmtParm->deviceName, &fecMode);
  if (status != VMK_OK) {
    printf("ERROR: FEC mode get failed with error 0x%x\n", status);
    return status;
  }

  printf("FEC parameters for %s:\n", pMgmtParm->deviceName);
  printf("Configured FEC encodings:");
  sfvmk_printFecMode(fecMode.fec);
  printf("\n");
  printf("Active FEC encoding:");
  sfvmk_printFecMode(fecMode.activeFec);
  printf("\n");

  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_hwSensorGet(sfvmk_mgmtDevInfo_t *pMgmtParm, int opType)
{
  sfvmk_hwSensor_t sensor;
  char *pBuffer;
  VMK_ReturnStatus status;

  if (opType != SFVMK_MGMT_DEV_OPS_GET) {
    status = VMK_BAD_PARAM;
    printf("ERROR: Set operation not supported\n");
    goto end;
  }

  sensor.subCmd = SFVMK_MGMT_SENSOR_GET_SIZE;
  sensor.size = 0;
  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_SENSOR_INFO_GET, pMgmtParm, &sensor);
  if (status != VMK_OK) {
    status = VMK_NO_CONNECT;
    printf("ERROR: Unable to connect to the backend, error 0x%x\n", status);
    goto end;
  }

  if (pMgmtParm->status != VMK_OK) {
    status = pMgmtParm->status;
    printf("ERROR: Hardware sensor info size get failed, error 0x%x\n", status);
    goto end;
  }

  if (!sensor.size) {
    status = VMK_BAD_PARAM;
    printf("ERROR: Invalid sensor info buffer size\n");
    goto end;
  }

  pBuffer = malloc(sensor.size);
  if (!pBuffer) {
    status = VMK_NO_MEMORY;
    printf("ERROR: Unable to allocate memmory for sensor info buffer\n");
    goto end;
  }

  memset(pBuffer, 0, sensor.size);
  sensor.sensorBuffer = (vmk_uint64)((vmk_uint32)pBuffer);
  sensor.subCmd = SFVMK_MGMT_SENSOR_GET;
  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_SENSOR_INFO_GET, pMgmtParm, &sensor);
  if (status != VMK_OK) {
    status = VMK_NO_CONNECT;
    printf("ERROR: Unable to connect to the backend, error 0x%x\n", status);
    goto free_buffer;
  }

  if (pMgmtParm->status != VMK_OK) {
    status = pMgmtParm->status;
    printf("ERROR: Hardware sensor info get failed, error 0x%x\n", status);
    goto free_buffer;
  }

  printf("%s", pBuffer);
  status = VMK_OK;

free_buffer:
  free(pBuffer);

end:
  return status;
}

