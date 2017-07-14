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
      SFVMK_ERROR("Failed to create spinlock (%s)", vmk_StatusToString(status));
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
**
** \param[in] size of the memory to be allocated.
**            memory is allocated always in pages
**
** \return:   kernel virtual address of the memory allocated <success>
**            NULL <failure>
*/
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

/*! \brief Free memory allocated from memPool
**
** \param[in]  va     virtual address of the mem needs to get freed
** \param[in]  size   size of the memory
**
** \return: void
*/
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

/*! \brief function to map machine address to DMA address
**
** \param[in]  ma       machine address
** \param[in]  size     size of the memory
** \param[in] direction DMADirection
** \param[in]  engine   dma engine to be used
** \param[out]  ioa     dma address
**
** \return: VMK_OK <success> error code <failure>
*/

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
      SFVMK_ERROR("Failed to map range [0x%lx, 0x%lx] (%s)",
                in.addr, in.addr + in.length - 1, vmk_StatusToString(status));
    }
  } else {
    VMK_ASSERT_EQ(in.length, out.length);
    *ioa = out.ioAddr;
  }

  return status;
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
sfvmk_freeCoherentDMAMapping(vmk_DMAEngine engine, void *pVA,
                                            vmk_IOA ioAddr, vmk_uint32 size)
{
  vmk_SgElem elem;
  VMK_ReturnStatus status;

  elem.ioAddr = ioAddr;
  elem.length = size;

  status = vmk_DMAUnmapElem(engine, VMK_DMA_DIRECTION_BIDIRECTIONAL, &elem);
  if (VMK_UNLIKELY(status != VMK_OK)) {
    SFVMK_ERROR("Failed to unmap range [0x%lx, 0x%lx] (%s)",
              elem.ioAddr, elem.ioAddr + elem.length - 1,
              vmk_StatusToString(status));
  }

  sfvmk_memPoolFree(pVA, size);
}

/*! \brief It creates a mutex lock with specified name and lock rank.
**
** \param[in]  lckName    brief name for the mutexLock
** \param[in]  rank       lock rank
** \param[out] mutex      mutex lock pointer to create
**
** \return: VMK_OK on success, and lock created. Error code if otherwise.
*/
VMK_ReturnStatus
sfvmk_mutexInit(const char *pLockName, vmk_Mutex *pMutex)           // OUT: created lock
{
   vmk_MutexCreateProps lockProps;
   VMK_ReturnStatus status;
   lockProps.moduleID = vmk_ModuleCurrentID;
   lockProps.heapID = sfvmk_ModInfo.heapID;
   vmk_NameFormat(&lockProps.className, "sfvmk-%s", pLockName);

   lockProps.type = VMK_MUTEX;
   lockProps.domain = VMK_LOCKDOMAIN_INVALID;
   lockProps.rank = VMK_MUTEX_UNRANKED;
   status = vmk_MutexCreate(&lockProps, pMutex);
   if (VMK_UNLIKELY(status != VMK_OK)) {
     SFVMK_ERROR("Failed to create mutex (%s)", vmk_StatusToString(status));
   }

   return status ;
}

/*! \brief It destroy mutex.
**
** \param[in]  mutex  mutex handle
**
** \return: void
*/
void sfvmk_mutexDestroy(vmk_Mutex mutex)
{
  if(mutex)
    vmk_MutexDestroy(mutex);
}

/*! \brief utility function to swap the low and high order bytes of a u16
**
** \param[in]  int16  16-bit unsigned integer value
**
** \return: 16-bit uint with low and high bytes swapped
*/
uint16_t sfvmk_swapBytes(uint16_t int16)
{
  const unsigned char *from;
  unsigned char *to;
  uint16_t t;

  from = (const unsigned char *) &int16;
  to = (unsigned char *) &t;

  to[0] = from[1];
  to[1] = from[0];

  return (t);
}


