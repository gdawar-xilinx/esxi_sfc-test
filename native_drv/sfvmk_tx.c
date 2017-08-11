/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"

#define SFVMK_TXQ_STOP_POLL_WAIT 100*SFVMK_ONE_MILISEC
#define SFVMK_TXQ_STOP_POLL_TIMEOUT 20

static int sfvmkTsoFwAssisted = SFVMK_FATSOV2;

/*
 * Size of preallocated TSO header buffers.  Larger blocks must be
 * allocated from the heap.
 */
#define	TSOH_STD_SIZE	128

/* At most half the descriptors in the queue at any time will refer to
 * a TSO header buffer, since they must always be followed by a
 * payload descriptor referring to an scatter gather element.
 */
#define	TSOH_COUNT(txqEntries)            ((txqEntries) / 2u)
#define	TSOH_PER_PAGE                     (VMK_PAGE_SIZE / TSOH_STD_SIZE)
#define	TSOH_PAGE_COUNT(txqEntries)       howmany(TSOH_COUNT(txqEntries), TSOH_PER_PAGE)

/* TCP header flag bits */
#define TH_FIN     0x01
#define TH_PUSH    0x08

/*
 * Software "TSO".  Not quite as good as doing it in hardware, but
 * still faster than segmenting in the stack.
 */
typedef struct sfvmk_tsoState_s {
  /* Output position */
  vmk_uint32 outLen;      /* Remaining length in current segment */
  vmk_uint32 seqnum;      /* Current sequence number */
  vmk_uint32 packetSpace; /* Remaining space in current packet */
  vmk_uint32 segsSpace;   /* Remaining number of DMA segments
                             for the packet (FATSOv2 only) */
  /* Input position */
  uint64_t dmaAddr;       /* DMA address of current position */
  vmk_uint32 inLen;       /* Remaining length in current mbuf */

  vmk_PktHandle *pkt;     /* Input packet */
  uint16_t protocol;      /* Network protocol (after VLAN decap) */
  vmk_uint32 nhOff;       /* Offset of network header */
  vmk_uint32 tcphOff;     /* Offset of TCP header */
  vmk_uint32 headerLen;   /* Number of bytes of header */
  vmk_uint32 segSize;     /* TCP segment size */
  vmk_int32 fwAssisted;   /* Use FW-assisted TSO */
  uint16_t packetId;      /* IPv4 packet ID from the original packet */
  uint8_t tcpFlags;       /* TCP flags */
  efx_desc_t headerDesc;  /* Precomputed header descriptor for
                           * FW-assisted TSO */
}sfvmk_tsoState_t;


/*! \brief TSO engine initialization. Allocate required buffers
**         for TSO processing
**
** \param[in]  pTxq pointer to txq
**
** \return: VMK_OK on success, VMK_FAILURE otherwise
*/
static int
sfvmk_tsoInit(sfvmk_txq_t *pTxq)
{
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;
  unsigned int tsohPageCount = TSOH_PAGE_COUNT(pAdapter->txqEntries);
  int i;

  SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "tsohPageCount: %d, VMK_PAGE_SIZE: %d, TSOH_PER_PAGE: %d",
              tsohPageCount, VMK_PAGE_SIZE, TSOH_PER_PAGE);

  /* Allocate TSO header buffers */
  pTxq->pTsohBuffer = (efsys_mem_t *)sfvmk_memPoolAlloc(tsohPageCount * sizeof(efsys_mem_t));
  if (NULL == pTxq->pTsohBuffer) {
    SFVMK_ERR(pAdapter,"failed to allocate memory for TSO header buffers");
    return VMK_FAILURE;
  }

  for (i = 0; i < tsohPageCount; i++) {
    pTxq->pTsohBuffer[i].esmHandle  = pAdapter->dmaEngine;

    /* Allocate and zero DMA space for TSO header buffers */
    pTxq->pTsohBuffer[i].ioElem.length = VMK_PAGE_SIZE;
    pTxq->pTsohBuffer[i].pEsmBase = sfvmk_allocCoherentDMAMapping(pAdapter->dmaEngine,
                                                  pTxq->pTsohBuffer[i].ioElem.length,
                                                  &pTxq->pTsohBuffer[i].ioElem.ioAddr);
    if(NULL == pTxq->pTsohBuffer[i].pEsmBase)
      goto fail;
  }

  return VMK_OK;

fail:
  while (i-- > 0)
    sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pTxq->pTsohBuffer[i].pEsmBase,
                                 pTxq->pTsohBuffer[i].ioElem.ioAddr,
                                 pTxq->pTsohBuffer[i].ioElem.length);
  sfvmk_memPoolFree(pTxq->pTsohBuffer, tsohPageCount * sizeof(efsys_mem_t));
  pTxq->pTsohBuffer = NULL;
  return VMK_FAILURE;
}

/*! \brief It releases the resource required for txQ module.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
** \param[in]  tx queue index
**
** \return: void
*/
static void
sfvmk_txqFini(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_txq_t *pTxq;
  efsys_mem_t *pTxqMem;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX, "qIndex[%d]", qIndex);

  pTxq = pAdapter->pTxq[qIndex];

  SFVMK_NULL_PTR_CHECK(pTxq);

  pTxqMem = &pTxq->mem;

  VMK_ASSERT_BUG(pTxq->initState == SFVMK_TXQ_INITIALIZED,
                    "txq->initState != SFVMK_TXQ_INITIALIZED");

  if (pTxq->type == SFVMK_TXQ_IP_TCP_UDP_CKSUM) {
    unsigned int tsohPageCount = TSOH_PAGE_COUNT(pAdapter->txqEntries);
    int i=0;
    for (; i < tsohPageCount; i++)
      sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pTxq->pTsohBuffer[i].pEsmBase,
                                   pTxq->pTsohBuffer[i].ioElem.ioAddr,
                                   pTxq->pTsohBuffer[i].ioElem.length);
    sfvmk_memPoolFree(pTxq->pTsohBuffer, tsohPageCount * sizeof(efsys_mem_t));
  }

  /* Free the context arrays. */
  sfvmk_memPoolFree(pTxq->pStmp, sizeof(sfvmk_txMapping_t) * pAdapter->txqEntries);
  sfvmk_memPoolFree(pTxq->pPendDesc , pTxq->pendDescSize);

  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pTxqMem->pEsmBase, pTxqMem->ioElem.ioAddr,
                                pTxqMem->ioElem.length);
  pAdapter->pTxq[qIndex] = NULL;

  sfvmk_mutexDestroy(pTxq->lock);

  sfvmk_memPoolFree(pTxq,  sizeof(sfvmk_txq_t));

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX, "qIndex[%d]", qIndex);

}

/*! \brief It allocates resources required for a particular tx queue.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
** \param[in]  tx queue index
** \param[in]  tx queue type
** \param[in]  associated event queue index
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_txqInit(sfvmk_adapter_t *pAdapter, unsigned int txqIndex,
                     enum sfvmk_txqType type, unsigned int evqIndex)
{
  sfvmk_txq_t *pTxq;
  efsys_mem_t *pTxqMem;
  VMK_ReturnStatus status ;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX, "qIndex[%d]", txqIndex);

  pTxq = (sfvmk_txq_t *)sfvmk_memPoolAlloc(sizeof(sfvmk_txq_t));
  if (NULL == pTxq) {
    SFVMK_ERR(pAdapter,"failed to allocate memory for txq obj");
    goto sfvmk_alloc_fail;
  }

  pTxq->pAdapter = pAdapter;
  pTxq->entries = pAdapter->txqEntries;
  pTxq->ptrMask = pTxq->entries - 1;

  pAdapter->pTxq[txqIndex] = pTxq;

  pTxqMem = &pTxq->mem;
  pTxqMem->esmHandle  = pAdapter->dmaEngine;

  /* Allocate and zero DMA space for the descriptor ring. */
  pTxqMem->ioElem.length = EFX_TXQ_SIZE(pAdapter->txqEntries);

  pTxqMem->pEsmBase = sfvmk_allocCoherentDMAMapping(pAdapter->dmaEngine,
                                                  pTxqMem->ioElem.length,
                                                  &pTxqMem->ioElem.ioAddr);
  if(NULL == pTxqMem->pEsmBase) {
    SFVMK_ERR(pAdapter,"failed to allocate memory for txq entries");
    goto sfvmk_dma_alloc_fail;
  }

  /* Allocate pending descriptor array for batching writes. */
  pTxq->pPendDesc = sfvmk_memPoolAlloc(sizeof(efx_desc_t) * pAdapter->txqEntries);
  if (NULL == pTxq->pPendDesc) {
    SFVMK_ERR(pAdapter,"failed to allocate memory for pend desc");
    goto sfvmk_pPendDesc_alloc_fail;
  }
  pTxq->pendDescSize = sizeof(efx_desc_t) * pAdapter->txqEntries;

  /* Allocate and initialise pkt DMA mapping array. */
  pTxq->pStmp = sfvmk_memPoolAlloc(sizeof(sfvmk_txMapping_t) * pAdapter->txqEntries);
  if (NULL == pTxq->pStmp) {
    SFVMK_ERR(pAdapter,"failed to allocate memory for stmp");
    goto sfvmk_stmp_alloc_fail;
  }

  if (type == SFVMK_TXQ_IP_TCP_UDP_CKSUM &&
    (status = sfvmk_tsoInit(pTxq)) != 0) {
    SFVMK_ERR(pAdapter,"failed to initialize tso");
    goto sfvmk_stmp_alloc_fail;
  }

  status = sfvmk_mutexInit("txq", &pTxq->lock);
  if(status != VMK_OK)
    goto sfvmk_mutex_fail;

  pTxq->type = type;
  pTxq->evqIndex = evqIndex;
  pTxq->txqIndex = txqIndex;
  pTxq->initState = SFVMK_TXQ_INITIALIZED;

  SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_INFO, "txq[%d] is "
            "initialized associated evq index is %d", txqIndex, evqIndex);

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX, "qIndex[%d]", txqIndex);

  return VMK_OK;

sfvmk_mutex_fail:
  sfvmk_memPoolFree(pTxq->pStmp, sizeof(sfvmk_txMapping_t) * pAdapter->txqEntries);
sfvmk_stmp_alloc_fail:
  sfvmk_memPoolFree(pTxq->pPendDesc, pTxq->pendDescSize);
sfvmk_pPendDesc_alloc_fail:
  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pTxqMem->pEsmBase,
                                pTxqMem->ioElem.ioAddr, pTxqMem->ioElem.length);
sfvmk_dma_alloc_fail:
  sfvmk_memPoolFree(pTxq, sizeof(sfvmk_txq_t));
sfvmk_alloc_fail:
  return VMK_FAILURE;
}

/*! \brief It allocates resource required for all the tx queues.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
int
sfvmk_txInit(sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  vmk_uint32    qIndex;
  vmk_uint32    evqIndex = 0;
  int rc;
  const efx_nic_cfg_t *pCfg = NULL;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  pCfg = efx_nic_cfg_get(pAdapter->pNic); 
  pIntr = &pAdapter->intr;

  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_INITIALIZED,
                    "intr->state != SFXGE_INTR_INITIALIZED");

  pAdapter->tsoFwAssisted = sfvmkTsoFwAssisted;

  if ((~pCfg->enc_features & EFX_FEATURE_FW_ASSISTED_TSO_V2) ||
     (!pCfg->enc_fw_assisted_tso_v2_enabled))
    pAdapter->tsoFwAssisted &= ~SFVMK_FATSOV2;
  
  SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "enc_features: 0x%x, tso_v2_enabled: %d, tcp_header_offset_limit: %d,\
              tsoFwAssisted: %d, max_tx_desc_size: %d", 
              pCfg->enc_features, pCfg->enc_fw_assisted_tso_v2_enabled,
              pCfg->enc_tx_tso_tcp_header_offset_limit, 
              pAdapter->tsoFwAssisted, pCfg->enc_tx_dma_desc_size_max);
  pAdapter->txDmaDescMaxSize = pCfg->enc_tx_dma_desc_size_max;

  pAdapter->txqCount = SFVMK_TXQ_NTYPES - 1 + pIntr->numIntrAlloc;

  /* Initialize transmit queues */
  rc = sfvmk_txqInit(pAdapter, SFVMK_TXQ_NON_CKSUM, SFVMK_TXQ_NON_CKSUM, 0);
  if(rc) {
    SFVMK_ERR(pAdapter, "failed to init txq[SFVMK_TXQ_NON_CKSUM] with err %d",
                rc);
    goto sfvmk_fail;
  }

  rc = sfvmk_txqInit(pAdapter, SFVMK_TXQ_IP_CKSUM,  SFVMK_TXQ_IP_CKSUM, 0);
  if(rc) {
    SFVMK_ERR(pAdapter,"failed to init txq[SFVMK_TXQ_IP_CKSUM] with err %d",
                rc);
    goto sfvmk_fail2;
  }

  qIndex = SFVMK_TXQ_NTYPES - 1;
  for (; qIndex < pAdapter->txqCount; qIndex++, evqIndex++) {
    rc = sfvmk_txqInit(pAdapter, qIndex, SFVMK_TXQ_IP_TCP_UDP_CKSUM, evqIndex);
    if (rc) {
      SFVMK_ERR(pAdapter,"failed to init txq[%d]", qIndex);
      goto sfvmk_fail3;
    }
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);

  return rc;

sfvmk_fail3:
  while (--qIndex >= SFVMK_TXQ_NTYPES - 1)
    sfvmk_txqFini(pAdapter,  qIndex);

  sfvmk_txqFini(pAdapter, SFVMK_TXQ_IP_CKSUM);

sfvmk_fail2:
  sfvmk_txqFini(pAdapter, SFVMK_TXQ_NON_CKSUM);

sfvmk_fail:

  return (rc);
}

/*! \brief It releases resource required for all allocated tx queues
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_txFini(sfvmk_adapter_t *pAdapter)
{
  int qIndex;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  qIndex = pAdapter->txqCount;
  while (--qIndex >= 0)
    sfvmk_txqFini(pAdapter, qIndex);

  pAdapter->txqCount = 0;

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
}

/*! \brief reap the tx queue
**
** \param[in]  tx queue ptr
**
** \return: void
*/
static void
sfvmk_txqReap(sfvmk_txq_t *pTxq)
{
  pTxq->reaped = pTxq->completed;
}

/*! \brief Mark the tx queue as unblocked
**
** \param[in]  tx queue ptr
**
** \return: void
*/
static void
sfvmk_txqUnblock(sfvmk_txq_t *pTxq)
{
  struct sfvmk_adapter_s *pAdapter = pTxq->pAdapter;
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  if (VMK_UNLIKELY(pTxq->initState != SFVMK_TXQ_STARTED))
    return;

  SFVMK_TXQ_LOCK(pTxq);

  if (pTxq->blocked) {
    unsigned int level;

    level = pTxq->added - pTxq->completed;
    SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "completed: %d, added: %d, level: %d", pTxq->completed, pTxq->added, level);
    if (level <= SFVMK_TXQ_UNBLOCK_LEVEL(pTxq->entries)) {
      /* reaped must be in sync with blocked */
      sfvmk_txqReap(pTxq);
      sfvmk_updateQueueStatus(pTxq->pAdapter, VMK_UPLINK_QUEUE_STATE_STARTED);
      pTxq->blocked = 0;
    }
  }

  SFVMK_TXQ_UNLOCK(pTxq);
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
}

/*! \brief called when a tx comletion event comes from the fw.
**
** \param[in]  tx queue ptr
** \param[in]  event queue ptr
**
** \return: void
*/
void
sfvmk_txqComplete(sfvmk_txq_t *pTxq, sfvmk_evq_t *pEvq)
{
  unsigned int completed;
  struct sfvmk_adapter_s *pAdapter = pTxq->pAdapter;

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  completed = pTxq->completed;
  while (completed != pTxq->pending) {
    sfvmk_txMapping_t *pStmp;
    unsigned int id;

    id = completed++ & pTxq->ptrMask;
    pStmp = &pTxq->pStmp[id];
    SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "completed: %d, pending: %d, id: %d, stmp: %p, isPkt: %d, pkt: %p", 
              completed, pTxq->pending, id, pStmp, pStmp->isPkt, pStmp->u.pkt);

    if (pStmp->sgelem.ioAddr != 0) {
      SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
               "Unmapping frag at addr: %lx", pStmp->sgelem.ioAddr);
      vmk_DMAUnmapElem(pAdapter->dmaEngine, VMK_DMA_DIRECTION_FROM_MEMORY, &pStmp->sgelem);
    }

    if(pStmp->isPkt) {
      if(pStmp->u.pkt) {
        SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                  "Free pkt: %p", pStmp->u.pkt);
        if(pTxq->initState == SFVMK_TXQ_STARTED)
          vmk_NetPollQueueCompPkt(pEvq->netPoll, pStmp->u.pkt);
        else
          vmk_PktRelease(pStmp->u.pkt);
      }
    }
    else {
      sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pStmp->u.hdrMem.pEsmBase,
                                   pStmp->u.hdrMem.ioElem.ioAddr, 
                                   pStmp->u.hdrMem.ioElem.length);
    }
  }

  pTxq->completed = completed;

  SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
     "completed: %d, pending: %d, blocked: %d", pTxq->completed, pTxq->pending, pTxq->blocked);

  /* Check whether we need to unblock the queue. */
  vmk_CPUMemFenceWrite();
  if (pTxq->blocked) {
    unsigned int level;

    level = pTxq->added - pTxq->completed;
    SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "added: %d, completed: %d, level: %d", pTxq->added, pTxq->completed, level);
    if (level <= SFVMK_TXQ_UNBLOCK_LEVEL(pTxq->entries))
      sfvmk_txqUnblock(pTxq);
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
}

/*! \brief flush txq and destroy txq module for a particular txq
**
** \param[in]  adapter pointer to sfvmk_adapter_t
** \param[in]  tx queue index
**
** \return: void
*/
static void
sfvmk_txqStop(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_txq_t *pTxq;
  sfvmk_evq_t *pEvq;
  vmk_uint32 count;
  vmk_uint32 spinTime = SFVMK_ONE_MILISEC;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX, "qIndex[%d]", qIndex);

  pTxq = pAdapter->pTxq[qIndex];
  pEvq = pAdapter->pEvq[pTxq->evqIndex];

  SFVMK_TXQ_LOCK(pTxq);
  SFVMK_EVQ_LOCK(pEvq);

  VMK_ASSERT_BUG(pTxq->initState == SFVMK_TXQ_STARTED,
                 "pTxq->initState != SFVMK_TXQ_STARTED");

  pTxq->initState = SFVMK_TXQ_INITIALIZED;

  if (pTxq->flushState != SFVMK_FLUSH_DONE) {

    pTxq->flushState = SFVMK_FLUSH_PENDING;

    SFVMK_EVQ_UNLOCK(pEvq);
    SFVMK_TXQ_UNLOCK(pTxq);

    /* Flush the transmit queue. */
    if (efx_tx_qflush(pTxq->pCommonTxq) != 0) {
      SFVMK_ERR(pAdapter, "Flushing Tx queue %u failed", qIndex);
      pTxq->flushState = SFVMK_FLUSH_DONE;
    }
    else {
        count = 0;
        do {
          vmk_WorldSleep(spinTime);
          spinTime *= 2;

          /* MAX Spin will not be more than SFVMK_TXQ_STOP_POLL_WAIT */
          if (spinTime >= SFVMK_TXQ_STOP_POLL_WAIT);
            spinTime = SFVMK_TXQ_STOP_POLL_WAIT;

          if (pTxq->flushState != SFVMK_FLUSH_PENDING)
            break;

      } while (++count < SFVMK_TXQ_STOP_POLL_TIMEOUT);
    }

    SFVMK_TXQ_LOCK(pTxq);
    SFVMK_EVQ_LOCK(pEvq);

    if(pTxq->flushState == SFVMK_FLUSH_FAILED)
      SFVMK_ERR(pAdapter, "pTxq->flushState = SFVMK_FLUSH_FAILED");

    if (pTxq->flushState != SFVMK_FLUSH_DONE) {
      /* Flush timeout */
      SFVMK_ERR(pAdapter, " Cannot flush Tx queue %u", qIndex);
      pTxq->flushState = SFVMK_FLUSH_DONE;
    }
  }

  pTxq->blocked = 0;
  pTxq->pending = pTxq->added;

  sfvmk_txqComplete(pTxq, pEvq);
  if(pTxq->completed != pTxq->added)
    SFVMK_ERR(pAdapter, "pTxq->completed != pTxq->added");

  sfvmk_txqReap(pTxq);

  pTxq->added = 0;
  pTxq->pending = 0;
  pTxq->completed = 0;
  pTxq->reaped = 0;

  /* Destroy the common code transmit queue. */
  efx_tx_qdestroy(pTxq->pCommonTxq);
  pTxq->pCommonTxq = NULL;

  SFVMK_EVQ_UNLOCK(pEvq);
  SFVMK_TXQ_UNLOCK(pTxq);

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX, "qIndex[%d]", qIndex);
  return;
}

/*! \brief flush txq and destroy txq module for all allocated txq.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
void sfvmk_txStop(sfvmk_adapter_t *pAdapter)
{
  int qIndex;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  qIndex = pAdapter->txqCount;
  while (--qIndex >= 0)
    sfvmk_txqStop(pAdapter, qIndex);

  /* Tear down the transmit module */
  efx_tx_fini(pAdapter->pNic);

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
}

/*! \brief Estimate maximum number of Tx descriptors required for TSO packet.
**
** \param[in] pAdapter       pointer to sfvmk_adapter_t
** \param[in] type           tx queue type
** \param[in] tsoFwAssisted  type of FW assisted tso
**
** \return: VMK_OK <success> error code <failure>
*/

static unsigned int
sfvmk_txMaxPktDesc(sfvmk_adapter_t *pAdapter,
                   enum sfvmk_txqType type,
                   unsigned int tsoFwAssisted)
{
  /* One descriptor for every input fragment */
  unsigned int maxDesc = SFVMK_TX_MAPPING_MAX_SEG;
  unsigned int swTsoMaxDesc;
  unsigned int faTsov2MaxDesc = 0;

  /* VLAN tagging Tx option descriptor may be required */
  if (efx_nic_cfg_get(pAdapter->pNic)->enc_hw_tx_insert_vlan_enabled)
    maxDesc++;

  if (type == SFVMK_TXQ_IP_TCP_UDP_CKSUM) {
    /*
     * Plus header and payload descriptor for each output segment.
     * Minus one since header fragment is already counted.
     * Even if FATSO is used, we should be ready to fallback
     * to do it in the driver.
     */
    swTsoMaxDesc = SFVMK_TSO_MAX_SEGS * 2 - 1;

    /* FW assisted TSOv2 requires 3 (2 FATSO plus header) extra
     * descriptors per superframe limited by number of DMA fetches
     * per packet. The first packet header is already counted.
     */
    if (tsoFwAssisted & SFVMK_FATSOV2) {
      faTsov2MaxDesc =
          howmany(SFVMK_TX_MAPPING_MAX_SEG,
            EFX_TX_FATSOV2_DMA_SEGS_PER_PKT_MAX - 1) *
          (EFX_TX_FATSOV2_OPT_NDESCS + 1) - 1;
    }

    maxDesc += MAX(swTsoMaxDesc, faTsov2MaxDesc);
  }

  return (maxDesc);
}

/*! \brief creates txq module for a particular txq.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
** \param[in]  tx queue index
**
** \return: VMK_OK <success> error code <failure>
*/
static int
sfvmk_txqStart(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_txq_t *pTxq;
  efsys_mem_t *pTxqMem;
  uint16_t flags;
  vmk_uint32 tsoFwAssisted;
  sfvmk_evq_t *pEvq;
  vmk_uint32 descIndex;
  int rc;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX, "qIndex[%d]", qIndex);

  pTxq = pAdapter->pTxq[qIndex];
  SFVMK_NULL_PTR_CHECK(pTxq);

  pTxqMem = &pTxq->mem;
  pEvq = pAdapter->pEvq[pTxq->evqIndex];

  VMK_ASSERT_BUG(pTxq->initState == SFVMK_TXQ_INITIALIZED,
                  "txq is not initialized");
  VMK_ASSERT_BUG(pEvq->initState == SFVMK_EVQ_STARTED,
                  "evq is not initialized");

  /* Determine the kind of queue we are creating. */
  tsoFwAssisted = 0;

  switch (pTxq->type) {

    case SFVMK_TXQ_NON_CKSUM:
      flags = 0;
      break;

    case SFVMK_TXQ_IP_CKSUM:
      flags = EFX_TXQ_CKSUM_IPV4;
      break;

    case SFVMK_TXQ_IP_TCP_UDP_CKSUM:
      flags = EFX_TXQ_CKSUM_IPV4 | EFX_TXQ_CKSUM_TCPUDP;
      tsoFwAssisted = pAdapter->tsoFwAssisted;

      if (tsoFwAssisted & SFVMK_FATSOV2)
        flags |= EFX_TXQ_FATSOV2;
      break;

    default:
      SFVMK_ERR(pAdapter, "default pTxq->type: %d",pTxq->type);
      flags = 0;
      break;

  }

  /* Create the common code transmit queue. */
  if ((rc = efx_tx_qcreate(pAdapter->pNic, qIndex, pTxq->type, pTxqMem,
                            pAdapter->txqEntries, 0, flags, pEvq->pCommonEvq,
                            &pTxq->pCommonTxq, &descIndex)) != 0) {
  /* Retry if no FATSOv2 resources, otherwise fail */
    if ((rc != ENOSPC) || (~flags & EFX_TXQ_FATSOV2)) {
      SFVMK_ERR(pAdapter,"failed to create txq[%d]", qIndex);
      goto fail;
    }
    /* Looks like all FATSOv2 contexts are used */
    flags &= ~EFX_TXQ_FATSOV2;
    tsoFwAssisted &= ~SFVMK_FATSOV2;

    if ((rc = efx_tx_qcreate(pAdapter->pNic, qIndex, pTxq->type, pTxqMem,
                              pAdapter->txqEntries, 0, flags, pEvq->pCommonEvq,
                              &pTxq->pCommonTxq, &descIndex)) != 0) {
      SFVMK_ERR(pAdapter,"failed to create txq[%d]", qIndex);
      goto fail;
    }
  }

  SFVMK_TXQ_LOCK(pTxq);

  /* Initialise queue descriptor indexes */
  pTxq->added = pTxq->pending = pTxq->completed = pTxq->reaped = descIndex;

  /* Enable the transmit queue. */
  efx_tx_qenable(pTxq->pCommonTxq);

  pTxq->initState = SFVMK_TXQ_STARTED;
  pTxq->flushState = SFVMK_FLUSH_REQUIRED;
  pTxq->tsoFwAssisted = tsoFwAssisted;
  pTxq->maxPktDesc = sfvmk_txMaxPktDesc(pAdapter, pTxq->type, tsoFwAssisted);

  SFVMK_TXQ_UNLOCK(pTxq);

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX, "qIndex[%d]", qIndex);

  return (0);

fail:
  return (rc);
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
  int qIndex;
  int rc;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  /* Initialize the common code transmit module. */
  if ((rc = efx_tx_init(pAdapter->pNic)) != 0) {
    SFVMK_ERR(pAdapter,"failed to init tx module");
    return VMK_FAILURE;
  }

  for (qIndex = 0; qIndex < pAdapter->txqCount; qIndex++) {
    if ((rc = sfvmk_txqStart(pAdapter, qIndex)) != 0) {
      SFVMK_ERR(pAdapter,"failed to start txq[%d]", qIndex);
      goto sfvmk_fail;
    }
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);

  return VMK_OK;

sfvmk_fail:
  while (--qIndex >= 0)
    sfvmk_txqStop(pAdapter, qIndex);

  efx_tx_fini(pAdapter->pNic);
  pAdapter->txqCount = 0;
  return VMK_FAILURE;
}

/*! \brief change the flush state to done. to bec called by event module.
**
** \param[in]  pTxq    pointer to txq
**
** \return: void
*/
void
sfvmk_txqFlushDone(struct sfvmk_txq_s *pTxq)
{
  pTxq->flushState = SFVMK_FLUSH_DONE;
}

/*! \brief post the queue descriptors.
**
** \param[in]  pTxq    pointer to txq
**
** \return: void
*/
static void
sfvmk_txqListPost(sfvmk_txq_t *pTxq)
{
  unsigned int oldAdded;
  unsigned int blockLevel;
  unsigned int level;
  int rc;
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  if(pTxq->nPendDesc == 0)
    SFVMK_ERR(pAdapter, "pTxq->nPendDesc == 0");
  if(pTxq->nPendDesc > pTxq->maxPktDesc)
    SFVMK_ERR(pAdapter, "pTxq->nPendDesc too large");
  if(pTxq->blocked)
    SFVMK_ERR(pAdapter, "pTxq->blocked");

  oldAdded = pTxq->added;
  /*
  ** SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
  **           "oldAdded: %d, reaped: %d", oldAdded, pTxq->reaped);
  */

  /* Post the fragment list. */
  rc = efx_tx_qdesc_post(pTxq->pCommonTxq, pTxq->pPendDesc, pTxq->nPendDesc,
        pTxq->reaped, &pTxq->added);
  if(rc != 0)
    SFVMK_ERR(pAdapter, "efx_tx_qdesc_post() failed");

  /* If efx_tx_qdesc_post() had to refragment, our information about
   * buffers to free may be associated with the wrong
   * descriptors.
   */
  if(pTxq->added - oldAdded != pTxq->nPendDesc)
    SFVMK_ERR(pAdapter, "efx_tx_qdesc_post() refragmented descriptors");

  level = pTxq->added - pTxq->reaped;
  if(level > pTxq->entries)
    SFVMK_ERR(pAdapter, "overfilled TX queue");

  /* Clear the fragment list. */
  pTxq->nPendDesc = 0;

  /*
   * Set the block level to ensure there is space to generate a
   * large number of descriptors for TSO.
   */
  blockLevel = EFX_TXQ_LIMIT(pTxq->entries) - pTxq->maxPktDesc;
  SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
            "added: %d, reaped: %d, level: %d, blocklevel: %d", pTxq->added, pTxq->reaped, level, blockLevel);

  /* Have we reached the block level? */
  if (level < blockLevel) {
    SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
    return;
  }

  /* Reap, and check again */
  sfvmk_txqReap(pTxq);
  level = pTxq->added - pTxq->reaped;
  if (level < blockLevel) {
    SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
    return;
  }

  pTxq->blocked = 1;
  /* Mark the queue as stopped */
  sfvmk_updateQueueStatus(pTxq->pAdapter, VMK_UPLINK_QUEUE_STATE_STOPPED);
  SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
            "Marked queue as stopped as level: %d", level);

  /*
   * Avoid a race with completion interrupt handling that could leave
   * the queue blocked.
   */
  vmk_CPUMemFenceWrite();
  sfvmk_txqReap(pTxq);
  level = pTxq->added - pTxq->reaped;
  if (level < blockLevel) {
    vmk_CPUMemFenceWrite();
    /* Mark the queue as started */
    pTxq->blocked = 0;
    sfvmk_updateQueueStatus(pTxq->pAdapter, VMK_UPLINK_QUEUE_STATE_STARTED);

    SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "Marked queue as started as level: %d", level);
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
}

/*! \brief move to the next stmp entry taking care of wrap around condition
**
** \param[in]  pTxq    pointer to txq
** \param[in]  pStmp   pointer to stmp entry
**
** \return: void
*/
static inline void
sfvmk_nextStmp(sfvmk_txq_t *pTxq, sfvmk_txMapping_t **pStmp)
{
  //SFVMK_DBG_FUNC_ENTRY(pTxq->pAdapter, SFVMK_DBG_TX);
  if (VMK_UNLIKELY(*pStmp == &pTxq->pStmp[pTxq->ptrMask]))
    *pStmp = &pTxq->pStmp[0];
  else
    (*pStmp)++;

  SFVMK_DBG(pTxq->pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
            "pStmp id: %ld", *pStmp - &pTxq->pStmp[0]);
  //SFVMK_DBG_FUNC_EXIT(pTxq->pAdapter, SFVMK_DBG_TX);
}

/*! \brief  insert vlan tag in the packet header
**
** \param[in,out]    ppPkt       pointer to pointer to packet to be transmitted
** \param[in]        vlanTag     vlan Tag to be inserted
**
** \return: VMK_OK if success, VMK_FAILURE otherwise
*
*/

static VMK_ReturnStatus
sfvmk_insertVlanHdr(vmk_PktHandle **ppPkt, vmk_uint16 vlanTag)
{
  VMK_ReturnStatus ret=VMK_OK;
  vmk_uint8 vlanHdrSize=4;
  vmk_PktHandle *pkt = *ppPkt;
  vmk_uint16 *ptr = NULL;
  vmk_uint8 *frameVA=NULL;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DBG_TX);
  /*
   *  See if we have enough headroom in the packet to insert vlan hdr
  */
  ret = vmk_PktPullHeadroom(pkt, vlanHdrSize);
  if (ret != VMK_OK) {
    SFVMK_ERROR("vmk_PktPullHeadroom failed: 0x%x", ret);
    vmk_PktHandle *pNewPkt = NULL;

    ret = vmk_PktPartialCopyWithHeadroom(pkt, SFVMK_VLAN_HDR_START_OFFSET,
                                            vlanHdrSize, &pNewPkt);
    if (VMK_UNLIKELY(ret != VMK_OK)) {
      SFVMK_ERROR("vmk_PktPartialCopyWithHeadroom failed: 0x%x", ret);
      SFVMK_DEBUG_FUNC_EXIT(SFVMK_DBG_TX);
      return ret;
    }

    ret = vmk_PktPullHeadroom(pNewPkt, vlanHdrSize);
    if (VMK_UNLIKELY(ret != VMK_OK)) {
      SFVMK_ERROR("vmk_PktPullHeadroom failed: 0x%x",ret);
      vmk_PktRelease(pNewPkt);
      SFVMK_DEBUG_FUNC_EXIT(SFVMK_DBG_TX);
      return ret;
    }

    /* TODO: We don't release the original packet ?? */
    *ppPkt = pkt = pNewPkt;
  }

  frameVA = (vmk_uint8 *) vmk_PktFrameMappedPointerGet(pkt);
  SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "pkt VA: %p, frame len: %d", frameVA, vmk_PktFrameLenGet(pkt));

  /* pull the mac headers to create space for vlan hdr */
  vmk_Memmove(frameVA, (frameVA + vlanHdrSize), SFVMK_VLAN_HDR_START_OFFSET);

  frameVA += SFVMK_VLAN_HDR_START_OFFSET;

  ptr = (vmk_uint16 *)frameVA;
  *ptr++ = sfvmk_swapBytes(VMK_ETH_TYPE_VLAN);
  *ptr = sfvmk_swapBytes(vlanTag);
  frameVA += vlanHdrSize;

  /* Invalidate cache entries */
  vmk_PktHeaderInvalidateAll(pkt);

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DBG_TX);
  return ret;
}



/*! \brief check and insert vlan tag in the packet header
**
** \param[in]        pTxq        pointer to txq
** \param[in,out]    ppPkt       pointer to pointer to packet to be transmitted
** \param[in,out]    pPktLen     pointer to packet length
**
** \return: void
*/
static int
sfvmk_txMaybeInsertTag(sfvmk_txq_t *pTxq, vmk_PktHandle **ppPkt, vmk_ByteCountSmall *pPktLen)
{
  uint16_t thisTag=0;
  vmk_VlanID vlanId;
  vmk_VlanPriority vlanPrio;
  vmk_PktHandle *pkt = *ppPkt;
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  if(vmk_PktMustVlanTag(pkt)) {
    const efx_nic_cfg_t *pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
    vlanId = vmk_PktVlanIDGet(pkt);
    vlanPrio = vmk_PktPriorityGet(pkt);

    thisTag = (vlanId & SFVMK_VLAN_VID_MASK) | (vlanPrio << SFVMK_VLAN_PRIO_SHIFT);

    SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_FUNCTION,
              "vlan_id: %d, prio: %d, tci: %d, hwVlanTci: %d",
              vlanId, vlanPrio, thisTag, pTxq->hwVlanTci);

    if(pNicCfg->enc_hw_tx_insert_vlan_enabled) {
      SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_FUNCTION,
                "FW assisted tag insertion..");
      if (thisTag == pTxq->hwVlanTci) {
        SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
        return (0);
      }

      efx_tx_qdesc_vlantci_create(pTxq->pCommonTxq,
          sfvmk_swapBytes(thisTag),
          &pTxq->pPendDesc[0]);

      pTxq->nPendDesc = 1;
      pTxq->hwVlanTci = thisTag;
      SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
      return (1);
    }
    else {
      SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_FUNCTION,
                  "software implementation for vlan tag insertion");
      if(VMK_OK == sfvmk_insertVlanHdr(&pkt, thisTag)) {
            *ppPkt = pkt;
            *pPktLen = *pPktLen+4;
      }
      SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
      return 0;
    }
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
  return 0;
}

/*! \brief  Print data members of tso structure
**
** \param[out] pTso        pointer to tso state structure
**
** \return: void
**
*/
static void
printTsoStruct(sfvmk_tsoState_t *pTso)
{
  SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
    "outLen: %d, \
    packetSpace: %u,\
    segsSpace: %u,\
    inLen: %u,\
    pkt: %p,",
    pTso->outLen,  
    pTso->packetSpace,
    pTso->segsSpace,  
    pTso->inLen,  
    pTso->pkt);
}

/*! \brief  Extract TSO information from packet headers 
**          and prepare header descriptor for FATSOv2
**
** \param[in]  pTxq        pointer to txq
** \param[out] pTso        pointer to tso state structure
** \param[in]  pHdrDmaSeg  pointer to header dma segment
** \param[in]  pkt         packet to be transmitted
**
** \return: void
**
*/
static void 
sfvmk_tsoStart(sfvmk_txq_t *pTxq,
               sfvmk_tsoState_t *pTso,
               const sfvmk_dmaSegment_t *pHdrDmaSeg,
               vmk_PktHandle *pkt)
{
  const efx_nic_cfg_t *pCfg = efx_nic_cfg_get(pTxq->pAdapter->pNic);
  VMK_ReturnStatus status;
  vmk_Bool isTcp;
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;
  vmk_PktHeaderEntry *pL2HdrEntry = NULL;
  vmk_PktHeaderEntry *pL3HdrEntry = NULL;
  vmk_PktHeaderEntry *pL4HdrEntry = NULL;
  void *pL3Hdr = NULL;
  void *pL4Hdr = NULL;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);
  pTso->fwAssisted = pTxq->tsoFwAssisted;
  pTso->pkt = pkt;

  /* Find network protocol and header */
  if((status = vmk_PktHeaderL2Find (pkt, &pL2HdrEntry, NULL)) != VMK_OK) {
    SFVMK_ERR(pAdapter, "Unable to find L2 header: %s", vmk_StatusToString(status));
    return; 
  }
    
  SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "L2 header entry: type: 0x%x, offset: %d, proto: 0x%04x, nextHdrOff: %d", 
              pL2HdrEntry->type, pL2HdrEntry->offset,
              sfvmk_swapBytes(pL2HdrEntry->nextHdrProto), pL2HdrEntry->nextHdrOffset);

  pTso->protocol = sfvmk_swapBytes(pL2HdrEntry->nextHdrProto);
  pTso->nhOff = pL2HdrEntry->nextHdrOffset;

  /*
  ** For a tagged packet (simulation mode), protocol and nextHdrOffset fields
  ** are returned as expected
  */
#ifdef DEBUG_VLAN
  if ((pL2HdrEntry->type == VMK_PKT_HEADER_L2_ETHERNET_802_1PQ) || 
             (pL2HdrEntry->type == VMK_PKT_HEADER_L2_ETHERNET_802_1PQ)) {
    SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, "Found tagged packet ");
    if(VMK_OK != (status = vmk_PktHeaderDataGet(pkt, pL2HdrEntry, (void **)&pEh))) {
      SFVMK_ERR(pAdapter, "Unable to fetch L2 header: %s", vmk_StatusToString(status));
      return ; 
    }

    vmk_VLANHdr *veh = ((vmk_uint8 *)pEh) + SFVMK_VLAN_HDR_START_OFFSET;
    SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                "veh->type: 0x%04x, vlan id: %d", sfvmk_swapBytes(veh->type), VMK_VLAN_HDR_GET_VID(veh));
    pTso->protocol = sfvmk_swapBytes(veh->type);
    pTso->nhOff = sizeof(*veh);
    vmk_PktHeaderDataRelease(pkt, pL2HdrEntry, (void *)pEh, VMK_FALSE);
  } 
#endif      

  /* Find TCP header */
  if ((pTso->protocol == VMK_ETH_TYPE_IPV4) || (pTso->protocol == VMK_ETH_TYPE_IPV6)) {
    status = vmk_PktHeaderL3Find(pkt, &pL3HdrEntry, NULL);
    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "No L3 header found: %s", vmk_StatusToString(status));
      return;
    }
    else
    {
      SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                  "l3 type: %x, offset: %d next proto: 0x%04x, offset: %d", 
                  pL3HdrEntry->type, pL3HdrEntry->offset, 
                  pL3HdrEntry->nextHdrProto, pL3HdrEntry->nextHdrOffset);

      isTcp = (pL3HdrEntry->nextHdrProto == VMK_IP_PROTO_TCP) ? VMK_TRUE:VMK_FALSE;

      if (isTcp) {
        pTso->tcphOff = pL3HdrEntry->nextHdrOffset;
        status = vmk_PktHeaderDataGet(pkt, pL3HdrEntry, (void **)&pL3Hdr);
        if (status == VMK_OK) {
          pTso->packetId = ((vmk_IPv4Hdr *)pL3Hdr)->identification;
          if(pTso->protocol == VMK_ETH_TYPE_IPV6)
            pTso->packetId = 0;

          vmk_PktHeaderDataRelease(pkt, pL3HdrEntry, (void *)pL3Hdr, VMK_FALSE);
        }
      }
      else {
        SFVMK_ERR(pAdapter, "Non TCP packet in TSO handling");
        return;
      }
    }
  }
  else {
    SFVMK_ERR(pAdapter, "Non IP packet, protocol : %d", pTso->protocol);
    return;
  }

  if (pTso->fwAssisted &&
      VMK_UNLIKELY(pTso->tcphOff > pCfg->enc_tx_tso_tcp_header_offset_limit)) {
    SFVMK_ERR(pAdapter, "tcp hdr offset: %d beyond limit: %d", 
              pTso->tcphOff, pCfg->enc_tx_tso_tcp_header_offset_limit);
    pTso->fwAssisted = 0;
  }


  if(pHdrDmaSeg->dsLen < pTso->tcphOff)
    SFVMK_ERR(pAdapter, "network header is fragmented in hdr segment");
  
  status = vmk_PktHeaderL4Find(pkt, &pL4HdrEntry, NULL);
  if (status == VMK_OK) {
    SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                "l4 type: %x, offset: %d next proto: 0x%04x, offset: %d", 
                pL4HdrEntry->type, pL4HdrEntry->offset, 
                sfvmk_swapBytes(pL4HdrEntry->nextHdrProto), pL4HdrEntry->nextHdrOffset);

    status = vmk_PktHeaderDataGet(pkt, pL4HdrEntry, (void **)&pL4Hdr);
    if (status == VMK_OK) {
      /* this is complete header len */
      pTso->headerLen = pTso->tcphOff + 4 * ((vmk_TCPHdr *)pL4Hdr)->dataOffset;
      SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                  "tcp header len: %d, initial seq: %u, flags: 0x%x",
                  pTso->headerLen, ((vmk_TCPHdr *)pL4Hdr)->seq, ((vmk_TCPHdr *)pL4Hdr)->flags);
    }
    else {
      SFVMK_ERR(pAdapter, "Couldn't get L4 hdr data : %s", vmk_StatusToString(status));
      return;
    }
  }

  pTso->seqnum = vmk_BE32ToCPU(((vmk_TCPHdr *)pL4Hdr)->seq);

  if(((vmk_TCPHdr *)pL4Hdr)->syn || ((vmk_TCPHdr *)pL4Hdr)->urg)
    SFVMK_ERR(pAdapter, "incompatible TCP flag 0x%x on TSO packet",
             ((vmk_TCPHdr *)pL4Hdr)->flags);
  pTso->tcpFlags = ((vmk_TCPHdr *)pL4Hdr)->flags;

  vmk_PktHeaderDataRelease(pkt, pL4HdrEntry, (void *)pL4Hdr, VMK_FALSE);
  pTso->outLen = vmk_PktFrameLenGet(pkt) - pTso->headerLen;

  if (pTso->fwAssisted) {
    SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                "hdr seg len: %d, headerLen: %d", pHdrDmaSeg->dsLen, pTso->headerLen);

    if (pHdrDmaSeg->dsLen >= pTso->headerLen) {
      efx_tx_qdesc_dma_create(pTxq->pCommonTxq,
            pHdrDmaSeg->dsAddr,
            pTso->headerLen,
            VMK_FALSE,
            &pTso->headerDesc);
    }
    else {
      pTso->fwAssisted = 0;
      SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                  "setting fwAssisted = 0");
    }
  }

  printTsoStruct(pTso);
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
}

/*! \brief  form descriptors for the current fragment,
**          until we reach the end of fragment or end-of-packet.
**
** \param[in]      pTxq        pointer to txq
** \param[in]      pTso        pointer to tso state structure
**
** \return: void
**
*/
static void
sfvmk_tsoFillPktWithFragments(sfvmk_txq_t *pTxq,
                              sfvmk_tsoState_t *pTso)
{
  efx_desc_t *desc;
  int n;
  uint64_t dmaAddr = pTso->dmaAddr;
  vmk_Bool eop;
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;

  SFVMK_NULL_PTR_CHECK(pAdapter);
//  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "inlen: %u, pktSpace: %u", pTso->inLen, pTso->packetSpace);
  if( (pTso->inLen <= 0) || (pTso->packetSpace <= 0)) {
    SFVMK_ERR(pAdapter, "Invalid TSO input length: %d or packet space: %d",
              pTso->inLen, pTso->packetSpace);
    return;
  }
  
  if (pTso->fwAssisted & SFVMK_FATSOV2) {
    n = MIN(pTso->inLen, pAdapter->txDmaDescMaxSize);
    pTso->outLen -= n;
    pTso->seqnum += n;
    pTso->inLen -= n;
    if (n < pTso->packetSpace) {
      pTso->packetSpace -= n;
      pTso->segsSpace--;
    } 
    else {
      pTso->packetSpace = pTso->segSize -
          (n - pTso->packetSpace) % pTso->segSize;
      pTso->segsSpace =
          EFX_TX_FATSOV2_DMA_SEGS_PER_PKT_MAX - 1 -
          (pTso->packetSpace != pTso->segSize);
    }
  } else {
    n = MIN(pTso->inLen, pTso->packetSpace);
    pTso->packetSpace -= n;
    pTso->outLen -= n;
    pTso->dmaAddr += n;
    pTso->inLen -= n;
  }

  /*
   * It is OK to use binary OR below to avoid extra branching
   * since all conditions may always be checked.
   */
  eop = (pTso->outLen == 0) | (pTso->packetSpace == 0) |
      (pTso->segsSpace == 0);
  if(eop)
    SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "eop at id: %u", pTxq->nPendDesc);
  desc = &pTxq->pPendDesc[pTxq->nPendDesc++];
  efx_tx_qdesc_dma_create(pTxq->pCommonTxq, dmaAddr, n, eop, desc);
//  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
}

/*! \brief  generate a new header and prepare for the new packet
**
** \param[in]      pTxq        pointer to txq
** \param[in]      pTso        pointer to tso state structure
** \param[in,out]  pId         pointer to stmp array index
**
** \return: stmp array index
**
*/
static int 
sfvmk_tsoStartNewPacket(sfvmk_txq_t *pTxq,
                        sfvmk_tsoState_t *pTso,
                        uint32_t *pId)
{
  unsigned int id = *pId;
  efx_desc_t *desc;
  VMK_ReturnStatus status;
  vmk_TCPHdr *pTcpHdr;
  unsigned ip_length;
  char *pHeader;
  uint64_t dmaAddr;
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DBG_TX);

  if (pTso->fwAssisted & SFVMK_FATSOV2) {
    /* Add 2 FATSOv2 option descriptors */
    desc = &pTxq->pPendDesc[pTxq->nPendDesc];
    efx_tx_qdesc_tso2_create(pTxq->pCommonTxq,
           pTso->packetId,
           pTso->seqnum,
           pTso->segSize,
           desc,
           EFX_TX_FATSOV2_OPT_NDESCS);
    desc += EFX_TX_FATSOV2_OPT_NDESCS;
    pTxq->nPendDesc += EFX_TX_FATSOV2_OPT_NDESCS;
    id = (id + EFX_TX_FATSOV2_OPT_NDESCS) & pTxq->ptrMask;

    pTso->segsSpace =
        EFX_TX_FATSOV2_DMA_SEGS_PER_PKT_MAX - 1;
 
    /* Header DMA descriptor */
    *desc = pTso->headerDesc;
    pTxq->nPendDesc++;
    id = (id + 1) & pTxq->ptrMask;
  } 
  else {
    SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, 
                "TSO Software implementation id: %d", id);
    /* Allocate a DMA-mapped header buffer. */
    if (VMK_LIKELY(pTso->headerLen <= TSOH_STD_SIZE)) {
      unsigned int page_index = (id / 2) / TSOH_PER_PAGE;
      unsigned int buf_index = (id / 2) % TSOH_PER_PAGE;

      SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, 
                  "page_index: %d, buf_index: %d", page_index, buf_index);
      pHeader = (pTxq->pTsohBuffer[page_index].pEsmBase +
          buf_index * TSOH_STD_SIZE);
      dmaAddr = (pTxq->pTsohBuffer[page_index].ioElem.ioAddr +
            buf_index * TSOH_STD_SIZE);
    } else {
      sfvmk_txMapping_t *pStmp = &pTxq->pStmp[id];
      efsys_mem_t *pHdrMem = &pStmp->u.hdrMem;

      pHdrMem->ioElem.length = pTso->headerLen;
      pHdrMem->pEsmBase = sfvmk_allocCoherentDMAMapping(pTxq->pAdapter->dmaEngine,
                                             pHdrMem->ioElem.length,
                                             &pHdrMem->ioElem.ioAddr);
      SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, 
                  "pEsmBase: %p, len: %d", pHdrMem->pEsmBase, pHdrMem->ioElem.length);

      if (VMK_UNLIKELY(NULL == pHdrMem->pEsmBase))
        return VMK_NO_MEMORY;

      pStmp->isPkt = VMK_FALSE;
      pHeader = pHdrMem->pEsmBase;
      dmaAddr = pHdrMem->ioElem.ioAddr;

      pTxq->tsoLongHeaders++;
    }

    pTcpHdr = (vmk_TCPHdr *)(pHeader + pTso->tcphOff);

    /* Copy and update the headers. */
    if(VMK_OK != (status = vmk_PktCopyBytesOut(pHeader, pTso->headerLen, 
                                               0, pTso->pkt)))
      SFVMK_ERR(pTxq->pAdapter, "Unable to copy pkt bytes to buffer %s", 
                vmk_StatusToString(status));
    pTcpHdr->seq = vmk_CPUToBE32(pTso->seqnum);
    pTso->seqnum += pTso->segSize;

    SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, 
                "outlen: %d, segSize: %d, flags: 0x%x",
                pTso->outLen, pTso->segSize, pTcpHdr->flags);

    if (pTso->outLen > pTso->segSize) {
      /* This packet will not finish the TSO burst. */
      ip_length = pTso->headerLen - pTso->nhOff + pTso->segSize;
      pTcpHdr->flags &= ~(TH_FIN | TH_PUSH);
    } 
    else {
      /* This packet will be the last in the TSO burst. */
      ip_length = pTso->headerLen - pTso->nhOff + pTso->outLen;
    }

    SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, 
                "seq: %u, ip_len: %d, tcp flags: 0x%x",
                pTcpHdr->seq, ip_length, pTcpHdr->flags);

    if (pTso->protocol == VMK_ETH_TYPE_IPV4) {
      vmk_IPv4Hdr *pIpHdr = (vmk_IPv4Hdr *)(pHeader + pTso->nhOff);
      pIpHdr->totalLength = vmk_CPUToBE16(ip_length);
    } 
    else if(pTso->protocol == VMK_ETH_TYPE_IPV6) {
      vmk_IPv6Hdr *pIpHdr =
        (vmk_IPv6Hdr *)(pHeader + pTso->nhOff);
      pIpHdr->payloadLength = vmk_CPUToBE16(ip_length - sizeof(*pIpHdr));
    }

    /* Form a descriptor for this header. */
    desc = &pTxq->pPendDesc[pTxq->nPendDesc++];
    efx_tx_qdesc_dma_create(pTxq->pCommonTxq,
          dmaAddr,
          pTso->headerLen,
          0,
          desc);
    id = (id + 1) & pTxq->ptrMask;

    pTso->segsSpace = VMK_UINT32_MAX;
  }
  pTso->packetSpace = pTso->segSize;
  pTxq->tsoPackets++;
  *pId = id;

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DBG_TX);
  return (0);
}

/*! \brief  TCP Segmentation Offload processing
**
** \param[in]  pTxq        pointer to txq
** \param[in]  pkt         packet to be transmitted
** \param[in]  pDmaSeg     pointer to array of pkt segments
** \param[in]  nDmaSeg     number of pkt segments
** \param[in]  vlanTagged  flag for vlan tag presence
** \param[in]  tsoSegSize  MSS for TSO processing
**
** \return: descriptor index for the next entry to be added or -1
**
*/
static int
sfvmk_txQueueTso(sfvmk_txq_t *pTxq, vmk_PktHandle *pkt, 
      sfvmk_dmaSegment_t *pDmaSeg, vmk_uint16 nDmaSeg, 
      vmk_uint16 vlanTagged, vmk_uint16 tsoSegSize)
{
  sfvmk_tsoState_t tso;
  vmk_uint32 id;
  vmk_uint32 numFatsoOptDesc=0;
  vmk_uint32 skipped = 0;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DBG_TX);

  tso.segSize = tsoSegSize;
  sfvmk_tsoStart(pTxq, &tso, pDmaSeg, pkt);

  /* go over dma segments which include just headers */
  while (pDmaSeg->dsLen + skipped <= tso.headerLen) {
    skipped += pDmaSeg->dsLen;
    --nDmaSeg;
    VMK_ASSERT_BUG(nDmaSeg != 0, "No payload in TSO packet");
    ++pDmaSeg;
  }

  /* first pDmaSeg is pointing to segment containing partial hdr and data */
  SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, 
             "skipped: %d, nDmaSeg: %d", skipped, nDmaSeg);

  /* inLen will contain the data bytes in pDmaSeg */
  tso.inLen = pDmaSeg->dsLen - (tso.headerLen - skipped);
  /* dmaAddr will point to address of first data byte */
  tso.dmaAddr = pDmaSeg->dsAddr + (tso.headerLen - skipped);

  SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, 
              "inLen: %d, dmaAddr: %lx", tso.inLen, tso.dmaAddr);

  id = (pTxq->added + vlanTagged) & pTxq->ptrMask;

  if (VMK_UNLIKELY(sfvmk_tsoStartNewPacket(pTxq, &tso, &id)))
    return (-1);

  numFatsoOptDesc = (tso.fwAssisted & SFVMK_FATSOV2) ?
                    EFX_TX_FATSOV2_OPT_NDESCS : 0;

  while (1) {
    sfvmk_tsoFillPktWithFragments(pTxq, &tso);
    /* Exactly one DMA descriptor is added */
    id = (id + 1) & pTxq->ptrMask;

    SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, "id: %d", id);
    /* Move onto the next fragment? */
    if (tso.inLen == 0) {
      --nDmaSeg;
      if (nDmaSeg == 0)
        break;
      ++pDmaSeg;
      tso.inLen = pDmaSeg->dsLen;
      tso.dmaAddr = pDmaSeg->dsAddr;
    }
    printTsoStruct(&tso);

    /* End of packet? */
    if ((tso.packetSpace == 0) | (tso.segsSpace == 0)) {
      /* If the queue is now full due to tiny MSS,
       * or we can't create another header, discard
       * the remainder of the input buf but do not
       * roll back the work we have done.
       */
      if (pTxq->nPendDesc + numFatsoOptDesc +
          1 /* header */ + nDmaSeg > pTxq->maxPktDesc) {
        pTxq->tsoPktDropTooMany++;
        SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                    "pTxq->tsoPktDropTooMany: %lu", pTxq->tsoPktDropTooMany);
        break;
      }
      if (VMK_UNLIKELY(sfvmk_tsoStartNewPacket(pTxq, &tso, &id))) {
        pTxq->tsoPktDropNoRscr++;
        SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                    "pTxq->tsoPktDropNoRscr: %lu", pTxq->tsoPktDropNoRscr);
        break;
      }
    }
  }

  pTxq->tsoBursts++;
  SFVMK_DEBUG(SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, "tsoBursts: %lu id: %d", pTxq->tsoBursts, id);
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DBG_TX);
  return (id);
}

/*! \brief fill the transmit buffer descriptor with pkt fragments
**
** \param[in]  pAdapter  pointer to adapter structure
** \param[in]  pTxq      pointer to txq
** \param[in]  pkt       packet to be transmitted
** \param[in]  pktLen    length of the packet to be transmitted
**
** \return: VMK_OK in case of success, VMK_FAILURE otherwise
**
*/

VMK_ReturnStatus
sfvmk_populateTxDescriptor(sfvmk_adapter_t *pAdapter,sfvmk_txq_t *pTxq,vmk_PktHandle *pkt, vmk_ByteCountSmall pktLen)
{
  int i=0;
  int id=0;
  vmk_uint16 numElems=0;
  vmk_uint16 vlanTagged=0;
  const vmk_SgElem *pSgElem;
  efx_desc_t *desc;
  int eop;
  vmk_SgElem inAddr, mappedAddr;
  vmk_DMAMapErrorInfo dmaMapErr;
  vmk_ByteCountSmall elemLength;
  int startId = id = (pTxq->added) & pTxq->ptrMask;
  sfvmk_txMapping_t *pStmp = &pTxq->pStmp[id];
  sfvmk_dmaSegment_t *pDmaSeg = NULL;
  VMK_ReturnStatus status;

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  /* VLAN handling */
  vlanTagged = sfvmk_txMaybeInsertTag(pTxq, &pkt, &pktLen);
  if (vlanTagged) {
     sfvmk_nextStmp(pTxq, &pStmp);
  }

  SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
        "id: %d, pTxq: %p, pStmp: %p, vlanTagged: %d", id, pTxq, pStmp, vlanTagged);
  numElems = vmk_PktSgArrayGet(pkt)->numElems;
  SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
        "number of pkt segments: %d", numElems);

  /*
  ** TODO: This allocation from heap can be replaced with stack if we can
  ** safely estimate the upper limit on the number of SG elements that can
  ** be generated for one packet
  */
  pDmaSeg = vmk_HeapAlloc(sfvmk_ModInfo.heapID, sizeof(sfvmk_dmaSegment_t)*numElems);
  if(!pDmaSeg) {
    SFVMK_ERR(pAdapter, "Couldn't allocate memory for dma segments");
    return VMK_FAILURE;
  }

  for (i = 0; i < numElems; i++) {

    /* Get MA of pSgElemment and its length. */
    pSgElem = vmk_PktSgElemGet(pkt, i);
    SFVMK_NULL_PTR_CHECK(pSgElem);

    elemLength = pSgElem->length;
    SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "pSgElem %d len: %d, pkt len: %d", i+1, elemLength, pktLen);

    if (pktLen < elemLength) {
      elemLength = pktLen;
    }

    pktLen -= elemLength;

    inAddr.addr = pSgElem->addr;
    inAddr.length = elemLength;
    status = vmk_DMAMapElem(pAdapter->dmaEngine,
        VMK_DMA_DIRECTION_FROM_MEMORY,
        &inAddr, VMK_TRUE, &mappedAddr, &dmaMapErr);

    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter,"Failed to map %p size %d to IO address, %s",
          pkt, vmk_PktFrameLenGet(pkt),
          vmk_DMAMapErrorReasonToString(dmaMapErr.reason));
      goto err_ret;
    }
    else {
      SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
               "Mapped pSgElem addr %lx len: %d to IO addr: %lx, len: %d",
               inAddr.addr, inAddr.length, mappedAddr.ioAddr, mappedAddr.length);
    }

    pDmaSeg[i].dsAddr = mappedAddr.ioAddr;
    pDmaSeg[i].dsLen = elemLength;
  }

  if(vmk_PktIsLargeTcpPacket(pkt)) {
    vmk_uint16 tsoSegSize = vmk_PktGetLargeTcpPacketMss(pkt);
    vmk_uint16 j=0;
    SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "TSO handling required, mss: %d", tsoSegSize);
    status = sfvmk_txQueueTso(pTxq, pkt, pDmaSeg, numElems, vlanTagged, tsoSegSize);
    if (status < 0)
      goto err_ret;
    else if (status < id) { /* wrap around condition */
      int diff = (pTxq->ptrMask+1-id);
      status = id + diff + status;
      SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, "WRAP AROUND, status: %d", status);
    }

    for(i=id;i<status;i++) {
      /* for option descriptors, make sure txqComplete doesn't try clean-up */
      pStmp->isPkt = VMK_TRUE;
      pStmp->u.pkt = NULL;

      if(status - i > numElems) {
        SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                 "Bypassing id: %d for completion, pStmp: %p", i, pStmp);
        pStmp->sgelem.ioAddr = 0;
        sfvmk_nextStmp(pTxq, &pStmp);
        continue;
      }
      vmk_Memcpy(&pStmp->sgelem, &pDmaSeg[j++], sizeof(sfvmk_dmaSegment_t));
      if(i != status -1)
        sfvmk_nextStmp(pTxq, &pStmp);
    }
    SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
             "Filling pkt %p pStmp: %p, pStmp[%d]: %p", pkt, pStmp,
             (status - 1) & pTxq->ptrMask,
             &pTxq->pStmp[(status - 1) & pTxq->ptrMask]);
    pStmp->u.pkt = pkt;
    pStmp->isPkt = VMK_TRUE;    
  }
  else {
    for (i = 0; i < numElems; i++) {
      /* update the list of in-flight packets */
      id = (pTxq->added + i + vlanTagged) & pTxq->ptrMask;
      SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                "added: %d pStmp: %p, id: %d, &pTxq->pStmp[id]: %p, pTxq: %p",
                 pTxq->added, pStmp, id, &pTxq->pStmp[id], pTxq);

      vmk_Memcpy(&pStmp->sgelem, &pDmaSeg[i], sizeof(sfvmk_dmaSegment_t));
      pStmp->u.pkt=NULL;
      pStmp->isPkt = VMK_TRUE;

      /*TODO: handle pDmaSeg[i].dsLen > enc_tx_dma_desc_size_max */

      desc = &pTxq->pPendDesc[i + vlanTagged];
      eop = (i == numElems - 1);
      efx_tx_qdesc_dma_create(pTxq->pCommonTxq,
                        pDmaSeg[i].dsAddr ,
                        pDmaSeg[i].dsLen,
                        eop,
                        desc);
      if(!eop)
          sfvmk_nextStmp(pTxq, &pStmp);
    }

    /* fill the pkt handle in the last mapping area */
    if(pStmp) {
      SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                "Filling in pkt handle %p for id: %d", pkt, id);
      pStmp->u.pkt = pkt;
    }

    pTxq->nPendDesc = numElems + vlanTagged;
  }

  /* Post the pSgElemment list. */
  sfvmk_txqListPost(pTxq);

  vmk_HeapFree(sfvmk_ModInfo.heapID, pDmaSeg);
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);

  return  VMK_OK;

err_ret:
  vmk_HeapFree(sfvmk_ModInfo.heapID, pDmaSeg);
  //check for successfully mapped elements and unmap them
  for (i = startId+vlanTagged; i < id; i++) {
     pStmp = &pTxq->pStmp[i];
     if (pStmp->sgelem.ioAddr != 0) {
       SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                 "Unmapping pSgElem at addr: %lx", pStmp->sgelem.ioAddr);
       vmk_DMAUnmapElem(pTxq->pAdapter->dmaEngine, VMK_DMA_DIRECTION_FROM_MEMORY,
                        &pStmp->sgelem);
     }
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);

  return VMK_FAILURE;
}

/*! \brief transmit the packet on the uplink interface
**
** \param[in]  pAdapter  pointer to adapter structure
** \param[in]  pTxq      pointer to txq
** \param[in]  pkt       packet to be transmitted
** \param[in]  pktLen    length of the packet to be transmitted
**
** \return: VMK_OK in case of success or VMK_FAILURE otherwise
*/

VMK_ReturnStatus sfvmk_transmitPkt(sfvmk_adapter_t *pAdapter,  sfvmk_txq_t *pTxq, vmk_PktHandle *pkt, vmk_ByteCountSmall pktLen)
{
  VMK_ReturnStatus status;

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  if(!pTxq || !pTxq->pCommonTxq) {
    SFVMK_ERR(pAdapter, "sfvmk_transmitPkt: Txq not initialized yet");
    return VMK_FAILURE;
  }
  unsigned int pushed = pTxq->added;

  status = sfvmk_populateTxDescriptor(pAdapter, pTxq, pkt, pktLen);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed to populate tx desc");
    return status;
  }

  if (pTxq->blocked) {
    SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_INFO, "Txq is blocked, returning");
  }

  SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, "added: %d, pushed: %d", pTxq->added, pushed);
  if (pTxq->added != pushed)
    efx_tx_qpush(pTxq->pCommonTxq, pTxq->added, pushed);

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);

  return VMK_OK;
}

