/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/


#include "sfvmk.h"
#include "sfvmk_ut.h"

sfvmk_ModInfo_t sfvmk_ModInfo = {
   .heapID           = VMK_INVALID_HEAP_ID,
   .memPoolID        = VMK_MEMPOOL_INVALID,
   .logID            = VMK_INVALID_LOG_HANDLE,
   .logThrottledID   = VMK_INVALID_LOG_HANDLE,
   .lockDomain       = VMK_LOCKDOMAIN_INVALID,
};

static void
sfvmk_ModInfoCleanup(void)
{
   if (sfvmk_ModInfo.driverID != NULL) {
      sfvmk_DriverUnregister();
      sfvmk_ModInfo.driverID = NULL;
   }
   if (sfvmk_ModInfo.lockDomain != VMK_LOCKDOMAIN_INVALID) {
      vmk_LockDomainDestroy(sfvmk_ModInfo.lockDomain);
      sfvmk_ModInfo.lockDomain = VMK_LOCKDOMAIN_INVALID;
   }
   if (sfvmk_ModInfo.logThrottledID != VMK_INVALID_LOG_HANDLE) {
      vmk_LogUnregister(sfvmk_ModInfo.logThrottledID);
      sfvmk_ModInfo.logThrottledID = VMK_INVALID_LOG_HANDLE;
   }
   if (sfvmk_ModInfo.logID != VMK_INVALID_LOG_HANDLE) {
      vmk_LogUnregister(sfvmk_ModInfo.logID);
      sfvmk_ModInfo.logID = VMK_INVALID_LOG_HANDLE;
   }
   if (sfvmk_ModInfo.heapID != VMK_INVALID_HEAP_ID) {
      vmk_HeapDestroy(sfvmk_ModInfo.heapID);
      sfvmk_ModInfo.heapID = VMK_INVALID_HEAP_ID;
   }
}

/************************************************************************
 * init_module --
 *
 * @brief   This is the driver module entry point that gets invoked
 * automatically when this module is loaded.
 *
 * @param  None
 *
 * @return 0 <Success> or 1 <Failure>
 *
 ************************************************************************/
int
init_module(void)
{
  VMK_ReturnStatus status;
  vmk_ByteCount byteCount;
  vmk_LogProperties logProps;
  vmk_HeapCreateProps heapProps;
  vmk_LogThrottleProperties logThrottledProps;

  /* TBD :  Memory for other modules needs to be added */
  vmk_HeapAllocationDescriptor allocDesc[] = {
      /* size, alignment, count */
      { SFC_HEAP_EST, 1, 1},
      { vmk_LogHeapAllocSize(), 1, 1 },
      { vmk_LockDomainAllocSize(), 1, 1 },
      { vmk_SpinlockAllocSize(VMK_SPINLOCK), 1, 2}
   };

  /* Populate sfvmk_ModInfo fields */

  /* 1. Driver Name */
  vmk_NameInitialize(&sfvmk_ModInfo.driverName, SFC_DRIVER_NAME);

  /* 2. Heap */
  status = vmk_HeapDetermineMaxSize(allocDesc,
                                     sizeof(allocDesc) / sizeof(allocDesc[0]),
                                     &byteCount);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to determine heap max size (%x)", status);
     goto err;
  }
  heapProps.type = VMK_HEAP_TYPE_SIMPLE;
  vmk_NameCopy(&heapProps.name, &sfvmk_ModInfo.driverName);
  heapProps.module = vmk_ModuleCurrentID;
  heapProps.initial = 0;
  heapProps.max = byteCount;
  heapProps.creationTimeoutMS = VMK_TIMEOUT_UNLIMITED_MS;
  status = vmk_HeapCreate(&heapProps, &sfvmk_ModInfo.heapID);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to create heap (%x) for sfc_native driver", status);
     goto err;
  }

  /* 3. Log  */
  vmk_NameCopy(&logProps.name, &sfvmk_ModInfo.driverName);
  logProps.module = vmk_ModuleCurrentID;
  logProps.heap = sfvmk_ModInfo.heapID;
  logProps.defaultLevel = 0;
  logProps.throttle = NULL;
  status = vmk_LogRegister(&logProps, &sfvmk_ModInfo.logID);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to register log component (%x)", status);
     goto err;
  }

  logThrottledProps.type = VMK_LOG_THROTTLE_COUNT;
  logProps.throttle = &logThrottledProps;
  vmk_NameInitialize(&logProps.name, SFC_DRIVER_NAME"_throttled");
  status = vmk_LogRegister(&logProps, &sfvmk_ModInfo.logThrottledID);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to register throttled log component (%x)", status);
     goto err;
  }

  /* 4. Lock Domain */
  status = vmk_LockDomainCreate(vmk_ModuleCurrentID,
                                 sfvmk_ModInfo.heapID,
                                 &sfvmk_ModInfo.driverName,
                                 &sfvmk_ModInfo.lockDomain);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to create lock domain (%x)", status);
     goto err;
  }

  vmk_ModuleSetHeapID(vmk_ModuleCurrentID, sfvmk_ModInfo.heapID);

  /* 5. MemPool - TBD */

  /* Register Driver with with device layer */
  status = sfvmk_DriverRegister();

  if (status == VMK_OK) {
    vmk_LogMessage("Initialization of SFC  driver successful");
  } else {
    vmk_LogMessage("Initialization of SFC driver failed (%x)", status);
  }

  err:
  if (status != VMK_OK) {
    sfvmk_ModInfoCleanup();
  }

  return status;
}

/************************************************************************
 * cleanup_module --
 *
 * @brief: This is the module entry point that gets called automatically when
 * this module is unloaded.
 *
 * @param  None
 *
 * @return None
 *
 ************************************************************************/

void
cleanup_module(void)
{
  sfvmk_ModInfoCleanup();

  vmk_LogMessage("Exit: -- Solarflare Native Driver -- ");
}
