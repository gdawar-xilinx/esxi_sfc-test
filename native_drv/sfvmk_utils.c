/*
 * Copyright (c) 2017-2020 Xilinx, Inc.
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

/*! \brief It creates a spin lock with specified name and lock rank.
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
** \param[in]  pLockName brief name for the spinlock
** \param[in]  rank      lock rank
** \param[out] pLock     lock pointer to create
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
** \param[in]  vAddr  virtual address of the mem needs to get freed
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
** \param[out] pIoAddr    dma addressable memory
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

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
/*! \brief It creates a mutex lock with specified name.
**
** \param[in]  pLockName    brief name for the mutexLock
** \param[out] pMutex        mutex lock pointer to create
**
** \return: VMK_OK on success, and lock created. Error code if otherwise.
*/
VMK_ReturnStatus
sfvmk_mutexInit(const char *pLockName, sfvmk_Lock *pMutex)
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

#else
/*! \brief It creates a semaphore  with specified name.
**
** \param[in]  pLockName    brief name for the mutexLock
** \param[out] pLock        semaphore pointer to create
**
** \return: VMK_OK on success, and lock created. Error code if otherwise.
*/
VMK_ReturnStatus
sfvmk_mutexInit(const char *pLockName, sfvmk_Lock *pLock)
{
  VMK_ReturnStatus status;

  VMK_ASSERT_NOT_NULL(pLockName);
  VMK_ASSERT_NOT_NULL(pLock);

  status = vmk_BinarySemaCreate(pLock, vmk_ModuleCurrentID,
                                (const char *)pLockName);
  if (status != VMK_OK) {
    SFVMK_ERROR("Failed to create semaphore (%s)", vmk_StatusToString(status));
  }

  return status;
}

/*! \brief It destroy semaphore.
**
** \param[in]  lock  semaphore
**
** \return: void
*/
void sfvmk_mutexDestroy(sfvmk_Lock lock)
{
  if (lock != NULL)
    vmk_SemaDestroy(&lock);
}
#endif

/*! \brief  World Sleep function ensures that
**          the world sleeps for specified time
**          even when VMK_WAIT_INTERRUPTED is
**          returned.
**          Note: The time to wait is in microseconds.
**
** \param[in] sleepTime  Duration to wait in microseconds.
**
** \return: VMK_OK Awakened after at least the specified delay.
**          VMK_DEATH_PENDING Awakened because the world is
**          dying and being reaped by the scheduler. The
**          entire delay may not have passed.
**
*/
VMK_ReturnStatus
sfvmk_worldSleep(vmk_uint64 sleepTime)
{
  vmk_uint64 currentTime;
  vmk_uint64 timeOut;
  VMK_ReturnStatus status;

  sfvmk_getTime(&currentTime);
  timeOut = currentTime + sleepTime;

  while (currentTime < timeOut) {
    status = vmk_WorldSleep(sleepTime);
    if (status != VMK_WAIT_INTERRUPTED)
      return status;

    sfvmk_getTime(&currentTime);
    sleepTime = timeOut-currentTime;
  }

  return VMK_OK;
}

/*! \brief Helper function to print MAC address in provided buffer.
**
** \param[in]   pAddr      MAC address to be printed
** \param[out]  pBuffer    Buffer of minimum size SFVMK_MAC_BUF_SIZE
**                         to hold the MAC address string
**
** \return: Pointer to the output buffer
*/
char *
sfvmk_printMac(const vmk_uint8 *pAddr, vmk_int8 *pBuffer)
{
  vmk_StringFormat(pBuffer, SFVMK_MAC_BUF_SIZE, NULL,
                   "%02x:%02x:%02x:%02x:%02x:%02x",
                   pAddr[0], pAddr[1], pAddr[2], pAddr[3], pAddr[4], pAddr[5]);
  return pBuffer;
}

/*! \brief helper function to compare two mac addresses
**
** \param[in]   pAddr1     first mac address
** \param[in]   pAddr2     second mac address
**
** \return: VMK_TRUE if entered mac address are same, VMK_FALSE otherwise
*/
inline vmk_Bool
sfvmk_macAddrSame(const vmk_uint8 *pAddr1, const vmk_uint8 *pAddr2)
{
  return (memcmp(pAddr1, pAddr2, VMK_ETH_ADDR_LENGTH) == 0);
}

/*! \brief Helper function check if the given MAC address is Broadcast address
**
** \param[in]   pAddr      MAC address to be checked
**
** \return: VMK_TRUE if input address is Broadcast address
*/
inline vmk_Bool
sfvmk_isBroadcastEtherAddr(const vmk_uint8 *pAddr)
{
  return ((pAddr[0] & pAddr[1] & pAddr[2] & pAddr[3] & pAddr[4] & pAddr[5]) ==
           0xff);
}

/*! \brief Helper function to find first set bit
**
** \param[in]   mask       Input byte to be scanned
**
** \return: first set bit position considering lsb as 1
*/
inline vmk_uint8
sfvmk_firstBitSet(vmk_uint8 mask)
{
  register vmk_int8 bit;

  if (mask == 0)
    return 0;

  for (bit = 1; !(mask & 1); bit++)
    mask >>= 1;

  return bit;
}

/*! \brief wrapper function to allocate dma-able memory
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t
** \param[out] pMem       pointer to dma-able memory buffer
** \param[in]  size       size of the memory
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_allocDmaBuffer(sfvmk_adapter_t *pAdapter,
                          efsys_mem_t *pMem,
                          size_t size)
{
  VMK_ReturnStatus status = VMK_OK;

  pMem->ioElem.length =  size;
  pMem->esmHandle = pAdapter->dmaEngine;
  pMem->pEsmBase = sfvmk_allocDMAMappedMem(pAdapter->dmaEngine,
                                           pMem->ioElem.length,
                                           &pMem->ioElem.ioAddr);
  if (pMem->pEsmBase == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_allocDMAMappedMem failed");
    status = VMK_NO_MEMORY;
  }

  return status;
}

/*! \brief wrapper function to change atomic variable holding value 'old'
**         to value 'new'. Wait for 'timeout' microseconds before re-check.
**         In case termCheck is true and old value is found equal to termVal,
**         return with Failure.
**
** \param[in]  var           pointer to atomic variable to be modified
** \param[in]  old           old value of atomic variable
** \param[in]  new           new value of atomic variable
** \param[in]  termCheck     whether the termVal should be checked
** \param[in]  termVal       if termCheck is true and old value is found
**                           equal to termVal, return with Failure
** \param[in]  timeout       timeout in microseconds to wait before re-checking
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_changeAtomicVar(vmk_atomic64  *var,
                      vmk_uint64 old,
                      vmk_uint64 new,
                      vmk_Bool   termCheck,
                      vmk_uint64 termVal,
                      vmk_uint64 timeout)
{
  VMK_ReturnStatus status = VMK_OK;
  vmk_uint64 val = 0;
  vmk_uint64 cnt = 0;

  val = vmk_AtomicReadIfEqualWrite64(var, old, new);
  while (val != old) {
    if ((termCheck == VMK_TRUE) && (val == termVal)) {
      SFVMK_DEBUG(SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                  "Exiting on term val: %lu", termVal);
      status = VMK_FAILURE;
      break;
    }
    status = sfvmk_worldSleep(timeout);
    if (status != VMK_OK) {
      SFVMK_ERROR("vmk_WorldSleep failed status: %s", vmk_StatusToString(status));
      break;
    }

    /* Log an error if stuck in this loop */
    if ((++cnt & 0xFF) == 0)
      SFVMK_ERROR("State machine stuck? cnt: %lu", cnt);

    val = vmk_AtomicReadIfEqualWrite64(var, old, new);
  }

  return status;
}

