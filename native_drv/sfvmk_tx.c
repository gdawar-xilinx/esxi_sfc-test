/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"

/* Wait time  for common TXQ to stop */
#define SFVMK_TXQ_STOP_POLL_TIME_USEC         VMK_USEC_PER_MSEC
#define SFVMK_TXQ_STOP_TIME_OUT_USEC          (200 * SFVMK_TXQ_STOP_POLL_TIME_USEC)

#define SFVMK_TXQ_UNBLOCK_LEVEL(n)            (EFX_TXQ_LIMIT(n) / 4)
/* Number of desc needed for each SG */
#define SFVMK_TXD_NEEDED(sgSize, maxBufSize)  EFX_DIV_ROUND_UP(sgSize, maxBufSize)
/* utility macros for VLAN handling */
#define SFVMK_VLAN_PRIO_SHIFT                 13
#define SFVMK_VLAN_VID_MASK                   0x0fff

/*! \brief  Allocate resources required for a particular TX queue.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  txqIndex TX queue index
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_txqInit(sfvmk_adapter_t *pAdapter, vmk_uint32 txqIndex)
{
  sfvmk_txq_t *pTxq = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", txqIndex);
  VMK_ASSERT_NOT_NULL(pAdapter);

  if (txqIndex >= pAdapter->numTxqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid TXQ index: %u", txqIndex);
    status = VMK_BAD_PARAM;
    goto done;
  }

  pTxq = vmk_HeapAlloc(sfvmk_modInfo.heapID, sizeof(sfvmk_txq_t));
  if (pTxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HeapAlloc failed");
    status = VMK_NO_MEMORY;
    goto done;
  }
  vmk_Memset(pTxq, 0, sizeof(sfvmk_txq_t));

  if (!ISP2(pAdapter->numTxqBuffDesc)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid value of numTxqBuffDesc:%u",
                        pAdapter->numTxqBuffDesc);
    status = VMK_FAILURE;
    goto failed_valid_buff_desc;
  }

  pTxq->pAdapter = pAdapter;
  status = sfvmk_createLock(pAdapter, "txqLock",
                            SFVMK_SPINLOCK_RANK_TXQ_LOCK,
                            &pTxq->lock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock failed status:  %s",
                        vmk_StatusToString(status));
    goto failed_create_lock;
  }

  pTxq->index = txqIndex;
  pTxq->state = SFVMK_TXQ_STATE_INITIALIZED;
  pAdapter->ppTxq[txqIndex] = pTxq;

  goto done;

failed_create_lock:
failed_valid_buff_desc:
  vmk_HeapFree(sfvmk_modInfo.heapID, pTxq);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX, "Tx qIndex[%u] %p", txqIndex, pTxq);

  return status;
}

/*! \brief Release resources of a TXQ.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  qIndex   tx queue index
**
** \return: void
*/
static void
sfvmk_txqFini(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  sfvmk_txq_t *pTxq = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", qIndex);

  VMK_ASSERT_NOT_NULL(pAdapter);

  if (qIndex >= pAdapter->numTxqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", qIndex);
    goto done;
  }

  pTxq = pAdapter->ppTxq[qIndex];
  VMK_ASSERT_NOT_NULL(pTxq);

  VMK_ASSERT_EQ(pTxq->state, SFVMK_TXQ_STATE_INITIALIZED);

  sfvmk_destroyLock(pTxq->lock);

  vmk_HeapFree(sfvmk_modInfo.heapID, pTxq);

  pAdapter->ppTxq[qIndex] = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", qIndex);
}

/*! \brief Allocate resources required for all TXQs.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_txInit(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32    qIndex;
  vmk_uint32    txqArraySize = 0;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  VMK_ASSERT_NOT_NULL(pAdapter);

  pAdapter->numTxqsAllocated = MIN(pAdapter->numEvqsAllocated,
                                   pAdapter->numTxqsAllotted);

  txqArraySize = sizeof(sfvmk_txq_t *) * pAdapter->numTxqsAllocated;

  pAdapter->ppTxq = vmk_HeapAlloc(sfvmk_modInfo.heapID, txqArraySize);
  if (pAdapter->ppTxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter,"vmk_HeapAlloc failed");
    status = VMK_NO_MEMORY;
    goto failed_txq_alloc;
  }
  vmk_Memset(pAdapter->ppTxq, 0, txqArraySize);

  for (qIndex = 0; qIndex < pAdapter->numTxqsAllocated; qIndex++) {
    status = sfvmk_txqInit(pAdapter, qIndex);
    if (status) {
      SFVMK_ADAPTER_ERROR(pAdapter,"sfvmk_txqInit(%u) failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto failed_txq_init;
    }
  }

  goto done;

failed_txq_init:
  while (qIndex)
    sfvmk_txqFini(pAdapter, --qIndex);

  vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter->ppTxq);
  pAdapter->ppTxq = NULL;

failed_txq_alloc:
  pAdapter->numTxqsAllocated = 0;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);

  return status;
}

/*! \brief Release resources required for all allocated TXQ
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_txFini(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 qIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  VMK_ASSERT_NOT_NULL(pAdapter);

  qIndex = pAdapter->numTxqsAllocated;
  while (qIndex > 0)
    sfvmk_txqFini(pAdapter, --qIndex);

  pAdapter->numTxqsAllocated = 0;

  if (pAdapter->ppTxq != NULL) {
    vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter->ppTxq);
    pAdapter->ppTxq = NULL;
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
}

/*! \brief  Flush common code TXQs.
**
** \param[in]  pAdapter  Pointer to sfvmk_adapter_t
**
** \return: void
*/
static void sfvmk_txFlush(sfvmk_adapter_t *pAdapter)
{
  sfvmk_txq_t *pTxq = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 qIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pAdapter->ppTxq);

  for (qIndex = 0; qIndex < pAdapter->numTxqsAllocated; qIndex++) {

    pTxq = pAdapter->ppTxq[qIndex];
    VMK_ASSERT_NOT_NULL(pTxq);

    vmk_SpinlockLock(pTxq->lock);

    if (pTxq->state != SFVMK_TXQ_STATE_STARTED) {
      SFVMK_ADAPTER_ERROR(pAdapter, "TXQ is not yet started");
      vmk_SpinlockUnlock(pTxq->lock);
      continue;
    }

    pTxq->state = SFVMK_TXQ_STATE_INITIALIZED;
    pTxq->flushState = SFVMK_FLUSH_STATE_PENDING;
    vmk_SpinlockUnlock(pTxq->lock);

    /* Flush the transmit queue */
    status = efx_tx_qflush(pTxq->pCommonTxq);
    if (status != VMK_OK) {
      vmk_SpinlockLock(pTxq->lock);
      if (status == VMK_EALREADY)
        pTxq->flushState = SFVMK_FLUSH_STATE_DONE;
      else
        pTxq->flushState = SFVMK_FLUSH_STATE_FAILED;
      vmk_SpinlockUnlock(pTxq->lock);
    }
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
}

/*! \brief  Wait for flush and destroy common code TXQ.
**
** \param[in]  pAdapter    Pointer to sfvmk_adapter_t
**
** \return: void
*/
static void
sfvmk_txFlushWaitAndDestroy(sfvmk_adapter_t *pAdapter)
{
  sfvmk_txq_t *pTxq = NULL;
  sfvmk_evq_t *pEvq = NULL;
  vmk_uint64 timeout, currentTime;
  vmk_uint32 qIndex;
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_pktCompCtx_t compCtx = {
    .type = SFVMK_PKT_COMPLETION_OTHERS,
  };

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pAdapter->ppTxq);
  VMK_ASSERT_NOT_NULL(pAdapter->ppEvq);

  sfvmk_getTime(&currentTime);
  timeout = currentTime + SFVMK_TXQ_STOP_TIME_OUT_USEC;

  for (qIndex = 0; qIndex < pAdapter->numTxqsAllocated; qIndex++) {

    pTxq = pAdapter->ppTxq[qIndex];
    VMK_ASSERT_NOT_NULL(pTxq);

    pEvq = pAdapter->ppEvq[qIndex];
    VMK_ASSERT_NOT_NULL(pEvq);

    vmk_SpinlockLock(pTxq->lock);

    while (currentTime < timeout) {
      /* Check to see if the flush event has been processed */
      if (pTxq->flushState != SFVMK_FLUSH_STATE_PENDING) {
        break;
      }
      vmk_SpinlockUnlock(pTxq->lock);

      status = vmk_WorldSleep(SFVMK_TXQ_STOP_POLL_TIME_USEC);
      if ((status != VMK_OK) && (status != VMK_WAIT_INTERRUPTED)) {
        SFVMK_ADAPTER_ERROR(pAdapter, "vmk_WorldSleep failed status: %s",
                            vmk_StatusToString(status));
        vmk_SpinlockLock(pTxq->lock);
        break;
      }

      sfvmk_getTime(&currentTime);
      vmk_SpinlockLock(pTxq->lock);
    }
    if (pTxq->flushState != SFVMK_FLUSH_STATE_DONE) {
      SFVMK_ADAPTER_ERROR(pAdapter, "TXQ[%u] flush timeout", qIndex);
      pTxq->flushState = SFVMK_FLUSH_STATE_DONE;
    }

    pTxq->blocked = VMK_FALSE;
    pTxq->pending = pTxq->added;

    sfvmk_txqComplete(pTxq, pEvq, &compCtx);
    if(pTxq->completed != pTxq->added)
      SFVMK_ADAPTER_ERROR(pAdapter, "pTxq->completed != pTxq->added");

    pTxq->added = 0;
    pTxq->pending = 0;
    pTxq->completed = 0;
    pTxq->reaped = 0;

    sfvmk_memPoolFree((vmk_VA)pTxq->pPendDesc, sizeof(efx_desc_t) *
                      pTxq->numDesc);
    pTxq->pPendDesc = NULL;
    sfvmk_memPoolFree((vmk_VA)pTxq->pTxMap, sizeof(sfvmk_txMapping_t) *
                    pTxq->numDesc);
    pTxq->pTxMap = NULL;

    vmk_SpinlockUnlock(pTxq->lock);

    /* Destroy the common code transmit queue. */
    efx_tx_qdestroy(pTxq->pCommonTxq);
    pTxq->pCommonTxq = NULL;

    sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                           pTxq->mem.pEsmBase,
                           pTxq->mem.ioElem.ioAddr,
                           pTxq->mem.ioElem.length);
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
}

/*! \brief Flush and destroy all common code TXQs.
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
**
** \return: void
**
*/
void
sfvmk_txStop(sfvmk_adapter_t *pAdapter)
{
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  VMK_ASSERT_NOT_NULL(pAdapter);

  /* Stop transmit queue(s) */
  sfvmk_txFlush(pAdapter);
  sfvmk_txFlushWaitAndDestroy(pAdapter);

  /* Tear down the transmit module */
  efx_tx_fini(pAdapter->pNic);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
}

/*! \brief Create common code TXQ.
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  qIndex      TXQ index
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus
sfvmk_txqStart(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  sfvmk_txq_t *pTxq = NULL;
  vmk_uint32 flags;
  vmk_uint32 descIndex;
  sfvmk_evq_t *pEvq = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;
  efx_desc_t *pPendDesc = NULL;
  sfvmk_txMapping_t *pTxMap = NULL;
  uint8_t *pTxqMem = NULL;
  vmk_IOA ioAddr = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", qIndex);

  VMK_ASSERT_NOT_NULL(pAdapter);

  if (qIndex >= pAdapter->numTxqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", qIndex);
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pAdapter->ppTxq != NULL)
    pTxq = pAdapter->ppTxq[qIndex];
  VMK_ASSERT_NOT_NULL(pTxq);

  if (pAdapter->ppEvq != NULL)
    pEvq = pAdapter->ppEvq[qIndex];
  VMK_ASSERT_NOT_NULL(pEvq);

  VMK_ASSERT_EQ(pTxq->state, SFVMK_TXQ_STATE_INITIALIZED);

  /* Allocate and zero DMA space for the descriptor ring. */
  pTxqMem = sfvmk_allocDMAMappedMem(pAdapter->dmaEngine,
                                    EFX_TXQ_SIZE(pAdapter->numTxqBuffDesc),
                                    &ioAddr);
  if (pTxqMem == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "sfvmk_allocDMAMappedMem(TXQ desc DMA memory) failed");
    status = VMK_NO_MEMORY;
    goto done;
  }

  vmk_SpinlockLock(pTxq->lock);

  pTxq->hwVlanTci = 0;
  pTxq->numDesc = pAdapter->numTxqBuffDesc;
  pTxq->ptrMask = pTxq->numDesc - 1;

  pTxq->mem.esmHandle  = pAdapter->dmaEngine;
  pTxq->mem.ioElem.length = EFX_TXQ_SIZE(pTxq->numDesc);
  pTxq->mem.ioElem.ioAddr = ioAddr;
  pTxq->mem.pEsmBase = pTxqMem;

  vmk_SpinlockUnlock(pTxq->lock);

  flags = EFX_TXQ_CKSUM_IPV4 | EFX_TXQ_CKSUM_TCPUDP;

  /* Create the common code transmit queue. */
  status = efx_tx_qcreate(pAdapter->pNic, qIndex,
                          0, &pTxq->mem,
                          pAdapter->numTxqBuffDesc, 0, flags,
                          pEvq->pCommonEvq,
                          &pTxq->pCommonTxq, &descIndex);
  if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_tx_qcreate(%u) failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto tx_qcreate_failed;
  }

  /* Enable the transmit queue. */
  efx_tx_qenable(pTxq->pCommonTxq);

  /* Allocate and initialise pkt DMA mapping array. */
  pTxMap = (sfvmk_txMapping_t *)sfvmk_memPoolAlloc(sizeof(sfvmk_txMapping_t)*
                                                   pTxq->numDesc);
  if (pTxMap == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "failed to allocate memory for txMap");
    goto tx_map_alloc_failed;
  }

  /* Allocate pending descriptor array for batching writes. */
  pPendDesc = (efx_desc_t *)sfvmk_memPoolAlloc(sizeof(efx_desc_t) * pTxq->numDesc);
  if (pPendDesc == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "failed to allocate memory for pend desc");
    goto pend_desc_alloc_failed;
  }

  vmk_SpinlockLock(pTxq->lock);
  pTxq->pTxMap = pTxMap;
  pTxq->pPendDesc = pPendDesc;
  pTxq->nPendDesc = 0;
  pTxq->added = pTxq->pending = pTxq->completed = pTxq->reaped = descIndex;
  pTxq->blocked = VMK_FALSE;
  pTxq->state = SFVMK_TXQ_STATE_STARTED;
  pTxq->flushState = SFVMK_FLUSH_STATE_REQUIRED;
  vmk_SpinlockUnlock(pTxq->lock);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "Txq[%u] ioa: %lx vmem: %p, pPendDesc: %p, numDesc: %u",
                      qIndex, pTxq->mem.ioElem.ioAddr, pTxqMem, pPendDesc,
                      pTxq->numDesc);
  goto done;

pend_desc_alloc_failed:
  sfvmk_memPoolFree((vmk_VA)pTxMap, sizeof(sfvmk_txMapping_t) * pTxq->numDesc);

tx_map_alloc_failed:
tx_qcreate_failed:
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                         pTxq->mem.pEsmBase,
                         pTxq->mem.ioElem.ioAddr,
                         pTxq->mem.ioElem.length);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", qIndex);
  return status;
}

/*! \brief creates txq module for all allocated txqs.
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_txStart(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 qIndex;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  VMK_ASSERT_NOT_NULL(pAdapter);

  /* Initialize the common code transmit module. */

  status = efx_tx_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,"efx_tx_init failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto failed_tx_init;
  }

  for (qIndex = 0; qIndex < pAdapter->numTxqsAllocated; qIndex++) {
    status = sfvmk_txqStart(pAdapter, qIndex);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,"sfvmk_txqStart(%u) failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto failed_txq_start;
    }
  }

  goto done;

failed_txq_start:
  sfvmk_txFlush(pAdapter);
  sfvmk_txFlushWaitAndDestroy(pAdapter);

  efx_tx_fini(pAdapter->pNic);

failed_tx_init:
  pAdapter->numTxqsAllocated = 0;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);

  return status;

}

/*! \brief Change the flush state to done.
**
** \param[in]  pTxq    pointer to txq
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_txqFlushDone(sfvmk_txq_t *pTxq)
{
  VMK_ASSERT_NOT_NULL(pTxq);

  vmk_SpinlockLock(pTxq->lock);
  pTxq->flushState = SFVMK_FLUSH_STATE_DONE;
  vmk_SpinlockUnlock(pTxq->lock);

  return VMK_OK;
}

/*! \brief reap the tx queue
**
** \param[in]  pTxq    pointer to txq
**
** \return: void
*/
void
sfvmk_txqReap(sfvmk_txq_t *pTxq)
{
  pTxq->reaped = pTxq->completed;
}

/*! \brief Mark the tx queue as unblocked
**
** \param[in] pTxq  pointer to txq
**
** \return: void
*/
static void
sfvmk_txqUnblock(sfvmk_txq_t *pTxq)
{
  struct sfvmk_adapter_s *pAdapter = pTxq->pAdapter;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  if (VMK_UNLIKELY(pTxq->state != SFVMK_TXQ_STATE_STARTED)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid TXQ state[%d]", pTxq->state);
    goto done;
  }

  /* Reaped must be in sync with blocked */
  sfvmk_txqReap(pTxq);
  pTxq->blocked = VMK_FALSE;
  sfvmk_updateQueueStatus(pTxq->pAdapter, VMK_UPLINK_QUEUE_STATE_STARTED, pTxq->index);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
}

/*! \brief called when a tx completion event comes from the fw.
**
** \param[in]  pTxq      Tx queue ptr
** \param[in]  pEvq      event queue ptr
** \param[in]  pCompCtx  pointer to completion context
**
** \return: void
*/
void
sfvmk_txqComplete(sfvmk_txq_t *pTxq, sfvmk_evq_t *pEvq, sfvmk_pktCompCtx_t *pCompCtx)
{
  unsigned int completed;
  struct sfvmk_adapter_s *pAdapter = pTxq->pAdapter;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  completed = pTxq->completed;
  while (completed != pTxq->pending) {
    sfvmk_txMapping_t *pTxMap;
    unsigned int id;

    id = completed++ & pTxq->ptrMask;
    pTxMap = &pTxq->pTxMap[id];

    if (pTxMap->sgElem.ioAddr != 0)
      vmk_DMAUnmapElem(pAdapter->dmaEngine, VMK_DMA_DIRECTION_FROM_MEMORY, &pTxMap->sgElem);

    if (pTxMap->pXmitPkt != NULL)
      sfvmk_pktRelease(pAdapter, pCompCtx, pTxMap->pXmitPkt);
    vmk_Memset(pTxMap, 0, sizeof(sfvmk_txMapping_t));
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "Processed completions from id: %u to %u",
                      pTxq->completed, pTxq->pending);
  pTxq->completed = completed;

  /* Check whether we need to unblock the queue. */
  vmk_CPUMemFenceWrite();
  if (pTxq->blocked) {
    unsigned int level;

    level = pTxq->added - pTxq->completed;

    if (level <= SFVMK_TXQ_UNBLOCK_LEVEL(pTxq->numDesc))
      sfvmk_txqUnblock(pTxq);
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
}

/*! \brief Estimate the number of tx descriptors required for this packet
**
** \param[in]  pTxq    pointer to txq
** \param[in]  pkt     pointer to packet handle
**
** \return: estimated number of tx descriptors
*/
static inline vmk_uint32
sfvmk_txDmaDescEstimate(sfvmk_txq_t *pTxq,
                        vmk_PktHandle *pkt)
{
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;
  vmk_uint32 numDesc = 0, i = 0;
  vmk_uint32 numElems = vmk_PktSgArrayGet(pkt)->numElems;
  const vmk_SgElem *pSge;

  for (i = 0; i < numElems; i++) {
     pSge = vmk_PktSgElemGet(pkt, i);
     numDesc += SFVMK_TXD_NEEDED(pSge->length, pAdapter->txDmaDescMaxSize);
  }

  /* +1 for VLAN optional descriptor */
  numDesc++;

  /* TODO: handle TSO estimation */
  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "Estimated number of DMA desc = %u", numDesc);
  return numDesc;
}

/*! \brief Create the tx queue dma descriptor
**
** \param[in]        pTxq    pointer to txq
** \param[in]        ioa     SG element mapped IO address
** \param[in]        len     length of SG element
** \param[in]        eop     end of packet
** \param[in, out]   pId     pointer to TXQ descriptor index
**
** \return: void
*/
static inline void
sfvmk_createDmaDesc(sfvmk_txq_t *pTxq,
              vmk_IOA ioa,
              vmk_ByteCountSmall len,
              vmk_Bool eop,
              vmk_uint32 *pId)
{
  efx_desc_t *desc;

  VMK_ASSERT(ioa && len);

  desc = &pTxq->pPendDesc[pTxq->nPendDesc ++];
  efx_tx_qdesc_dma_create(pTxq->pCommonTxq, ioa, len, eop, desc);
  *pId = (*pId + 1) & pTxq->ptrMask;
}

/*! \brief process transmission of non-TSO packet
**
** \param[in]        pTxq       pointer to txq
** \param[in]        pXmitPkt   pointer to packet handle
** \param[in,out]    pTxMapId   pointer to txMap Id for next element
**
** \return: VMK_OK in success, error code otherwise
*/
static VMK_ReturnStatus
sfvmk_txNonTsoPkt(sfvmk_txq_t *pTxq,
            vmk_PktHandle *pXmitPkt,
            vmk_uint32 *pTxMapId)
{
  vmk_uint32 i = 0, j = 0, descCount = 0;
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;
  vmk_Bool eop;
  vmk_DMAMapErrorInfo dmaMapErr;
  const vmk_SgElem *pSgElem;
  vmk_uint16 numElems;
  vmk_uint32 id, startID;
  vmk_SgElem inAddr, mappedAddr;
  vmk_ByteCountSmall elemLength, pktLenLeft;
  sfvmk_txMapping_t *pTxMap = pTxq->pTxMap;
  vmk_uint32 nPendDescOri = pTxq->nPendDesc;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  id = startID = *pTxMapId;
  pktLenLeft = vmk_PktFrameLenGet(pXmitPkt);
  numElems = vmk_PktSgArrayGet(pXmitPkt)->numElems;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "Start creating non-TSO desc, pXmitPkt=%p, "
                      "numElems=%d, startID=%d", pXmitPkt, numElems, startID);

  for (i = 0; i < numElems && pktLenLeft > 0; i ++) {
     pSgElem = vmk_PktSgElemGet(pXmitPkt, i);
     if (pSgElem == NULL) {
       SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PktSgElemGet returned NULL");
       status = VMK_FAILURE;
       goto fail_map;
     }

     elemLength = pSgElem->length;
     if (elemLength > pAdapter->txDmaDescMaxSize) {
       SFVMK_ADAPTER_ERROR(pAdapter, "ElemLength[%u] exceeded max allowed",
                           elemLength);
       status = VMK_FAILURE;
       goto fail_map;
     }

     if (pktLenLeft < elemLength) {
       elemLength = pktLenLeft;
     }

     pktLenLeft -= elemLength;

     inAddr.addr = pSgElem->addr;
     inAddr.length = elemLength;
     status = vmk_DMAMapElem(pAdapter->dmaEngine,
                             VMK_DMA_DIRECTION_FROM_MEMORY,
                             &inAddr, VMK_TRUE,
                             &mappedAddr, &dmaMapErr);
     if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter,"Failed to map elem %lx size %u: %s",
                            pSgElem->addr, elemLength,
                            vmk_DMAMapErrorReasonToString(dmaMapErr.reason));
        goto fail_map;
     }

     SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                         "sge[%d] DMA mapped, ioa = %lx, len = %d", i,
                         mappedAddr.ioAddr, mappedAddr.length);

     pTxMap[id].sgElem.ioAddr = mappedAddr.ioAddr;
     pTxMap[id].sgElem.length = mappedAddr.length;
     pTxMap[id].pXmitPkt      = NULL;

     eop = (i == numElems - 1 ) || (pktLenLeft == 0);
     if (eop) {
        pTxMap[id].pXmitPkt = pXmitPkt;
     }

     sfvmk_createDmaDesc(pTxq, mappedAddr.ioAddr, mappedAddr.length, eop, &id);
  }

  *pTxMapId = id;
  descCount = pTxq->nPendDesc - nPendDescOri;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "non-TSO %u descriptor created, next startID = %u",
                      descCount, id);

  status = VMK_OK;
  goto done;

fail_map:
  descCount = pTxq->nPendDesc - nPendDescOri;
  pTxq->nPendDesc -= descCount;
  for (i = startID; i < startID + descCount; i ++) {
     j = i & pTxq->ptrMask;
     if (pTxMap[j].sgElem.ioAddr) {
       vmk_DMAUnmapElem(pTxq->pAdapter->dmaEngine,
                        VMK_DMA_DIRECTION_FROM_MEMORY, &pTxMap[j].sgElem);
       vmk_Memset(&pTxMap[j], 0, sizeof(sfvmk_txMapping_t));
     }
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
  return status;
}

/*! \brief check and insert vlan tag in the packet header
**
** \param[in]        pTxq        pointer to txq
** \param[in]        pXmitInfo   pointer to transmit info structure
** \param[in,out]    pTxMapId    pointer to txMap Id
**
** \return: void
*/
static void
sfvmk_txMaybeInsertTag(sfvmk_txq_t *pTxq,
                       sfvmk_xmitInfo_t *pXmitInfo,
                       vmk_uint32 *pTxMapId)
{
  vmk_uint16 thisTag = 0;
  vmk_VlanID vlanId = 0;
  vmk_VlanPriority vlanPrio = 0;
  vmk_PktHandle *pkt = pXmitInfo->pXmitPkt;
  sfvmk_txMapping_t *pTxMap = pTxq->pTxMap;
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  if (pXmitInfo->offloadFlag & SFVMK_TX_VLAN) {
    vlanId = vmk_PktVlanIDGet(pkt);
    vlanPrio = vmk_PktPriorityGet(pkt);

    thisTag = (vlanId & SFVMK_VLAN_VID_MASK) | (vlanPrio << SFVMK_VLAN_PRIO_SHIFT);
  }

  if (thisTag != pTxq->hwVlanTci) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                        "vlan_id: %d, prio: %d, tci: %d, hwVlanTci: %d",
                        vlanId, vlanPrio, thisTag, pTxq->hwVlanTci);

    efx_tx_qdesc_vlantci_create(pTxq->pCommonTxq,
                                vmk_CPUToBE16(thisTag),
                                &pTxq->pPendDesc[pTxq->nPendDesc ++]);
    pTxq->hwVlanTci = thisTag;
    /* zero tx map array elem for option desc, to make sure completion process
     * doesn't try to clean-up.
     */
    vmk_Memset(&pTxMap[*pTxMapId], 0, sizeof(sfvmk_txMapping_t));
    *pTxMapId = (*pTxMapId + 1) & pTxq->ptrMask;
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
  return;
}

/*! \brief post the queue descriptors.
**
** \param[in]  pTxq    pointer to txq
**
** \return: VMK_OK in success, error code otherwise
*/
static VMK_ReturnStatus
sfvmk_txqListPost(sfvmk_txq_t *pTxq)
{
  unsigned int oldAdded;
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  VMK_ASSERT(pTxq->nPendDesc != 0);
  VMK_ASSERT(pTxq->blocked == VMK_FALSE);

  oldAdded = pTxq->added;

  /* Post the fragment list. */
  status = efx_tx_qdesc_post(pTxq->pCommonTxq, pTxq->pPendDesc, pTxq->nPendDesc,
                             pTxq->reaped, &pTxq->added);

  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_tx_qdesc_post failed: %s",
                        vmk_StatusToString(status));
    if(status == VMK_NO_SPACE)
      status = VMK_BUSY;
    /* panic to catch issues during development */
    VMK_ASSERT(0);
    goto done;
  }

  VMK_ASSERT(pTxq->added - oldAdded == pTxq->nPendDesc);
  VMK_ASSERT(pTxq->added - pTxq->reaped <= pTxq->numDesc);

  /* Clear the fragment list. */
  pTxq->nPendDesc = 0;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
  return status;
}

/*! \brief fill the transmit buffer descriptor with pkt fragments
**
** \param[in]  pTxq      pointer to txq
** \param[in]  pXmitInfo pointer to transmit info structure
**
** \return: VMK_OK in case of success, VMK_FAILURE otherwise
**
*/
VMK_ReturnStatus
sfvmk_populateTxDescriptor(sfvmk_txq_t *pTxq,
                           sfvmk_xmitInfo_t *pXmitInfo)
{
   VMK_ReturnStatus status = VMK_FAILURE;
   sfvmk_adapter_t *pAdapter = pTxq->pAdapter;
   vmk_uint32 txMapId = (pTxq->added) & pTxq->ptrMask;

   SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);
   VMK_ASSERT(pTxq->nPendDesc == 0);

   /* VLAN handling */
   sfvmk_txMaybeInsertTag(pTxq, pXmitInfo, &txMapId);

   /* TODO: CSO and TSO handling */

   status = sfvmk_txNonTsoPkt(pTxq, pXmitInfo->pXmitPkt, &txMapId);
   if (status != VMK_OK) {
     SFVMK_ADAPTER_ERROR(pAdapter, "pkt[%p] tx failed: %s",
                         pXmitInfo->pXmitPkt, vmk_StatusToString(status));
     goto done;
   }

   /* Post the pSgElemment list. */
   status = sfvmk_txqListPost(pTxq);
   if (status != VMK_OK) {
     SFVMK_ADAPTER_ERROR(pAdapter, "pkt[%p] post failed: %s",
                         pXmitInfo->pXmitPkt, vmk_StatusToString(status));
   }

done:
   SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
   return status;
}

/*! \brief function to check if the transmit queue is stopped
**
** \param[in]  pAdapter       pointer to sfvmk_adapter_t
** \param[in]  txqIndex     transmit queue id
**
** \return: VMK_TRUE if txq is stopped, VMK_FALSE otherwise.
**
*/
vmk_Bool
sfvmk_isTxqStopped(sfvmk_adapter_t *pAdapter, vmk_uint32 txqIndex)
{
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_uint32 queueIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  pQueueInfo = &pAdapter->uplink.queueInfo;
  queueIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);
  pQueueData = &pQueueInfo->queueData[queueIndex];

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
  return (pQueueData[txqIndex].state == VMK_UPLINK_QUEUE_STATE_STOPPED);
}

/*! \brief transmit the packet on the uplink interface
**
** \param[in]  pTxq      pointer to txq
** \param[in]  pkt       packet to be transmitted
**
** \return: VMK_OK in case of success, VMK_FAILURE otherwise
*/

VMK_ReturnStatus
sfvmk_transmitPkt(sfvmk_txq_t *pTxq,
                  vmk_PktHandle *pkt)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 pushed = 0;
  vmk_uint32 nTotalDesc = 0;
  sfvmk_adapter_t *pAdapter = NULL;
  sfvmk_xmitInfo_t xmitInfo;

  VMK_ASSERT_NOT_NULL(pTxq);
  VMK_ASSERT_NOT_NULL(pTxq->pCommonTxq);
  pAdapter = pTxq->pAdapter;
  VMK_ASSERT_NOT_NULL(pAdapter);

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  vmk_Memset(&xmitInfo, 0, sizeof(sfvmk_xmitInfo_t));
  xmitInfo.offloadFlag |= vmk_PktMustVlanTag(pkt) ? SFVMK_TX_VLAN : 0;
  xmitInfo.pXmitPkt = pkt;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "Xmit start, pkt = %p, TX VLAN offload %s needed, "
                      "numElems = %d, pktLen = %d", pkt,
                      xmitInfo.offloadFlag & SFVMK_TX_VLAN ? "" : "not ",
                      vmk_PktSgArrayGet(pkt)->numElems,
                      vmk_PktFrameLenGet(pkt));


  /* Do estimation as early as possible */
  nTotalDesc = sfvmk_txDmaDescEstimate(pTxq, pkt);

  pushed = pTxq->added;

  /* Check if need to stop the queue */
  vmk_CPUMemFenceWrite();
  sfvmk_txqReap(pTxq);
  if (pTxq->added - pTxq->reaped + nTotalDesc > EFX_TXQ_LIMIT(pTxq->numDesc)) {
    pTxq->blocked = VMK_TRUE;
    sfvmk_updateQueueStatus(pAdapter, VMK_UPLINK_QUEUE_STATE_STOPPED,
                            pTxq->index);
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_INFO,
                        "not enough desc entries in txq[%u], stopping the queue",
                        pTxq->index);
    status = VMK_BUSY;
    goto done;
  }

  VMK_ASSERT_EQ(pTxq->blocked, VMK_FALSE);

  status = sfvmk_populateTxDescriptor(pTxq, &xmitInfo);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_populateTxDescriptor failed: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  if (pTxq->added != pushed)
    efx_tx_qpush(pTxq->pCommonTxq, pTxq->added, pushed);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
  return status;
}

