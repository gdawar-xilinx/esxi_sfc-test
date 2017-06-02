
/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"

/**-----------------------------------------------------------------------------
*
* sfvmk_txqFini --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param  adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
static void
sfvmk_txqFini(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_txq_t *pTxq;
  efsys_mem_t *pTxqMem;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");

  pTxq = pAdapter->pTxq[qIndex];

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");
  pTxqMem = &pTxq->mem;

  VMK_ASSERT_BUG(pTxq->initState == SFVMK_TXQ_INITIALIZED,
                    "txq->initState != SFVMK_TXQ_INITIALIZED");

  /* Free the context arrays. */
  sfvmk_memPoolFree(pTxq->pPendDesc , pTxq->pendDescSize);

  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pTxqMem->esm_base, pTxqMem->io_elem.ioAddr,
                                pTxqMem->io_elem.length);
  pAdapter->pTxq[qIndex] = NULL;

  sfvmk_mutexDestroy(pTxq->lock);

  sfvmk_memPoolFree(pTxq,  sizeof(sfvmk_txq_t));

}

/**-----------------------------------------------------------------------------
*
* sfvmk_txqInit --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param  adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
static VMK_ReturnStatus
sfvmk_txqInit(sfvmk_adapter_t *pAdapter, unsigned int txqIndex,
                     enum sfvmk_txqType type, unsigned int evqIndex)
{
  sfvmk_txq_t *pTxq;
  sfvmk_evq_t *pEvq;
  efsys_mem_t *pTxqMem;
  VMK_ReturnStatus status ;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");

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
  pTxqMem->esm_handle  = pAdapter->dmaEngine;

  pEvq = pAdapter->pEvq[evqIndex];

  /* Allocate and zero DMA space for the descriptor ring. */
  pTxqMem->io_elem.length = EFX_TXQ_SIZE(pAdapter->txqEntries);

  pTxqMem->esm_base = sfvmk_allocCoherentDMAMapping(pAdapter->dmaEngine,
                                                  pTxqMem->io_elem.length,
                                                  &pTxqMem->io_elem.ioAddr);
  if(NULL == pTxqMem->esm_base) {
    SFVMK_ERR(pAdapter,"failed to allocate memory for txq enteries");
    goto sfvmk_dma_alloc_fail;
  }

  /* Allocate pending descriptor array for batching writes. */
  pTxq->pPendDesc = sfvmk_memPoolAlloc(sizeof(efx_desc_t) * pAdapter->txqEntries);
  if (NULL == pTxq->pPendDesc) {
    SFVMK_ERR(pAdapter,"failed to allocate memory for pend desc");
    goto sfvmk_pPendDesc_alloc_fail;
  }
  pTxq->pendDescSize = sizeof(efx_desc_t) * pAdapter->txqEntries;

  status = sfvmk_mutexInit("pTxq" ,VMK_MUTEX_RANK_HIGHEST - 2, &pTxq->lock);
  if(status != VMK_OK)
    goto sfvmk_mutex_fail;

  pTxq->type = type;
  pTxq->evqIndex = evqIndex;
  pTxq->txqIndex = txqIndex;
  pTxq->initState = SFVMK_TXQ_INITIALIZED;

  return VMK_OK;

sfvmk_mutex_fail:
  sfvmk_memPoolFree(pTxq->pPendDesc, pTxq->pendDescSize);
sfvmk_pPendDesc_alloc_fail:
  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pTxqMem->esm_base,
                                pTxqMem->io_elem.ioAddr, pTxqMem->io_elem.length);
sfvmk_dma_alloc_fail:
  sfvmk_memPoolFree(pTxq, sizeof(sfvmk_txq_t));
sfvmk_alloc_fail:
  return VMK_FAILURE;


}

/**-----------------------------------------------------------------------------
*
* sfvmk_txInit --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param  adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
int
sfvmk_txInit(sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  vmk_uint32    qIndex;
  vmk_uint32    evqIndex = 0;

  int rc;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");

  pIntr = &pAdapter->intr;

  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_INITIALIZED,
                    "intr->state != SFXGE_INTR_INITIALIZED");

  pAdapter->txqCount = SFVMK_TXQ_NTYPES - 1 + pIntr->numIntrAlloc;

  /* Initialize transmit queues */
  rc = sfvmk_txqInit(pAdapter, SFVMK_TXQ_NON_CKSUM, SFVMK_TXQ_NON_CKSUM, 0);
  if(rc) {
    SFVMK_ERR(pAdapter, "failed to init txq[SFVMK_TXQ_NON_CKSUM]");
    goto sfvmk_fail;
  }

  rc = sfvmk_txqInit(pAdapter, SFVMK_TXQ_IP_CKSUM,  SFVMK_TXQ_IP_CKSUM, 0);
  if(rc) {
    SFVMK_ERR(pAdapter,"failed to init txq[SFVMK_TXQ_IP_CKSUM]");
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

  return (0);

sfvmk_fail3:
  while (--qIndex >= SFVMK_TXQ_NTYPES - 1)
    sfvmk_txqFini(pAdapter,  qIndex);

  sfvmk_txqFini(pAdapter, SFVMK_TXQ_IP_CKSUM);

sfvmk_fail2:
  sfvmk_txqFini(pAdapter, SFVMK_TXQ_NON_CKSUM);

sfvmk_fail:
  pAdapter->txqCount = 0;

  return (rc);
}
/**-----------------------------------------------------------------------------
*
* sfvmk_txFini --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param  adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
void
sfvmk_txFini(sfvmk_adapter_t *pAdapter)
{
  int qIndex;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");

  qIndex = pAdapter->txqCount;
  while (--qIndex >= 0)
    sfvmk_txqFini(pAdapter, qIndex);

  pAdapter->txqCount = 0;
}

/**-----------------------------------------------------------------------------
*
* sfvmk_txqComplete --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param  adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
void
sfvmk_txqComplete(sfvmk_txq_t *pTxq, sfvmk_evq_t *pEvq)
{
}
/**-----------------------------------------------------------------------------
*
* sfvmk_txqReap --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param  adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
static void
sfvmk_txqReap(sfvmk_txq_t *pTxq)
{
  SFVMK_TXQ_LOCK_ASSERT_OWNED(pTxq);
  pTxq->reaped = pTxq->completed;
}
/**-----------------------------------------------------------------------------
*
* sfvmk_txqStop --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param  adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
static void
sfvmk_txqStop(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_txq_t *pTxq;
  sfvmk_evq_t *pEvq;
  vmk_uint32 count;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");

  SFVMK_ADAPTER_LOCK_ASSERT_OWNED(pAdapter);

  pTxq = pAdapter->pTxq[qIndex];
  pEvq = pAdapter->pEvq[pTxq->evqIndex];

  SFVMK_TXQ_LOCK(pTxq);
  SFVMK_EVQ_LOCK(pEvq);

  VMK_ASSERT(pTxq->initState == SFVMK_TXQ_STARTED);

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
          /* Spin for 100ms. */
          vmk_DelayUsecs(100*1000);

          if (pTxq->flushState != SFVMK_FLUSH_PENDING)
            break;

      } while (++count < 20);
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
}

/**-----------------------------------------------------------------------------
*
* sfvmk_txStop --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param  adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/

void sfvmk_txStop(sfvmk_adapter_t *pAdapter)
{
  int qIndex;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");

  qIndex = pAdapter->txqCount;
  while (--qIndex >= 0)
    sfvmk_txqStop(pAdapter, qIndex);

  /* Tear down the transmit module */
  efx_tx_fini(pAdapter->pNic);
}

/**-----------------------------------------------------------------------------
*
* sfvmk_txqStart --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param  adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/

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

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");

  SFVMK_ADAPTER_LOCK_ASSERT_OWNED(pAdapter);

  pTxq = pAdapter->pTxq[qIndex];
  VMK_ASSERT_BUG(NULL != pTxq, " NULL txq ptr");

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

  //praveen
  //  pAdapter->tsoFwAssisted needs to be initialized

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

  /* Initialise queue descriptor indexes */
  pTxq->added = pTxq->pending = pTxq->completed = pTxq->reaped = descIndex;

  SFVMK_TXQ_LOCK(pTxq);

  /* Enable the transmit queue. */
  efx_tx_qenable(pTxq->pCommonTxq);

  pTxq->initState = SFVMK_TXQ_STARTED;
  pTxq->flushState = SFVMK_FLUSH_REQUIRED;
  pTxq->tsoFwAssisted = tsoFwAssisted;

  // will see later
  /*
  pTxq->max_pkt_desc = sfvmk_tx_max_pkt_desc(pAdapter, pTxq->type,
  tsoFwAssisted);
  */

  SFVMK_TXQ_UNLOCK(pTxq);

  return (0);

  fail:
    return (rc);
}

/**-----------------------------------------------------------------------------
*
* sfvmk_txStart --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param  adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
VMK_ReturnStatus
sfvmk_txStart(sfvmk_adapter_t *pAdapter)
{
  int qIndex;
  int rc;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");
  SFVMK_DBG(pAdapter, SFVMK_DBG_TX, 5, "entered sfvmk_txStart");

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

  return VMK_OK;

sfvmk_fail:
  while (--qIndex >= 0)
    sfvmk_txqStop(pAdapter, qIndex);

  efx_tx_fini(pAdapter->pNic);
  pAdapter->txqCount = 0;
  return VMK_FAILURE;
}

