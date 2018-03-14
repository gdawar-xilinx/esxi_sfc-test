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

#define SFVMK_MIN_PKT_SIZE  60

/* Value for batch processing */
#define SFVMK_REFILL_BATCH  64

/* Wait time  for common RXQ to stop */
#define SFVMK_RXQ_STOP_POLL_TIME_USEC   VMK_USEC_PER_MSEC
#define SFVMK_RXQ_STOP_TIME_OUT_USEC    (200 * SFVMK_RXQ_STOP_POLL_TIME_USEC)

/* Refill threshold */
#define	RX_REFILL_THRESHOLD(_entries)	(EFX_RXQ_LIMIT(_entries) * 9 / 10)
/* Refill delay */
#define SFVMK_RXQ_REFILL_DELAY_MS       10

/*! \brief    Configure RSS by setting hash key, indirection table
**            and scale mode.
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
** \param[in]  pKey         pointer to hash key
** \param[in]  keySize      size of the key
** \param[in]  pIndTable    pointer to indirection table
** \param[in]  indTableSize indirection table size
**
** \return: VMK_OK on success or error code otherwise
*/
VMK_ReturnStatus
sfvmk_configRSS(sfvmk_adapter_t *pAdapter,
                vmk_uint8 *pKey,
                vmk_uint32 keySize,
                vmk_uint32 *pIndTable,
                vmk_uint32 indTableSize)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  efx_rx_scale_context_type_t supportRSS;
  const efx_nic_cfg_t *pNicCfg = NULL;
  vmk_uint32 rssModes;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_RX);

  VMK_ASSERT_NOT_NULL(pAdapter);

  if (pAdapter->state != SFVMK_ADAPTER_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter is not yet started");
    goto done;
  }

  status = efx_rx_scale_default_support_get(pAdapter->pNic, &supportRSS);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_rx_scale_default_support_get failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  if (supportRSS != EFX_RX_SCALE_EXCLUSIVE) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RSS is not supported");
    status = VMK_FAILURE;
    goto done;
  }

  if (indTableSize > EFX_RSS_TBL_SIZE) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid ind table size(%u)", indTableSize);
    status = VMK_FAILURE;
    goto done;
  }

  status = efx_rx_scale_tbl_set(pAdapter->pNic, EFX_RSS_CONTEXT_DEFAULT,
                                pIndTable, indTableSize);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_rx_scale_tbl_set failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  rssModes = EFX_RX_HASH_IPV4 | EFX_RX_HASH_TCPIPV4;

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg != NULL) {
    if (pNicCfg->enc_features & EFX_FEATURE_IPV6)
      rssModes |= EFX_RX_HASH_IPV6 | EFX_RX_HASH_TCPIPV6;
  }

  status = efx_rx_scale_mode_set(pAdapter->pNic,
                                 EFX_RSS_CONTEXT_DEFAULT,
                                 EFX_RX_HASHALG_TOEPLITZ,
                                 rssModes, B_TRUE);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_rx_scale_mode_set failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  status = efx_rx_scale_key_set(pAdapter->pNic,
                                EFX_RSS_CONTEXT_DEFAULT,
                                pKey, keySize);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_rx_scale_key_set failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX);

  return status;
}

/*! \brief     Initialize all resources required for a RXQ
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex       RXQ Index
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus
sfvmk_rxqInit(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  sfvmk_rxq_t *pRxq = NULL;
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_RX, "qIndex[%u]" , qIndex);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Check if qIndex is valid. */
  if (qIndex >= pAdapter->numRxqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid RXQ index %u", qIndex);
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Allocate memory for RXQ data struture */
  pRxq = vmk_HeapAlloc(sfvmk_modInfo.heapID, sizeof(sfvmk_rxq_t));
  if(pRxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_HeapAlloc failed");
    status = VMK_NO_MEMORY;
    goto done;
  }
  vmk_Memset(pRxq, 0, sizeof(sfvmk_rxq_t));

  pRxq->pAdapter = pAdapter;
  pRxq->index = qIndex;

  pRxq->state = SFVMK_RXQ_STATE_INITIALIZED;

  pAdapter->ppRxq[qIndex] = pRxq;

  status = VMK_OK;
  goto done;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX, "qIndex[%u]" , qIndex);

  return status;
}

/*! \brief     Destroy all resources acquired by a RXQ
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  qIndex      RXQ Index
**
** \return: void
*/
static void
sfvmk_rxqFini(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  sfvmk_rxq_t *pRxq = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_RX, "qIndex[%u]" , qIndex);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  if (qIndex >= pAdapter->numRxqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", qIndex);
    goto done;
  }

  pRxq = pAdapter->ppRxq[qIndex];
  if (pRxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL RXQ ptr");
    goto done;
  }

  /* Check if RXQ is initialized. */
  if (pRxq->state != SFVMK_RXQ_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RXQ is not initialized");
    goto done;
  }

  vmk_HeapFree(sfvmk_modInfo.heapID, pRxq);

  pAdapter->ppRxq[qIndex] = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX, "qIndex[%u]" , qIndex);
}

/*! \brief     Initialize all resources required for RX module
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
**
** @return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_rxInit(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 qIndex;
  vmk_uint32 rxqArraySize;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_RX);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  pAdapter->isRxCsumEnabled = VMK_TRUE;
  pAdapter->numRxqsAllocated = MIN(pAdapter->numEvqsAllocated,
                                   pAdapter->numRxqsAllotted);

  if (pAdapter->numRxqsAllocated > sfvmk_getRSSQStartIndex(pAdapter))
    pAdapter->numRSSQs = pAdapter->numRxqsAllocated - sfvmk_getRSSQStartIndex(pAdapter);

  rxqArraySize = sizeof(sfvmk_rxq_t *) * pAdapter->numRxqsAllocated;

  pAdapter->ppRxq = vmk_HeapAlloc(sfvmk_modInfo.heapID, rxqArraySize);
  if (pAdapter->ppRxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HeapAlloc failed");
    status = VMK_NO_MEMORY;
    goto failed_rxq_alloc;
  }
  vmk_Memset(pAdapter->ppRxq, 0, rxqArraySize);

  /* Initialize the receive queue(s) - one per interrupt. */
  for (qIndex = 0; qIndex < pAdapter->numRxqsAllocated; qIndex++) {
    if ((status = sfvmk_rxqInit(pAdapter, qIndex)) != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,"sfvmk_rxqInit(%u) failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto failed_rxq_init;
    }
  }

  /* Set default RXQ index */
  pAdapter->defRxqIndex = 0;

  goto done;

failed_rxq_init:
  /* Tear down the receive queue(s). */
  while (qIndex > 0)
    sfvmk_rxqFini(pAdapter, --qIndex);

  vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter->ppRxq);
  pAdapter->ppRxq = NULL;

failed_rxq_alloc:
  pAdapter->numRxqsAllocated = 0;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX);

  return status;
}

/*! \brief     Destroy all resources acquired by RX module
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
**
** \return: void
*/
void sfvmk_rxFini(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 qIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_RX);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  qIndex = pAdapter->numRxqsAllocated;
  while (qIndex > 0)
    sfvmk_rxqFini(pAdapter, --qIndex);

  pAdapter->numRxqsAllocated = 0;

  vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter->ppRxq);
  pAdapter->ppRxq = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX);
}

/*! \brief Function to be invoked by helper world queue to trigger
**         sw event for filling rx buffers.
**
** \param[in]  data    vmk_AddrCookie pointing to RXQ
**
** \return: None
**
*/
static void sfvmk_rxqFillHelper(vmk_AddrCookie data)
{
  sfvmk_rxq_t *pRxq = (sfvmk_rxq_t *)data.ptr;
  sfvmk_adapter_t *pAdapter;
  sfvmk_evq_t *pEvq;
  vmk_uint16 magic;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_RX);

  VMK_ASSERT_NOT_NULL(pRxq);

  pAdapter = pRxq->pAdapter;
  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pAdapter->ppEvq);

  pEvq = pAdapter->ppEvq[pRxq->index];
  VMK_ASSERT_NOT_NULL(pEvq);

  /* TODO Take adapter lock */
  if (pRxq->state != SFVMK_RXQ_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RXQ[%u] is not yet started", pRxq->index);
    goto done;
  }

  magic = SFVMK_SW_EV_MAGIC(SFVMK_SW_EV_RX_QREFILL);
  efx_ev_qpost(pEvq->pCommonEvq, magic);

done:
  /* TODO Release adapter lock */
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_RX);
}

/*! \brief Fuction to submit request for filling up rx buffer.
**
** \param[in] pRxq  Pointer to sfvmk_rxq_t
**
** \return: VMK_OK on success or error code on failure
**
*/
VMK_ReturnStatus
sfvmk_rxScheduleRefill(sfvmk_rxq_t *pRxq)
{
  vmk_HelperRequestProps props = {0};
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_RX);

  VMK_ASSERT_NOT_NULL(pRxq);
  VMK_ASSERT_NOT_NULL(pRxq->pAdapter);

  /* Create a request and submit */
  props.requestMayBlock = VMK_FALSE;
  props.tag = (vmk_AddrCookie)NULL;
  props.cancelFunc = NULL;
  props.worldToBill = VMK_INVALID_WORLD_ID;
  status = vmk_HelperSubmitDelayedRequest(pRxq->pAdapter->helper,
                                          sfvmk_rxqFillHelper,
                                          (vmk_AddrCookie *)pRxq,
                                          pRxq->refillDelay,
                                          &props);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pRxq->pAdapter, "vmk_HelperSubmitDelayedRequest failed status: %s",
                        vmk_StatusToString(status));
  }

  pRxq->refillDelay *= 2;

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_RX);

  return status;
}

/*! \brief   Deliver RX pkt to uplink layer
**
** \param[in]  pAdapter    Pointer to sfvmk_adapter_t
** \param[in]  pRxDesc     RX descriptor
** \param[in]  qIndex      RXQ Index
**
** \return: void
*/
void sfvmk_rxDeliver(sfvmk_adapter_t *pAdapter,
                     sfvmk_rxSwDesc_t *pRxDesc,
                     vmk_uint32 qIndex)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_PktHandle *pPkt = NULL;
  vmk_SgElem elem;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_RX);

  VMK_ASSERT_NOT_NULL(pAdapter);

  if ((pRxDesc == NULL) || (qIndex >= pAdapter->numRxqsAllocated)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid arguments pRxDesc = %p qIndex = %u",
                        pRxDesc, qIndex);
    vmk_AtomicInc64(&pAdapter->ppRxq[qIndex]->stats[SFVMK_RXQ_INVALID_DESC]);
    status = VMK_BAD_PARAM;
    goto done;
  }

  pPkt = pRxDesc->pPkt;
  if (pPkt == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL Pkt");
    vmk_AtomicInc64(&pAdapter->ppRxq[qIndex]->stats[SFVMK_RXQ_INVALID_PKT_BUFFER]);
    status = VMK_FAILURE;
    goto done;
  }

  elem.ioAddr = pRxDesc->ioAddr;
  elem.length = pRxDesc->size;
  status = vmk_DMAUnmapElem(pAdapter->dmaEngine, VMK_DMA_DIRECTION_TO_MEMORY, &elem);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_DMAUnmapElem failed status: %s",
                        vmk_StatusToString(status));
    vmk_AtomicInc64(&pAdapter->ppRxq[qIndex]->stats[SFVMK_RXQ_DMA_UNMAP_FAILED]);
  }

  /* Convert checksum flags */
  if (pRxDesc->flags & EFX_CKSUM_TCPUDP)
    vmk_PktSetCsumVfd(pPkt);

  if (VMK_UNLIKELY(pAdapter->ppEvq[qIndex]->panicPktList != NULL)) {
    VMK_ASSERT(vmk_SystemCheckState(VMK_SYSTEM_STATE_PANIC) == VMK_TRUE);
    vmk_PktListAppendPkt(pAdapter->ppEvq[qIndex]->panicPktList, pPkt);
  }
  else {
    /* Deliver the pkt to uplink layer */
    vmk_NetPollRxPktQueue(pAdapter->ppEvq[qIndex]->netPoll, pPkt);
  }

  vmk_AtomicAdd64(&pAdapter->ppRxq[qIndex]->stats[SFVMK_RXQ_BYTES], pRxDesc->size);
  vmk_AtomicInc64(&pAdapter->ppRxq[qIndex]->stats[SFVMK_RXQ_PKTS]);

  pRxDesc->flags = EFX_DISCARD;
  pRxDesc->pPkt = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX);
  return;
}

/*! \brief      Read the pkt from the queue and pass it to uplink layer
**              or discard it
**
** \param[in]  pRxq      Ptr to RXQ
** \param[in]  pCompCtx  Ptr to context info (netpoll, others) as this
**                       function can be called in multiple context.
**
** \return: void
*/
void sfvmk_rxqComplete(sfvmk_rxq_t *pRxq, sfvmk_pktCompCtx_t *pCompCtx)
{
  sfvmk_adapter_t *pAdapter = NULL;
  vmk_uint32 completed;
  vmk_uint32 level;
  vmk_PktHandle *pPkt ;
  vmk_SgElem elem;
  VMK_ReturnStatus status;
  vmk_VA pFrameVa;
  vmk_Bool isRxCsumEnabled = VMK_FALSE;
  vmk_uint32 sharedReadLockVer;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_RX);

  VMK_ASSERT_NOT_NULL(pRxq);

  pAdapter = pRxq->pAdapter;
  VMK_ASSERT_NOT_NULL(pAdapter);

  completed = pRxq->completed;
  while (completed != pRxq->pending) {
    vmk_uint32 id;
    sfvmk_rxSwDesc_t *pRxDesc = NULL;

    id = completed++ & pRxq->ptrMask;
    pRxDesc = &pRxq->pQueue[id];
    VMK_ASSERT_NOT_NULL(pRxDesc);

    pPkt = pRxDesc->pPkt;
    VMK_ASSERT_NOT_NULL(pPkt);

    if (pRxDesc->flags & EFX_DISCARD)
      goto discard_pkt;

    /* Length is stored in the packet preefix when EF10 hardware is operating
     * in cut-through mode (so that the RX event is generated before the
     * length is known). */
    if (pRxDesc->flags & EFX_PKT_PREFIX_LEN) {
      vmk_uint16 len = 0;

      status = efx_pseudo_hdr_pkt_length_get(pRxq->pCommonRxq,
                                             (vmk_uint8 *)vmk_PktFrameMappedPointerGet(pPkt),
                                             &len);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "efx_pseudo_hdr_pkt_length_get failed status: %s",
                            vmk_StatusToString(status));
        vmk_AtomicInc64(&pRxq->stats[SFVMK_RXQ_PSEUDO_HDR_PKT_LEN_FAILED]);
        goto discard_pkt;
      }
      pRxDesc->size = len + pAdapter->rxPrefixSize;
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_RX, SFVMK_LOG_LEVEL_DBG,
                          "rx_desc_size: %u", pRxDesc->size);
    }

    /* Initialize the pkt len for vmk_PktPushHeadroom to work */
    vmk_PktFrameLenSet(pPkt, pRxDesc->size);

    /* If prefix header is present, remove it */
    status = vmk_PktPushHeadroom(pPkt, pAdapter->rxPrefixSize);
    if(status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PktPushHeadroom failed status: %s",
                          vmk_StatusToString(status));
      vmk_AtomicInc64(&pRxq->stats[SFVMK_RXQ_PKT_HEAD_ROOM_FAILED]);
      goto discard_pkt;
    }
    pRxDesc->size -= pAdapter->rxPrefixSize;

    /* Pad the pPkt if it is shorter than SFVMK_MIN_PKT_SIZE bytes */
    if (pRxDesc->size < SFVMK_MIN_PKT_SIZE) {
      pFrameVa = vmk_PktFrameMappedPointerGet(pPkt);
      if (!pFrameVa) {
        SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PktFrameMappedPointerGet failed status: %s",
                            vmk_StatusToString(status));
        vmk_AtomicInc64(&pRxq->stats[SFVMK_RXQ_PKT_FRAME_MAPPED_PTR_FAILED]);
        goto discard_pkt;
      } else {
        if (vmk_PktIsBufDescWritable(pPkt) == VMK_TRUE) {
          vmk_Memset((vmk_uint8 *)pFrameVa + pRxDesc->size, 0,
                     SFVMK_MIN_PKT_SIZE - pRxDesc->size);
          pRxDesc->size = SFVMK_MIN_PKT_SIZE;
          vmk_PktFrameLenSet(pPkt, pRxDesc->size);
        } else {
          SFVMK_ADAPTER_ERROR(pAdapter, "Buff desc is not writable(pkt size = %u",
                              pRxDesc->size);
          vmk_AtomicInc64(&pRxq->stats[SFVMK_RXQ_INVALID_BUFFER_DESC]);
          goto discard_pkt;
        }
      }
    }

    if (VMK_UNLIKELY(pRxDesc->size > pAdapter->rxMaxFrameSize)) {
      SFVMK_ADAPTER_ERROR(pAdapter, "RXQ[%u]: pkt size(%u) is invalid",
                          pRxq->index, pRxDesc->size);
      vmk_AtomicInc64(&pRxq->stats[SFVMK_RXQ_INVALID_FRAME_SZ]);
      goto discard_pkt;
    }

    do {
      sharedReadLockVer = vmk_VersionedAtomicBeginTryRead(&pAdapter->isRxCsumLock);
      isRxCsumEnabled = pAdapter->isRxCsumEnabled;
    } while (!vmk_VersionedAtomicEndTryRead(&pAdapter->isRxCsumLock, sharedReadLockVer));

    /* Clear the CKSUM flags if Rx CSUM validation is disabled */
    switch (pRxDesc->flags & (EFX_PKT_IPV4 | EFX_PKT_IPV6)) {
      case EFX_PKT_IPV4:
        if(!isRxCsumEnabled)
          pRxDesc->flags &= ~(EFX_CKSUM_IPV4 | EFX_CKSUM_TCPUDP);
        break;
      case EFX_PKT_IPV6:
        if(!isRxCsumEnabled)
          pRxDesc->flags &= ~EFX_CKSUM_TCPUDP;
        break;
      case 0:
        break;
      default:
        SFVMK_ADAPTER_ERROR(pAdapter, "RX Desc with both ipv4 and ipv6 flags");
        vmk_AtomicInc64(&pRxq->stats[SFVMK_RXQ_INVALID_PROTO]);
        goto discard_pkt;
    }

    /* Pass packet up the stack  */
    sfvmk_rxDeliver(pAdapter, pRxDesc, pRxq->index);

    continue;

discard_pkt:

    if (pRxq->state == SFVMK_RXQ_STATE_STARTED)
      vmk_AtomicInc64(&pRxq->stats[SFVMK_RXQ_DISCARD]);

    /* Return the packet to the pool */
    elem.ioAddr = pRxDesc->ioAddr;
    elem.length = pRxDesc->size;
    vmk_DMAUnmapElem(pAdapter->dmaEngine, VMK_DMA_DIRECTION_TO_MEMORY, &elem);

    if (pPkt != NULL) {
      sfvmk_pktRelease(pAdapter, pCompCtx, pPkt);
      pRxDesc->pPkt = NULL;
    }

    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_RX, SFVMK_LOG_LEVEL_DBG, "completed = %u"
                        "pending = %u", completed, pRxq->pending);
  }

  pRxq->completed = completed;
  level = pRxq->added - pRxq->completed;

  if (level < pRxq->refillThreshold)
    sfvmk_rxqFill(pRxq, pCompCtx);

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_RX);
  return;
}

/*! \brief     Fill RX buffer desc with pkt information.
**
** \param[in]  pRxq      RXQ pointer
** \param[in]  pCompCtx  Ptr to context info (netpoll, others) as this
**                       function can be called in multiple context.
**
** \return: void
*/
void sfvmk_rxqFill(sfvmk_rxq_t *pRxq, sfvmk_pktCompCtx_t *pCompCtx)
{
  sfvmk_adapter_t *pAdapter = NULL;
  sfvmk_rxSwDesc_t *rxDesc = NULL;
  efsys_dma_addr_t addr[SFVMK_REFILL_BATCH];
  vmk_PktHandle *pNewpkt = NULL;
  const vmk_SgElem *pFrag;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_DMAMapErrorInfo dmaMapErr;
  vmk_SgElem mapperIN, mapperOut;
  vmk_uint32 posted, maxBuf;
  vmk_uint32 batch;
  vmk_uint32 rxfill;
  vmk_uint32 mblkAllocSize;
  vmk_uint32 headroom;
  vmk_uint32 id;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_RX);

  VMK_ASSERT_NOT_NULL(pRxq);

  if (pRxq->state != SFVMK_RXQ_STATE_STARTED) {
    SFVMK_ERROR("RXQ[%u] is not yet started", pRxq->index);
    goto done;
  }

  pAdapter = pRxq->pAdapter;
  VMK_ASSERT_NOT_NULL(pAdapter);

  batch = 0;
  rxfill = pRxq->added - pRxq->completed;

  maxBuf = EFX_RXQ_LIMIT(pRxq->numDesc) - rxfill;

  mblkAllocSize = pAdapter->rxBufferSize + pAdapter->rxBufferStartAlignment;

  for (posted = 0; posted < maxBuf; posted++) {

    status = vmk_PktAllocForDMAEngine(mblkAllocSize, pAdapter->dmaEngine, &pNewpkt);
    if (VMK_UNLIKELY(status != VMK_OK)) {
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_RX, SFVMK_LOG_LEVEL_INFO,
                          "vmk_PktAllocForDMAEngine failed status %s",
                          vmk_StatusToString(status));
      break;
    }

    vmk_PktFrameLenSet(pNewpkt, mblkAllocSize);

    pFrag = vmk_PktSgElemGet(pNewpkt, 0);
    if (pFrag == NULL) {
      sfvmk_pktRelease(pAdapter, pCompCtx, pNewpkt);
      break;
    }

    /* Align start addr to startAlignemnt */
    if (pFrag->addr & (pAdapter->rxBufferStartAlignment - 1)) {
      headroom = pAdapter->rxBufferStartAlignment -
                 (pFrag->addr & (pAdapter->rxBufferStartAlignment - 1));

      vmk_PktPushHeadroom(pNewpkt, headroom);

      /* Called again to get the modifed address */
      pFrag = vmk_PktSgElemGet(pNewpkt, 0);
      if (pFrag == NULL) {
        sfvmk_pktRelease(pAdapter, pCompCtx, pNewpkt);
        break;
      }
    }

    /* Map it io dma address */
    mapperIN.addr = pFrag->addr;
    mapperIN.length = pAdapter->rxBufferSize;
    status = vmk_DMAMapElem(pAdapter->dmaEngine, VMK_DMA_DIRECTION_TO_MEMORY,
                            &mapperIN, VMK_TRUE, &mapperOut, &dmaMapErr);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_RX, SFVMK_LOG_LEVEL_INFO,
                          "vmk_DMAMapElem failed to map %p size %lu to IO address, status %s",
                          pNewpkt, pAdapter->rxBufferSize,
                          vmk_DMAMapErrorReasonToString(dmaMapErr.reason));
      sfvmk_pktRelease(pAdapter, pCompCtx, pNewpkt);
      break;
    }

    id = (pRxq->added + batch) & pRxq->ptrMask;

    rxDesc = &pRxq->pQueue[id];
    rxDesc->flags = EFX_DISCARD;
    rxDesc->ioAddr = mapperOut.ioAddr;
    rxDesc->pPkt = pNewpkt;
    rxDesc->size = pAdapter->rxBufferSize;
    addr[batch++] = mapperOut.ioAddr;

    if (batch == SFVMK_REFILL_BATCH) {
      /* Post buffer to RX module */
      efx_rx_qpost(pRxq->pCommonRxq, addr, pAdapter->rxBufferSize, batch,
                   pRxq->completed, pRxq->added);
      pRxq->added += batch;
      batch = 0;
    }
  }

  if (batch != 0) {
    /* Post buffer to rxq module */
    efx_rx_qpost(pRxq->pCommonRxq, addr, pAdapter->rxBufferSize, batch,
                 pRxq->completed, pRxq->added);
    pRxq->added += batch;
    batch = 0;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_RX, SFVMK_LOG_LEVEL_DBG,
                      "No of allocated buffer = %u", posted);

  /* Push entries in queue */
  efx_rx_qpush(pRxq->pCommonRxq, pRxq->added, &pRxq->pushed);

  /* The queue could still be empty if no descriptors were actually
   * pushed, in which case there will be no event to cause the next
   * refill, so we must schedule a refill ourselves.
   */
  if(pRxq->pushed == pRxq->completed) {
    sfvmk_rxScheduleRefill(pRxq);
  } else {
    pRxq->refillDelay = SFVMK_RXQ_REFILL_DELAY_MS;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_RX, SFVMK_LOG_LEVEL_DBG,
                      "No of pushed buffer = %u", pRxq->pushed);

done:
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_RX);
  return;
}

/*! \brief  Create common code RXQ.
**
** \param[in]  pAdapter    Pointer to sfvmk_adapter_t
** \param[in]  qIndex      RXQ Index
**
** \return: VMK_OK [success] or error code [failure]
*/
static VMK_ReturnStatus
sfvmk_rxqStart(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  sfvmk_rxq_t *pRxq = NULL;
  sfvmk_evq_t *pEvq = NULL;
  VMK_ReturnStatus status = VMK_BAD_PARAM;
  sfvmk_pktCompCtx_t compCtx = {
    .type = SFVMK_PKT_COMPLETION_OTHERS,
  };

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_RX, "qIndex[%u]", qIndex);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (qIndex >= pAdapter->numRxqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid RXQ index %u", qIndex);
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pAdapter->ppRxq != NULL)
    pRxq = pAdapter->ppRxq[qIndex];

  if (pRxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL receive queue ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pAdapter->ppEvq != NULL)
    pEvq = pAdapter->ppEvq[qIndex];

  if (pEvq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL event queue ptr");
    status = VMK_FAILURE;
    goto done;
  }

  pRxq->numDesc = pAdapter->numRxqBuffDesc;
  if (!ISP2(pRxq->numDesc)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid RX descriptors count: %u",
                        pAdapter->numRxqBuffDesc);
    status = VMK_FAILURE;
    goto failed_valid_desc;
  }

  pRxq->mem.esmHandle  = pAdapter->dmaEngine;
  pRxq->mem.ioElem.length = EFX_RXQ_SIZE(pRxq->numDesc);

  /* Allocate and zero DMA space. */
  pRxq->mem.pEsmBase = sfvmk_allocDMAMappedMem(pAdapter->dmaEngine,
                                               pRxq->mem.ioElem.length,
                                               &pRxq->mem.ioElem.ioAddr);
  if (pRxq->mem.pEsmBase == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_allocDMAMappedMem failed");
    status = VMK_NO_MEMORY;
    goto failed_dma_alloc;
  }

  /* Allocate memory for RX descriptors */
  pRxq->pQueue = vmk_HeapAlloc(sfvmk_modInfo.heapID, sizeof(sfvmk_rxSwDesc_t) * pRxq->numDesc);
  if (pRxq->pQueue == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HeapAlloc failed");
    status = VMK_NO_MEMORY;
    goto failed_desc_alloc;
  }
  vmk_Memset(pRxq->pQueue, 0, sizeof(sfvmk_rxSwDesc_t) * pRxq->numDesc);

  /* Create the common code receive queue. */
  status = efx_rx_qcreate(pAdapter->pNic,
                          qIndex, 0,
                          EFX_RXQ_TYPE_DEFAULT,
                          &pRxq->mem,
                          pRxq->numDesc, 0,
                          EFX_RXQ_FLAG_NONE,
                          pEvq->pCommonEvq,
                          &pRxq->pCommonRxq);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_rx_qcreate(%u) failed status: %s",
                        qIndex, vmk_StatusToString(status));
    goto failed_rx_qcreate;
  }

  efx_rx_qenable(pRxq->pCommonRxq);

  vmk_SpinlockLock(pEvq->lock);
  pRxq->ptrMask = pAdapter->numRxqBuffDesc - 1;
  pRxq->refillThreshold = RX_REFILL_THRESHOLD(pRxq->numDesc);
  pRxq->flushState = SFVMK_FLUSH_STATE_REQUIRED;
  pRxq->state = SFVMK_RXQ_STATE_STARTED;

  /* Try to fill the queue from the pool. */
  sfvmk_rxqFill(pRxq, &compCtx);
  vmk_SpinlockUnlock(pEvq->lock);

  goto done;

failed_rx_qcreate:
failed_desc_alloc:
  sfvmk_freeDMAMappedMem(pRxq->mem.esmHandle,
                         pRxq->mem.pEsmBase,
                         pRxq->mem.ioElem.ioAddr,
                         pRxq->mem.ioElem.length);
failed_dma_alloc:
failed_valid_desc:
done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX, "qIndex[%u]", qIndex);

  return status;
}

/*! \brief  Flush common code RXQs.
**
** \param[in]  pAdapter    Pointer to sfvmk_adapter_t
**
** \return: void
*/
static void sfvmk_rxFlush(sfvmk_adapter_t *pAdapter)
{
  sfvmk_rxq_t *pRxq = NULL;
  sfvmk_evq_t *pEvq = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 qIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_RX);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if ((pAdapter->ppEvq == NULL) || (pAdapter->ppRxq == NULL)) {
    SFVMK_ERROR("No EVQ/RXQ has been initialized");
    goto done;
  }

  for (qIndex = 0; qIndex < pAdapter->numRxqsAllocated; qIndex++) {

    pRxq = pAdapter->ppRxq[qIndex];
    if (pRxq == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL RXQ ptr for RXQ[%u]", qIndex);
      goto done;
    }

    pEvq = pAdapter->ppEvq[qIndex];
    if (pEvq == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL EVQ ptr for EVQ[%u]", qIndex);
      goto done;
    }

    vmk_SpinlockLock(pEvq->lock);

    if (pRxq->state != SFVMK_RXQ_STATE_STARTED) {
      SFVMK_ADAPTER_ERROR(pAdapter, "RXQ is not yet started");
      vmk_SpinlockUnlock(pEvq->lock);
      continue;
    }

    pRxq->state = SFVMK_RXQ_STATE_INITIALIZED;
    pRxq->flushState = SFVMK_FLUSH_STATE_PENDING;
    vmk_SpinlockUnlock(pEvq->lock);

    /* Flush the receive queue */
    status = efx_rx_qflush(pRxq->pCommonRxq);
    if (status != VMK_OK) {
      vmk_SpinlockLock(pEvq->lock);
      pRxq->flushState = SFVMK_FLUSH_STATE_FAILED;
      vmk_SpinlockUnlock(pEvq->lock);
    }
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX);
}

/*! \brief  Wait for flush and destroy common code RXQ.
**
** \param[in]  pAdapter    Pointer to sfvmk_adapter_t
**
** \return: void
*/
static void
sfvmk_rxFlushWaitAndDestroy(sfvmk_adapter_t *pAdapter)
{
  sfvmk_rxq_t *pRxq = NULL;
  sfvmk_evq_t *pEvq = NULL;
  vmk_uint64 timeout, currentTime;
  vmk_uint32 qIndex;
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_pktCompCtx_t compCtx = {
    .type = SFVMK_PKT_COMPLETION_OTHERS,
  };


  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_RX);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if ((pAdapter->ppEvq == NULL) || (pAdapter->ppRxq == NULL)) {
    SFVMK_ERROR("No EVQ/RXQ has been initialized");
    goto done;
  }

  sfvmk_getTime(&currentTime);
  timeout = currentTime + SFVMK_RXQ_STOP_TIME_OUT_USEC;

  for (qIndex = 0; qIndex < pAdapter->numRxqsAllocated; qIndex++) {

    pRxq = pAdapter->ppRxq[qIndex];
    if (pRxq == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL RXQ ptr for RXQ[%u]", qIndex);
      goto done;
    }

    pEvq = pAdapter->ppEvq[qIndex];
    if (pEvq == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL EVQ ptr for EVQ[%u]", qIndex);
      goto done;
    }

    vmk_SpinlockLock(pEvq->lock);
    while (currentTime < timeout) {
      /* Check to see if the flush event has been processed */
      if (pRxq->flushState != SFVMK_FLUSH_STATE_PENDING) {
        break;
      }
      vmk_SpinlockUnlock(pEvq->lock);

      status = vmk_WorldSleep(SFVMK_RXQ_STOP_POLL_TIME_USEC);
      if ((status != VMK_OK) && (status != VMK_WAIT_INTERRUPTED)) {
        SFVMK_ADAPTER_ERROR(pAdapter, "vmk_WorldSleep failed status: %s",
                            vmk_StatusToString(status));
        vmk_SpinlockLock(pEvq->lock);
        break;
      }

      sfvmk_getTime(&currentTime);
      vmk_SpinlockLock(pEvq->lock);
    }

    /* If flush state is pending it means Flush timeout neither done nor failed */
    if ((pRxq->flushState == SFVMK_FLUSH_STATE_PENDING) ||
        (pRxq->flushState == SFVMK_FLUSH_STATE_FAILED)) {
      pRxq->flushState = SFVMK_FLUSH_STATE_DONE;
      SFVMK_ADAPTER_ERROR(pAdapter, "RXQ[%u] flush timeout", qIndex);
    }

    pRxq->pending = pRxq->added;
    sfvmk_rxqComplete(pRxq, &compCtx);

    pRxq->added = 0;
    pRxq->pushed = 0;
    pRxq->pending = 0;
    pRxq->completed = 0;
    vmk_SpinlockUnlock(pEvq->lock);

    /* Release DMA memory. */
    sfvmk_freeDMAMappedMem(pRxq->mem.esmHandle,
                           pRxq->mem.pEsmBase,
                           pRxq->mem.ioElem.ioAddr,
                           pRxq->mem.ioElem.length);

    vmk_HeapFree(sfvmk_modInfo.heapID, pRxq->pQueue);
    /* Destroy the common code receive queue. */
    efx_rx_qdestroy(pRxq->pCommonRxq);
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX);
}

/*! \brief  Create all Common code RXQs.
**
** \param[in]  pAdapter  Pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_rxStart(sfvmk_adapter_t *pAdapter)
{
  const efx_nic_cfg_t *pNicCfg;
  vmk_uint32 qIndex;
  VMK_ReturnStatus status = VMK_BAD_PARAM;
  size_t alignEnd;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_RX);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pAdapter->ppRxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RXQs are not initialized");
    status = VMK_FAILURE;
    goto done;
  }
  /* Initialize the common code receive module. */
  status = efx_rx_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_rx_init failed status: %s",
                        vmk_StatusToString(status));
    goto failed_rxq_init;
  }

  pAdapter->rssInit = VMK_FALSE;

  pAdapter->rxBufferSize = EFX_MAC_PDU(pAdapter->uplink.sharedData.mtu);

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);

  /* Calculate receive packet buffer size. */
  pAdapter->rxPrefixSize = pNicCfg->enc_rx_prefix_size;

  pAdapter->rxBufferSize += pAdapter->rxPrefixSize;

  /* Get alignment requirement for RX DMA padding */
  alignEnd = MAX(1, pNicCfg->enc_rx_buf_align_end);
  EFSYS_ASSERT(ISP2(alignEnd));

  pAdapter->rxBufferStartAlignment = MAX(1, pNicCfg->enc_rx_buf_align_start);
  EFSYS_ASSERT(ISP2(pAdapter->rxBufferStartAlignment));
  pAdapter->rxBufferStartAlignment = MAX(pAdapter->rxBufferStartAlignment,
                                         MAX(alignEnd, VMK_L1_CACHELINE_SIZE));

  pAdapter->rxBufferSize = P2ROUNDUP(pAdapter->rxBufferSize, alignEnd);

  /* Maximum frame size that should be accepted */
  pAdapter->rxMaxFrameSize = pAdapter->uplink.sharedData.mtu +
                             sizeof(vmk_EthHdr) + sizeof(vmk_VLANHdr);

  /* Start the receive queue(s). */
  for (qIndex = 0; qIndex < pAdapter->numRxqsAllocated; qIndex++) {
    status = sfvmk_rxqStart(pAdapter, qIndex);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_rxqStart(%u) failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto failed_rxq_start;
    }
  }

  status = efx_mac_filter_default_rxq_set(pAdapter->pNic,
                                          pAdapter->ppRxq[pAdapter->defRxqIndex]->pCommonRxq,
                                          VMK_FALSE);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_filter_default_rxq_set failed status: %s",
                        vmk_StatusToString(status));
    goto failed_default_rxq_set;
  }

  goto done;

failed_default_rxq_set:
failed_rxq_start:
  sfvmk_rxFlush(pAdapter);
  sfvmk_rxFlushWaitAndDestroy(pAdapter);

  efx_rx_fini(pAdapter->pNic);

failed_rxq_init:
done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX);

  return status;
}

/*! \brief    Destroy all common code RXQs
**
** \param[in]  pAdapter  Pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_rxStop(sfvmk_adapter_t *pAdapter)
{
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_RX);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  efx_mac_filter_default_rxq_clear(pAdapter->pNic);

  /* Stop the receive queue(s) */
  sfvmk_rxFlush(pAdapter);
  sfvmk_rxFlushWaitAndDestroy(pAdapter);

  pAdapter->rxPrefixSize = 0;
  pAdapter->rxBufferSize = 0;
  pAdapter->rxBufferStartAlignment = 0;
  pAdapter->rxMaxFrameSize = 0;

  efx_rx_fini(pAdapter->pNic);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX);
}

/*! \brief Set the RXQ flush state.
**
** \param[in]  pRxq       Pointer to RXQ
** \param[in]  flushState flush state
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_setRxqFlushState(sfvmk_rxq_t *pRxq, sfvmk_flushState_t flushState)
{
  VMK_ASSERT(pRxq != NULL);
  pRxq->flushState = flushState;

  return VMK_OK;
}

