/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/


#include "sfvmk_driver.h"

/**-----------------------------------------------------------------------------
 *
 * sfvmk_createLock --
 *
 * @brief It creates a spin lock with specified name and lock rank.
 *
 * @param[in]  lockName  brief name for the spinlock
 * @param[in]  rank      lock rank)
 * @param[out] lock      lock pointer to create
 *
 * @result: VMK_OK on success, and lock created. Error code if otherwise.
 *
 *-----------------------------------------------------------------------------*/

VMK_ReturnStatus
sfvmk_createLock(const char *lockName,
                       vmk_LockRank rank,
                       vmk_Lock *lock)
{
   vmk_SpinlockCreateProps lockProps;
   VMK_ReturnStatus status;
   lockProps.moduleID = vmk_ModuleCurrentID;
   lockProps.heapID = sfvmk_ModInfo.heapID;

   if((lock == NULL) || (lockName == NULL))
     return VMK_BAD_PARAM;

   vmk_NameFormat(&lockProps.name, "sfvmk-%s", lockName);

   lockProps.type = VMK_SPINLOCK;
   lockProps.domain = sfvmk_ModInfo.lockDomain;
   lockProps.rank = rank;
   status = vmk_SpinlockCreate(&lockProps, lock);
   if (status != VMK_OK) {
      vmk_LogMessage("Failed to create spinlock (%x)", status);
   }
   return status;
}

/**-----------------------------------------------------------------------------
 *
 * sfvmk_DestroyLock --
 *
 * @brief It destroys the spin lock.
 *
 * @param[in,out] lock  lock pointer to create
 *
 * @result  None
 *
 *-----------------------------------------------------------------------------*/

void
sfvmk_destroyLock(vmk_Lock lock)
{
  if (lock)
    vmk_SpinlockDestroy(lock);
}

/**-----------------------------------------------------------------------------
 *
 * sfvmk_memPoolAlloc --
 *
 * @brief It creates a spin lock with specified name and lock rank.
 *
 * @param[in]  lockName  brief name for the spinlock
 *
 *
 * @result: VMK_OK on success, and lock created. Error code if otherwise.
 *
 *-----------------------------------------------------------------------------*/

void *
sfvmk_memPoolAlloc(vmk_uint64 size)
{
  VMK_ReturnStatus status;
  vmk_VA va;
  vmk_MemPoolAllocRequest request;
  vmk_MemPoolAllocProps props;
  vmk_MapRequest mapReq;
  vmk_MpnRange mpnRange;

  size = VMK_UTIL_ROUNDUP(size, VMK_PAGE_SIZE);

  /* convert bytes to pages and allocate from the memory pool */
  props.physContiguity = VMK_MEM_PHYS_CONTIGUOUS;
  props.physRange = VMK_PHYS_ADDR_ANY;
  props.creationTimeoutMS = 10;

  request.numPages = size >> VMK_PAGE_SHIFT;
  request.numElements = 1;
  request.mpnRanges = &mpnRange;



  status = vmk_MemPoolAlloc(sfvmk_ModInfo.memPoolID, &props, &request);
  if (status != VMK_OK) {
    SFVMK_ERROR("vmk_MemPoolAlloc failed: %s", vmk_StatusToString(status));
    goto sfvmk_allocation_err;
  }

  /* Call vmk_Map to get virtual address */
  mapReq.mapType     = VMK_MAPTYPE_DEFAULT;
  mapReq.mapAttrs    = VMK_MAPATTRS_READWRITE;
  mapReq.numElements = 1;
  mapReq.mpnRanges   = &mpnRange;

  status = vmk_Map(vmk_ModuleCurrentID, &mapReq, &va);
  if (status != VMK_OK) {
    SFVMK_ERROR("vmk_Map failed: %s", vmk_StatusToString(status));
    goto sfvmk_mapping_err;
  }

  if ((void *)va != NULL) {
    vmk_Memset((void *)va, 0, size);
  }

  return (void *)va;

sfvmk_mapping_err:
  vmk_MemPoolFree(&request);
sfvmk_allocation_err:
  return NULL;
}

/**-----------------------------------------------------------------------------
 *
 * sfvmk_memPoolFree --
 *
 * @brief It creates a spin lock with specified name and lock rank.
 *
 * @param[in]  lockName  brief name for the spinlock
 * @param[in]  rank      lock rank)
 * @param[out] lock      lock pointer to create
 *
 * @result: VMK_OK on success, and lock created. Error code if otherwise.
 *
 *-----------------------------------------------------------------------------*/
void
sfvmk_memPoolFree(void *va, vmk_uint64 size)
{
  VMK_ReturnStatus        status;
  vmk_MA                  ma;
  vmk_MpnRange            mpnRange;
  vmk_MemPoolAllocRequest allocRequest;

  size = VMK_UTIL_ROUNDUP(size, VMK_PAGE_SIZE);

  status = vmk_VA2MA((vmk_VA)va, size, &ma);
  if (status != VMK_OK) {
    SFVMK_ERROR("vmk_VA2MA failed: %s", vmk_StatusToString(status));
    VMK_ASSERT(0);
  }

  /* Unmap */
  vmk_Unmap((vmk_VA)va);

  /* Free pages */
  mpnRange.startMPN = vmk_MA2MPN(ma);
  mpnRange.numPages = (size/VMK_PAGE_SIZE);

  allocRequest.numPages    = mpnRange.numPages;
  allocRequest.numElements = 1;
  allocRequest.mpnRanges   = &mpnRange;

  status = vmk_MemPoolFree(&allocRequest);
  if (status != VMK_OK) {
    SFVMK_ERROR("vmk_MemPoolFree failed: %s", vmk_StatusToString(status));
  }

}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_createLock --
 *
 * @brief It creates a spin lock with specified name and lock rank.
 *
 * @param[in]  lockName  brief name for the spinlock
 * @param[in]  rank      lock rank)
 * @param[out] lock      lock pointer to create
 *
 * @result: VMK_OK on success, and lock created. Error code if otherwise.
 *
 *-----------------------------------------------------------------------------*/

static VMK_ReturnStatus
sfvmk_DMAMapMA(vmk_MA ma,
                      vmk_uint32 size,
                      vmk_DMADirection direction,
                      vmk_DMAEngine engine,
                      vmk_IOA *ioa)
{
  VMK_ReturnStatus status;
  vmk_SgElem in, out;
  vmk_DMAMapErrorInfo err;

  in.addr = ma;
  in.length = size;
  status = vmk_DMAMapElem(engine, direction, &in, VMK_TRUE, &out, &err);

  if (VMK_UNLIKELY(status != VMK_OK)) {
    if (status == VMK_DMA_MAPPING_FAILED) {
      SFVMK_ERROR("Failed to map range [0x%lx, 0x%lx]: %s",
                in.addr, in.addr + in.length - 1,
                vmk_DMAMapErrorReasonToString(err.reason));

    } else {
      SFVMK_ERROR("Failed to map range [0x%lx, 0x%lx] (%x)",
                in.addr, in.addr + in.length - 1, status);
    }
  } else {
    VMK_ASSERT_EQ(in.length, out.length);
    *ioa = out.ioAddr;
  }

  return status;
}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_allocCoherentDMAMapping --
 *
 * @brief It creates a spin lock with specified name and lock rank.
 *
 * @param[in]  lockName  brief name for the spinlock
 * @param[in]  rank      lock rank)
 * @param[out] lock      lock pointer to create
 *
 * @result: VMK_OK on success, and lock created. Error code if otherwise.
 *
 *-----------------------------------------------------------------------------*/

void *
sfvmk_allocCoherentDMAMapping(vmk_DMAEngine dmaEngine, vmk_uint32 size,
                                            vmk_IOA *ioAddr)
{
  VMK_ReturnStatus status;
  void *pVA;
  vmk_MA ma;

  *ioAddr = 0;
  pVA = sfvmk_memPoolAlloc(size);
  if (pVA == NULL) {
    goto sfvmk_mempool_alloc_fail;
  }

  vmk_VA2MA((vmk_VA)pVA, size, &ma);
  status = sfvmk_DMAMapMA(ma, size, VMK_DMA_DIRECTION_BIDIRECTIONAL,
                          dmaEngine, ioAddr);
  if (VMK_UNLIKELY(status != VMK_OK)) {
    goto sfvmk_dma_map_fail;
  }

  return pVA;

sfvmk_dma_map_fail:
  sfvmk_memPoolFree(pVA, size);
sfvmk_mempool_alloc_fail:
  return NULL;
}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_freeCoherentDMAMapping --
 *
 * @brief It creates a spin lock with specified name and lock rank.
 *
 * @param[in]  lockName  brief name for the spinlock
 * @param[in]  rank      lock rank)
 * @param[out] lock      lock pointer to create
 *
 * @result: VMK_OK on success, and lock created. Error code if otherwise.
 *
 *-----------------------------------------------------------------------------*/

void
sfvmk_freeCoherentDMAMapping(vmk_DMAEngine engine, void *pVA,
                                            vmk_IOA ioAddr, vmk_uint32 size)
{
  vmk_SgElem elem;
  VMK_ReturnStatus status;

  elem.ioAddr = ioAddr;
  elem.length = size;

  status = vmk_DMAUnmapElem(engine, VMK_DMA_DIRECTION_BIDIRECTIONAL, &elem);
  if (VMK_UNLIKELY(status != VMK_OK)) {
    SFVMK_ERROR("Failed to unmap range [0x%lx, 0x%lx] (%x)",
              elem.ioAddr, elem.ioAddr + elem.length - 1,
              status);
  }

  sfvmk_memPoolFree(pVA, size);
}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_mutexInit --
 *
 * @brief It creates a spin lock with specified name and lock rank.
 *
 * @param[in]  lockName  brief name for the spinlock
 * @param[in]  rank      lock rank)
 * @param[out] lock      lock pointer to create
 *
 * @result: VMK_OK on success, and lock created. Error code if otherwise.
 *
 *-----------------------------------------------------------------------------*/
VMK_ReturnStatus
sfvmk_mutexInit(const char *lckName,vmk_LockRank rank,vmk_Mutex *mutex)           // OUT: created lock
{
   vmk_MutexCreateProps lockProps;
   VMK_ReturnStatus status;
   lockProps.moduleID = vmk_ModuleCurrentID;
   lockProps.heapID = sfvmk_ModInfo.heapID;
   vmk_NameFormat(&lockProps.className, "sfvmk-%s", lckName);

   lockProps.type = VMK_MUTEX;
   lockProps.domain = sfvmk_ModInfo.lockDomain;
   lockProps.rank = rank;
   status = vmk_MutexCreate(&lockProps ,mutex);
   if (VMK_UNLIKELY(status != VMK_OK)) {
      vmk_LogMessage("Failed to create mutex (%x)", status);
   }

   return status ;

}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_mutexDestroy --
 *
 * @brief It creates a spin lock with specified name and lock rank.
 *
 * @param[in]  lockName  brief name for the spinlock
 * @param[in]  rank      lock rank)
 * @param[out] lock      lock pointer to create
 *
 * @result: VMK_OK on success, and lock created. Error code if otherwise.
 *
 *-----------------------------------------------------------------------------*/
void sfvmk_mutexDestroy(vmk_Mutex mutex)
{
  if(mutex)
    vmk_MutexDestroy(mutex);
}
