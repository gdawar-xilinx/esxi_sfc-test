/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"

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

  /* For each TXQ_TYPE_IP_TCP_UDP_CKSUM TXQ there is one EVQ
   * EVQ-0 also handles:
   * Events for one (shared) TXQ_TYPE_NON_CKSUM TXQ
   * Events for one (shared) TXQ_TYPE_IP_CKSUM TXQ
   */
  pAdapter->numTxqsAllocated = MIN((SFVMK_TXQ_NTYPES - 1 + pAdapter->numEvqsAllocated),
                                    pAdapter->numTxqsAllotted);

  txqArraySize = sizeof(sfvmk_txq_t *) * pAdapter->numTxqsAllocated;

  pAdapter->ppTxq = vmk_HeapAlloc(sfvmk_modInfo.heapID, txqArraySize);
  if (pAdapter->ppTxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter,"vmk_HeapAlloc failed");
    status = VMK_NO_MEMORY;
    goto done;
  }
  vmk_Memset(pAdapter->ppTxq, 0, txqArraySize);

  status = sfvmk_txqInit(pAdapter, SFVMK_TXQ_TYPE_NON_CKSUM,
                         SFVMK_TXQ_TYPE_NON_CKSUM, evqIndex);
  if(status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_txqInit(%u) failed status: %s",
                        SFVMK_TXQ_TYPE_NON_CKSUM, vmk_StatusToString(status));
    goto failed_non_cksum_txq_init;
  }

  status = sfvmk_txqInit(pAdapter, SFVMK_TXQ_TYPE_IP_CKSUM,
                         SFVMK_TXQ_TYPE_IP_CKSUM, evqIndex);
  if(status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,"sfvmk_txqInit(%u) failed status: %s",
                        SFVMK_TXQ_TYPE_IP_CKSUM, vmk_StatusToString(status));
    goto failed_ip_cksum_txq_init;
  }

  /* Initialize SFVMK_TXQ_IP_TCP_UDP_CKSUM transmit queues */
  for (qIndex = SFVMK_TXQ_NTYPES - 1; qIndex < pAdapter->numTxqsAllocated;
       qIndex++, evqIndex++) {
    status = sfvmk_txqInit(pAdapter, qIndex, SFVMK_TXQ_TYPE_IP_TCP_UDP_CKSUM,
                           evqIndex);
    if (status) {
      SFVMK_ADAPTER_ERROR(pAdapter,"sfvmk_txqInit(%u) failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto failed_tcpudp_cksum_txq_init;
    }
  }

  goto done;

failed_tcpudp_cksum_txq_init:
  while (qIndex >= SFVMK_TXQ_NTYPES)
    sfvmk_txqFini(pAdapter, --qIndex);

  sfvmk_txqFini(pAdapter, SFVMK_TXQ_TYPE_IP_CKSUM);

failed_ip_cksum_txq_init:
  sfvmk_txqFini(pAdapter, SFVMK_TXQ_TYPE_NON_CKSUM);

failed_non_cksum_txq_init:
  pAdapter->numTxqsAllocated = 0;
  vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter->ppTxq);
  pAdapter->ppTxq = NULL;

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
