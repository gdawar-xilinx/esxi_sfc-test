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
   .lock             = NULL,
#ifdef SFVMK_SUPPORT_SRIOV
   .proxyRequestId   = 1,
   .listsLock        = NULL,
#endif
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

#ifdef SFVMK_SUPPORT_SRIOV
  if (sfvmk_modInfo.listsLock) {
    sfvmk_mutexDestroy(sfvmk_modInfo.listsLock);
  }
#endif /* SFVMK_SUPPORT_SRIOV */

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
#define SFVMK_ALLOC_DESC_SIZE  32
  vmk_ByteCount maxSize = 0;
  vmk_HeapAllocationDescriptor allocDesc[SFVMK_ALLOC_DESC_SIZE];
  VMK_ReturnStatus status;
  vmk_uint32 index = 0;

  allocDesc[index].size = vmk_LogHeapAllocSize();
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = 2;

  allocDesc[index].size = vmk_LockDomainAllocSize();
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = 1;

  allocDesc[index].size = vmk_HashGetAllocSize(SFVMK_MAX_ADAPTER);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = 1;

  allocDesc[index].size = vmk_HashGetAllocSize(SFVMK_MAX_FILTER);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = SFVMK_MAX_ADAPTER;

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
  /* For MCDI lock, adapter lock */
  vmk_MutexAllocSize(VMK_MUTEX, &allocDesc[index].size, &allocDesc[index].alignment);
  allocDesc[index++].count = 2 * SFVMK_MAX_ADAPTER;

  /* Binary semaphore at the module level */
  allocDesc[index].size = vmk_SemaAllocSize((vmk_uint32 *)&allocDesc[index].alignment);
  allocDesc[index++].count = 1;
#endif

  allocDesc[index].size = vmk_SpinlockAllocSize(VMK_SPINLOCK);
  allocDesc[index].alignment = 0;
  /* Per adapter - memBarLock, nicLock, uplinkLock, evqLock for each EVQ,
   * txqLock for each TXQ.
   */
  allocDesc[index++].count = (3 + SFVMK_MAX_EVQ + SFVMK_MAX_TXQ) * SFVMK_MAX_ADAPTER;

  /* Space for helper thread */
  allocDesc[index].size = vmk_WorldCreateAllocSize(&allocDesc[index].alignment);
  allocDesc[index++].count = 1;

  allocDesc[index].size = sizeof(sfvmk_adapter_t);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = SFVMK_MAX_ADAPTER;

  allocDesc[index].size = sizeof(sfvmk_evq_t);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_EVQ;

  allocDesc[index].size = sizeof(sfvmk_evq_t *);
  allocDesc[index].alignment = sizeof(sfvmk_evq_t *);
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_EVQ;

  allocDesc[index].size = sizeof(vmk_IntrCookie);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_INTR;

  allocDesc[index].size = sizeof(sfvmk_rxq_t);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_RXQ;

  allocDesc[index].size = sizeof(sfvmk_rxq_t *);
  allocDesc[index].alignment = sizeof(sfvmk_rxq_t *);
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_RXQ;

  allocDesc[index].size = sizeof(sfvmk_rxSwDesc_t);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_RXQ * EFX_RXQ_MAXNDESCS;

  allocDesc[index].size = sizeof(sfvmk_txq_t);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_TXQ;

  allocDesc[index].size = sizeof(sfvmk_txq_t *);
  allocDesc[index].alignment = sizeof(sfvmk_txq_t *);
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_TXQ;

  allocDesc[index].size = sizeof(vmk_UplinkSharedQueueData);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * (SFVMK_MAX_RXQ * SFVMK_MAX_TXQ);

  allocDesc[index].size = sizeof(sfvmk_filterDBEntry_t);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_MAX_FILTER;

  /* Allocaion done for both Signed and Unsigned Image type*/
  allocDesc[index].size = SFVMK_ALLOC_FW_IMAGE_SIZE;
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = 1;

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
  status = vmk_MgmtGetStaticHeapRequired((vmk_MgmtApiSignature *)&sfvmk_mgmtSig,
                                         1, 0, 0, &allocDesc[index].size);
  if (status != VMK_OK) {
    allocDesc[index].size = 0;
    SFVMK_ERROR("Failed to determine management heap size: status:%s",
                 vmk_StatusToString(status));
  }

  allocDesc[index].alignment = 0;
  allocDesc[index++].count = 1;
#endif

  allocDesc[index].size = SFVMK_STATS_BUFFER_SZ;
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = 1;

  allocDesc[index].size = (sizeof(efx_mon_stat_value_t) * EFX_MON_NSTATS);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = 1;

  allocDesc[index].size = (sizeof(efx_mon_stat_limits_t) * EFX_MON_NSTATS);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = 1;

  allocDesc[index].size = (SFVMK_SENSOR_INFO_MAX_WIDTH * EFX_MON_NSTATS);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = 1;

#ifdef SFVMK_SUPPORT_SRIOV
  allocDesc[index].size = sizeof(sfvmk_proxyAdminState_t);
  allocDesc[index].alignment = 0;
  /* Although this allocation is required per card and not per adapter,
   * keeping it at per adapter considering upcoming single port 100G card
   */
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_PROXY_AUTH_NUM_BLOCKS;

  allocDesc[index].size = sizeof(sfvmk_proxyEvent_t);
  allocDesc[index].alignment = 0;
  /* Although this allocation is required per card and not per adapter,
   * keeping it at per adapter considering upcoming single port 100G card
   */
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_PROXY_AUTH_NUM_BLOCKS;

  /* Allocation for pAllowedVlans */
  allocDesc[index].size = vmk_BitVectorSize(SFVMK_MAX_VLANS);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_PROXY_AUTH_NUM_BLOCKS;

  /* Allocation for pActiveVlans */
  allocDesc[index].size = vmk_BitVectorSize(SFVMK_MAX_VLANS);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = SFVMK_MAX_ADAPTER * SFVMK_PROXY_AUTH_NUM_BLOCKS;

  /* For secondaryListLock */
  vmk_MutexAllocSize(VMK_MUTEX, &allocDesc[index].size, &allocDesc[index].alignment);
  allocDesc[index++].count = SFVMK_MAX_ADAPTER;

  /* For listsLock */
  vmk_MutexAllocSize(VMK_MUTEX, &allocDesc[index].size, &allocDesc[index].alignment);
  allocDesc[index++].count = 1;
#endif

  /* Allocation for activeQueues */
  allocDesc[index].size = vmk_BitVectorSize(SFVMK_MAX_NETQ_COUNT * 2);
  allocDesc[index].alignment = 0;
  allocDesc[index++].count = SFVMK_MAX_ADAPTER;

  VMK_ASSERT(index <= SFVMK_ALLOC_DESC_SIZE);

  status = vmk_HeapDetermineMaxSize(allocDesc,
                                    index,
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
#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
                                sfvmk_modInfo.heapID,
#else
                                vmk_ModuleCurrentID,
#endif
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

#ifdef SFVMK_SUPPORT_SRIOV
  vmk_ListInit(&sfvmk_modInfo.primaryList);
  vmk_ListInit(&sfvmk_modInfo.unassociatedList);

  status = sfvmk_mutexInit("listsLock", &sfvmk_modInfo.listsLock);

  if (status != VMK_OK) {
    SFVMK_ERROR("Creating listsLock failed status %s",
                vmk_StatusToString(status));
    goto failed_listslock_init;
  }
#endif /* SFVMK_SUPPORT_SRIOV */

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
#ifdef SFVMK_SUPPORT_SRIOV
failed_listslock_init:
#endif /* SFVMK_SUPPORT_SRIOV */
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
