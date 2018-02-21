/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

/*! \brief It creates a spin lock with specified name and lock rank.
**
** \param[in]  lockName  brief name for the spinlock
** \param[in]  rank      lock rank)
** \param[out] lock      lock pointer to create
**
** \return: VMK_OK on success, and lock created. Error code if otherwise.
*/
VMK_ReturnStatus
sfvmk_createLock(sfvmk_adapter_t *pAdapter,
                 const char *pLockName,
                 vmk_LockRank rank,
                 vmk_Lock *pLock)
{
  vmk_SpinlockCreateProps lockProps;
  VMK_ReturnStatus status;

  if ((pLock == NULL) || (pLockName == NULL) || (pAdapter == NULL))
    return VMK_BAD_PARAM;

  vmk_Memset(&lockProps, 0, sizeof(vmk_SpinlockCreateProps));

  lockProps.moduleID = vmk_ModuleCurrentID;
  lockProps.heapID = sfvmk_modInfo.heapID;
  lockProps.domain = sfvmk_modInfo.lockDomain;
  lockProps.type = VMK_SPINLOCK;
  lockProps.rank = rank;
  vmk_NameFormat(&lockProps.name, "%s_%s", pAdapter->pciDeviceName.string,
                 pLockName);

  status = vmk_SpinlockCreate(&lockProps, pLock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_SpinlockCreate failed status: %s",
                        vmk_StatusToString(status));
  }

  return status;
}

/*! \brief It destroys the spin lock.
**
** \param[in] lock  vmk_lock to destory
**
** \return  void
*/

void
sfvmk_destroyLock(vmk_Lock lock)
{
  if (lock)
    vmk_SpinlockDestroy(lock);
}

/*! \brief create contiguous memory from memory pool
** \param[in] size of the memory to be allocated.
**            memory is allocated always in pages
**
** \return:   kernel virtual address of the memory allocated [success]
**            NULL [failure]
*/
vmk_VA
sfvmk_memPoolAlloc(size_t size)
{
  vmk_VA vAddr;
  VMK_ReturnStatus status;
  vmk_MapRequest mapReq = {0};
  vmk_MpnRange mpnRange = {0};
  vmk_MemPoolAllocProps props = {0};
  vmk_MemPoolAllocRequest request = {0};

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_UTILS);

  /* Convert bytes to pages and allocate from the memory pool */
  props.physContiguity = VMK_MEM_PHYS_CONTIGUOUS;
  props.physRange = VMK_PHYS_ADDR_ANY;
  props.creationTimeoutMS = 10;

  size = VMK_UTIL_ROUNDUP(size, VMK_PAGE_SIZE);
  request.numPages = vmk_Bytes2Pages(size);
  request.numElements = 1;
  request.mpnRanges = &mpnRange;

  status = vmk_MemPoolAlloc(sfvmk_modInfo.memPoolID, &props, &request);
  if (status != VMK_OK) {
    SFVMK_ERROR("vmk_MemPoolAlloc failed status: %s",
                vmk_StatusToString(status));
    goto failed_mempool_alloc;
  }

  /* Call vmk_Map to get virtual address */
  mapReq.mapType     = VMK_MAPTYPE_DEFAULT;
  mapReq.mapAttrs    = VMK_MAPATTRS_READWRITE;
  mapReq.mpnRanges   = &mpnRange;
  mapReq.numElements = 1;

  status = vmk_Map(vmk_ModuleCurrentID, &mapReq, &vAddr);
  if (status != VMK_OK) {
    SFVMK_ERROR("vmk_Map failed status: %s", vmk_StatusToString(status));
    goto failed_mapping;
  }

  if ((void *)vAddr != NULL) {
    vmk_Memset((void *)vAddr, 0, size);
  }

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_UTILS);

  return vAddr;

failed_mapping:
  vmk_MemPoolFree(&request);

failed_mempool_alloc:
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_UTILS);

  return (vmk_VA)0;
}

/*! \brief Free memory allocated from memPool
**
** \param[in]  va     virtual address of the mem needs to get freed
** \param[in]  size   size of the memory
**
** \return: void
*/
void
sfvmk_memPoolFree(vmk_VA vAddr, size_t size)
{
  vmk_MA mAddr;
  vmk_MpnRange mpnRange = {0};
  VMK_ReturnStatus status;
  vmk_MemPoolAllocRequest allocRequest = {0};

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_UTILS);

  size = VMK_UTIL_ROUNDUP(size, VMK_PAGE_SIZE);

  status = vmk_VA2MA(vAddr, size, &mAddr);
  if (status != VMK_OK) {
    SFVMK_ERROR("vmk_VA2MA for virtual address %"VMK_FMT64"x status: %s",
                vAddr, vmk_StatusToString(status));
    goto done;
  }

  /* Unmap */
  vmk_Unmap(vAddr);

  /* Free pages */
  mpnRange.startMPN = vmk_MA2MPN(mAddr);
  mpnRange.numPages = vmk_Bytes2Pages(size);

  allocRequest.numPages = mpnRange.numPages;
  allocRequest.mpnRanges = &mpnRange;
  allocRequest.numElements = 1;

  status = vmk_MemPoolFree(&allocRequest);
  if (status != VMK_OK) {
    SFVMK_ERROR("vmk_MemPoolFree failed status: %s", vmk_StatusToString(status));
  }

done:
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_UTILS);
}


/*! \brief this function allocate dmaable memory
**
** \param[in]  dmaEngine  dma engine to be used
** \param[in]  size       size of the memory
** \param[out] ioAddr     dma addressable memory
**
** \return: kernel virtual address of the dmaable memory
*/
void *
sfvmk_allocDMAMappedMem(vmk_DMAEngine dmaEngine, size_t size,
                        vmk_IOA *pIoAddr)
{
  VMK_ReturnStatus status;
  vmk_DMAMapErrorInfo err;
  vmk_SgElem in, out;
  vmk_VA vAddr;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_UTILS);

  vAddr = sfvmk_memPoolAlloc(size);
  if (!vAddr) {
    goto failed_mempool_alloc;
  }

  in.length = size;
  status = vmk_VA2MA(vAddr, size, &in.addr);
  if (status != VMK_OK) {
    SFVMK_ERROR("vmk_VA2MA failed for virtual address %"VMK_FMT64"x status: %s",
                vAddr, vmk_StatusToString(status));
    goto failed_va2ma_map;
  }

  status = vmk_DMAMapElem(dmaEngine, VMK_DMA_DIRECTION_BIDIRECTIONAL,
                          &in, VMK_TRUE, &out, &err);
  if (VMK_UNLIKELY(status != VMK_OK)) {
    if (status == VMK_DMA_MAPPING_FAILED) {
      SFVMK_ERROR("vmk_DMAMapElem failed to map range [%"VMK_FMT64"x, %"VMK_FMT64"x] status: %s",
                  in.addr, in.addr + in.length - 1,
                  vmk_DMAMapErrorReasonToString(err.reason));

    } else {
      SFVMK_ERROR("vmk_DMAMapElem failed to map range [%"VMK_FMT64"x, %"VMK_FMT64"x] status: %s",
                  in.addr, in.addr + in.length - 1,
                  vmk_StatusToString(status));
    }
    goto failed_dma_map;
  } else {
    *pIoAddr = out.ioAddr;
  }

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_UTILS);

  return (void *)vAddr;

failed_dma_map:
  sfvmk_memPoolFree(vAddr, size);

failed_va2ma_map:
failed_mempool_alloc:
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_UTILS);

  return NULL;
}

/*! \brief unmap dma mapping and free memory
**
** \param[in]  engine   dma engine to be used
** \param[in]  pVA      kernel virtual address of the memory needs to be freed.
** \param[in]  ioAddr   dma address of the memory
** \param[out] size     size of the memory.
**
** \return: void
*/
void
sfvmk_freeDMAMappedMem(vmk_DMAEngine engine, void *pVA,
                       vmk_IOA ioAddr, size_t size)
{
  vmk_SgElem elem;
  VMK_ReturnStatus status;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_UTILS);

  elem.ioAddr = ioAddr;
  elem.length = size;

  status = vmk_DMAUnmapElem(engine, VMK_DMA_DIRECTION_BIDIRECTIONAL, &elem);
  if (VMK_UNLIKELY(status != VMK_OK)) {
    SFVMK_ERROR("vmk_DMAUnmapElem failed to unmap range [%"VMK_FMT64"x, %"VMK_FMT64"x] status: %s",
                elem.ioAddr, elem.ioAddr + elem.length - 1,
                vmk_StatusToString(status));
  }

  sfvmk_memPoolFree((vmk_VA)pVA, size);

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_UTILS);
}

/*! \brief  Convert a number in to next  power of 2 value
**
** \param[in]  value
**
** \return: A next power of 2 value which could be greater than
**          or equal to the value given.
*/
vmk_uint32 sfvmk_pow2GE(vmk_uint32 value)
{
  vmk_uint32 order = 0;
  while ((1ul << order) < value)
    ++order;

  return (1ul << (order));
}

/*! \brief It creates a mutex lock with specified name and lock rank.
**
** \param[in]  pLockName    brief name for the mutexLock
** \param[out] pMutex        mutex lock pointer to create
**
** \return: VMK_OK on success, and lock created. Error code if otherwise.
*/
VMK_ReturnStatus
sfvmk_mutexInit(const char *pLockName, vmk_Mutex *pMutex)
{
  vmk_MutexCreateProps lockProps;
  VMK_ReturnStatus status;

  lockProps.moduleID = vmk_ModuleCurrentID;
  lockProps.heapID = sfvmk_modInfo.heapID;
  vmk_NameFormat(&lockProps.className, "sfvmk-%s", pLockName);

  lockProps.type = VMK_MUTEX;
  lockProps.domain = VMK_LOCKDOMAIN_INVALID;
  lockProps.rank = VMK_MUTEX_UNRANKED;
  status = vmk_MutexCreate(&lockProps, pMutex);
  if (status != VMK_OK) {
    SFVMK_ERROR("Failed to create mutex (%s)", vmk_StatusToString(status));
  }

  return status;
}

/*! \brief It destroy mutex.
**
** \param[in]  mutex  mutex handle
**
** \return: void
*/
void sfvmk_mutexDestroy(vmk_Mutex mutex)
{
  if (mutex != NULL)
    vmk_MutexDestroy(mutex);
}
