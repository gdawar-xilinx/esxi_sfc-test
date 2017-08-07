/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk.h"
#include "sfvmk_driver.h"
#include "sfvmk_ut.h"
#include "sfvmk_mgmtInterface.h"

extern vmk_MgmtApiSignature mgmtSig;

sfvmk_ModInfo_t sfvmk_ModInfo = {
   .heapID           = VMK_INVALID_HEAP_ID,
   .memPoolID        = VMK_MEMPOOL_INVALID,
   .logID            = VMK_INVALID_LOG_HANDLE,
   .logThrottledID   = VMK_INVALID_LOG_HANDLE,
   .lockDomain       = VMK_LOCKDOMAIN_INVALID,
   .mgmtHandle       = NULL,
   .vmkdevHashTable = VMK_INVALID_HASH_HANDLE,
};

static void
sfvmk_ModInfoCleanup(void)
{
  if (sfvmk_ModInfo.mgmtHandle) {
    vmk_MgmtDestroy(sfvmk_ModInfo.mgmtHandle);
  }

  if (sfvmk_ModInfo.vmkdevHashTable != VMK_INVALID_HASH_HANDLE) {
    vmk_HashDeleteAll(sfvmk_ModInfo.vmkdevHashTable);
    if (vmk_HashIsEmpty(sfvmk_ModInfo.vmkdevHashTable)) {
      vmk_HashRelease(sfvmk_ModInfo.vmkdevHashTable);
    }
  }

  if (sfvmk_ModInfo.driverID != NULL) {
    sfvmk_driverUnregister();
    sfvmk_ModInfo.driverID = NULL;
  }
  if (sfvmk_ModInfo.lockDomain != VMK_LOCKDOMAIN_INVALID) {
    vmk_LockDomainDestroy(sfvmk_ModInfo.lockDomain);
    sfvmk_ModInfo.lockDomain = VMK_LOCKDOMAIN_INVALID;
  }
  if (sfvmk_ModInfo.memPoolID != VMK_MEMPOOL_INVALID) {
    vmk_MemPoolDestroy(sfvmk_ModInfo.memPoolID);
    sfvmk_ModInfo.memPoolID = VMK_MEMPOOL_INVALID;
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
  vmk_HashProperties hashProps;
  vmk_MgmtProps mgmtProps;

  /* TBD :  Memory for other modules needs to be added */
  vmk_HeapAllocationDescriptor allocDesc[] = {
      /* size, alignment, count */
      { SFVMK_HEAP_EST, 1, 1},
      { vmk_LogHeapAllocSize(), 1, 1 },
      { vmk_LockDomainAllocSize(), 1, 1 },
      { vmk_SpinlockAllocSize(VMK_SPINLOCK), 1, 8}
   };

  /* Populate sfvmk_ModInfo fields */

  /* 1. Driver Name */
  vmk_NameInitialize(&sfvmk_ModInfo.driverName, SFVMK_DRIVER_NAME);

  /* 2. Heap */
  status = vmk_HeapDetermineMaxSize(allocDesc,
                                     sizeof(allocDesc) / sizeof(allocDesc[0]),
                                     &byteCount);
  if (status != VMK_OK) {
     SFVMK_ERROR("Failed to determine heap max size (%x)", status);
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
     SFVMK_ERROR("Failed to create heap (%s) for sfc_native driver",
                 vmk_StatusToString(status));
     goto err;
  }

  /* 3. Log  */
  vmk_NameCopy(&logProps.name, &sfvmk_ModInfo.driverName);
  logProps.module = vmk_ModuleCurrentID;
  logProps.heap = sfvmk_ModInfo.heapID;
  logProps.defaultLevel = SFVMK_LOG_LEVEL_FUNCTION;
  logProps.throttle = NULL;
  status = vmk_LogRegister(&logProps, &sfvmk_ModInfo.logID);
  if (status != VMK_OK) {
     SFVMK_ERROR("Failed to register log component (%s)",
                  vmk_StatusToString(status));
     goto err;
  }

  logThrottledProps.type = VMK_LOG_THROTTLE_COUNT;
  logProps.throttle = &logThrottledProps;
  vmk_NameInitialize(&logProps.name, SFVMK_DRIVER_NAME"_throttled");
  status = vmk_LogRegister(&logProps, &sfvmk_ModInfo.logThrottledID);
  if (status != VMK_OK) {
     SFVMK_ERROR("Failed to register throttled log component (%s)",
                  vmk_StatusToString(status));
     goto err;
  }

  /* 4. Lock Domain */
  status = vmk_LockDomainCreate(vmk_ModuleCurrentID,
                                 sfvmk_ModInfo.heapID,
                                 &sfvmk_ModInfo.driverName,
                                 &sfvmk_ModInfo.lockDomain);
  if (status != VMK_OK) {
     SFVMK_ERROR("Failed to create lock domain (%s)",
                  vmk_StatusToString(status));
     goto err;
  }

  vmk_ModuleSetHeapID(vmk_ModuleCurrentID, sfvmk_ModInfo.heapID);

  /* 5. MemPool */
  vmk_NameCopy(&memPoolProps.name, &sfvmk_ModInfo.driverName);
  memPoolProps.module = vmk_ModuleCurrentID;
  memPoolProps.parentMemPool = VMK_MEMPOOL_INVALID;
  memPoolProps.memPoolType = VMK_MEM_POOL_LEAF;
  memPoolProps.resourceProps.reservation = 0;
  memPoolProps.resourceProps.limit = 0;

  status = vmk_MemPoolCreate(&memPoolProps, &sfvmk_ModInfo.memPoolID);
  if (status != VMK_OK) {
    SFVMK_ERROR("Failed to create mempool (%s)", vmk_StatusToString(status));
    goto err;
  }

  /* Register Driver with with device layer */
  status = sfvmk_driverRegister();

  if (status == VMK_OK) {
    vmk_LogMessage("Initialization of SFC  driver successful");
  } else {
    SFVMK_ERROR("Initialization of SFC driver failed (%s)",
                vmk_StatusToString(status));
  }

  hashProps.moduleID  = vmk_ModuleCurrentID;
  hashProps.heapID    = sfvmk_ModInfo.heapID;
  hashProps.keyType   = VMK_HASH_KEY_TYPE_STR;
  hashProps.keyFlags  = VMK_HASH_KEY_FLAGS_LOCAL_COPY;
  hashProps.keySize   = SFVMK_DEV_NAME_LEN;
  hashProps.nbEntries = SFVMK_ADAPTER_TABLE_SIZE;
  hashProps.acquire   = NULL;
  hashProps.release   = NULL;

  status = vmk_HashAlloc(&hashProps, &sfvmk_ModInfo.vmkdevHashTable);
  if (status != VMK_OK) {
    SFVMK_ERROR("Initialization of sfvmk_vmkdevHashTable failed: (%s)",
                 vmk_StatusToString(status));
    goto err;
  }

  mgmtProps.modId = vmk_ModuleCurrentID;
  mgmtProps.heapId = sfvmk_ModInfo.heapID;
  mgmtProps.sig = &mgmtSig;
  mgmtProps.cleanupFn = NULL;
  mgmtProps.sessionAnnounceFn = NULL;
  mgmtProps.sessionCleanupFn = NULL;
  mgmtProps.handleCookie = 0;

  status = vmk_MgmtInit(&mgmtProps, &sfvmk_ModInfo.mgmtHandle);
  if (status != VMK_OK) {
    SFVMK_ERROR("Initialization of mgmtProps failed: (%s)",
                 vmk_StatusToString(status));
    goto err;
  }

err:
  if (status != VMK_OK) {
    sfvmk_ModInfoCleanup();
  }

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
  SFVMK_DEBUG(SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_INFO,
              "Exit: -- Solarflare Native Driver -- ");
  sfvmk_ModInfoCleanup();

}
