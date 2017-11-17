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

#include "sfvmk_driver.h"
#include "sfvmk_ut.h"
#include "sfvmk_mgmt_interface.h"

extern const vmk_MgmtApiSignature sfvmk_mgmtSig;

sfvmk_modInfo_t sfvmk_modInfo = {
   .heapID           = VMK_INVALID_HEAP_ID,
   .memPoolID        = VMK_MEMPOOL_INVALID,
   .logID            = VMK_INVALID_LOG_HANDLE,
   .logThrottledID   = VMK_INVALID_LOG_HANDLE,
   .lockDomain       = VMK_LOCKDOMAIN_INVALID,
   .mgmtHandle       = NULL,
};

static void
sfvmk_modInfoCleanup(void)
{
  if (sfvmk_modInfo.mgmtHandle) {
    vmk_MgmtDestroy(sfvmk_modInfo.mgmtHandle);
    sfvmk_modInfo.mgmtHandle = NULL;
  }

  if (sfvmk_modInfo.driverID != NULL) {
    sfvmk_driverUnregister();
    sfvmk_modInfo.driverID = NULL;
  }
  if (sfvmk_modInfo.lockDomain != VMK_LOCKDOMAIN_INVALID) {
    vmk_LockDomainDestroy(sfvmk_modInfo.lockDomain);
    sfvmk_modInfo.lockDomain = VMK_LOCKDOMAIN_INVALID;
  }
  if (sfvmk_modInfo.memPoolID != VMK_MEMPOOL_INVALID) {
    vmk_MemPoolDestroy(sfvmk_modInfo.memPoolID);
    sfvmk_modInfo.memPoolID = VMK_MEMPOOL_INVALID;
  }
  if (sfvmk_modInfo.logThrottledID != VMK_INVALID_LOG_HANDLE) {
    vmk_LogUnregister(sfvmk_modInfo.logThrottledID);
    sfvmk_modInfo.logThrottledID = VMK_INVALID_LOG_HANDLE;
  }
  if (sfvmk_modInfo.logID != VMK_INVALID_LOG_HANDLE) {
    vmk_LogUnregister(sfvmk_modInfo.logID);
    sfvmk_modInfo.logID = VMK_INVALID_LOG_HANDLE;
  }
  if (sfvmk_modInfo.heapID != VMK_INVALID_HEAP_ID) {
    vmk_HeapDestroy(sfvmk_modInfo.heapID);
    sfvmk_modInfo.heapID = VMK_INVALID_HEAP_ID;
  }
}

/*! \brief This is the driver module entry point that gets invoked
**         automatically when this module is loaded.
**
** \param  None
**
** \return: 0 <success> or 1 <failure>
**
*/
int
init_module(void)
{
  VMK_ReturnStatus status;
  vmk_ByteCount byteCount;
  vmk_LogProperties logProps;
  vmk_HeapCreateProps heapProps;
  vmk_LogThrottleProperties logThrottledProps;
  vmk_MemPoolProps memPoolProps;
  vmk_MgmtProps mgmtProps;

  /* TBD :  Memory for other modules needs to be added */
  vmk_HeapAllocationDescriptor allocDesc[] = {
      /* size, alignment, count */
      { SFVMK_HEAP_EST, 1, 1},
      { vmk_LogHeapAllocSize(), 1, 1 },
      { vmk_LockDomainAllocSize(), 1, 1 },
      { vmk_SpinlockAllocSize(VMK_SPINLOCK), 1, 2}
   };

  /* Populate sfvmk_ModInfo fields */

  /* 1. Driver Name */
  vmk_NameInitialize(&sfvmk_modInfo.driverName, SFVMK_DRIVER_NAME);

  /* 2. Heap */
  status = vmk_HeapDetermineMaxSize(allocDesc,
                                     sizeof(allocDesc) / sizeof(allocDesc[0]),
                                     &byteCount);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to determine heap max size (%x)", status);
     goto failed_max_heap_size;
  }

  heapProps.type = VMK_HEAP_TYPE_SIMPLE;
  vmk_NameCopy(&heapProps.name, &sfvmk_modInfo.driverName);
  heapProps.module = vmk_ModuleCurrentID;
  heapProps.initial = 0;
  heapProps.max = byteCount;
  heapProps.creationTimeoutMS = VMK_TIMEOUT_UNLIMITED_MS;

  status = vmk_HeapCreate(&heapProps, &sfvmk_modInfo.heapID);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to create heap (%x) for sfc_native driver", status);
     goto failed_heap_create;
  }

  /* 3. Log  */
  vmk_NameCopy(&logProps.name, &sfvmk_modInfo.driverName);
  logProps.module = vmk_ModuleCurrentID;
  logProps.heap = sfvmk_modInfo.heapID;
  logProps.defaultLevel = 0;
  logProps.throttle = NULL;

  status = vmk_LogRegister(&logProps, &sfvmk_modInfo.logID);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to register log component (%x)", status);
     goto failed_log_register;
  }

  logThrottledProps.type = VMK_LOG_THROTTLE_COUNT;
  logProps.throttle = &logThrottledProps;
  vmk_NameInitialize(&logProps.name, SFVMK_DRIVER_NAME"_throttled");

  status = vmk_LogRegister(&logProps, &sfvmk_modInfo.logThrottledID);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to register throttled log component (%x)", status);
     goto failed_throttled_log_register;
  }

  /* 4. Lock Domain */
  status = vmk_LockDomainCreate(vmk_ModuleCurrentID,
                                sfvmk_modInfo.heapID,
                                &sfvmk_modInfo.driverName,
                                &sfvmk_modInfo.lockDomain);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to create lock domain (%x)", status);
     goto failed_lock_domain_create;
  }

  vmk_ModuleSetHeapID(vmk_ModuleCurrentID, sfvmk_modInfo.heapID);

  /* 5. MemPool */
  vmk_NameCopy(&memPoolProps.name, &sfvmk_modInfo.driverName);
  memPoolProps.module = vmk_ModuleCurrentID;
  memPoolProps.parentMemPool = VMK_MEMPOOL_INVALID;
  memPoolProps.memPoolType = VMK_MEM_POOL_LEAF;
  memPoolProps.resourceProps.reservation = 0;
  memPoolProps.resourceProps.limit = 0;

  status = vmk_MemPoolCreate(&memPoolProps, &sfvmk_modInfo.memPoolID);
  if (status != VMK_OK) {
    SFVMK_ERROR("vmk_MemPoolCreate failed status: %s", vmk_StatusToString(status));
    goto failed_mem_pool_create;
  }

  /* Register Driver with with device layer */
  status = sfvmk_driverRegister();
  if (status != VMK_OK) {
    vmk_WarningMessage("Initialization of SFC driver failed (%s)",
                       vmk_StatusToString(status));
    goto failed_driver_register;
  }

  mgmtProps.modId = vmk_ModuleCurrentID;
  mgmtProps.heapId = sfvmk_modInfo.heapID;
  mgmtProps.sig = (vmk_MgmtApiSignature *)&sfvmk_mgmtSig;
  mgmtProps.cleanupFn = NULL;
  mgmtProps.sessionAnnounceFn = NULL;
  mgmtProps.sessionCleanupFn = NULL;
  mgmtProps.handleCookie = 0;

  status = vmk_MgmtInit(&mgmtProps, &sfvmk_modInfo.mgmtHandle);
  if (status != VMK_OK) {
    SFVMK_ERROR("Initialization of mgmtProps failed: (%s)",
                 vmk_StatusToString(status));
    goto failed_mgmt_init;
  }

  vmk_LogMessage("Initialization of SFC  driver successful");
  return status;

failed_max_heap_size:
failed_heap_create:
failed_log_register:
failed_throttled_log_register:
failed_lock_domain_create:
failed_mem_pool_create:
failed_driver_register:
failed_mgmt_init:
  sfvmk_modInfoCleanup();

  return status;
}

/*! \brief This is the  module exit point that gets invoked
**         automatically when this module is unloaded.
**
** \param  None
**
** \return: None
**
*/
void
cleanup_module(void)
{
  sfvmk_modInfoCleanup();

  vmk_LogMessage("Exit: -- Solarflare Native Driver -- ");
}
