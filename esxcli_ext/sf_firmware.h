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

#ifndef _SF_FIRMWARE_H
#define _SF_FIRMWARE_H

#include "sf_utils.h"

typedef struct sfvmk_ifaceNode_s {
  struct sfvmk_ifaceNode_s *pNext;
  struct sfvmk_ifaceNode_s *pPrev;
  vmk_Name pciBDF;
  vmk_Name ifaceName;
  vmk_uint8 macAddress[6];
} sfvmk_ifaceNode_t;

typedef struct sfvmk_masterDevNode_s {
  struct sfvmk_masterDevNode_s *pNext;
  sfvmk_ifaceNode_t *pMsIfaceNode;
  vmk_Name mcVer;
  vmk_Name sucVer;
  vmk_Name bootromVer;
  vmk_Name uefiromVer;
  vmk_uint8 nicModel[256];
} sfvmk_masterDevNode_t;

typedef struct sfvmk_firmwareCtx_s {
  struct sfvmk_masterDevNode_s *pMasters;
  int pciBDF;
  char fwFileName[SFVMK_MAX_FILENAME_LENGTH];
  char errorMsg[SFVMK_MAX_FW_RDWR_ERR_LENGTH];
  sfvmk_firmwareType_t fwType;
  vmk_Name ifaceName;
  vmk_uint8 nicCount;
  vmk_Bool isForce;
  vmk_Bool applyAllNic;
  vmk_Bool updateAllFirmware;
} sfvmk_firmwareCtx_t;

VMK_ReturnStatus sfvmk_runFirmwareOps(int opType, sfvmk_firmwareCtx_t *pfwCtx);

#endif
