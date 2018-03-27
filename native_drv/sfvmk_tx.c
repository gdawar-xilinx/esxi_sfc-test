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
#define SFVMK_TXQ_STOP_TIME_OUT_USEC    (200 * SFVMK_TXQ_STOP_POLL_TIME_USEC)

#define SFVMK_TXQ_UNBLOCK_LEVEL(n)            (EFX_TXQ_LIMIT(n) / 4)
/* Number of desc needed for each SG */
#define SFVMK_TXD_NEEDED(sgSize, maxBufSize)  EFX_DIV_ROUND_UP(sgSize, maxBufSize)
/* utility macros for VLAN handling */
#define SFVMK_VLAN_PRIO_SHIFT                 13
#define SFVMK_VLAN_VID_MASK                   0x0fff
#define SFVMK_TX_TSO_DMA_DESC_MAX             EFX_TX_FATSOV2_DMA_SEGS_PER_PKT_MAX

static const
sfvmk_hdrParseCtrl_t sfvmk_tsoHdrList[] = {
  { VMK_PKT_HEADER_L2_ETHERNET_MASK, SFVMK_HDR_INFO_TYPE_UNUSED},
  { VMK_PKT_HEADER_L3_MASK,          SFVMK_HDR_INFO_TYPE_IP},
  { VMK_PKT_HEADER_L4_TCP,           SFVMK_HDR_INFO_TYPE_TCP},
  { 0,                               SFVMK_HDR_INFO_TYPE_UNUSED}
};

static const
sfvmk_hdrParseCtrl_t sfvmk_encapTsoHdrList[] = {
  { VMK_PKT_HEADER_L2_ETHERNET_MASK, SFVMK_HDR_INFO_TYPE_UNUSED},
  { VMK_PKT_HEADER_L3_MASK,          SFVMK_HDR_INFO_TYPE_IP},
  { VMK_PKT_HEADER_L4_UDP,           SFVMK_HDR_INFO_TYPE_UNUSED},
  { VMK_PKT_HEADER_ENCAP_VXLAN,      SFVMK_HDR_INFO_TYPE_UNUSED},
  { VMK_PKT_HEADER_L2_ETHERNET_MASK, SFVMK_HDR_INFO_TYPE_UNUSED},
  { VMK_PKT_HEADER_L3_MASK,          SFVMK_HDR_INFO_TYPE_ENCAP_IP},
  { VMK_PKT_HEADER_L4_TCP,           SFVMK_HDR_INFO_TYPE_TCP},
  { 0,                               SFVMK_HDR_INFO_TYPE_UNUSED}
};

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
  pTxq->hwVlanTci = 0;
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
  vmk_uint32            qIndex;
  vmk_uint32            txqArraySize = 0;
  const efx_nic_cfg_t   *pCfg = NULL;
  VMK_ReturnStatus      status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  VMK_ASSERT_NOT_NULL(pAdapter);
  pCfg = efx_nic_cfg_get(pAdapter->pNic);
  VMK_ASSERT_NOT_NULL(pCfg);

  if ((pCfg->enc_features & EFX_FEATURE_FW_ASSISTED_TSO_V2) &&
      ((pCfg->enc_fw_assisted_tso_v2_enabled) ||
       ((pCfg->enc_fw_assisted_tso_v2_encap_enabled) &&
        (pAdapter->isTunnelEncapSupported)))) {
    pAdapter->isTsoFwAssisted = VMK_TRUE;
  } else {
    pAdapter->isTsoFwAssisted = VMK_FALSE;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "enc_features[%x] enc_fw_assisted_tso_v2_enabled: %u,"
                      "isTsoFwAssisted: %u", pCfg->enc_features,
                      pCfg->enc_fw_assisted_tso_v2_enabled,
                      pAdapter->isTsoFwAssisted);

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

  /* Initialize all transmit queues with same processing capabilities (CSO/TSO)
   * on incoming packets. Queues will be started with call to efx_tx_qcreate 
   * with flags parameter including EFX_TXQ_FATSOV2, if HW supports FATSOv2. 
   * Then, whether checksum offload is required will be determined on per 
   * packet basis and if required, checksum offload option descriptor will
   * be created at run time if it doesn't exist on that queue.
   */
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
    if (pTxq == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL TXQ ptr for TXQ[%u]", qIndex);
      goto done;
    }

    vmk_SpinlockLock(pTxq->lock);

    if (pTxq->state != SFVMK_TXQ_STATE_STOPPING) {
      SFVMK_ADAPTER_ERROR(pAdapter, "TXQ[%u] is not in stopping state", qIndex);
      vmk_SpinlockUnlock(pTxq->lock);
      continue;
    }

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

    pEvq = pAdapter->ppEvq[qIndex];
    VMK_ASSERT_NOT_NULL(pEvq);

    sfvmk_txqComplete(pTxq, pEvq, &compCtx);
    if(pTxq->completed != pTxq->added)
      SFVMK_ADAPTER_ERROR(pAdapter, "pTxq->completed != pTxq->added");

    pTxq->added = 0;
    pTxq->pending = 0;
    pTxq->completed = 0;
    pTxq->reaped = 0;

    pTxq->state = SFVMK_TXQ_STATE_INITIALIZED;

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

done:
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
    if (pTxq == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL TXQ ptr for TXQ[%u]", qIndex);
      goto done;
    }

    vmk_SpinlockLock(pTxq->lock);

    if (pTxq->state != SFVMK_TXQ_STATE_STARTED) {
      SFVMK_ADAPTER_ERROR(pAdapter, "TXQ is not yet started");
      vmk_SpinlockUnlock(pTxq->lock);
      continue;
    }

    pTxq->state = SFVMK_TXQ_STATE_STOPPING;
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

done:
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
** \param[in]  pAdapter    Pointer to sfvmk_adapter_t
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
    pEvq = pAdapter->ppEvq[pTxq->index];
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
  if (pAdapter->isTsoFwAssisted == VMK_TRUE)
    flags |= EFX_TXQ_FATSOV2;

  /* Create the common code transmit queue. */
  status = efx_tx_qcreate(pAdapter->pNic, qIndex, 0, &pTxq->mem,
                          pAdapter->numTxqBuffDesc, 0, flags,
                          pEvq->pCommonEvq, &pTxq->pCommonTxq, &descIndex);
  if (status != VMK_OK) {
    if((status != VMK_NO_SPACE) || (~flags & EFX_TXQ_FATSOV2)) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_tx_qcreate(%u) failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto tx_qcreate_failed;
    }

    /* Looks like all FATSOv2 contexts are used */
    flags &= ~EFX_TXQ_FATSOV2;
    pAdapter->isTsoFwAssisted = VMK_FALSE;

    status = efx_tx_qcreate(pAdapter->pNic, qIndex, 0, &pTxq->mem,
                            pAdapter->numTxqBuffDesc, 0, flags,
                            pEvq->pCommonEvq, &pTxq->pCommonTxq, &descIndex);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_tx_qcreate(%u) failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto tx_qcreate_failed;
    }
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
  pTxq->isCso = VMK_TRUE;
  pTxq->isEncapCso = VMK_FALSE;
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
  /* Stop transmit queue(s) */
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

  if (VMK_UNLIKELY(vmk_SystemCheckState(VMK_SYSTEM_STATE_PANIC) == VMK_TRUE) &&
     (pEvq != pAdapter->ppEvq[0])) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                        "System in panic state, returning");
    goto done;
  }

  completed = pTxq->completed;
  while (completed != pTxq->pending) {
    sfvmk_txMapping_t *pTxMap;
    unsigned int id;

    id = completed++ & pTxq->ptrMask;
    pTxMap = &pTxq->pTxMap[id];

    if (pTxMap->sgElem.ioAddr != 0) {
      vmk_DMAUnmapElem(pAdapter->dmaEngine, VMK_DMA_DIRECTION_FROM_MEMORY, &pTxMap->sgElem);
    }

    if (pTxMap->pXmitPkt != NULL) {
      sfvmk_pktRelease(pAdapter, pCompCtx, pTxMap->pXmitPkt);
    }

    if (pTxMap->pOrigPkt != NULL) {
      sfvmk_pktRelease(pAdapter, pCompCtx, pTxMap->pOrigPkt);
    }

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

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
}

/*! \brief Estimate the number of tx descriptors required for this packet
**
** \param[in]       pTxq        pointer to txq
** \param[in]       pkt         pointer to packet handle
** \param[in, out]  pXmitInfo   pointer to transmit info structure
**
** \return: estimated number of tx descriptors
*/
static inline vmk_uint32
sfvmk_txDmaDescEstimate(sfvmk_txq_t *pTxq,
                        vmk_PktHandle *pkt,
                        sfvmk_xmitInfo_t *pXmitInfo)
{
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;
  vmk_uint32 numDesc = 0, i = 0;
  vmk_uint32 numElems = vmk_PktSgArrayGet(pkt)->numElems;
  const vmk_SgElem *pSge;

  for (i = 0; i < numElems; i++) {
     pSge = vmk_PktSgElemGet(pkt, i);
     numDesc += SFVMK_TXD_NEEDED(pSge->length, pAdapter->txDmaDescMaxSize);
  }

  if ((pXmitInfo->offloadFlag & SFVMK_TX_TSO) ||
      (pXmitInfo->offloadFlag & SFVMK_TX_ENCAP_TSO)) {
    /* For hw TSO, due to hardware constraint, headers and data must be
     * placed on different desc, so we assume 1 more desc is needed to separate
     * headers and data in the first SG. If the first SG contains exactly the
     * headers without any data, dmaDescsEst will be adjusted after parsing
     * the pkt.
     * Here we can only over-estimate the desc needed, which is fine. */
    numDesc += 1;
    pXmitInfo->dmaDescsEst = numDesc;


    /* This pkt will be fixed later if number of desc is beyond limit */
    if (numDesc > SFVMK_TX_TSO_DMA_DESC_MAX) {
      numDesc = SFVMK_TX_TSO_DMA_DESC_MAX;
    }

    numDesc += EFX_TX_FATSOV2_OPT_NDESCS;
  }

  /* +1 for VLAN optional descriptor */
  numDesc++;

  /* +1 for CSO optional descriptor */
  numDesc++;

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
  int i = 0, j = 0, descCount = 0;
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;
  vmk_Bool eop;
  vmk_DMAMapErrorInfo dmaMapErr;
  const vmk_SgElem *pSgElem;
  vmk_uint16 numElems;
  vmk_uint32 id, startID;
  vmk_SgElem inAddr, mappedAddr;
  vmk_ByteCountSmall elemLength, pktLenLeft, pktLen;
  sfvmk_txMapping_t *pTxMap = pTxq->pTxMap;
  vmk_uint32 nPendDescOri = pTxq->nPendDesc;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  id = startID = *pTxMapId;
  pktLen = pktLenLeft = vmk_PktFrameLenGet(pXmitPkt);
  numElems = vmk_PktSgArrayGet(pXmitPkt)->numElems;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "Start creating non-TSO desc, pXmitPkt=%p, "
                      "numElems=%d, startID=%d", pXmitPkt, numElems, startID);

  for (i = 0; i < numElems && pktLenLeft > 0; i ++) {
     pSgElem = vmk_PktSgElemGet(pXmitPkt, i);
     if (pSgElem == NULL) {
       vmk_AtomicInc64(&pTxq->stats[SFVMK_TXQ_SG_ELEM_GET_FAILED]);
       SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PktSgElemGet returned NULL");
       status = VMK_FAILURE;
       goto fail_map;
     }

     elemLength = pSgElem->length;
     if (elemLength > pAdapter->txDmaDescMaxSize) {
       vmk_AtomicInc64(&pTxq->stats[SFVMK_TXQ_SG_ELEM_TOO_LONG]);
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
        vmk_AtomicInc64(&pTxq->stats[SFVMK_TXQ_DMA_MAP_ERROR]);
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

  vmk_AtomicAdd64(&pTxq->stats[SFVMK_TXQ_BYTES], pktLen);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "non-TSO %u descriptors created, next startID = %u",
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
  vmk_uint16 thisTag=0;
  vmk_VlanID vlanId;
  vmk_VlanPriority vlanPrio;
  vmk_PktHandle *pkt = pXmitInfo->pXmitPkt;
  sfvmk_txMapping_t *pTxMap = pTxq->pTxMap;
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  if (pXmitInfo->offloadFlag & SFVMK_TX_VLAN) {
    vlanId = vmk_PktVlanIDGet(pkt);
    vlanPrio = vmk_PktPriorityGet(pkt);

    thisTag = (vlanId & SFVMK_VLAN_VID_MASK) | (vlanPrio << SFVMK_VLAN_PRIO_SHIFT);

    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                        "vlan_id: %d, prio: %d, tci: %d, hwVlanTci: %d",
                        vlanId, vlanPrio, thisTag, pTxq->hwVlanTci);

    if (thisTag == pTxq->hwVlanTci) {
        goto done;
    }

    efx_tx_qdesc_vlantci_create(pTxq->pCommonTxq,
                                vmk_CPUToBE16(thisTag),
                                &pTxq->pPendDesc[pTxq->nPendDesc ++]);
    pTxq->hwVlanTci = thisTag;
    goto tagged;
  }
  else {
    if (pTxq->hwVlanTci != 0) {
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                          "clear sticky desc for non-tagged traffic");
      efx_tx_qdesc_vlantci_create(pTxq->pCommonTxq, 0,
                                  &pTxq->pPendDesc[pTxq->nPendDesc ++]);
      pTxq->hwVlanTci = 0;
      goto tagged;
    }
    goto done;
  }

tagged:
   /* zero tx map array elem for option desc, to make sure completion process
    * doesn't try to clean-up.
    */
   vmk_Memset(&pTxMap[*pTxMapId], 0, sizeof(sfvmk_txMapping_t));
   *pTxMapId = (*pTxMapId + 1) & pTxq->ptrMask;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
  return;
}

/*! \brief Parse packet headers and fill in transmit info structure.
**
** \param[in]      pTxq        pointer to TXQ
** \param[in]      pkt         packet to be parsed
** \param[in,out]  pXmitInfo   pointer to transmit information structure
**
** \return: VMK_OK on success, one of the following error codes otherwise
**          VMK_NOT_FOUND: No header found matching the given criteria
**          VMK_LIMIT_EXCEEDED: Parsing failure due to truncated header
**          VMK_NOT_IMPLEMENTED: Parsing failure due to missing parser
**          VMK_NO_MEMORY: Failed to allocate memory for header copy
**          VMK_BAD_ADDR_RANGE: Header entry offset is above pkt frame length
**          VMK_FAILURE: Generic failure
*/
static inline VMK_ReturnStatus
sfvmk_fillXmitInfo(sfvmk_txq_t *pTxq,
                   vmk_PktHandle *pkt,
                   sfvmk_xmitInfo_t *pXmitInfo)
{
  VMK_ReturnStatus       status = VMK_FAILURE;
  sfvmk_adapter_t        *pAdapter = NULL;
  const  efx_nic_cfg_t   *pCfg = NULL;
  vmk_uint32             tcphOff = 0;
  vmk_uint32             totalHdrLen = 0;
  vmk_uint32             firstSgLen = 0;
  vmk_TCPHdr             *pTcpHdrData = NULL;
  vmk_uint16             hdrIndex;
  sfvmk_hdrInfoType_t    hdrInfoType;
  const sfvmk_hdrParseCtrl_t   *pExpectHdrList = NULL;
  sfvmk_hdrInfo_t hdrInfoArray[SFVMK_HDR_INFO_TYPE_MAX] = {{NULL, NULL},
                                                           {NULL, NULL},
                                                           {NULL, NULL}};
  sfvmk_hdrInfo_t *pIpHdr      = &hdrInfoArray[SFVMK_HDR_INFO_TYPE_IP];
  sfvmk_hdrInfo_t *pTcpHdr     = &hdrInfoArray[SFVMK_HDR_INFO_TYPE_TCP];
  sfvmk_hdrInfo_t *pEncapIpHdr = &hdrInfoArray[SFVMK_HDR_INFO_TYPE_ENCAP_IP];

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  VMK_ASSERT_NOT_NULL(pTxq);

  pAdapter = pTxq->pAdapter;
  VMK_ASSERT_NOT_NULL(pAdapter);

  pCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "pCfg pointer NULL");
    status = VMK_FAILURE;
    goto done;
  }

  pXmitInfo->pOrigPkt = pkt;

  if (vmk_PktIsInnerOffload(pkt)  &&
      (pXmitInfo->offloadFlag & SFVMK_TX_ENCAP_TSO)) {
    pExpectHdrList = sfvmk_encapTsoHdrList;
  }
  else if (pXmitInfo->offloadFlag & SFVMK_TX_TSO) {
    pExpectHdrList = sfvmk_tsoHdrList;
  }
  else {
    SFVMK_ADAPTER_ERROR(pAdapter, "Unexpected Offload flag : 0x%x",
                        pXmitInfo->offloadFlag);
    status = VMK_FAILURE;
    goto done;
  }

  for (hdrIndex = 0; pExpectHdrList[hdrIndex].expHdrType != 0; hdrIndex++) {
    vmk_PktHeaderEntry   *pHdrEntry = NULL;

    if ((status = vmk_PktHeaderEntryGet(pkt, hdrIndex, &pHdrEntry)) != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "Failed to get pkt header entry: %s"
                          "at index: %u",vmk_StatusToString(status), hdrIndex);
      goto done;
    }

    if ((pHdrEntry->type & pExpectHdrList[hdrIndex].expHdrType) !=
         pExpectHdrList[hdrIndex].expHdrType) {
      SFVMK_ADAPTER_ERROR(pAdapter, "Unexpected hdr type 0x%x. Expected = 0x%x",
      pHdrEntry->type, pExpectHdrList[hdrIndex].expHdrType);
      goto done;
    }

    hdrInfoType = pExpectHdrList[hdrIndex].hdrInfoType;
    if (hdrInfoType != SFVMK_HDR_INFO_TYPE_UNUSED) {
      hdrInfoArray[hdrInfoType].pHdrEntry = pHdrEntry;
      status = vmk_PktHeaderDataGet(pkt, pHdrEntry,
                                    (void **) &(hdrInfoArray[hdrInfoType].pMappedPtr));
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Failed to get pkt header: %s at index: %u",
                             vmk_StatusToString(status), hdrIndex);
        goto done;
      }
    }
  }

  if (pIpHdr->pHdrEntry->type == VMK_PKT_HEADER_L3_IPv4)
    pXmitInfo->packetId = ((vmk_IPv4Hdr *)pIpHdr->pMappedPtr)->identification;
  else if (pIpHdr->pHdrEntry->type == VMK_PKT_HEADER_L3_IPv6)
    pXmitInfo->packetId = 0;
  else {
    status = VMK_FAILURE;
    SFVMK_ADAPTER_ERROR(pAdapter, "Unexpected L3 header type: 0x%x,"
                        "expected is : 0x%x or 0x%x",
                        pIpHdr->pHdrEntry->type, VMK_PKT_HEADER_L3_IPv4,
                        VMK_PKT_HEADER_L3_IPv6);
    goto done;
  }

  if (pXmitInfo->offloadFlag & SFVMK_TX_ENCAP_TSO) {
    /* move packetId to outerPacketId as the packet is enncapsulated */
    pXmitInfo->outerPacketId = pXmitInfo->packetId;
    if (pEncapIpHdr->pHdrEntry->type == VMK_PKT_HEADER_L3_IPv4)
      pXmitInfo->packetId = ((vmk_IPv4Hdr *)pEncapIpHdr->pMappedPtr)->identification;
    else if (pEncapIpHdr->pHdrEntry->type == VMK_PKT_HEADER_L3_IPv6)
      pXmitInfo->packetId = 0;
    else {
      status = VMK_FAILURE;
      SFVMK_ADAPTER_ERROR(pAdapter, "Unexpected Encap L3 header type: 0x%x,"
                          "expected is : 0x%x or 0x%x",
                          pEncapIpHdr->pHdrEntry->type, VMK_PKT_HEADER_L3_IPv4,
                          VMK_PKT_HEADER_L3_IPv6);
      goto done;
    }
  }

  /* SF NIC cannot use FW assisted TSO if TCP header offset is beyond limit(208)
   * In practice, max offset of TCP header should be
   * Ethernet(14) + ipv4(20~60)/ipv6(40) + ipv6 extension header +
   * UDP(8) + VXLAN(8) + Ethernet(14) + ip4(20~60)/6(40) for vxlan pkts
   * Since we don't claim any offload for IPv6 with extension headers, it's
   * impossible to exceed enc_tx_tso_tcp_header_offset_limit(208), so simply
   * drop the pkt. Refer section 2.3.3 of Doxbox doc SF-108452-SW for details.
   */

  tcphOff = pTcpHdr->pHdrEntry->nextHdrOffset;

  if (VMK_UNLIKELY(tcphOff > pCfg->enc_tx_tso_tcp_header_offset_limit)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Tcp hdr offset: %u beyond limit: %u",
                        tcphOff, pCfg->enc_tx_tso_tcp_header_offset_limit);
    status = VMK_FAILURE;
    goto done;
  }

  pTcpHdrData = (vmk_TCPHdr *)pTcpHdr->pMappedPtr;
  if(pTcpHdrData->syn || pTcpHdrData->urg) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Incompatible TCP flag 0x%x on TSO packet",
                        (pTcpHdrData->flags));
    status = VMK_FAILURE;
    goto done;
  }

  pXmitInfo->seqNumNbo = vmk_CPUToBE32(pTcpHdrData->seq);

  /* Check if need to defragment headers to leverage hw TSO */
  firstSgLen = vmk_PktSgElemGet(pkt, 0)->length;
  totalHdrLen = pTcpHdr->pHdrEntry->nextHdrOffset;

  if (firstSgLen < totalHdrLen) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                        "Pkt headers are fragmented, headerLen = %u, first SG len = %u",
                        totalHdrLen, firstSgLen);
    pXmitInfo->fixFlag |= SFVMK_TSO_DEFRAG_HEADER;
  }

  /* Correct the over-estimation if the first SG fully covers all the headers
   * without any payload data.
   */
  if (firstSgLen == totalHdrLen) {
    pXmitInfo->dmaDescsEst -= 1;
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                        "Estimated number of DMA desc adjusted to %u",
                         pXmitInfo->dmaDescsEst);
  }

  /* Check if the number of desc is beyond limit */
  if (pXmitInfo->dmaDescsEst > SFVMK_TX_TSO_DMA_DESC_MAX) {
    pXmitInfo->fixFlag |= SFVMK_TSO_DEFRAG_SGES;
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                        "Estimated no of DMA desc[%u] beyond limit of FATSOv2",
                        pXmitInfo->dmaDescsEst);
  }

  pXmitInfo->headerLen = totalHdrLen;
  pXmitInfo->firstSgLen = firstSgLen;
  pXmitInfo->mss = vmk_PktGetLargeTcpPacketMss(pkt);

done:
  if (pIpHdr->pMappedPtr)
    vmk_PktHeaderDataRelease(pkt, pIpHdr->pHdrEntry,
                             (void *)pIpHdr->pMappedPtr, VMK_FALSE);

  if (pEncapIpHdr->pMappedPtr)
    vmk_PktHeaderDataRelease(pkt, pEncapIpHdr->pHdrEntry,
                             (void *)pEncapIpHdr->pMappedPtr, VMK_FALSE);

  if (pTcpHdr->pMappedPtr)
    vmk_PktHeaderDataRelease(pkt, pTcpHdr->pHdrEntry,
                             (void *)pTcpHdr->pMappedPtr, VMK_FALSE);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);

  return status;
}

/*! \brief Re-structure the packet to fix FATSOv2 constraint violations.
**
** \param[in]      pTxq        pointer to TXQ
** \param[in,out]  pXmitInfo   pointer to transmit information structure
**
** \return: VMK_OK in case of success, VMK_FAILURE otherwise
*/
static inline VMK_ReturnStatus
sfvmk_hwTsoFixPkt(sfvmk_txq_t *pTxq,
                  sfvmk_xmitInfo_t *pXmitInfo)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_adapter_t  *pAdapter = pTxq->pAdapter;
  vmk_uint32       numElems = 0;
  vmk_uint32       newNumElems = 0;
  vmk_uint32       elemLen = 0;
  vmk_uint32       copyBytes = 0;
  vmk_uint32       copiedEsti = 0;
  vmk_uint32       oldEsti = 0;
  vmk_uint32       descMaxSz = pAdapter->txDmaDescMaxSize;
  vmk_PktHandle    *pOrigPkt = pXmitInfo->pOrigPkt;
  vmk_PktHandle    *pXmitPkt = NULL;
  vmk_Bool         headerSaved = (pXmitInfo->headerLen == pXmitInfo->firstSgLen);
  vmk_uint32       i = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);

  numElems = vmk_PktSgArrayGet(pOrigPkt)->numElems;
  VMK_ASSERT(numElems > 1);

  copyBytes = vmk_PktSgElemGet(pOrigPkt, 0)->length;
  copiedEsti = SFVMK_TXD_NEEDED(copyBytes, descMaxSz);

  for (i = 1; i < numElems; i++) {
    elemLen = vmk_PktSgElemGet(pOrigPkt, i)->length;
    copyBytes += elemLen;

    /* Try to reduce number of SG */
    if (pXmitInfo->fixFlag & SFVMK_TSO_DEFRAG_SGES) {
      oldEsti = SFVMK_TXD_NEEDED(elemLen, descMaxSz) + copiedEsti;
      copiedEsti = SFVMK_TXD_NEEDED(copyBytes, descMaxSz);

      /* By merging more SG, number of SG are decreasing */
      VMK_ASSERT(oldEsti >= copiedEsti);
      pXmitInfo->dmaDescsEst -= (oldEsti - copiedEsti);

      /* If SG[0] holds exactly header, combining SG[0] and SG[1] results
       * SG[0] holding header + data, which will be splitted again.
       * Hence revert the adjustment which was done in sfvmk_parseXmitInfo.
       *
       * In case where headerSaved is false, dmaDescsEst already considered
       * the extra SG needed for header splitting in sfvmk_txDmaDescEstimate.
       */
      if (i == 1 && headerSaved) {
        pXmitInfo->dmaDescsEst += 1;
      }

      if (pXmitInfo->dmaDescsEst <= SFVMK_TX_TSO_DMA_DESC_MAX) {
        pXmitInfo->fixFlag &= ~SFVMK_TSO_DEFRAG_SGES;
      }
    }

    /* Try to merge SGs which have header content */
    if ((pXmitInfo->fixFlag & SFVMK_TSO_DEFRAG_HEADER) &&
        (copyBytes >= pXmitInfo->headerLen)) {
       pXmitInfo->fixFlag &= ~SFVMK_TSO_DEFRAG_HEADER;
    }

    /* All fixed! */
    if (!(pXmitInfo->fixFlag
          & (SFVMK_TSO_DEFRAG_HEADER | SFVMK_TSO_DEFRAG_SGES))) {
       break;
    }
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                        "Iteration[%u] copyBytes: %u, copyEst: %u, oldEst: %u",
                        i, copyBytes, copiedEsti, oldEsti);
  }

  /*
   * As the max IP packet can be 65535 bytes and FW supports
   * SFVMK_TX_TSO_DMA_DESC_MAX (24) buffers of size 16383 (16K-1) each,
   * packet will eventually get fixed by this algorithm */
  VMK_ASSERT_EQ(pXmitInfo->fixFlag & SFVMK_TSO_DEFRAG_SGES, 0);
  VMK_ASSERT_EQ(pXmitInfo->fixFlag & SFVMK_TSO_DEFRAG_HEADER, 0);

  /*
   * Frame contents of pXmitPkt up till copyBytes will be modifiable but
   * contents beyond that will be referring to buffers shared with pOrigPkt */
  status = vmk_PktPartialCopy(pOrigPkt, copyBytes, &pXmitPkt);
  if (status != VMK_OK) {
    vmk_AtomicInc64(&pTxq->stats[SFVMK_TXQ_PARTIAL_COPY_FAILED]);
    SFVMK_ADAPTER_ERROR(pAdapter, "Partial copy[%p] failed[%s], numBytes: %u",
                        pOrigPkt, vmk_StatusToString(status), copyBytes);
    goto done;
  }

  pXmitInfo->pXmitPkt = pXmitPkt;
  newNumElems = vmk_PktSgArrayGet(pXmitPkt)->numElems;
  VMK_ASSERT(newNumElems <= numElems);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "Pkt fixed, pOrigPkt = %p, pXmitPkt = %p,"
                      "copyBytes = %u, numElems = %u new numElems = %u",
                      pOrigPkt, pXmitPkt, copyBytes, numElems, newNumElems);
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
  return status;
}

/*! \brief Create the option descriptor required for FATSOv2
**
** \param[in]      pTxq        pointer to TXQ
** \param[in,out]  pXmitInfo   pointer to transmit information structure
** \param[in,out]  pId         pointer to buffer descriptor index
**
** \return: void
*/
static inline void
sfvmk_hwTsoOptDesc(sfvmk_txq_t *pTxq,
                   sfvmk_xmitInfo_t *pXmitInfo,
                   vmk_uint32 *pId)
{
   sfvmk_adapter_t *pAdapter = pTxq->pAdapter;
   efx_desc_t *desc = &pTxq->pPendDesc[pTxq->nPendDesc];

   efx_tx_qdesc_tso2_create(pTxq->pCommonTxq,
                            pXmitInfo->packetId,
                            pXmitInfo->outerPacketId,
                            pXmitInfo->seqNumNbo,
                            pXmitInfo->mss,
                            desc,
                            EFX_TX_FATSOV2_OPT_NDESCS);

   pTxq->nPendDesc += EFX_TX_FATSOV2_OPT_NDESCS;
   *pId = (*pId + EFX_TX_FATSOV2_OPT_NDESCS) & pTxq->ptrMask;

   SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                       "TSO opt desc pktID = %u, outerPktID = %u,"
                       "seqNumNbo = %u, mss = %u",
                       pXmitInfo->packetId, pXmitInfo->outerPacketId,
                       pXmitInfo->seqNumNbo, pXmitInfo->mss);
}

/*! \brief Prepare and dispatch packet using FATSOv2
**
** \param[in]      pTxq        pointer to TXQ
** \param[in,out]  pXmitInfo   pointer to transmit information structure
** \param[in,out]  pTxMapId    pointer to txMap ID
**
** \return: VMK_OK on success, one of the following error codes otherwise
**          VMK_BAD_PARAM: The specified DMA engine is invalid
**          VMK_DMA_MAPPING_FAILED: DMA constraints couldn't be met for mapping
**          VMK_NO_MEMORY: Insufficient memory available to construct the mapping
**          VMK_FAILURE: Generic failure
*/
static VMK_ReturnStatus
sfvmk_txHwTso(sfvmk_txq_t *pTxq,
              sfvmk_xmitInfo_t *pXmitInfo,
              vmk_uint32 *pTxMapId)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;
  vmk_uint16 numElems = 0;
  vmk_ByteCountSmall pktLen = 0;
  vmk_ByteCountSmall elemLength = 0;
  vmk_ByteCountSmall pktLenLeft = 0;
  vmk_ByteCountSmall elemBytesLeft = 0;
  vmk_ByteCountSmall offset = 0;
  vmk_ByteCountSmall descLen = 0;
  sfvmk_txMapping_t *pTxMap = NULL;
  const vmk_SgElem *pSgElem = NULL;
  vmk_SgElem inAddr, mappedAddr;
  vmk_Bool eop, first;
  vmk_DMAMapErrorInfo dmaMapErr;
  vmk_uint32 startID = *pTxMapId, id = *pTxMapId;
  vmk_uint32 i = 0, j = 0, descCount = 0;
  vmk_uint32 nPendDescOri = pTxq->nPendDesc;

  vmk_PktHandle *pOrigPkt = pXmitInfo->pOrigPkt;
  vmk_PktHandle *pXmitPkt = pXmitInfo->pXmitPkt;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);
  VMK_ASSERT_NOT_NULL(pAdapter);

  pTxMap = pTxq->pTxMap;
  pktLenLeft = pktLen = vmk_PktFrameLenGet(pXmitPkt);
  numElems = vmk_PktSgArrayGet(pXmitPkt)->numElems;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "TSO desc, pXmitPkt=%p, pOrigPkt=%p, numElems=%u "
                      "mss=%u, headerLen=%u, pktId=%u, outerPktId=%u, startID=%u",
                      pXmitPkt, pOrigPkt, numElems, pXmitInfo->mss,
                      pXmitInfo->headerLen, pXmitInfo->packetId,
                      pXmitInfo->outerPacketId, startID);

  /* options desc */
  sfvmk_hwTsoOptDesc(pTxq, pXmitInfo, &id);

  /* skip option headers */
  for (i = startID; i < startID + EFX_TX_FATSOV2_OPT_NDESCS; i++) {
    vmk_Memset(&pTxMap[i & pTxq->ptrMask], 0, sizeof(sfvmk_txMapping_t));
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                        "Skipping txMap[%u] for opt desc", i & pTxq->ptrMask);
  }

  for (i = 0; i < numElems && pktLenLeft > 0; i++) {
    pSgElem = vmk_PktSgElemGet(pXmitPkt, i);
    elemLength = MIN(pktLenLeft, pSgElem->length);
    pktLenLeft -= elemLength;

    inAddr.addr   = pSgElem->addr;
    inAddr.length = elemLength;

    status = vmk_DMAMapElem(pAdapter->dmaEngine,
                            VMK_DMA_DIRECTION_FROM_MEMORY,
                            &inAddr, VMK_TRUE,
                            &mappedAddr, &dmaMapErr);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,"Failed to map pkt %p size %u: %s",
                          pXmitPkt, pktLen,
                          vmk_DMAMapErrorReasonToString(dmaMapErr.reason));
      vmk_AtomicInc64(&pTxq->stats[SFVMK_TXQ_DMA_MAP_ERROR]);
      goto fail_map;
    }

    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                        "sge[%u] DMA mapped, ioa = %lx, len = %u", i,
                        mappedAddr.ioAddr, mappedAddr.length);

    elemBytesLeft = mappedAddr.length;
    if(elemBytesLeft != elemLength) {
       SFVMK_ADAPTER_ERROR(pAdapter,"Mapped len [%u] != input len [%u]",
                           elemBytesLeft, elemLength);
       status = VMK_FAILURE;
       goto fail_map;
    }

    first = VMK_TRUE;
    while (elemBytesLeft > 0) {
      descLen = MIN(elemBytesLeft, pAdapter->txDmaDescMaxSize);
      /* pkt headers need a separate DMA desc */
      if ((i == 0) && first) {
        VMK_ASSERT(pXmitInfo->headerLen <= descLen);
        descLen = pXmitInfo->headerLen;
      }

      offset = mappedAddr.length - elemBytesLeft;
      elemBytesLeft -= descLen;

      /* for each DMA SG, only keeps IOA/len in the first pTxMap. */
      if (first) {
        pTxMap[id].pOrigPkt       = NULL;
        pTxMap[id].pXmitPkt       = NULL;
        pTxMap[id].sgElem.ioAddr = mappedAddr.ioAddr;
        pTxMap[id].sgElem.length = mappedAddr.length;
        first = VMK_FALSE;
      }
      else {
        vmk_Memset(&pTxMap[id], 0, sizeof(sfvmk_txMapping_t));
      }

      /* The last txMap for this pkt keeps the pkt pointer */
      eop = ((i == numElems - 1) || (pktLenLeft == 0)) && (!elemBytesLeft);
      if (eop) {
        /* If pXmitPkt and pOrigPkt are the same, keep .pOrigPkt field NULL
         * to make sure the completion process doesn't try to release it
         * twice.
         */
        pTxMap[id].pXmitPkt = pXmitPkt;
        pTxMap[id].pOrigPkt = (pXmitPkt == pOrigPkt) ? NULL : pOrigPkt;
      }

      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                          "txMap[%u]: pXmitPkt=%p, pOrigPkt=%p, ioAddr=0x%lx "
                          "length=%u",id, pTxMap[id].pXmitPkt, pTxMap[id].pOrigPkt,
                          pTxMap[id].sgElem.ioAddr, pTxMap[id].sgElem.length);

      /* create DMA desc */
      sfvmk_createDmaDesc(pTxq, mappedAddr.ioAddr + offset, descLen, eop, &id);
    }
  }

  descCount = pTxq->nPendDesc - nPendDescOri;
  VMK_ASSERT(descCount <= pXmitInfo->dmaDescsEst + EFX_TX_FATSOV2_OPT_NDESCS);

  vmk_AtomicAdd64(&pTxq->stats[SFVMK_TXQ_BYTES], pktLen);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "FATSO done: created %u desc, next startID = %u",
                      descCount, id);

  *pTxMapId = id;
  status = VMK_OK;
  goto done;

fail_map:
   descCount = pTxq->nPendDesc - nPendDescOri;
   pTxq->nPendDesc -= descCount;
   for (i = startID; i < startID + descCount; i++) {
     j = i & pTxq->ptrMask;
     if (pTxMap[j].sgElem.ioAddr) {
       vmk_DMAUnmapElem(pTxq->pAdapter->dmaEngine,
                        VMK_DMA_DIRECTION_FROM_MEMORY, &pTxMap[j].sgElem);
     }
   }

done:
   SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_TX);
   return status;
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

/*! \brief  Create checksum offload option descriptor
**
** \param[in]  pTxq        pointer to txq
** \param[in]  isCso       whether to enable or disable cso
** \param[in]  isEncapCso  whether to enable or disable Encapcso
**
** \return: void
**
*/
static void
sfvmk_txCreateCsumDesc(sfvmk_txq_t *pTxq, vmk_Bool isCso, vmk_Bool isEncapCso) {

  vmk_uint16 flags = 0;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_TX);
  SFVMK_ADAPTER_DEBUG(pTxq->pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "isCso: %u, isEncapCso: %u", isCso, isEncapCso);

  if(isCso)
    flags = EFX_TXQ_CKSUM_TCPUDP | EFX_TXQ_CKSUM_IPV4;

  if(isEncapCso)
    flags |= EFX_TXQ_CKSUM_INNER_TCPUDP | EFX_TXQ_CKSUM_INNER_IPV4;

  efx_tx_qdesc_checksum_create(pTxq->pCommonTxq,
                               flags,
                               &pTxq->pPendDesc[pTxq->nPendDesc ++]);

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_TX);
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
   vmk_Bool isCso = VMK_FALSE;
   vmk_Bool isEncapCso = VMK_FALSE;

   SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_TX);
   VMK_ASSERT(pTxq->nPendDesc == 0);

   /* VLAN handling */
   sfvmk_txMaybeInsertTag(pTxq, pXmitInfo, &txMapId);

   if ((vmk_PktIsInnerOffload(pXmitInfo->pXmitPkt) == VMK_TRUE) &&
       (pAdapter->isTunnelEncapSupported)) {
     isCso = vmk_PktIsLargeTcpPacket(pXmitInfo->pXmitPkt) ||
             vmk_PktIsMustOuterCsum(pXmitInfo->pXmitPkt);

     isEncapCso = vmk_PktIsInnerLargeTcpPacket(pXmitInfo->pXmitPkt) ||
                  vmk_PktIsMustInnerCsum(pXmitInfo->pXmitPkt);
   }
   else {
     isCso = vmk_PktIsLargeTcpPacket(pXmitInfo->pXmitPkt) ||
             vmk_PktIsMustCsum(pXmitInfo->pXmitPkt);
     isEncapCso = 0;
   }

   if ((pTxq->isCso != isCso) || (pTxq->isEncapCso != isEncapCso)) {
     sfvmk_txCreateCsumDesc(pTxq, isCso, isEncapCso);
     pTxq->isCso = isCso;
     pTxq->isEncapCso = isEncapCso;
     /* for option descriptors, make sure txqComplete doesn't try clean-up */
     vmk_Memset(&pTxq->pTxMap[txMapId], 0, sizeof(sfvmk_txMapping_t));
     txMapId = (txMapId + 1) & pTxq->ptrMask;
   }

   /* TSO handling*/
   if ((pXmitInfo->offloadFlag & SFVMK_TX_TSO) ||
       (pXmitInfo->offloadFlag & SFVMK_TX_ENCAP_TSO)) {
     status = sfvmk_txHwTso(pTxq, pXmitInfo, &txMapId);
     if (status != VMK_OK) {
       SFVMK_ADAPTER_ERROR(pAdapter, "pkt[%p] tx TSO failed: %s",
                           pXmitInfo->pXmitPkt, vmk_StatusToString(status));
       goto done;
      }
   } else {
     status = sfvmk_txNonTsoPkt(pTxq, pXmitInfo->pXmitPkt, &txMapId);
     if (status != VMK_OK) {
       SFVMK_ADAPTER_ERROR(pAdapter, "pkt[%p] tx failed: %s",
                           pXmitInfo->pXmitPkt, vmk_StatusToString(status));
       goto done;
     }
   }

   /* Post the pSgElemment list. */
   status = sfvmk_txqListPost(pTxq);
   if (status != VMK_OK) {
     vmk_AtomicInc64(&pTxq->stats[SFVMK_TXQ_DESC_POST_FAILED]);
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
  xmitInfo.offloadFlag |= vmk_PktIsLargeTcpPacket(pkt) ? SFVMK_TX_TSO : 0;
  if (pAdapter->isTunnelEncapSupported)
    xmitInfo.offloadFlag |= vmk_PktIsInnerLargeTcpPacket(pkt) ? SFVMK_TX_ENCAP_TSO : 0;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                      "Xmit start, pkt = %p, TX VLAN offload %s needed, "
                      "TSO %sabled ENCAP_TSO %sabled numElems = %u,"
                      "pktLen = %u", pkt,
                      xmitInfo.offloadFlag & SFVMK_TX_VLAN ? "" : "not ",
                      xmitInfo.offloadFlag & SFVMK_TX_TSO  ? "en" : "dis",
                      xmitInfo.offloadFlag & SFVMK_TX_ENCAP_TSO  ? "en" : "dis",
                      vmk_PktSgArrayGet(pkt)->numElems,
                      vmk_PktFrameLenGet(pkt));

  /* Do estimation as early as possible */
  nTotalDesc = sfvmk_txDmaDescEstimate(pTxq, pkt, &xmitInfo);

  pushed = pTxq->added;

  /* Check if need to stop the queue */
  vmk_CPUMemFenceWrite();
  sfvmk_txqReap(pTxq);
  if (pTxq->added - pTxq->reaped + nTotalDesc > EFX_TXQ_LIMIT(pTxq->numDesc)) {
    VMK_ASSERT(sfvmk_isTxqStopped(pAdapter, pTxq->index) == VMK_FALSE,
               "Txq index = %u", pTxq->index);
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

  xmitInfo.pOrigPkt = NULL;
  xmitInfo.pXmitPkt = pkt;

  if ((xmitInfo.offloadFlag & SFVMK_TX_TSO) ||
      (xmitInfo.offloadFlag & SFVMK_TX_ENCAP_TSO)) {
    status = sfvmk_fillXmitInfo(pTxq, pkt, &xmitInfo);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "failed to parse xmit pkt info");
      vmk_AtomicInc64(&pTxq->stats[SFVMK_TXQ_TSO_PARSING_FAILED]);
      goto done;
    }

    /* fix pkt to meet hw TSO requirements */
    if (xmitInfo.fixFlag & (SFVMK_TSO_DEFRAG_HEADER | SFVMK_TSO_DEFRAG_SGES)) {
      VMK_ASSERT(xmitInfo.offloadFlag & SFVMK_TX_TSO);

      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_DBG,
                          "Pkt fix needed, type = %u", xmitInfo.fixFlag);

      status = sfvmk_hwTsoFixPkt(pTxq, &xmitInfo);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Failed to fix pkt for hw TSO");
        goto done;
      }

      /* A new partial copied pkt should have been generated in pXmitPkt */
      VMK_ASSERT(xmitInfo.pOrigPkt != xmitInfo.pXmitPkt);
    }
  }

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

