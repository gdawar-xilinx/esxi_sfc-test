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
#include <errno.h>
#include "sfvmk_mgmt_interface.h"

extern const vmk_MgmtApiSignature sfvmk_mgmtSig;
static vmk_MgmtUserHandle mgmtHandle;

#define OBJECT_NAME_LEN  16

typedef enum sfvmk_objectType_e {
  SFVMK_OBJECT_MCLOG = 0,
  SFVMK_OBJECT_STATS,
  SFVMK_OBJECT_VPD,
  SFVMK_OBJECT_MAX
} sfvmk_objectType_t;

static const char * supportedObjects[] = {
  "mclog",
  "stats",
  "vpd"
};

/*
 * Functions to handle request for each object
 */
static void sfvmk_mcLog(int opType, sfvmk_mgmtDevInfo_t *mgmtParm, char cmdOption);
static void sfvmk_hwQueueStats(int opType, sfvmk_mgmtDevInfo_t *mgmtParm);
static void sfvmk_vpdGet(int opType, sfvmk_mgmtDevInfo_t *mgmtParm);

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

int
main(int argc, char **argv)
{
  int c;
  int opType = SFVMK_MGMT_DEV_OPS_INVALID;
  int status = 0 ;
  int stringLength = 0;
  vmk_Bool optionEnable = VMK_FALSE;
  vmk_Bool nicNameSet = VMK_FALSE;
  char optionValue = ' ';
  char objectName[16];
  sfvmk_mgmtDevInfo_t mgmtParm;

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

        optionValue = optarg[0];
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

  switch (sfvmk_findObjectType(objectName)) {
    case SFVMK_OBJECT_MCLOG:
      if (opType == SFVMK_MGMT_DEV_OPS_SET) {
        if (!strchr("yYnN", optionValue)) {
          printf("ERROR: --enable Enable/ Disable MC "
                 "logging (Y/ Yes/ N / No) required\n");
          goto end;
        }

        optionEnable = ((optionValue == 'y') || (optionValue == 'Y')) ?
                        VMK_TRUE : VMK_FALSE;
      }

      sfvmk_mcLog(opType, &mgmtParm, optionEnable);
      break;
    case SFVMK_OBJECT_STATS:
      sfvmk_hwQueueStats(opType, &mgmtParm);
      break;
    case SFVMK_OBJECT_VPD:
      sfvmk_vpdGet(opType, &mgmtParm);
      break;
    default:
      printf("ERROR: Unknown object - %s\n", objectName);
  }

  vmk_MgmtUserDestroy(mgmtHandle);

end:
  printf("</string>");
  printf("</output>\n");
  return 0;
}

static void sfvmk_mcLog(int opType, sfvmk_mgmtDevInfo_t *mgmtParm, vmk_Bool cmdOption)
{
  sfvmk_mcdiLogging_t mcLog;
  int status;

  memset(&mcLog, 0, sizeof(mcLog));
  mcLog.mcLoggingOp = opType;
  mcLog.state =  cmdOption;

  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_MC_LOGGING_REQUEST, mgmtParm, &mcLog);
  if (status != VMK_OK){
    printf("ERROR: unable to connect to driver\n");
    return;
  }

  if (mgmtParm->status != VMK_OK) {
    printf("ERROR: MC logging state %s failed, error 0x%x\n",
           (opType == SFVMK_MGMT_DEV_OPS_GET) ? "get" : "set", status);
    return;
  }

  printf("%s", mcLog.state ? "Enabled" : "Disabled");
}

static void sfvmk_hwQueueStats(int opType, sfvmk_mgmtDevInfo_t *mgmtParm)
{
  sfvmk_hwQueueStats_t stats;
  char *pBuffer;
  int status;

  if (opType != SFVMK_MGMT_DEV_OPS_GET) {
    printf("ERROR: Set operation not supported\n");
    goto end;
  }

  stats.subCmd = SFVMK_MGMT_STATS_GET_SIZE;
  stats.size = 0;
  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_HW_QUEUE_STATS_GET, mgmtParm, &stats);
  if (status != VMK_OK){
    printf("ERROR: Unable to connect to the backend, error 0x%x\n", status);
    goto end;
  }

  if (mgmtParm->status != VMK_OK) {
    printf("ERROR: Hardware queue statistics get failed, error 0x%x\n", mgmtParm->status);
    goto end;
  }

  if (!stats.size) {
    printf("ERROR: Invalid statistics buffer size\n");
    goto end;
  }

  pBuffer = malloc(stats.size);
  if (!pBuffer) {
    printf("ERROR: Unable to allocate memmory for queue statistics buffer\n");
    goto end;
  }

  memset(pBuffer, 0, stats.size);
  stats.statsBuffer = (vmk_uint64)pBuffer;
  stats.subCmd = SFVMK_MGMT_STATS_GET;
  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_HW_QUEUE_STATS_GET, mgmtParm, &stats);
  if (status != VMK_OK){
    printf("ERROR: Unable to connect to the backend, error 0x%x\n", status);
    goto free_buffer;
  }

  if (mgmtParm->status != VMK_OK) {
    printf("ERROR: Hardware queue statistics get failed, error 0x%x\n", mgmtParm->status);
    goto free_buffer;
  }

  printf("%s\n", pBuffer);

free_buffer:
  free(pBuffer);

end:
  return;
}

static int sfvmk_vpdGetByTag(sfvmk_mgmtDevInfo_t *mgmtParm, sfvmk_vpdInfo_t *vpdInfo,
                             vmk_uint8 tag, vmk_uint16 keyword)
{
  int status;

  memset(vpdInfo, 0, sizeof(*vpdInfo));
  vpdInfo->vpdOp = SFVMK_MGMT_DEV_OPS_GET;
  vpdInfo->vpdTag = tag;
  vpdInfo->vpdKeyword = keyword;
  status = vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                      SFVMK_CB_VPD_REQUEST, mgmtParm, vpdInfo);
  if (status != VMK_OK) {
    printf("ERROR: Unable to connect to the backend, error 0x%x\n", status);
    return -EAGAIN;
  }

  if (mgmtParm->status != VMK_OK) {
    printf("ERROR: VPD info get failed, error 0x%x\n", mgmtParm->status);
    return -EINVAL;
  }

  return 0;
}

static void sfvmk_vpdGet(int opType, sfvmk_mgmtDevInfo_t *mgmtParm)
{
  sfvmk_vpdInfo_t vpdInfo;
  int ret;

  if (opType != SFVMK_MGMT_DEV_OPS_GET) {
    printf("ERROR: Set operation not supported\n");
    return;
  }

  ret = sfvmk_vpdGetByTag(mgmtParm, &vpdInfo, 0x02, 0x0);
  if (ret < 0)
    return;
  printf("Product Name: %s\n", vpdInfo.vpdPayload);

  ret = sfvmk_vpdGetByTag(mgmtParm, &vpdInfo, 0x10, ('P' | 'N' << 8));
  if (ret < 0)
    return;
  printf("[PN] Part number: %s\n", vpdInfo.vpdPayload);

  ret = sfvmk_vpdGetByTag(mgmtParm, &vpdInfo, 0x10, ('S' | 'N' << 8));
  if (ret < 0)
    return;
  printf("[SN] Serial number: %s\n", vpdInfo.vpdPayload);

  ret = sfvmk_vpdGetByTag(mgmtParm, &vpdInfo, 0x10, ('E' | 'C' << 8));
  if (ret < 0)
    return;
  printf("[EC] Engineering changes: %s\n", vpdInfo.vpdPayload);

  ret = sfvmk_vpdGetByTag(mgmtParm, &vpdInfo, 0x10, ('V' | 'D' << 8));
  if (ret < 0)
    return;
  printf("[VD] Version: %s\n", vpdInfo.vpdPayload);
}

