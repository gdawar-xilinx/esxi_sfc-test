
/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"

/* default interrupt moderation value */
#define SFVMK_MODERATION  30

static void
sfvmk_evqFini(sfvmk_adapter_t *pAdapter, unsigned int qIndex);
static VMK_ReturnStatus
sfvmk_evqInit(sfvmk_adapter_t *pAdapter, unsigned int qIndex);
static void
sfvmk_evqStop(sfvmk_adapter_t *pAdapter, unsigned int qIndex);
static int
sfvmk_evqStart(sfvmk_adapter_t *pAdapter, unsigned int qIndex);




static boolean_t sfvmk_evInitialized(void *arg);
static boolean_t sfvmk_evRX(void *arg, uint32_t label, uint32_t id, uint32_t size,
           uint16_t flags);

static boolean_t sfvmk_evTX(void *arg, uint32_t label, uint32_t id);
static boolean_t sfvmk_evException(void *arg, uint32_t code, uint32_t data);
static boolean_t sfvmk_evRxqFlushDone(void *arg, uint32_t rxq_index);
static boolean_t sfvmk_evRxqFlushFailed(void *arg, uint32_t rxq_index);
static boolean_t sfvmk_evTxqFlushDone(void *arg, uint32_t txq_index);
static boolean_t sfvmk_evSoftware(void *arg, uint16_t magic);
static boolean_t sfvmk_evSram(void *arg, uint32_t code);
static boolean_t sfvmk_evTimer(void *arg, uint32_t index);
static boolean_t sfvmk_evWakeup(void *arg, uint32_t index);
static boolean_t sfvmk_evLinkChange(void *arg, efx_link_mode_t link_mode);

static const efx_ev_callbacks_t sfvmk_evCallbacks = {
  .eec_initialized  = sfvmk_evInitialized,
  .eec_rx    = sfvmk_evRX,
  .eec_tx    = sfvmk_evTX,
  .eec_exception   = sfvmk_evException,
  .eec_rxq_flush_done = sfvmk_evRxqFlushDone,
  .eec_rxq_flush_failed = sfvmk_evRxqFlushFailed,
  .eec_txq_flush_done = sfvmk_evTxqFlushDone,
  .eec_software  = sfvmk_evSoftware,
  .eec_sram  = sfvmk_evSram,
  .eec_wake_up   = sfvmk_evWakeup,
  .eec_timer   = sfvmk_evTimer,
  .eec_link_change  = sfvmk_evLinkChange,
};




static boolean_t
sfvmk_evInitialized(void *arg)
{
  sfvmk_evq_t *pEvq;

  pEvq = (sfvmk_evq_t *)arg;
  SFVMK_EVQ_LOCK_ASSERT_OWNED(pEvq);

  /* Init done events may be duplicated on 7xxx */
  //  VMK_ASSERT(pEvq->initState == SFVMK_EVQ_STARTING ||
  //   pEvq->initState == SFVMK_EVQ_STARTED);

  SFVMK_DBG(pEvq->pAdapter, SFVMK_DBG_QUEUE, 5, "eventQ is initialized");
  pEvq->initState = SFVMK_EVQ_STARTED;

  return (0);
}


static boolean_t
sfvmk_evLinkChange(void *arg, efx_link_mode_t linkMode)
{
  sfvmk_evq_t *pEvq;
  sfvmk_adapter_t *pAdapter;

  pEvq = (sfvmk_evq_t *)arg;
  SFVMK_EVQ_LOCK_ASSERT_OWNED(pEvq);

  pAdapter = pEvq->pAdapter;

  SFVMK_DBG(pAdapter, SFVMK_DBG_QUEUE, 5, "Link change is detected");
  sfvmk_macLinkUpdate(pAdapter, linkMode);

  return (0);
}

static boolean_t
sfvmk_evRX(void *arg, uint32_t label, uint32_t id, uint32_t size,
   uint16_t flags)
{
  return  B_FALSE ;
}

static boolean_t
sfvmk_evException(void *arg, uint32_t code, uint32_t data)
{
  sfvmk_evq_t *pEvq;
  sfvmk_adapter_t *pAdapter;

  pEvq = (sfvmk_evq_t *)arg;
  SFVMK_EVQ_LOCK_ASSERT_OWNED(pEvq);

  pAdapter = pEvq->pAdapter;

  pEvq->exception = B_TRUE;

  if (code != EFX_EXCEPTION_UNKNOWN_SENSOREVT) {
    SFVMK_DBG(pAdapter, SFVMK_DBG_QUEUE, 5, "got Exception %x", code);
  }

  return (B_FALSE);
}

static boolean_t
sfvmk_evRxqFlushDone(void *arg, uint32_t rxq_index)
{
  return (B_FALSE);
}

static boolean_t
sfvmk_evRxqFlushFailed(void *arg, uint32_t rxq_index)
{
  return (B_FALSE);
}
static boolean_t
sfvmk_evTX(void *arg, uint32_t label, uint32_t id)
{
  return B_FALSE;
}

static boolean_t
sfvmk_evTxqFlushDone(void *arg, uint32_t txq_index)
{
  return (B_FALSE);
}

static boolean_t
sfvmk_evSoftware(void *arg, uint16_t magic)
{
  return (B_FALSE);
}


static boolean_t
sfvmk_evSram(void *arg, uint32_t code)
{
  return (B_FALSE);
}
static boolean_t
sfvmk_evTimer(void *arg, uint32_t index)
{
  (void)arg;
  (void)index;

  return (B_FALSE);
}

static boolean_t
sfvmk_evWakeup(void *arg, uint32_t index)
{
  (void)arg;
  (void)index;

  return (B_FALSE);
}


void sfvmk_evqComplete(sfvmk_evq_t *pEvq, boolean_t eop)
{
  return ;
}

int
sfvmk_evqPoll(sfvmk_evq_t *pEvq)
{

  int rc;

  SFVMK_EVQ_LOCK(pEvq);

  SFVMK_DBG(pEvq->pAdapter, SFVMK_DBG_QUEUE, 5, "enetered evqPoll");

  if ((pEvq->initState != SFVMK_EVQ_STARTING &&
        pEvq->initState != SFVMK_EVQ_STARTED)) {
    rc = EINVAL;
    goto sfvmk_fail;
  }

  //  VMK_ASSERT(pEvq->rxDone == 0);
  //  VMK_ASSERT(pEvq->txDone == 0);
  //  VMK_ASSERT(pEvq->txq == NULL);
  //  VMK_ASSERT(pEvq->txqs == &pEvq->txq);

  /* Poll the queue */

  efx_ev_qpoll(pEvq->pCommonEvq, &pEvq->readPtr, &sfvmk_evCallbacks, pEvq);

  pEvq->rxDone = 0;
  pEvq->txDone = 0;

  /* Perform any pending completion processing */
  sfvmk_evqComplete(pEvq, B_TRUE);

  /* Re-prime the event queue for interrupts */
  if ((rc = efx_ev_qprime(pEvq->pCommonEvq, pEvq->readPtr)) != 0)
    goto sfvmk_fail;


  SFVMK_EVQ_UNLOCK(pEvq);

  return (0);

sfvmk_fail:
  SFVMK_EVQ_UNLOCK(pEvq);
  return (rc);
}




/**-----------------------------------------------------------------------------
*
* sfvmk_roundupPowOfTwo --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param[in]  lockName  brief name for the spinlock
* @param[in]  rank      lock rank)
* @param[out] lock      lock pointer to create
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
int sfvmk_roundupPowOfTwo(vmk_uint32 data)
{
  return (1 << (sizeof(vmk_uint32)*8 - __builtin_clz(data) + 1));
}

/**-----------------------------------------------------------------------------
*
* sfvmk_evqFini --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param	adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
static void
sfvmk_evqFini(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_evq_t *pEvq;
  efsys_mem_t *pEvqMem;

  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");

  pEvq = pAdapter->pEvq[qIndex];

  VMK_ASSERT_BUG((NULL != pEvq), " null event queue ptr");

  pEvqMem = &pEvq->mem;

  VMK_ASSERT_BUG(pEvq->initState == SFVMK_EVQ_INITIALIZED,
                    " event queue not yet initialized");

  //VMK_ASSERT_BUG(pEvq->pTxqs != &pEvq->pTxq, "pEvq->txqs != &pEvq->txq");

  /* freeing up memory allocated for event queue */
  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pEvqMem->esm_base,
                              pEvqMem->io_elem.ioAddr,
                              EFX_RXQ_SIZE(pAdapter->rxqEntries));

  /* invalidating event queue ptr*/
  pAdapter->pEvq[qIndex] = NULL;

  sfvmk_mutexDestroy(pEvq->lock);

  /*freeing up memory allocated for event queue object*/
  sfvmk_memPoolFree(pEvq, sizeof(sfvmk_evq_t));
}

/**-----------------------------------------------------------------------------
*
* sfvmk_evqInit --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param	adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/

static VMK_ReturnStatus
sfvmk_evqInit(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_evq_t *pEvq;
  efsys_mem_t *pEvqMem;
  VMK_ReturnStatus status;

  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");

  /* qIndex must be less than SFVMK_RX_SCALE_MAX */
  VMK_ASSERT_BUG(qIndex <  SFVMK_RX_SCALE_MAX, "qIndex >= SFVMK_RX_SCALE_MAX" );

  pEvq = (sfvmk_evq_t *)sfvmk_memPoolAlloc(sizeof(sfvmk_evq_t));
  if(NULL == pEvq) {
    SFVMK_ERR(pAdapter, "failed to allocate mem for event queue object");
    goto sfvmk_ev_alloc_failed;
  }

  vmk_Memset(pEvq, 0 , sizeof(sfvmk_evq_t));

  pEvq->pAdapter = pAdapter;
  pEvq->index = qIndex;

  pAdapter->pEvq[qIndex] =pEvq;
  pEvqMem = &pEvq->mem;

  /* Build an event queue with room for one event per tx and rx buffer,
  * plus some extra for link state events and MCDI completions.
  * There are three tx queues in the first event queue and one in
  * other.
  */
  if (qIndex == 0)
   pEvq->entries = sfvmk_roundupPowOfTwo(pAdapter->rxqEntries +
                                       3 * pAdapter->txqEntries + 128);
  else
   pEvq->entries = sfvmk_roundupPowOfTwo(pAdapter->rxqEntries +
                                       pAdapter->txqEntries + 128);
  /* Initialise TX completion list */
  pEvq->pTxqs = &pEvq->pTxq;

  /* Allocate DMA space. */
  pEvqMem->esm_base = sfvmk_allocCoherentDMAMapping(pAdapter->dmaEngine,
                                                 EFX_EVQ_SIZE(pEvq->entries),
                                                 &pEvqMem->io_elem.ioAddr);
  if(NULL == pEvqMem->esm_base) {
    SFVMK_ERR(pAdapter, "failed to allocate mem for event queue entries");
    goto sfvmk_dma_alloc_failed;
  }

  pEvqMem->io_elem.length = EFX_EVQ_SIZE(pEvq->entries);
  pEvqMem->esm_handle = pAdapter->dmaEngine;

  // praveen needs to check the name
  status = sfvmk_mutexInit("pEvq" ,VMK_MUTEX_RANK_HIGHEST - 1, &pEvq->lock);
  if (status != VMK_OK)
    goto sfvmk_mutex_failed;

  pEvq->initState = SFVMK_EVQ_INITIALIZED;

  return VMK_OK;

sfvmk_mutex_failed:
  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pEvqMem->esm_base ,
                              pEvqMem->io_elem.ioAddr ,pEvqMem->io_elem.length);
sfvmk_dma_alloc_failed:
  sfvmk_memPoolFree(pEvq, sizeof (sfvmk_evq_t));
sfvmk_ev_alloc_failed:

  return VMK_FAILURE;

}
/**-----------------------------------------------------------------------------
*
* sfvmk_evInit --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param	adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
VMK_ReturnStatus
sfvmk_evInit(sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  int qIndex;
  VMK_ReturnStatus status = VMK_OK;

  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");

  pIntr = &pAdapter->intr;
  pAdapter->evqCount = pIntr->numIntrAlloc;

  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_INITIALIZED,
                 "pIntr->state != SFVMK_INTR_INITIALIZED");

  /* Set default interrupt moderation; */
  pAdapter->evModeration = SFVMK_MODERATION;

  /* Initialize the event queue(s) - one per interrupt.*/
  for (qIndex = 0; qIndex < pAdapter->evqCount; qIndex++) {
    status = sfvmk_evqInit(pAdapter, qIndex);
    if (status != VMK_OK)
    {
      SFVMK_ERR(pAdapter, "failed creating evq %d\n", qIndex);
      goto sfvmk_fail;
    }
    else
      SFVMK_DBG(pAdapter, SFVMK_DBG_QUEUE, 3, "creating evq %d\n", qIndex);
  }

  return status;

sfvmk_fail:
  while (--qIndex >= 0)
   sfvmk_evqFini(pAdapter, qIndex);

  pAdapter->evqCount = 0;

  return status ;
}

/**-----------------------------------------------------------------------------
*
* sfvmk_evFini --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param	adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/

void
sfvmk_evFini(sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  int qIndex;

  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");

  pIntr = &pAdapter->intr;

  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_INITIALIZED,
                 "intr->state != SFVMK_INTR_INITIALIZED");

  pAdapter->evModeration = 0;

  /* Tear down the event queue(s). */
  qIndex = pAdapter->evqCount;
  while (--qIndex >= 0)
    sfvmk_evqFini(pAdapter, qIndex);

  pAdapter->evqCount = 0;
}
/**-----------------------------------------------------------------------------
*
* sfvmk_evqStop --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param	adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
static void
sfvmk_evqStop(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_evq_t *pEvq;

  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");

  pEvq = pAdapter->pEvq[qIndex];
  VMK_ASSERT_BUG((NULL != pEvq), " null event queue ptr");


  VMK_ASSERT_BUG(pEvq->initState == SFVMK_EVQ_STARTED,
                       "pEvq->initState != SFVMK_EVQ_STARTED");

  SFVMK_EVQ_LOCK(pEvq);

  pEvq->initState = SFVMK_EVQ_INITIALIZED;
  pEvq->readPtr = 0;
  pEvq->exception = B_FALSE;
  efx_ev_qdestroy(pEvq->pCommonEvq);

  SFVMK_EVQ_UNLOCK(pEvq);
}
/**-----------------------------------------------------------------------------
*
* sfvmk_evStop --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param	adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
void
sfvmk_evStop(sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  efx_nic_t *pNic;
  int qIndex;

  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");

  pIntr = &pAdapter->intr;
  pNic = pAdapter->pNic;

  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_STARTED,
                    "Interrupts not started");

  /* Stop the event queue(s) */
  qIndex = pAdapter->evqCount;

  while (--qIndex >= 0)
    sfvmk_evqStop(pAdapter, qIndex);

  /* Tear down the event module */
  efx_ev_fini(pNic);
}
/**-----------------------------------------------------------------------------
*
* sfvmk_evqStart --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param	adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
static int
sfvmk_evqStart(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_evq_t *pEvq;
  efsys_mem_t *pEvqMem;
  int count;
  int rc;

  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");

  pEvq = pAdapter->pEvq[qIndex];
  pEvqMem = &pEvq->mem;

  VMK_ASSERT_BUG(pEvq->initState == SFVMK_EVQ_INITIALIZED,
                    "pEvq->initState != SFVMK_EVQ_INITIALIZED");


  /* Clear all events. */
  (void)vmk_Memset(pEvqMem->esm_base, 0xff, EFX_EVQ_SIZE(pEvq->entries));

  /* Create the common code event queue. */
  if ((rc = efx_ev_qcreate(pAdapter->pNic, qIndex, pEvqMem, pEvq->entries,0 , pAdapter->evModeration,
                            EFX_EVQ_FLAGS_NOTIFY_INTERRUPT, &pEvq->pCommonEvq)) != 0) {

    SFVMK_ERR(pAdapter, "failed to create event queue status %d", rc);
    goto sfvmk_qcreate_fail;
  }

  SFVMK_EVQ_LOCK(pEvq);

  /* Prime the event queue for interrupts */
  if ((rc = efx_ev_qprime(pEvq->pCommonEvq, pEvq->readPtr)) != 0) {
    SFVMK_ERR(pAdapter, "failed in efx_ev_qprime status %d", rc);
    goto sfvmk_qprime_fail;
  }

  pEvq->initState = SFVMK_EVQ_STARTING;

  SFVMK_EVQ_UNLOCK(pEvq);

  /* Wait for the initialization event */
  count = 0;
  do {

    vmk_DelayUsecs(100*1000);

    /* Check to see if the test event has been processed */
    if (pEvq->initState == SFVMK_EVQ_STARTED)
      goto done;

  } while (++count < 60);

  SFVMK_ERR(pAdapter, "Event queue[%d] is not initialized", qIndex);
  rc = ETIMEDOUT;

  goto sfvmk_qstart_fail;

done:

  SFVMK_DBG(pAdapter,SFVMK_DBG_QUEUE, 2, "eventQ[%d] is started " , qIndex);

  return (0);

sfvmk_qstart_fail:
  SFVMK_EVQ_LOCK(pEvq);
  pEvq->initState = SFVMK_EVQ_INITIALIZED;
sfvmk_qprime_fail:
  SFVMK_EVQ_UNLOCK(pEvq);
  efx_ev_qdestroy(pEvq->pCommonEvq);
sfvmk_qcreate_fail:
  return (rc);
}
/**-----------------------------------------------------------------------------
*
* sfvmk_EVStart --
*
* @brief It creates a spin lock with specified name and lock rank.
*
* @param	adapter pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/

VMK_ReturnStatus
sfvmk_evStart(sfvmk_adapter_t *pAdapter)
{

  sfvmk_intr_t *pIntr;
  int qIndex;
  int rc;

  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");

  SFVMK_DBG(pAdapter, SFVMK_DBG_QUEUE, 5, "entered sfvmk_evStart");
  pIntr = &pAdapter->intr;

  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_STARTED,
                    "Interrupts not started");

  /* Initialize the event module */
  if ((rc = efx_ev_init(pAdapter->pNic)) != 0) {
    SFVMK_ERR(pAdapter, "failed to init event queue module" );
    return VMK_FAILURE;
  }

  /* Start the event queues */
  for (qIndex = 0; qIndex < pAdapter->evqCount; qIndex++) {
    if ((rc = sfvmk_evqStart(pAdapter, qIndex)) != 0)
    {
      SFVMK_ERR(pAdapter,"failed to start event queue[%d]", qIndex);
      goto sfvmk_qstart_fail;
    }
    else
      vmk_NetPollEnable(pAdapter->pEvq[qIndex]->netPoll);  //praveen
  }

  return VMK_OK;

sfvmk_qstart_fail:
  /* Stop the event queue(s) */
  while (--qIndex >= 0)
    sfvmk_evqStop(pAdapter, qIndex);

  /* Tear down the event module */
  efx_ev_fini(pAdapter->pNic);

  return VMK_FAILURE;
}



