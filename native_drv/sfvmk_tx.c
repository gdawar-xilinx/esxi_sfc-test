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

/* Wait time  for common TXQ to stop */
#define SFVMK_TXQ_STOP_POLL_TIME_USEC   VMK_USEC_PER_MSEC
#define SFVMK_TXQ_STOP_POLL_COUNT       200

/*! \brief  Allocate resources required for a particular TX queue.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  TX queue index
** \param[in]  TX queue type
** \param[in]  associated event queue index
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_txqInit(sfvmk_adapter_t *pAdapter, vmk_uint32 txqIndex,
              sfvmk_txqType_t type, vmk_uint32 evqIndex)
{
  sfvmk_txq_t *pTxq = NULL;
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", txqIndex);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if ((txqIndex >= pAdapter->numTxqsAllocated) ||
      (evqIndex >= pAdapter->numEvqsAllocated) ||
      (pAdapter->ppEvq[evqIndex] == NULL)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid arguments TXQ = %u, EVQ = %u pEvq = %p",
                        txqIndex, evqIndex, pAdapter->ppEvq[evqIndex]);
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

  pTxq->numDesc = pAdapter->numTxqBuffDesc;
  pTxq->ptrMask = pTxq->numDesc - 1;

  pTxq->mem.esmHandle  = pAdapter->dmaEngine;
  pTxq->mem.ioElem.length = EFX_TXQ_SIZE(pTxq->numDesc);

  /* Allocate and zero DMA space for the descriptor ring. */
  pTxq->mem.pEsmBase = sfvmk_allocDMAMappedMem(pAdapter->dmaEngine,
                                               pTxq->mem.ioElem.length,
                                               &pTxq->mem.ioElem.ioAddr);
  if(pTxq->mem.pEsmBase == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "sfvmk_allocDMAMappedMem(TXQ desc DMA memory) failed");
    status = VMK_NO_MEMORY;
    goto failed_dma_alloc;
  }

  status = sfvmk_createLock(pAdapter, "txqLock",
                            SFVMK_SPINLOCK_RANK_TXQ_LOCK,
                            &pTxq->lock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock failed status:  %s",
                        vmk_StatusToString(status));
    goto failed_create_lock;
  }

  pTxq->type = type;
  pTxq->evqIndex = evqIndex;
  pTxq->index = txqIndex;
  pTxq->state = SFVMK_TXQ_STATE_INITIALIZED;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_INFO,
                      "TXQ[%u] is initialized associated EVQ index is %u",
                      txqIndex, evqIndex);

  pAdapter->ppTxq[txqIndex] = pTxq;

  goto done;

failed_create_lock:
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                         pTxq->mem.pEsmBase,
                         pTxq->mem.ioElem.ioAddr,
                         pTxq->mem.ioElem.length);

failed_dma_alloc:
failed_valid_buff_desc:
  vmk_HeapFree(sfvmk_modInfo.heapID, pTxq);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", txqIndex);

  return status;
}

/*! \brief Release resources of a TXQ.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  tx queue index
**
** \return: void
*/
static void
sfvmk_txqFini(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  sfvmk_txq_t *pTxq = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", qIndex);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  if (qIndex >= pAdapter->numTxqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", qIndex);
    goto done;
  }

  pTxq = pAdapter->ppTxq[qIndex];
  if (pTxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL TXQ ptr");
    goto done;
  }

  if (pTxq->state != SFVMK_TXQ_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "TXQ is not initialized");
    goto done;
  }

  sfvmk_destroyLock(pTxq->lock);

  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                         pTxq->mem.pEsmBase,
                         pTxq->mem.ioElem.ioAddr,
                         pTxq->mem.ioElem.length);


  vmk_HeapFree(sfvmk_modInfo.heapID, pTxq);
  pAdapter->ppTxq[qIndex] = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", qIndex);
}

/*! \brief Allocate resources required for all TXQs.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
VMK_ReturnStatus
sfvmk_txInit(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32    qIndex;
  vmk_uint32    evqIndex = 0;
  vmk_uint32    txqArraySize = 0;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

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

  /* Initialize all transmit queues as capable of performing CSO & FATSO
   * on incoming packets. Queues will be started (TBD) with call to
   * efx_tx_qcreate with flags parameter including EFX_TXQ_FATSOV2, if HW
   * supports FATSOv2. Then, whether checksum offload is required will be
   * determined on per packet basis and if required, checksum offload option
   * descriptor will be created at run time if it doesn't exist on that queue.
   */
  for (qIndex = 0; qIndex < pAdapter->numTxqsAllocated;
       qIndex++, evqIndex++) {
    status = sfvmk_txqInit(pAdapter, qIndex, SFVMK_TXQ_TYPE_IP_TCP_UDP_CKSUM,
                           evqIndex);
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

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  qIndex = pAdapter->numTxqsAllocated;
  while (qIndex > 0)
    sfvmk_txqFini(pAdapter, --qIndex);

  pAdapter->numTxqsAllocated = 0;

  if (pAdapter->ppTxq != NULL) {
    vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter->ppTxq);
    pAdapter->ppTxq = NULL;
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
}

/*! \brief Flush and destroy common code TXQ.
**
** \param[in]  pAdapter    Pointer to sfvmk_adapter_t
** \param[in]  qIndex      TXQ index
**
** \return: void
*/
static void
sfvmk_txqStop(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  sfvmk_txq_t *pTxq = NULL;
  sfvmk_evq_t *pEvq = NULL;
  vmk_uint32 count;
  VMK_ReturnStatus status;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", qIndex);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if (qIndex >= pAdapter->numTxqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid TXQ index %u", qIndex);
    goto done;
  }

  if (pAdapter->ppTxq != NULL)
    pTxq = pAdapter->ppTxq[qIndex];

  if (pTxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL transmit queue ptr");
    goto done;
  }

  if (pAdapter->ppEvq != NULL)
    pEvq = pAdapter->ppEvq[qIndex];

  if (pEvq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL event queue ptr");
    goto done;
  }

  vmk_SpinlockLock(pTxq->lock);

  if (pTxq->state != SFVMK_TXQ_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "TXQ is not yet started");
    vmk_SpinlockUnlock(pTxq->lock);
    goto done;
  }
  pTxq->state = SFVMK_TXQ_STATE_INITIALIZED;

  if (pTxq->flushState == SFVMK_FLUSH_STATE_DONE) {
    vmk_SpinlockUnlock(pTxq->lock);
    goto done;
  }
  pTxq->flushState = SFVMK_FLUSH_STATE_PENDING;

  vmk_SpinlockUnlock(pTxq->lock);

  /* Flush the transmit queue. */
  status = efx_tx_qflush(pTxq->pCommonTxq);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_tx_qflush failed status: %s",
                        vmk_StatusToString(status));
  } else {
    count = 0;
    do {
      status = vmk_WorldSleep(SFVMK_TXQ_STOP_POLL_TIME_USEC);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "vmk_WorldSleep failed status: %s",
                            vmk_StatusToString(status));
        break;
      }

      vmk_SpinlockLock(pTxq->lock);
      if (pTxq->flushState != SFVMK_FLUSH_STATE_PENDING) {
        vmk_SpinlockUnlock(pTxq->lock);
        break;
      }
      vmk_SpinlockUnlock(pTxq->lock);

    } while (++count < SFVMK_TXQ_STOP_POLL_COUNT);
  }

  vmk_SpinlockLock(pTxq->lock);
  if (pTxq->flushState != SFVMK_FLUSH_STATE_DONE) {
    /* Flush timeout */
    pTxq->flushState = SFVMK_FLUSH_STATE_DONE;
  }
  vmk_SpinlockUnlock(pTxq->lock);

  /* Destroy the common code transmit queue. */
  efx_tx_qdestroy(pTxq->pCommonTxq);
  pTxq->pCommonTxq = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", qIndex);

}

/*! \brief Flush and destroy all common code TXQs.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: void
**
*/
void
sfvmk_txStop(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 qIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  qIndex = pAdapter->numTxqsAllocated;
  while (qIndex--)
    sfvmk_txqStop(pAdapter, qIndex);

  /* Tear down the transmit module */
  efx_tx_fini(pAdapter->pNic);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
}

/*! \brief Create common code TXQ.
**
** \param[in]  pAdapter    Pointer to sfvmk_adapter_t
** \param[in]  qIndex      TXQ index
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_txqStart(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  sfvmk_txq_t *pTxq = NULL;
  vmk_uint32 flags;
  vmk_uint32 descIndex;
  sfvmk_evq_t *pEvq = NULL;
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", qIndex);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (qIndex >= pAdapter->numTxqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", qIndex);
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pAdapter->ppTxq != NULL)
    pTxq = pAdapter->ppTxq[qIndex];

  if (pTxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL transmit queue ptr");
    status = VMK_FAILURE;
    goto done;
  }

  if (pAdapter->ppEvq != NULL)
    pEvq = pAdapter->ppEvq[pTxq->evqIndex];

  if (pEvq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL Event queue ptr");
    status = VMK_FAILURE;
    goto done;
  }

  vmk_SpinlockLock(pTxq->lock);
  if (pTxq->state != SFVMK_TXQ_STATE_INITIALIZED) {
    vmk_SpinlockUnlock(pTxq->lock);
    SFVMK_ADAPTER_ERROR(pAdapter, "TXQ is not initialized");
    status = VMK_FAILURE;
    goto done;
  }
  vmk_SpinlockUnlock(pTxq->lock);

  flags = EFX_TXQ_CKSUM_IPV4 | EFX_TXQ_CKSUM_TCPUDP;

  /* Create the common code transmit queue. */
  status = efx_tx_qcreate(pAdapter->pNic, qIndex,
                          pTxq->type, &pTxq->mem,
                          pAdapter->numTxqBuffDesc, 0, flags,
                          pEvq->pCommonEvq,
                          &pTxq->pCommonTxq, &descIndex);
  if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_tx_qcreate(%u) failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto done;
  }

  /* Enable the transmit queue. */
  efx_tx_qenable(pTxq->pCommonTxq);

  vmk_SpinlockLock(pTxq->lock);
  pTxq->state = SFVMK_TXQ_STATE_STARTED;
  pTxq->flushState = SFVMK_FLUSH_STATE_REQUIRED;
  vmk_SpinlockUnlock(pTxq->lock);


done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX, "qIndex[%u]", qIndex);

  return status;
}

/*! \brief creates txq module for all allocated txqs.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
VMK_ReturnStatus
sfvmk_txStart(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 qIndex;
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

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
  while (qIndex)
    sfvmk_txqStop(pAdapter, --qIndex);

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
** \return: VMK_OK <success> error code <failure>
*/
VMK_ReturnStatus
sfvmk_txqFlushDone(sfvmk_txq_t *pTxq)
{
  if (pTxq == NULL) {
    SFVMK_ERROR("NULL TXQ ptr");
    return VMK_FAILURE;
  }

  vmk_SpinlockLock(pTxq->lock);
  pTxq->flushState = SFVMK_FLUSH_STATE_DONE;
  vmk_SpinlockUnlock(pTxq->lock);

  return VMK_OK;
}


