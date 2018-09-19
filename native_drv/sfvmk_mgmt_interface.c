/*
 * Copyright (c) 2017, Solarflare Communications Inc.
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

/*
 * This file has statically defined and initialized the array of
 * vmk_MgmtCallbackInfo structures and management signature and that fully
 * described all of the callbacks that the management interface ABI.
 * The array will include both user and kernel callback descriptions.
 *
 * This file is intended to be compiled for both user-space
 * and kernel-space builds.
 *
 */

#include <vmkapi.h>
#include "sfvmk_mgmt_interface.h"

const vmk_MgmtCallbackInfo sfvmk_mgmtCallbacks[] = {
  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtMcdiCallback,
      .synchronous = VMK_TRUE,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[1] = sizeof(sfvmk_mcdiRequest_t),

      .callbackId = SFVMK_CB_MCDI_REQUEST
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtMCLoggingCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[1] = sizeof(sfvmk_mcdiLogging_t),

      .callbackId = SFVMK_CB_MC_LOGGING_REQUEST
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtPCIInfoCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[1] = sizeof(sfvmk_pciInfo_t),

      .callbackId = SFVMK_CB_PCI_INFO_GET
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtVPDInfoCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[1] = sizeof(sfvmk_vpdInfo_t),

      .callbackId = SFVMK_CB_VPD_REQUEST
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtLinkStatusCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_OUT,
      .parmSizes[1] = sizeof(vmk_Bool),

      .callbackId = SFVMK_CB_LINK_STATUS_GET
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtLinkSpeedCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[1] = sizeof(sfvmk_linkSpeed_t),

      .callbackId = SFVMK_CB_LINK_SPEED_REQUEST
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtVerInfoCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[1] = sizeof(sfvmk_versionInfo_t),

      .callbackId = SFVMK_CB_VERINFO_GET
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtIntrModerationCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[1] = sizeof(sfvmk_intrCoalsParam_t),

      .callbackId = SFVMK_CB_INTR_MODERATION_REQUEST
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtNVRAMCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[1] = sizeof(sfvmk_nvramCmd_t),

      .callbackId = SFVMK_CB_NVRAM_REQUEST
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtImgUpdateCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] =  VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] =  sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] =  VMK_MGMT_PARMTYPE_IN,
      .parmSizes[1] =  sizeof(sfvmk_imgUpdate_t),

      .callbackId = SFVMK_CB_IMG_UPDATE
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtHWQStatsCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[1] = sizeof(sfvmk_hwQueueStats_t),

      .callbackId = SFVMK_CB_HW_QUEUE_STATS_GET
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtMACAddressCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[1] = sizeof(sfvmk_macAddress_t),

      .callbackId = SFVMK_CB_MAC_ADDRESS_GET
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtInterfaceListCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_OUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_OUT,
      .parmSizes[1] = sizeof(sfvmk_ifaceList_t),

      .callbackId = SFVMK_CB_IFACE_LIST_GET
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtFecModeCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] = sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] = VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[1] = sizeof(sfvmk_fecMode_t),

      .callbackId = SFVMK_CB_FEC_MODE_REQUEST
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtImgUpdateV2Callback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] =  VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] =  sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] =  VMK_MGMT_PARMTYPE_IN,
      .parmSizes[1] =  sizeof(sfvmk_imgUpdateV2_t),

      .callbackId = SFVMK_CB_IMG_UPDATE_V2
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtHWSensorInfoCallback,
      .synchronous = 1,
      .numParms = 2,

      .parmTypes[0] =  VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[0] =  sizeof(sfvmk_mgmtDevInfo_t),

      .parmTypes[1] =  VMK_MGMT_PARMTYPE_INOUT,
      .parmSizes[1] =  sizeof(sfvmk_hwSensor_t),

      .callbackId = SFVMK_CB_SENSOR_INFO_GET
  }

};

/*
 * The Management API Signature is used to define the overall signature of the
 * management interface that will be used.  It contains a name, vendor, version,
 * and a description of the callbacks that the interface contains.
 */
const vmk_MgmtApiSignature sfvmk_mgmtSig = {
   /*
    * The version specifies the version of this interface.  Only callers that
    * have the same major number are considered compatible.  The VMKAPI
    * management APIs will be extended to support compatibility shimming in
    * future versions of VMKAPI.
    */
   .version = VMK_REVISION_FROM_NUMBERS (1, 0, 0, 1006),
   /*
    * The name is the name of this interface. The name and vendor,
    * must be globally unique or else initialization will fail.
    * The user-space connection (vmk_MgmtUserInit) will only succeed
    * if there's a matching name and vendor.
    */
   .name.string = SFVMK_INTERFACE_NAME,
   /*
    * The vendor is the vendor providing this interface.  The name and
    * vendor, concatenated in that order, must be globally unique or
    * else initialization will fail.  Only user-space connections from
    * signatures bearing the same name and vendor will be allowed.
    * That is, vmk_MgmtUserInit will only succeed if there's a matching
    * name and vendor.
    */
   .vendor.string = SFVMK_INTERFACE_VENDOR,
   /*
    * This is the size of the .callbacks array.
    */
   .numCallbacks = (sizeof(sfvmk_mgmtCallbacks) / sizeof(sfvmk_mgmtCallbacks[0])),
   /*
    * 'callbacks' is the array vmk_MgmtCallbackInfo structures that
    * fully describe each individual callback that can be invoked by
    * the interface in this API signature.
    */
   .callbacks = (vmk_MgmtCallbackInfo *) sfvmk_mgmtCallbacks
};
