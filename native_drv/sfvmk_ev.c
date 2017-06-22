/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

/* default interrupt moderation value */
#define SFVMK_MODERATION_USEC   30

#define NUM_TX_QUEUES_FOR_EVQ0  3
/* space required in for link state event and mcdi completion */
#define EVQ_EXTRA_SPACE         128

/* Poll time to start the queue */
#define SFVMK_EVQ_START_POLL_WAIT 100*SFVMK_ONE_MILISEC
#define SFVMK_EVQ_START_POLL_TIMEOUT 20

/* helper functions */
static void sfvmk_evqFini(sfvmk_adapter_t *pAdapter, unsigned int qIndex);
static VMK_ReturnStatus sfvmk_evqInit(sfvmk_adapter_t *pAdapter,
                                            unsigned int qIndex);

static void sfvmk_evqStop(sfvmk_adapter_t *pAdapter, unsigned int qIndex);
static int sfvmk_evqStart(sfvmk_adapter_t *pAdapter, unsigned int qIndex);

/* call back functions which needs to be registered with common evq module */
static boolean_t sfvmk_evInitialized(void *arg);
static boolean_t sfvmk_evRX(void *arg, uint32_t label,
                                  uint32_t id, uint32_t size, uint16_t flags);
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

/* call back functions which needs to be registered with common evq module */
/*common evq  module call them apropriatly */

static const efx_ev_callbacks_t sfvmk_evCallbacks = {
  .eec_rx = sfvmk_evRX,
  .eec_tx = sfvmk_evTX,
  .eec_sram = sfvmk_evSram,
  .eec_timer = sfvmk_evTimer,
  .eec_wake_up = sfvmk_evWakeup,
  .eec_software = sfvmk_evSoftware,
  .eec_exception = sfvmk_evException,
  .eec_link_change = sfvmk_evLinkChange,
  .eec_initialized  = sfvmk_evInitialized,
  .eec_txq_flush_done = sfvmk_evTxqFlushDone,
  .eec_rxq_flush_done = sfvmk_evRxqFlushDone,
  .eec_rxq_flush_failed = sfvmk_evRxqFlushFailed,
};

/*! \brief gets called when initilized event comes for an eventQ
**
** \param[in] arg        pointer to eventQ
**
** \return: B_FALSE <success>
*/
static boolean_t
sfvmk_evInitialized(void *arg)
{
  sfvmk_evq_t *pEvq;
  pEvq = (sfvmk_evq_t *)arg;

  SFVMK_NULL_PTR_CHECK(pEvq);

  VMK_ASSERT_BUG(pEvq->initState == SFVMK_EVQ_STARTING ||
                  pEvq->initState == SFVMK_EVQ_STARTED,
                  "eventQ is not yet started");

  SFVMK_DBG(pEvq->pAdapter, SFVMK_DBG_EVQ, SFVMK_LOG_LEVEL_INFO,
            "eventQ is initialized");

  pEvq->initState = SFVMK_EVQ_STARTED;

  return B_FALSE;
}

/*! \brief gets called when a link change event comes
**
** \param[in] arg        ptr to event queue
** \param[in] linkMode   specify link mode properties (up/down , speed)
**
** \return: B_FALSE <success>
*/
static boolean_t
sfvmk_evLinkChange(void *arg, efx_link_mode_t linkMode)
{
  sfvmk_evq_t *pEvq;
  sfvmk_adapter_t *pAdapter;

  pEvq = (sfvmk_evq_t *)arg;

  SFVMK_NULL_PTR_CHECK(pEvq);
  pAdapter = pEvq->pAdapter;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG(pAdapter, SFVMK_DBG_EVQ, SFVMK_LOG_LEVEL_INFO,
            "Link change is detected");

  sfvmk_macLinkUpdate(pAdapter, linkMode);

  return B_FALSE;
}

/*! \brief called when an exception received on eventQ
**
** \param[in] arg      ptr to event queue
** \param[in] code     exception code
** \param[in] data
**
** \return: B_FALSE <success>
*/
static boolean_t
sfvmk_evException(void *arg, uint32_t code, uint32_t data)
{
  sfvmk_evq_t *pEvq;
  sfvmk_adapter_t *pAdapter;

  pEvq = (sfvmk_evq_t *)arg;
  SFVMK_NULL_PTR_CHECK(pEvq);
  pAdapter = pEvq->pAdapter;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG(pAdapter,SFVMK_DBG_EVQ, SFVMK_LOG_LEVEL_INFO, "[%d] %s", pEvq->index,
        (code == EFX_EXCEPTION_RX_RECOVERY) ? "RX_RECOVERY" :
        (code == EFX_EXCEPTION_RX_DSC_ERROR) ? "RX_DSC_ERROR" :
        (code == EFX_EXCEPTION_TX_DSC_ERROR) ? "TX_DSC_ERROR" :
        (code == EFX_EXCEPTION_UNKNOWN_SENSOREVT) ? "UNKNOWN_SENSOREVT" :
        (code == EFX_EXCEPTION_FWALERT_SRAM) ? "FWALERT_SRAM" :
        (code == EFX_EXCEPTION_UNKNOWN_FWALERT) ? "UNKNOWN_FWALERT" :
        (code == EFX_EXCEPTION_RX_ERROR) ? "RX_ERROR" :
        (code == EFX_EXCEPTION_TX_ERROR) ? "TX_ERROR" :
        (code == EFX_EXCEPTION_EV_ERROR) ? "EV_ERROR" :
        "UNKNOWN");

  pEvq->exception = B_TRUE;

  if (code != EFX_EXCEPTION_UNKNOWN_SENSOREVT) {
    SFVMK_DBG(pAdapter, SFVMK_DBG_EVQ, SFVMK_LOG_LEVEL_INFO,
              "got Exception %x", code);
    /* TODO: sfvmk_scheduleReset(sc);*/
  }

  return B_FALSE;
}

/*! \brief called when a RX event received on eventQ
**
** \param[in] arg      ptr to event queue
** \param[in[ label    queue label
** \param[in] id       queue ID
** \param[in] size     pkt size
** \param[in] flags    pkt metadeta info
**
** \return: B_FALSE <success>
*/
static boolean_t
sfvmk_evRX(void *arg, uint32_t label, uint32_t id, uint32_t size,
   uint16_t flags)
{
  /* TODO : implementation will be added later */
  return B_FALSE;
}

/*! \brief  called when commond module finshed flushing rxq and send the event
**         on eventQ
**
** \param[in] arg      ptr to event queue
** \param[in] rxqIndex rxq Index
**
** \return: B_FALSE <success>
*/
static boolean_t
sfvmk_evRxqFlushDone(void *arg, uint32_t rxqIndex)
{

  sfvmk_evq_t *pEvq;
  sfvmk_adapter_t *pAdapter;
  sfvmk_rxq_t *pRxq;
  unsigned int qIndex;
  uint16_t magic;

  pEvq = (sfvmk_evq_t *)arg;
  SFVMK_NULL_PTR_CHECK(pEvq);
  pAdapter = pEvq->pAdapter;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  pRxq = pAdapter->pRxq[rxqIndex];

  SFVMK_NULL_PTR_CHECK(pRxq);

  /* Resend a software event on the correct queue */
  qIndex = pRxq->index;
  if (qIndex == pEvq->index) {
    sfvmk_rxqFlushDone(pRxq);
    return (B_FALSE);
  }

  pEvq = pAdapter->pEvq[qIndex];
  magic = sfvmk_swEvRxqMagic(SFVMK_SW_EV_RX_QFLUSH_DONE, pRxq);

  VMK_ASSERT_BUG(pEvq->initState == SFVMK_EVQ_STARTED,
                  "evq not started");
  efx_ev_qpost(pEvq->pCommonEvq, magic);

  return B_FALSE;
}

/*! \brief called when commond module failed in flushing rxq and send the event
**        on eventQ
**
** \param[in] arg      ptr to event queue
** \param[in] rxqIndex rxq Index
**
** \return: B_FALSE <success>
*/
static boolean_t
sfvmk_evRxqFlushFailed(void *arg, uint32_t rxqIndex)
{
  sfvmk_evq_t *pEvq;
  sfvmk_adapter_t *pAdapter;
  sfvmk_rxq_t *pRxq;
  unsigned int qIndex;
  uint16_t magic;

  pEvq = (sfvmk_evq_t *)arg;
  SFVMK_NULL_PTR_CHECK(pEvq);
  pAdapter = pEvq->pAdapter;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  pRxq = pAdapter->pRxq[rxqIndex];

  SFVMK_NULL_PTR_CHECK(pRxq);

  /* Resend a software event on the correct queue */
  qIndex = pRxq->index;

  pEvq = pAdapter->pEvq[qIndex];
  magic = sfvmk_swEvRxqMagic(SFVMK_SW_EV_RX_QFLUSH_FAILED, pRxq);

  VMK_ASSERT_BUG(pEvq->initState == SFVMK_EVQ_STARTED,
                "evq not started");
  efx_ev_qpost(pEvq->pCommonEvq, magic);

  return B_FALSE;
}

/*! \brief   Local function to get txq attached to a given evq.
**
** \param[in] pEvq     pointer to eventQ
** \param[in] label    txq type
**
** \return: VMK_OK <success> error code <failure>
**
*/
static struct sfvmk_txq_s *
sfvmk_getTxqByLabel(struct sfvmk_evq_s *pEvq, enum sfvmk_txqType label)
{
  unsigned int index;

  VMK_ASSERT_BUG((pEvq->index == 0 && label < SFVMK_TXQ_NTYPES) ||
        (label == SFVMK_TXQ_IP_TCP_UDP_CKSUM), "unexpected txq label");
  index = (pEvq->index == 0) ? label : (pEvq->index - 1 + SFVMK_TXQ_NTYPES);

  return (pEvq->pAdapter->pTxq[index]);
}

/*! \brief called when a TX event received on eventQ
**
** \param[in] arg      ptr to event queue
** \param[in[ label    queue label
** \param[in] id       queue ID
**
** \return: B_FALSE <success>
*/
static boolean_t
sfvmk_evTX(void *arg, uint32_t label, uint32_t id)
{
  return B_FALSE;
}

/*! \brief  called when commond module finshed flushing txq and send the event
**         on eventQ
**
** \param[in] arg      ptr to event queue
** \param[in] txqIndex txq Index
**
** \return: B_FALSE <success>
*/
static boolean_t
sfvmk_evTxqFlushDone(void *arg, uint32_t txqIndex)
{
  sfvmk_evq_t *pEvq;
  sfvmk_txq_t *pTxq;
  sfvmk_adapter_t *pAdapter;
  vmk_uint16 magic;

  pEvq = (sfvmk_evq_t *)arg;
  SFVMK_NULL_PTR_CHECK(pEvq);
  pAdapter = pEvq->pAdapter;
  SFVMK_NULL_PTR_CHECK(pAdapter);
  pTxq = pAdapter->pTxq[txqIndex];
  SFVMK_NULL_PTR_CHECK(pTxq);

  VMK_ASSERT_BUG(pTxq->initState == SFVMK_TXQ_INITIALIZED,
                  "txq not initialized");

  if (pTxq->evqIndex == pEvq->index) {
    sfvmk_txqFlushDone(pTxq);
    return (B_FALSE);
  }
  /* Resend a software event on the correct queue */
  pEvq = pAdapter->pEvq[pTxq->evqIndex];
  magic = sfvmk_swEvTxqMagic(SFVMK_SW_EV_TX_QFLUSH_DONE, pTxq);

  VMK_ASSERT_BUG(pEvq->initState == SFVMK_EVQ_STARTED,
                  "evq not started");
  efx_ev_qpost(pEvq->pCommonEvq, magic);

  return B_FALSE;
}

/*! \brief    function to handle event raised by driver
**
** \param[in] arg      ptr to event queue
** \param[in] magic    to differentiate different sw events.
**
** \return: B_FALSE <success>
*/
static boolean_t
sfvmk_evSoftware(void *arg, uint16_t magic)
{

  sfvmk_evq_t *pEvq;
  sfvmk_adapter_t *pAdapter;
  vmk_uint32 label;
  struct sfvmk_rxq_s *pRxq ;

  pEvq = (sfvmk_evq_t *)arg;
  SFVMK_NULL_PTR_CHECK(pEvq);
  pAdapter = pEvq->pAdapter;
  SFVMK_NULL_PTR_CHECK(pAdapter);
  pRxq = pEvq->pAdapter->pRxq[pEvq->index];
  SFVMK_NULL_PTR_CHECK(pRxq);

  label = magic & SFVMK_MAGIC_DMAQ_LABEL_MASK;
  magic &= ~SFVMK_MAGIC_DMAQ_LABEL_MASK;

  switch (magic) {
    case SFVMK_SW_EV_MAGIC(SFVMK_SW_EV_RX_QFLUSH_DONE):
      sfvmk_rxqFlushDone(pRxq);
      break;

    case SFVMK_SW_EV_MAGIC(SFVMK_SW_EV_RX_QFLUSH_FAILED):
      sfvmk_rxqFlushFailed(pRxq);
      break;

    case SFVMK_SW_EV_MAGIC(SFVMK_SW_EV_TX_QFLUSH_DONE): {
      struct sfvmk_txq_s *pTxq = sfvmk_getTxqByLabel(pEvq, label);

      SFVMK_NULL_PTR_CHECK(pTxq);
      VMK_ASSERT_BUG(pEvq->index == pTxq->evqIndex,
                        "evq->index != txq->evq_index");

      sfvmk_txqFlushDone(pTxq);
      break;
    }
    default:
      break;
  }

  return B_FALSE;
}

/*! \brief   TBD :
**
** \param[in] arg      ptr to event queue
** \param[in] code
**
** \return: B_FALSE <success>
*/
static boolean_t
sfvmk_evSram(void *arg, uint32_t code)
{
  return B_FALSE;
}

/*! \brief    TBD:
**
** \param[in] arg      ptr to event queue
** \param[in] index
**
** \return: B_FALSE <success>
*/
static boolean_t
sfvmk_evTimer(void *arg, uint32_t index)
{
  (void)arg;
  (void)index;

  return B_FALSE;
}

/*! \brief          TBD:
**
** \param[in] arg      ptr to event queue
** \param[in] index
**
** \return: B_FALSE <success>
*/
static boolean_t
sfvmk_evWakeup(void *arg, uint32_t index)
{
  (void)arg;
  (void)index;

  return B_FALSE;
}

/*! \brief      function to Perform any pending completion processing
**
** \param[in] arg      ptr to event queue
** \param[in] eop      end of pkt
**
** \return: void
*/
void sfvmk_evqComplete(sfvmk_evq_t *pEvq, boolean_t eop)
{
  /* TODO : will be implemented for data path */
  return ;
}

/*! \brief  poll event from eventQ and process it. function should be called in thread
**        context only.
** \param[in] arg      ptr to event queue
**
** \return: B_FALSE
*/
int
sfvmk_evqPoll(sfvmk_evq_t *pEvq)
{
  int rc;

  SFVMK_EVQ_LOCK(pEvq);

  SFVMK_DBG_FUNC_ENTRY(pEvq->pAdapter, SFVMK_DBG_EVQ);

  if ((pEvq->initState != SFVMK_EVQ_STARTING &&
        pEvq->initState != SFVMK_EVQ_STARTED)) {
    rc = EINVAL;
    goto sfvmk_fail;
  }

  VMK_ASSERT(pEvq->rxDone == 0);
  VMK_ASSERT(pEvq->txDone == 0);
  VMK_ASSERT(pEvq->txq == NULL);
  VMK_ASSERT(pEvq->txqs == &pEvq->txq);

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

  return rc;

sfvmk_fail:
  SFVMK_EVQ_UNLOCK(pEvq);
  return (rc);
}

/*! \brief ceil the input data to the next power of two value.
**
** \param[in]  data
**
** \return: upscale value.
*/
int sfvmk_roundupPowOfTwo(vmk_uint32 data)
{
  return (1 << (sizeof(vmk_uint32)*8 - __builtin_clz(data) + 1));
}

/*! \brief  releases resource allocated for a particular evq
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex      eventQ index
**
** \return: void
*/
static void
sfvmk_evqFini(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_evq_t *pEvq;
  efsys_mem_t *pEvqMem;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_EVQ , "qIndex[%d]" , qIndex );

  pEvq = pAdapter->pEvq[qIndex];

  SFVMK_NULL_PTR_CHECK(pEvq);

  pEvqMem = &pEvq->mem;

  VMK_ASSERT_BUG(pEvq->initState == SFVMK_EVQ_INITIALIZED,
                    " event queue not yet initialized");

  /* freeing up memory allocated for event queue */
  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pEvqMem->pEsmBase,
                              pEvqMem->ioElem.ioAddr,
                              EFX_RXQ_SIZE(pAdapter->rxqEntries));

  /* invalidating event queue ptr*/
  pAdapter->pEvq[qIndex] = NULL;

  sfvmk_mutexDestroy(pEvq->lock);

  /*freeing up memory allocated for event queue object*/
  sfvmk_memPoolFree(pEvq, sizeof(sfvmk_evq_t));

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_EVQ , "qIndex[%d]" , qIndex );
}

/*! \brief  allocate resources for a particular evq
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex      eventQ index
**
** \return:   VMK_OK <success> VMK_FAILURE < failure>
*/
static VMK_ReturnStatus
sfvmk_evqInit(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_evq_t *pEvq;
  efsys_mem_t *pEvqMem;
  VMK_ReturnStatus status;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_EVQ , "qIndex[%d]" , qIndex );

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
  * There are three tx queues of type (NON_CKSUM, IP_CKSUM, IP_TCP_UDP_CKSUM)
  * in the first event queue and one in other. */

  if (qIndex == 0)
   pEvq->entries = sfvmk_roundupPowOfTwo(pAdapter->rxqEntries +
                                         NUM_TX_QUEUES_FOR_EVQ0 *
                                         pAdapter->txqEntries +
                                         EVQ_EXTRA_SPACE);
  else
   pEvq->entries = sfvmk_roundupPowOfTwo(pAdapter->rxqEntries +
                                         pAdapter->txqEntries +
                                         EVQ_EXTRA_SPACE);
  /* Initialise TX completion list */
  pEvq->pTxqs = &pEvq->pTxq;

  /* Allocate DMA space. */
  pEvqMem->pEsmBase = sfvmk_allocCoherentDMAMapping(pAdapter->dmaEngine,
                                                 EFX_EVQ_SIZE(pEvq->entries),
                                                 &pEvqMem->ioElem.ioAddr);
  if(NULL == pEvqMem->pEsmBase) {
    SFVMK_ERR(pAdapter, "failed to allocate mem for event queue entries");
    goto sfvmk_dma_alloc_failed;
  }

  pEvqMem->ioElem.length = EFX_EVQ_SIZE(pEvq->entries);
  pEvqMem->esmHandle = pAdapter->dmaEngine;

  status = sfvmk_mutexInit("evq", SFVMK_EVQ_LOCK_RANK, &pEvq->lock);
  if (status != VMK_OK)
    goto sfvmk_mutex_failed;

  pEvq->initState = SFVMK_EVQ_INITIALIZED;

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_EVQ , "qIndex[%d]" , qIndex );

  return VMK_OK;

sfvmk_mutex_failed:
  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pEvqMem->pEsmBase ,
                              pEvqMem->ioElem.ioAddr ,pEvqMem->ioElem.length);
sfvmk_dma_alloc_failed:
  sfvmk_memPoolFree(pEvq, sizeof (sfvmk_evq_t));
sfvmk_ev_alloc_failed:

  return VMK_FAILURE;

}

/*! \brief  allocate resources for all evq
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
VMK_ReturnStatus
sfvmk_evInit(sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  int qIndex;
  VMK_ReturnStatus status = VMK_OK;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_EVQ);

  pIntr = &pAdapter->intr;
  pAdapter->evqCount = pIntr->numIntrAlloc;

  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_INITIALIZED,
                 "pIntr->state != SFVMK_INTR_INITIALIZED");

  /* Set default interrupt moderation; */
  pAdapter->evModeration = SFVMK_MODERATION_USEC;

  /* Initialize the event queue(s) - one per interrupt.*/
  for (qIndex = 0; qIndex < pAdapter->evqCount; qIndex++) {
    status = sfvmk_evqInit(pAdapter, qIndex);
    if (status != VMK_OK)
    {
      SFVMK_ERR(pAdapter, "failed creating evq %d\n", qIndex);
      goto sfvmk_fail;
    }
    else
      SFVMK_DBG(pAdapter, SFVMK_DBG_EVQ, SFVMK_LOG_LEVEL_DBG,
                "creating evq %d\n", qIndex);
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_EVQ);

  return status;

sfvmk_fail:
  while (--qIndex >= 0)
   sfvmk_evqFini(pAdapter, qIndex);

  pAdapter->evqCount = 0;

  return status ;
}

/*! \brief  releases resources for all evq
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_evFini(sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  int qIndex;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_EVQ);

  pIntr = &pAdapter->intr;

  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_INITIALIZED,
                 "intr->state != SFVMK_INTR_INITIALIZED");

  pAdapter->evModeration = 0;

  /* Tear down the event queue(s). */
  qIndex = pAdapter->evqCount;
  while (--qIndex >= 0)
    sfvmk_evqFini(pAdapter, qIndex);

  pAdapter->evqCount = 0;

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_EVQ);

}

/*! \brief      destroy evq module for a particular evq.
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex      eventQ index
**
** \return: void
*/
static void
sfvmk_evqStop(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_evq_t *pEvq;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_EVQ , "qIndex[%d]" , qIndex );

  pEvq = pAdapter->pEvq[qIndex];
  SFVMK_NULL_PTR_CHECK(pEvq);

  VMK_ASSERT_BUG(pEvq->initState == SFVMK_EVQ_STARTED,
                 "pEvq->initState != SFVMK_EVQ_STARTED");

  SFVMK_EVQ_LOCK(pEvq);

  pEvq->initState = SFVMK_EVQ_INITIALIZED;
  pEvq->readPtr = 0;
  pEvq->exception = B_FALSE;
  efx_ev_qdestroy(pEvq->pCommonEvq);

  SFVMK_EVQ_UNLOCK(pEvq);

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_EVQ , "qIndex[%d]" , qIndex );
}

/*! \brief      destroy all evq module.
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_evStop(sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  efx_nic_t *pNic;
  int qIndex;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_EVQ);

  pIntr = &pAdapter->intr;
  pNic = pAdapter->pNic;

  SFVMK_NULL_PTR_CHECK(pNic);

  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_STARTED,
                    "Interrupts not started");

  /* Stop the event queue(s) */
  qIndex = pAdapter->evqCount;

  while (--qIndex >= 0)
    sfvmk_evqStop(pAdapter, qIndex);

  /* Tear down the event module */
  efx_ev_fini(pNic);
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_EVQ);
}

/*! \brief    Create common evq and wait for initilize event from the fw.
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex      eventQ index
**
** \return: 0 <success> error code <failure>
*/
static int
sfvmk_evqStart(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_evq_t *pEvq;
  efsys_mem_t *pEvqMem;
  int count;
  int rc;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_EVQ , "qIndex[%d]" , qIndex );

  pEvq = pAdapter->pEvq[qIndex];
  pEvqMem = &pEvq->mem;

  VMK_ASSERT_BUG(pEvq->initState == SFVMK_EVQ_INITIALIZED,
                 "pEvq->initState != SFVMK_EVQ_INITIALIZED");

  /* Clear all events. */
  (void)vmk_Memset(pEvqMem->pEsmBase, 0xff, EFX_EVQ_SIZE(pEvq->entries));

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

    vmk_WorldSleep(SFVMK_EVQ_START_POLL_WAIT);
    /* Check to see if the test event has been processed */
    if (pEvq->initState == SFVMK_EVQ_STARTED)
      goto done;

  } while (++count < SFVMK_EVQ_START_POLL_TIMEOUT);

  SFVMK_ERR(pAdapter, "Event queue[%d] is not initialized", qIndex);
  rc = ETIMEDOUT;

  goto sfvmk_qstart_fail;

done:

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_EVQ , "qIndex[%d]" , qIndex );

  return rc;

sfvmk_qstart_fail:
  SFVMK_EVQ_LOCK(pEvq);
  pEvq->initState = SFVMK_EVQ_INITIALIZED;
sfvmk_qprime_fail:
  SFVMK_EVQ_UNLOCK(pEvq);
  efx_ev_qdestroy(pEvq->pCommonEvq);
sfvmk_qcreate_fail:
  return (rc);
}

/*! \brief      init and allocate all evq module.
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
**
** \return:  VMK_OK <success> error code <failure>
*/
VMK_ReturnStatus
sfvmk_evStart(sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  int qIndex;
  int rc;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_EVQ);

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
      vmk_NetPollEnable(pAdapter->pEvq[qIndex]->netPoll);
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_EVQ);

  return VMK_OK;

sfvmk_qstart_fail:
  /* Stop the event queue(s) */
  while (--qIndex >= 0) {
    /* disable netPoll */
    vmk_NetPollDisable(pAdapter->pEvq[qIndex]->netPoll);
    sfvmk_evqStop(pAdapter, qIndex);
  }

  /* Tear down the event module */
  efx_ev_fini(pAdapter->pNic);

  return VMK_FAILURE;
}

