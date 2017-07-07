/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"

#define SFVMK_TXQ_STOP_POLL_WAIT 100*SFVMK_ONE_MILISEC
#define SFVMK_TXQ_STOP_POLL_TIMEOUT 20

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

  /* Free the context arrays. */
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
  pTxq->pStmp = sfvmk_memPoolAlloc(sizeof(struct sfvmk_tx_mapping_s) * pAdapter->txqEntries);
  if (NULL == pTxq->pStmp) {
    SFVMK_ERR(pAdapter,"failed to allocate memory for stmp");
    goto sfvmk_stmp_alloc_fail;
  }

  status = sfvmk_mutexInit("txq" ,SFVMK_TXQ_LOCK_RANK, &pTxq->lock);
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
  sfvmk_memPoolFree(pTxq->pStmp, pAdapter->txqEntries);
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

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  pIntr = &pAdapter->intr;

  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_INITIALIZED,
                    "intr->state != SFXGE_INTR_INITIALIZED");

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
    sfvmk_tx_mapping_t *pStmp;
    unsigned int id;

    id = completed++ & pTxq->ptrMask;
    SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
              "completed: %d, pending: %d, id: %d", completed, pTxq->pending, id);

    pStmp = &pTxq->pStmp[id];
    if (pStmp->sgelem.ioAddr != 0) {
      SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
               "Unmapping frag at addr: %lx", pStmp->sgelem.ioAddr);
      vmk_DMAUnmapElem(pAdapter->dmaEngine, VMK_DMA_DIRECTION_FROM_MEMORY, &pStmp->sgelem);
    }

    if (pStmp->pkt) {
      SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
               "Free pkt: %p", pStmp->pkt);
      if(pTxq->initState == SFVMK_TXQ_STARTED)
        vmk_NetPollQueueCompPkt(pEvq->netPoll, pStmp->pkt);
      else
        vmk_PktRelease(pStmp->pkt);
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

  VMK_ASSERT_BUG(pTxq->initState == SFVMK_TXQ_STARTED);

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

    VMK_ASSERT(pTxq->flushState != SFVMK_FLUSH_FAILED);

    if (pTxq->flushState != SFVMK_FLUSH_DONE) {
      /* Flush timeout */
      SFVMK_ERR(pAdapter, " Cannot flush Tx queue %u", qIndex);
      pTxq->flushState = SFVMK_FLUSH_DONE;
    }
  }

  pTxq->blocked = 0;
  pTxq->pending = pTxq->added;

  sfvmk_txqComplete(pTxq, pEvq);
  VMK_ASSERT(pTxq->completed == pTxq->added);

  sfvmk_txqReap(pTxq);
  VMK_ASSERT(pTxq->reaped == pTxq->completed);

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
      VMK_ASSERT(0);
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
  unsigned int old_added;
  unsigned int block_level;
  unsigned int level;
  int rc;
  sfvmk_adapter_t *pAdapter = pTxq->pAdapter;

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  VMK_ASSERT(pTxq->nPendDesc != 0, ("pTxq->nPendDesc == 0"));
  VMK_ASSERT(pTxq->nPendDesc <= pTxq->maxPktDesc, ("pTxq->nPendDesc too large"));
  VMK_ASSERT(!pTxq->blocked, ("pTxq->blocked"));

  old_added = pTxq->added;
  SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
            "old_added: %d, reaped: %d", old_added, pTxq->reaped);

  /* Post the fragment list. */
  rc = efx_tx_qdesc_post(pTxq->pCommonTxq, pTxq->pPendDesc, pTxq->nPendDesc,
        pTxq->reaped, &pTxq->added);
  VMK_ASSERT(rc == 0, ("efx_tx_qdesc_post() failed"));

  /* If efx_tx_qdesc_post() had to refragment, our information about
   * buffers to free may be associated with the wrong
   * descriptors.
   */
  SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
            "added: %d, reaped: %d", pTxq->added, pTxq->reaped);
  VMK_ASSERT(pTxq->added - old_added == pTxq->nPendDesc,
             ("efx_tx_qdesc_post() refragmented descriptors"));

  level = pTxq->added - pTxq->reaped;
  VMK_ASSERT(level <= pTxq->entries, ("overfilled TX queue"));

  /* Clear the fragment list. */
  pTxq->nPendDesc = 0;

  /*
   * Set the block level to ensure there is space to generate a
   * large number of descriptors for TSO.
   */
  block_level = EFX_TXQ_LIMIT(pTxq->entries) - pTxq->maxPktDesc;
  SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
        "TXQ_LIMIT: %d, max_pkt_dec: %d, block_level: %d",
        EFX_TXQ_LIMIT(pTxq->entries), pTxq->maxPktDesc, block_level);

  /* Have we reached the block level? */
  if (level < block_level) {
    SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
    return;
  }

  /* Reap, and check again */
  sfvmk_txqReap(pTxq);
  level = pTxq->added - pTxq->reaped;
  if (level < block_level) {
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
  if (level < block_level) {
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
sfvmk_nextStmp(sfvmk_txq_t *pTxq, sfvmk_tx_mapping_t **pStmp)
{
  SFVMK_DBG_FUNC_ENTRY(pTxq->pAdapter, SFVMK_DBG_TX);
  if (VMK_UNLIKELY(*pStmp == &pTxq->pStmp[pTxq->ptrMask]))
    *pStmp = &pTxq->pStmp[0];
  else
    (*pStmp)++;

  SFVMK_DBG(pTxq->pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
            "pStmp id: %ld", *pStmp - &pTxq->pStmp[0]);
  SFVMK_DBG_FUNC_EXIT(pTxq->pAdapter, SFVMK_DBG_TX);
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


/*! \brief fill the transmit buffer descriptor with pkt fragments
**
** \param[in]  pAdapter  pointer to adapter structure
** \param[in]  pTxq      pointer to txq
** \param[in]  pkt       packet to be transmitted
** \param[in]  pktLen    length of the packet to be transmitted
**
** \return: void
**
*/

void
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
  sfvmk_tx_mapping_t *pStmp = &pTxq->pStmp[id];
  sfvmk_dma_segment_t *pDmaSeg = NULL;

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

  /* VLAN handling */
  vlanTagged = sfvmk_txMaybeInsertTag(pTxq, &pkt, &pktLen);
  if (vlanTagged) {
     sfvmk_nextStmp(pTxq, &pStmp);
  }

  numElems = vmk_PktSgArrayGet(pkt)->numElems;
  SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
        "number of pkt segments: %d", numElems);

  pDmaSeg = vmk_HeapAlloc(sfvmk_ModInfo.heapID, sizeof(sfvmk_dma_segment_t)*numElems);
  if(!pDmaSeg) {
    SFVMK_ERR(pAdapter, "Couldn't allocate memory for dma segments");
    return;
  }

  for (i = 0; i < numElems; i++) {
    VMK_ReturnStatus status;

    /* Get MA of pSgElemment and its length. */
    pSgElem = vmk_PktSgElemGet(pkt, i);
    VMK_ASSERT(pSgElem != NULL);

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

    pDmaSeg[i].ds_addr = mappedAddr.ioAddr;
    pDmaSeg[i].ds_len = elemLength;
  }

  if(vmk_PktIsLargeTcpPacket(pkt)) {
    vmk_uint16 tsoSegSize = vmk_PktGetLargeTcpPacketMss(pkt);
    vmk_LogMessage("TSO handling required, mss: %d", tsoSegSize);
    /*TODO: TSO processing*/
  }
  else {
    for (i = 0; i < numElems; i++) {
      /* update the list of in-flight packets */
      id = (pTxq->added + i + vlanTagged) & pTxq->ptrMask;
      SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                "added: %d pStmp: %p, id: %d, &pTxq->pStmp[id]: %p",
                 pTxq->added, pStmp, id, &pTxq->pStmp[id]);

      vmk_Memcpy(&pStmp->sgelem, &pDmaSeg[i], sizeof(sfvmk_dma_segment_t));
      pStmp->pkt=NULL;

      desc = &pTxq->pPendDesc[i + vlanTagged];
      eop = (i == numElems - 1);
      efx_tx_qdesc_dma_create(pTxq->pCommonTxq,
                        pDmaSeg[i].ds_addr ,
                        pDmaSeg[i].ds_len,
                        eop,
                        desc);
      if(!eop)
          sfvmk_nextStmp(pTxq, &pStmp);
    }

    /* fill the pkt handle in the last mapping area */
    if(pStmp) {
      SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG,
                "Filling in pkt handle %p for id: %d", pkt, id);
      pStmp->pkt = pkt;
    }

    pTxq->nPendDesc = numElems + vlanTagged;
  }

  /* Post the pSgElemment list. */
  sfvmk_txqListPost(pTxq);

  vmk_HeapFree(sfvmk_ModInfo.heapID, pDmaSeg);
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
  return;

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
}

/*! \brief transmit the packet on the uplink interface
**
** \param[in]  pAdapter  pointer to adapter structure
** \param[in]  pTxq      pointer to txq
** \param[in]  pkt       packet to be transmitted
** \param[in]  pktLen    length of the packet to be transmitted
**
** \return: void
*/

void sfvmk_transmitPkt(sfvmk_adapter_t *pAdapter,  sfvmk_txq_t *pTxq, vmk_PktHandle *pkt, vmk_ByteCountSmall pktLen)
{
   SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_TX);

   if(!pTxq || !pTxq->pCommonTxq) {
     SFVMK_ERR(pAdapter, "sfvmk_transmitPkt: Txq not initialized yet");
     return;
   }
   unsigned int pushed = pTxq->added;

   sfvmk_populateTxDescriptor(pAdapter, pTxq, pkt, pktLen);

   if (pTxq->blocked) {
     SFVMK_ERR(pAdapter, "Txq is blocked, returning");
     return;
   }

   SFVMK_DBG(pAdapter, SFVMK_DBG_TX, SFVMK_LOG_LEVEL_DBG, "added: %d, pushed: %d", pTxq->added, pushed);
   if (pTxq->added != pushed)
     efx_tx_qpush(pTxq->pCommonTxq, pTxq->added, pushed);

   SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_TX);
}

