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
   .vmkdevHashTable  = VMK_INVALID_HASH_HANDLE,
   .lock             = NULL
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

  if (sfvmk_modInfo.vmkdevHashTable != VMK_INVALID_HASH_HANDLE) {
    if (vmk_HashDeleteAll(sfvmk_modInfo.vmkdevHashTable) != VMK_OK)
      SFVMK_ERROR("Error in deleting vmkdevHashTable entries");

    if (vmk_HashIsEmpty(sfvmk_modInfo.vmkdevHashTable)) {
      /* Free the hash table */
      vmk_HashRelease(sfvmk_modInfo.vmkdevHashTable);
      sfvmk_modInfo.vmkdevHashTable = VMK_INVALID_HASH_HANDLE;
    }
  }

  if (sfvmk_modInfo.lock != NULL) {
    vmk_SemaDestroy(&sfvmk_modInfo.lock);
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

/*! \brief Local function to calculate heap size requirement.
**
** \return: Calculated heap size in bytes.
*/
static vmk_ByteCount
sfvmk_calcHeapSize(void)
{
  vmk_ByteCount maxSize = 0;
  vmk_HeapAllocationDescriptor allocDesc[22];
  VMK_ReturnStatus status;

  allocDesc[0].size = vmk_LogHeapAllocSize();
  allocDesc[0].alignment = 0;
  allocDesc[0].count = 2;

  allocDesc[1].size = vmk_LockDomainAllocSize();
  allocDesc[1].alignment = 0;
  allocDesc[1].count = 1;

  allocDesc[2].size = vmk_HashGetAllocSize(SFVMK_MAX_ADAPTER);
  allocDesc[2].alignment = 0;
  allocDesc[2].count = 1;

  allocDesc[3].size = vmk_HashGetAllocSize(SFVMK_MAX_FILTER);
  allocDesc[3].alignment = 0;
  allocDesc[3].count = SFVMK_MAX_ADAPTER;

  /* For MCDI lock, adapter lock */
  vmk_MutexAllocSize(VMK_MUTEX, &allocDesc[4].size, &allocDesc[4].alignment);
  allocDesc[4].count = 2;

  /* Binary semaphore at the module level */
  allocDesc[5].size = vmk_SemaAllocSize((vmk_uint32 *)&allocDesc[5].alignment);
  allocDesc[5].count = 1;

  allocDesc[6].size = vmk_SpinlockAllocSize(VMK_SPINLOCK);
  allocDesc[6].alignment = 0;
  /* Per adapter - memBarLock, nicLock, uplinkLock, evqLock for each EVQ,
   * txqLock for each TXQ.
   */
  allocDesc[6].count = (3 + SFVMK_MAX_EVQ + SFVMK_MAX_TXQ) * SFVMK_MAX_ADAPTER;

  /* Space for helper thread */
  allocDesc[7].size = vmk_WorldCreateAllocSize(&allocDesc[7].alignment);
  allocDesc[7].count = 1;

  allocDesc[8].size = sizeof(sfvmk_adapter_t);
  allocDesc[8].alignment = 0;
  allocDesc[8].count = SFVMK_MAX_ADAPTER;

  allocDesc[9].size = sizeof(sfvmk_evq_t);
  allocDesc[9].alignment = 0;
  allocDesc[9].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_EVQ;

  allocDesc[10].size = sizeof(sfvmk_evq_t *);
  allocDesc[10].alignment = sizeof(sfvmk_evq_t *);
  allocDesc[10].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_EVQ;

  allocDesc[11].size = sizeof(vmk_IntrCookie);
  allocDesc[11].alignment = 0;
  allocDesc[11].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_INTR;

  allocDesc[12].size = sizeof(sfvmk_rxq_t);
  allocDesc[12].alignment = 0;
  allocDesc[12].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_RXQ;

  allocDesc[13].size = sizeof(sfvmk_rxq_t *);
  allocDesc[13].alignment = sizeof(sfvmk_rxq_t *);
  allocDesc[13].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_RXQ;

  allocDesc[14].size = sizeof(sfvmk_rxSwDesc_t);
  allocDesc[14].alignment = 0;
  allocDesc[14].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_RXQ * EFX_RXQ_MAXNDESCS;

  allocDesc[15].size = sizeof(sfvmk_txq_t);
  allocDesc[15].alignment = 0;
  allocDesc[15].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_TXQ;

  allocDesc[16].size = sizeof(sfvmk_txq_t *);
  allocDesc[16].alignment = sizeof(sfvmk_txq_t *);
  allocDesc[16].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_TXQ;

  allocDesc[17].size = sizeof(vmk_UplinkSharedQueueData);
  allocDesc[17].alignment = 0;
  allocDesc[17].count = SFVMK_MAX_ADAPTER * (SFVMK_MAX_RXQ * SFVMK_MAX_TXQ);

  allocDesc[18].size = sizeof(sfvmk_filterDBEntry_t);
  allocDesc[18].alignment = 0;
  allocDesc[18].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_FILTER;

  /* Allocaion done for both Signed and Unsigned Image type*/
  allocDesc[19].size = SFVMK_MAX_FW_IMAGE_SIZE + SFVMK_MAX_FW_IMAGE_SIZE;
  allocDesc[19].alignment = 0;
  allocDesc[19].count = 1;

  status = vmk_MgmtGetStaticHeapRequired((vmk_MgmtApiSignature *)&sfvmk_mgmtSig,
                                         1, 0, 0, &allocDesc[20].size);
  if (status != VMK_OK) {
    allocDesc[20].size = 0;
    SFVMK_ERROR("Failed to determine management heap size: status:%s",
                 vmk_StatusToString(status));
  }

  allocDesc[20].alignment = 0;
  allocDesc[20].count = 1;

  allocDesc[21].size = SFVMK_HWQ_STATS_BUFFER_SZ;
  allocDesc[21].alignment = 0;
  allocDesc[21].count = 1;

  status = vmk_HeapDetermineMaxSize(allocDesc,
                                    sizeof(allocDesc) / sizeof(allocDesc[0]),
                                    &maxSize);
  if (status != VMK_OK) {
    SFVMK_ERROR("Failed to determine heap max size: status:%s",
                 vmk_StatusToString(status));
  }

  /* Add 20% more for fragmentation */
  return (maxSize *120)/100;
}

/*! \brief This is the driver module entry point that gets invoked
**         automatically when this module is loaded.
**
** \return: 0 [success] or 1 [failure]
**
*/
int
init_module(void)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_ByteCount heapSize = 0;
  vmk_LogProperties logProps;
  vmk_HeapCreateProps heapProps;
  vmk_LogThrottleProperties logThrottledProps;
  vmk_MemPoolProps memPoolProps;
  vmk_MgmtProps mgmtProps;
  vmk_HashProperties hashProps;

  /* 1. Driver Name */
  vmk_NameInitialize(&sfvmk_modInfo.driverName, SFVMK_DRIVER_NAME);

  /* 2. Heap */
  heapSize = sfvmk_calcHeapSize();
  if (!heapSize) {
    SFVMK_ERROR("Failed to determine heap max size");
    goto failed_max_heap_size;
  }

  heapProps.type = VMK_HEAP_TYPE_SIMPLE;
  vmk_NameCopy(&heapProps.name, &sfvmk_modInfo.driverName);
  heapProps.module = vmk_ModuleCurrentID;
  heapProps.initial = 0;
  heapProps.max = heapSize;
  heapProps.creationTimeoutMS = VMK_TIMEOUT_UNLIMITED_MS;

  status = vmk_HeapCreate(&heapProps, &sfvmk_modInfo.heapID);
  if (status != VMK_OK) {
     SFVMK_ERROR("Failed to create heap (%x) for sfc_native driver", status);
     goto failed_heap_create;
  }

  /* 3. Log  */
  vmk_NameCopy(&logProps.name, &sfvmk_modInfo.driverName);
  logProps.module = vmk_ModuleCurrentID;
  logProps.heap = sfvmk_modInfo.heapID;
  logProps.defaultLevel = SFVMK_LOG_LEVEL_DEFAULT;
  logProps.throttle = NULL;

  status = vmk_LogRegister(&logProps, &sfvmk_modInfo.logID);
  if (status != VMK_OK) {
     SFVMK_ERROR("Failed to register log component (%x)", status);
     goto failed_log_register;
  }

  logThrottledProps.type = VMK_LOG_THROTTLE_COUNT;
  logProps.throttle = &logThrottledProps;
  vmk_NameInitialize(&logProps.name, SFVMK_DRIVER_NAME"_throttled");

  status = vmk_LogRegister(&logProps, &sfvmk_modInfo.logThrottledID);
  if (status != VMK_OK) {
     SFVMK_ERROR("Failed to register throttled log component (%x)", status);
     goto failed_throttled_log_register;
  }

  /* 4. Lock Domain */
  status = vmk_LockDomainCreate(vmk_ModuleCurrentID,
                                sfvmk_modInfo.heapID,
                                &sfvmk_modInfo.driverName,
                                &sfvmk_modInfo.lockDomain);
  if (status != VMK_OK) {
     SFVMK_ERROR("Failed to create lock domain (%x)", status);
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

  status = vmk_BinarySemaCreate(&sfvmk_modInfo.lock,
                                sfvmk_modInfo.heapID,
                                (const char *)"Module Lock");
  if (status != VMK_OK) {
    SFVMK_ERROR("Initialization of Module level lock failed (%s)",
                 vmk_StatusToString(status));
    goto failed_sema_init;
  }

  hashProps.moduleID  = vmk_ModuleCurrentID;
  hashProps.heapID    = sfvmk_modInfo.heapID;
  hashProps.keyType   = VMK_HASH_KEY_TYPE_STR;
  hashProps.keyFlags  = VMK_HASH_KEY_FLAGS_LOCAL_COPY;
  hashProps.keySize   = SFVMK_DEV_NAME_LEN;
  hashProps.nbEntries = SFVMK_MAX_ADAPTER;
  hashProps.acquire   = NULL;
  hashProps.release   = NULL;

  status = vmk_HashAlloc(&hashProps, &sfvmk_modInfo.vmkdevHashTable);
  if (status != VMK_OK) {
    SFVMK_ERROR("Initialization of sfvmk_vmkdevHashTable failed: (%s)",
                 vmk_StatusToString(status));
    goto failed_hash_init;
  }

  /* Register Driver with with device layer */
  status = sfvmk_driverRegister();
  if (status != VMK_OK) {
    SFVMK_ERROR("Initialization of SFC driver failed (%s)",
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
failed_sema_init:
failed_hash_init:
failed_driver_register:
failed_mgmt_init:
  sfvmk_modInfoCleanup();

  return status;
}

/*! \brief This is the  module exit point that gets invoked
**         automatically when this module is unloaded.
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
