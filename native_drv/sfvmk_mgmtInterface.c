
#include <vmkapi.h> /* Required for VMKAPI definitions and macros */
#include "sfvmk_mgmtInterface.h"

/*
 * In this file, we'll statically define and initialize the
 * array of vmk_MgmtCallbackInfo structures that fully describe
 * all of the callbacks that the management interface we're
 * using could invoke.  The array will include both user and
 * kernel callback descriptions.  In this example, there is
 * only one callback: a kernel callback.
 *
 * We also statically define and initialize the management
 * signature, which includes the callbacks.
 *
 * This file is intended to be compiled for both user-space
 * and kernel-space builds.
 */
vmk_MgmtCallbackInfo driverMgmtCallbacks[SFVMK_MGMT_CB_TOTAL] = {
  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtMcdiCallback,
      .synchronous = 1, /* 0 indicates asynchronous */
      .numParms = 2,
      .parmSizes = { sizeof(sfvmk_mgmtDevInfo_t),     /* the size of sfvmk_mgmtDevInfo_t */
                     sizeof(sfvmk_mcdiRequest2_t)},  /* the size of sfvmk_mcdiRequest2_t */
      .parmTypes = { VMK_MGMT_PARMTYPE_INOUT,
                     VMK_MGMT_PARMTYPE_INOUT}, /* Both Parameters from user is an input 
                                                  and output parameter */

      .callbackId = SFVMK_CB_MCDI_REQUEST_2,
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtNVRAMCallback,
      .synchronous = 1, /* 0 indicates asynchronous */
      .numParms = 2,
      .parmSizes = { sizeof(sfvmk_mgmtDevInfo_t),     /* the size of sfvmk_mgmtDevInfo_t */
                     sizeof(sfvmk_nvramCmd_t)},    /* the size of sfvmk_nvramCmd_t */
      .parmTypes = { VMK_MGMT_PARMTYPE_INOUT,
                     VMK_MGMT_PARMTYPE_INOUT},  /* Both Parameters from user is an input
                                                  and output parameter */

      .callbackId = SFVMK_CB_NVRAM_REQUEST,
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtVerInfoCallback,
      .synchronous = 1, /* 0 indicates asynchronous */
      .numParms = 2,
      .parmSizes = { sizeof(sfvmk_mgmtDevInfo_t),     /* the size of sfvmk_mgmtDevInfo_t */
                     sizeof(sfvmk_versionInfo_t)},  /* the size of sfvmk_versionInfo_t */
      .parmTypes = { VMK_MGMT_PARMTYPE_INOUT,
                     VMK_MGMT_PARMTYPE_INOUT},  /* Both Parameters from user is an input
                                                  and output parameter */

      .callbackId = SFVMK_CB_VERINFO_GET,
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtLinkStatusUpdate,
      .synchronous = 1, /* 0 indicates asynchronous */
      .numParms = 2,
      .parmSizes = { sizeof(sfvmk_mgmtDevInfo_t),     /* the size of sfvmk_mgmtDevInfo_t */
                     sizeof(sfvmk_linkStatus_t)},  /* the size of sfvmk_linkStatus_t */
      .parmTypes = { VMK_MGMT_PARMTYPE_INOUT,
                     VMK_MGMT_PARMTYPE_INOUT},  /* Both Parameters from user is an input
                                                  and output parameter */

      .callbackId = SFVMK_CB_LINK_STATUS_UPDATE,
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtLinkSpeedUpdate,
      .synchronous = 1, /* 0 indicates asynchronous */
      .numParms = 2,
      .parmSizes = { sizeof(sfvmk_mgmtDevInfo_t),     /* the size of sfvmk_mgmtDevInfo_t */
                     sizeof(sfvmk_linkSpeed_t)},  /* the size of sfvmk_linkSpeed_t */
      .parmTypes = { VMK_MGMT_PARMTYPE_INOUT,
                     VMK_MGMT_PARMTYPE_INOUT},  /* Both Parameters from user is an input
                                                  and output parameter */

      .callbackId = SFVMK_CB_LINK_SPEED_UPDATE,
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtIntrModeration,
      .synchronous = 1, /* 0 indicates asynchronous */
      .numParms = 2,
      .parmSizes = { sizeof(sfvmk_mgmtDevInfo_t),     /* the size of sfvmk_mgmtDevInfo_t */
                     sizeof(sfvmk_intrCoalsParam_t)},  /* the size of sfvmk_intrCoalsParam_t */
      .parmTypes = { VMK_MGMT_PARMTYPE_INOUT,
                     VMK_MGMT_PARMTYPE_INOUT},  /* Both Parameters from user is an input
                                                  and output parameter */

      .callbackId = SFVMK_CB_INTR_MODERATION,
  },

  {
      .location = VMK_MGMT_CALLBACK_KERNEL,
      .callback = sfvmk_mgmtPCIInfoCallback,
      .synchronous = 1, /* 0 indicates asynchronous */
      .numParms = 2,
      .parmSizes = { sizeof(sfvmk_mgmtDevInfo_t),   /* the size of sfvmk_mgmtDevInfo_t */
                     sizeof(sfvmk_pciInfo_t)},      /* the size of sfvmk_pciInfo_t */
      .parmTypes = { VMK_MGMT_PARMTYPE_INOUT,
                     VMK_MGMT_PARMTYPE_OUT},  /* First Parameter from user is an input
                                                 and output parameter and second parameter
                                                 is only outout */

      .callbackId = SFVMK_CB_PCI_INFO_GET,
  }

};

/*
 * The Management API Signature is used to define the overall signature of the
 * management interface that will be used.  It contains a name, vendor, version,
 * and a description of the callbacks that the interface contains.
 */
vmk_MgmtApiSignature driverMgmtSig = {
   /*
    * The version specifies the version of this interface.  Only callers that
    * have the same major number are considered compatible.  The VMKAPI
    * management APIs will be extended to support compatibility shimming in
    * future versions of VMKAPI.  For now, you can use this to express what
    * interfaces are compatible with which callers and callees.
    */
   .version = VMK_REVISION_FROM_NUMBERS(1,0,0,0),
   /*
    * The name is the name of this interface.  The name and vendor,
    * must be globally unique or else
    * initialization will fail.  Only user-space connections from
    * with signatures bearing the same name and vendor will be allowed.
    * That is, vmk_MgmtUserInit will only succeed if there's a matching
    * name and vendor.
    * NOTE:  Your name must be 32 characters or fewer, including NUL.
    */
   .name.string = SFVMK_INTERFACE_NAME,
   /*
    * The vendor is the vendor providing this interface.  The name and
    * vendor, concatenated in that order, must be globally unique or
    * else initialization will fail.  Only user-space connections from
    * signatures bearing the same name and vendor will be allowed.
    * That is, vmk_MgmtUserInit will only succeed if there's a matching
    * name and vendor.
    * NOTE:  Your vendor must be 32 characters or fewer, including NUL.
    */
   .vendor.string = SFVMK_INTERFACE_VENDOR,
   /*
    * 'numCallbacks' is the total number of callbacks and thus also
    * specifies the number of elements in the 'callbacks' array.
    */
   .numCallbacks = SFVMK_MGMT_CB_TOTAL,
   /*
    * 'callbacks' is the array vmk_MgmtCallbackInfo structures that
    * fully describe each individual callback that can be invoked by
    * the interface in this API signature.
    */
   .callbacks = driverMgmtCallbacks,
};
