/* **********************************************************
 * Copyright 2017 - 2018 Solarflare Inc.  All rights reserved.
 * -- Solarflare Confidential
 * **********************************************************/

#include "esxi_sfc.h"

/*
 ***********************************************************************
 * Module global variables
 ***********************************************************************
 */

SfcModInfo sfcModInfo = {
   .heapID           = VMK_INVALID_HEAP_ID,
   .memPoolID        = VMK_MEMPOOL_INVALID,
   .logID            = VMK_INVALID_LOG_HANDLE,
   .logThrottledID   = VMK_INVALID_LOG_HANDLE,
   .lockDomain       = VMK_LOCKDOMAIN_INVALID,
};

/*
 ***********************************************************************
 * cleanup_module --                                              */ /**
 *
 * This is the module entry point that gets called automatically when
 * this module is unloaded.
 *
 ***********************************************************************
 */

static void
esxi_sfc_ModInfoCleanup(void)
{
   if (sfcModInfo.driverID != NULL) {
      esxi_sfc_DriverUnregister();
   }
   if (sfcModInfo.lockDomain != VMK_LOCKDOMAIN_INVALID) {
      vmk_LockDomainDestroy(sfcModInfo.lockDomain);
   }
   if (sfcModInfo.logThrottledID != VMK_INVALID_LOG_HANDLE) {
      vmk_LogUnregister(sfcModInfo.logThrottledID);
   }
   if (sfcModInfo.logID != VMK_INVALID_LOG_HANDLE) {
      vmk_LogUnregister(sfcModInfo.logID);
   }
   if (sfcModInfo.heapID != VMK_INVALID_HEAP_ID) {
      vmk_HeapDestroy(sfcModInfo.heapID);
   }
}
/*
 ***********************************************************************
 * init_module --                                                 */ /**
 *
 * This is the driver module entry point that gets invoked automatically
 * when this module is loaded.
 *
 * Return values:
 *  0: Success
 *  1: Failure
 *
 ***********************************************************************
 */

int
init_module(void)
{
  VMK_ReturnStatus status;

  vmk_ByteCount byteCount;
  vmk_LogProperties logProps;
  vmk_HeapCreateProps heapProps;
  //vmk_DriverProps sfcDriverProps;
  vmk_LogThrottleProperties logThrottledProps;

  /* TBD :  Memory for other modules needs to be added */
  vmk_HeapAllocationDescriptor allocDesc[] = {
      /* size, alignment, count */
      { SFC_HEAP_EST, 1, 1},
      { vmk_LogHeapAllocSize(), 1, 1 },
      { vmk_LockDomainAllocSize(), 1, 1 }
   };

  /* Populate sfcModInfo fields */

  /* 1. Driver Name */
  vmk_NameInitialize(&sfcModInfo.driverName, SFC_DRIVER_NAME);

  /* 2. Heap */
  status = vmk_HeapDetermineMaxSize(allocDesc,
                                     sizeof(allocDesc) / sizeof(allocDesc[0]),
                                     &byteCount);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to determine heap max size (%x)", status);
     goto err;
  }
  heapProps.type = VMK_HEAP_TYPE_SIMPLE;
  vmk_NameCopy(&heapProps.name, &sfcModInfo.driverName);
  heapProps.module = vmk_ModuleCurrentID;
  heapProps.initial = 0;
  heapProps.max = byteCount;
  heapProps.creationTimeoutMS = VMK_TIMEOUT_UNLIMITED_MS;
  status = vmk_HeapCreate(&heapProps, &sfcModInfo.heapID);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to create heap (%x) for sfc_native driver", status);
     goto err;
  }
  /* 3. Log  */
  vmk_NameCopy(&logProps.name, &sfcModInfo.driverName);
  logProps.module = vmk_ModuleCurrentID;
  logProps.heap = sfcModInfo.heapID;
  logProps.defaultLevel = 0;
  logProps.throttle = NULL;
  status = vmk_LogRegister(&logProps, &sfcModInfo.logID);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to register log component (%x)", status);
     goto err;
  }

  logThrottledProps.type = VMK_LOG_THROTTLE_COUNT;
  logProps.throttle = &logThrottledProps;
  vmk_NameInitialize(&logProps.name, SFC_DRIVER_NAME"_throttled");
  status = vmk_LogRegister(&logProps, &sfcModInfo.logThrottledID);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to register throttled log component (%x)", status);
     goto err;
  }


  /* 4. Lock Domain */
  status = vmk_LockDomainCreate(vmk_ModuleCurrentID,
                                 sfcModInfo.heapID,
                                 &sfcModInfo.driverName,
                                 &sfcModInfo.lockDomain);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to create lock domain (%x)", status);
     goto err;
  }

  vmk_ModuleSetHeapID(vmk_ModuleCurrentID, sfcModInfo.heapID);

  /* 5. MemPool - TBD */

  /* Register Driver with with device layer */
  status = esxi_sfc_DriverRegister();

  if (status == VMK_OK) {
    vmk_LogMessage("Initialization of SFC  driver successful");
  } else {
    vmk_LogMessage("Initialization of SFC driver failed:");
  }

  err:
  if (status != VMK_OK) {
    esxi_sfc_ModInfoCleanup();
  }

  return status;
}

void
cleanup_module(void)
{
  esxi_sfc_ModInfoCleanup();
  vmk_LogMessage("Exit: -- Solarflare Native Driver -- ");
}

