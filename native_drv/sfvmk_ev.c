/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

/* EVQ budget (in descriptors) to allow for MCDI and Rx/Tx error events. */
#define SFVMK_EVQ_EXTRA_EVENT_SPACE   128

/* Wait time  for common EVQ to start */
#define SFVMK_EVQ_START_POLL_TIME_USEC   VMK_USEC_PER_MSEC
#define SFVMK_EVQ_START_POLL_COUNT       200

/* TODO: Needs to revisit when dynamic interrupt moderation support
 * is added and performance tuning is done */
#define SFVMK_MODERATION_USEC         30

/* Call back functions which needs to be registered with common EVQ module */
static boolean_t sfvmk_evInitialized(void *arg);
static boolean_t sfvmk_evException(void *arg, uint32_t code, uint32_t data);
static boolean_t sfvmk_evLinkChange(void *arg, efx_link_mode_t linkMode);
static boolean_t sfvmk_evRxqFlushDone(void *arg, uint32_t rxq_index);
static boolean_t sfvmk_evRxqFlushFailed(void *arg, uint32_t rxqIndex);
static boolean_t sfvmk_evTxqFlushDone(void *arg, uint32_t txqIndex);

static const efx_ev_callbacks_t sfvmk_evCallbacks = {
  .eec_exception = sfvmk_evException,
  .eec_link_change = sfvmk_evLinkChange,
  .eec_initialized  = sfvmk_evInitialized,
  .eec_txq_flush_done = sfvmk_evTxqFlushDone,
  .eec_rxq_flush_done = sfvmk_evRxqFlushDone,
  .eec_rxq_flush_failed = sfvmk_evRxqFlushFailed,
};

/*! \brief Gets called when initilized event comes for an eventQ.
**
** \param[in] arg        Pointer to eventQ
**
** \return: VMK_FALSE <success>
*/
static boolean_t
sfvmk_evInitialized(void *arg)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;

  if (pEvq == NULL) {
    SFVMK_ERROR("NULL event queue ptr");
    goto fail;
  }

  if (pEvq->state != SFVMK_EVQ_STATE_STARTING) {
    SFVMK_ADAPTER_ERROR(pEvq->pAdapter, "Invalid EVQ state(%u)", pEvq->state);
    goto fail;
  }

  SFVMK_ADAPTER_DEBUG(pEvq->pAdapter, SFVMK_DEBUG_EVQ, SFVMK_LOG_LEVEL_INFO,
                      "EventQ is started");

  pEvq->state = SFVMK_EVQ_STATE_STARTED;

  return VMK_FALSE;

fail:
  return VMK_TRUE;
}

/*! \brief Gets called when an exception received on eventQ.
**
** \param[in] arg      Pointer to event queue
** \param[in] code     Exception code
** \param[in] data
**
** \return: VMK_FALSE <success>
*/
static boolean_t
sfvmk_evException(void *arg, uint32_t code, uint32_t data)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;

  if (pEvq == NULL) {
    SFVMK_ERROR("NULL event queue ptr");
    goto fail;
  }

  SFVMK_ADAPTER_DEBUG(pEvq->pAdapter, SFVMK_DEBUG_EVQ, SFVMK_LOG_LEVEL_INFO,
                      "[%u] %s", pEvq->index,
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

  pEvq->exception = VMK_TRUE;

  if (code != EFX_EXCEPTION_UNKNOWN_SENSOREVT) {
    sfvmk_scheduleReset(pEvq->pAdapter);
  }

  return VMK_FALSE;

fail:
  return VMK_TRUE;
}

/*! \brief Gets called when a link change event comes
**
** \param[in] arg        Pointer to event queue
** \param[in] linkMode   Specify link status (up/down, speed)
**
** \return: VMK_FALSE <success>
*/
static boolean_t
sfvmk_evLinkChange(void *arg, efx_link_mode_t linkMode)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;
  sfvmk_adapter_t *pAdapter = NULL;

  if (pEvq == NULL) {
    SFVMK_ERROR("NULL event queue ptr");
    goto fail;
  }

  pAdapter = pEvq->pAdapter;
  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto fail;
  }

  if (pAdapter->port.linkMode == linkMode) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_EVQ, SFVMK_LOG_LEVEL_DBG,
                        "Spurious link change event: %d", linkMode);
    goto done;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_EVQ, SFVMK_LOG_LEVEL_INFO,
                      "Link change is detected: %d", linkMode);

  sfvmk_macLinkUpdate(pAdapter, linkMode);

done:
  return VMK_FALSE;

fail:
  return VMK_TRUE;

}

/*! \brief Checked flushed event received for a RXQ, resend the event to
**         the EVQ associated with the given RXQ.
**
** \param[in] arg       Pointer to event queue
** \param[in] rxqIndex  RXQ index
** \param[in] swEvent   Software event.
**
** \return: VMK_FALSE <success>
*/
static boolean_t
sfvmk_evRxqFlush(void *arg, uint32_t rxqIndex, sfvmk_flushState_t flushState)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;
  sfvmk_adapter_t *pAdapter = NULL;
  sfvmk_rxq_t *pRxq = NULL;

  if (pEvq == NULL) {
    SFVMK_ERROR("NULL event queue ptr");
    goto fail;
  }

  pAdapter = pEvq->pAdapter;
  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto fail;
  }

  if (rxqIndex >= pAdapter->numRxqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid RXQ index %u", rxqIndex);
    goto fail;
  }

  if (pAdapter->ppRxq != NULL)
    pRxq = pAdapter->ppRxq[rxqIndex];

  if (pRxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL RXQ ptr");
    goto fail;
  }

  if (flushState == SFVMK_FLUSH_STATE_DONE) {
    if (pRxq->index == pEvq->index) {
      sfvmk_setRxqFlushState(pRxq, flushState);
      goto done;
    }
  }

done:
  return VMK_FALSE;

fail:
  return VMK_TRUE;
}

/*! \brief  Gets called when flushing RXQ is done.
**
** \param[in] arg       Pointer to event queue
** \param[in] rxqIndex  RXQ Index
**
** \return: VMK_FALSE <success>
*/
static boolean_t
sfvmk_evRxqFlushDone(void *arg, uint32_t rxqIndex)
{
  return sfvmk_evRxqFlush(arg, rxqIndex, SFVMK_FLUSH_STATE_DONE);
}

/*! \brief  Gets called when flushing RXQ is failed.
**
** \param[in] arg       Pointer to event queue
** \param[in] rxqIndex  RXQ Index
**
** \return: VMK_FALSE <success>
*/
static boolean_t
sfvmk_evRxqFlushFailed(void *arg, uint32_t rxqIndex)
{
  return sfvmk_evRxqFlush(arg, rxqIndex, SFVMK_FLUSH_STATE_FAILED);
}

/*! \brief  Called when TXQ flush is done.
**
** \param[in] arg        Pointer to event queue
** \param[in] txqIndex   TXQ index
**
** \return: VMK_FALSE <success>
*/
static boolean_t
sfvmk_evTxqFlushDone(void *arg, uint32_t txqIndex)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;;
  sfvmk_txq_t *pTxq = NULL;
  sfvmk_adapter_t *pAdapter = NULL;

  if (pEvq == NULL) {
    SFVMK_ERROR("NULL event queue ptr");
    goto fail;
  }

  pAdapter = pEvq->pAdapter;
  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto fail;
  }

  if (pAdapter->ppTxq != NULL)
    pTxq = pAdapter->ppTxq[txqIndex];

  if (pTxq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL TXQ ptr");
    goto fail;
  }

  if (pTxq->evqIndex == pEvq->index) {
    sfvmk_txqFlushDone(pTxq);
    goto done;
  }

done:
  return VMK_FALSE;

fail:
  return VMK_TRUE;
}

/*! \brief  Poll event from eventQ and process it. function should be called in thread
**          context only.
**
** \param[in] pEvq     Pointer to event queue
**
** \return: VMK_OK <success> error code <failure>
*/
VMK_ReturnStatus
sfvmk_evqPoll(sfvmk_evq_t *pEvq)
{
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_EVQ);

  if (pEvq == NULL) {
    SFVMK_ERROR("NULL event queue ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  vmk_SpinlockLock(pEvq->lock);

  if ((pEvq->state != SFVMK_EVQ_STATE_STARTING)
      && (pEvq->state != SFVMK_EVQ_STATE_STARTED)) {
    status = VMK_FAILURE;
    goto done;
  }

  /* Poll the queue */
  efx_ev_qpoll(pEvq->pCommonEvq, &pEvq->readPtr, &sfvmk_evCallbacks, pEvq);

  /* Re-prime the event queue for interrupts */
  status = efx_ev_qprime(pEvq->pCommonEvq, pEvq->readPtr);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pEvq->pAdapter, "efx_ev_qprime failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

done:
  vmk_SpinlockUnlock(pEvq->lock);

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_EVQ);
  return status;
}

/*! \brief    Create common code EVQ and wait for initilize event from the fw.
**
** \param[in]  pAdapter    Pointer to sfvmk_adapter_t
** \param[in]  qIndex      EventQ index
**
** \return: 0 <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_evqStart(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  sfvmk_evq_t *pEvq = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 count;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_EVQ, "qIndex[%u]", qIndex);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (qIndex >= pAdapter->numEvqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid EVQ index %u", qIndex);
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

  vmk_SpinlockLock(pEvq->lock);
  if (pEvq->state != SFVMK_EVQ_STATE_INITIALIZED) {
    vmk_SpinlockUnlock(pEvq->lock);
    SFVMK_ADAPTER_ERROR(pAdapter, "EVQ is not initialized");
    status = VMK_FAILURE;
    goto done;
  }

  pEvq->state = SFVMK_EVQ_STATE_STARTING;

  vmk_SpinlockUnlock(pEvq->lock);

  vmk_Memset(pEvq->mem.pEsmBase, 0xff, EFX_EVQ_SIZE(pEvq->numDesc));

  /* Create common code event queue. */
  status = efx_ev_qcreate(pAdapter->pNic, qIndex, &pEvq->mem, pEvq->numDesc, 0,
                          pAdapter->intrModeration, EFX_EVQ_FLAGS_NOTIFY_INTERRUPT,
                          &pEvq->pCommonEvq);
  if (status != VMK_OK) {

    goto failed_ev_qcreate;
  }

  vmk_SpinlockLock(pEvq->lock);

  /* Prime the event queue for interrupts */
  status = efx_ev_qprime(pEvq->pCommonEvq, pEvq->readPtr);
  if (status != VMK_OK) {
    vmk_SpinlockUnlock(pEvq->lock);
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_ev_qprime failed status: %s",
                        vmk_StatusToString(status));
    goto failed_ev_qprime;
  }

  vmk_SpinlockUnlock(pEvq->lock);

  count = 0;
  do {
    status = vmk_WorldSleep(SFVMK_EVQ_START_POLL_TIME_USEC);
    if ((status != VMK_OK) || (status != VMK_WAIT_INTERRUPTED)) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_WorldSleep failed status: %s",
                          vmk_StatusToString(status));
      goto failed_world_sleep;
    }

    /* Check to see if the test event has been processed */
    vmk_SpinlockLock(pEvq->lock);
    if (pEvq->state == SFVMK_EVQ_STATE_STARTED) {
      vmk_SpinlockUnlock(pEvq->lock);
      goto done;
    }
    vmk_SpinlockUnlock(pEvq->lock);

  } while (++count < SFVMK_EVQ_START_POLL_COUNT);

  SFVMK_ADAPTER_ERROR(pAdapter, "Event queue[%u] is not started", qIndex);
  status = VMK_TIMEOUT;

failed_world_sleep:
failed_ev_qprime:
  efx_ev_qdestroy(pEvq->pCommonEvq);

failed_ev_qcreate:
  vmk_SpinlockLock(pEvq->lock);
  pEvq->state = SFVMK_EVQ_STATE_INITIALIZED;
  vmk_SpinlockUnlock(pEvq->lock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_EVQ, "qIndex[%u]", qIndex);

  return status;
}

/*! \brief   Destroy a common code EVQ.
**
** \param[in]  pAdapter    Pointer to sfvmk_adapter_t
** \param[in]  qIndex      EVQ index
**
** \return: void
*/
static void
sfvmk_evqStop(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  sfvmk_evq_t *pEvq = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_EVQ, "qIndex[%u]", qIndex);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if (qIndex >= pAdapter->numEvqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid EVQ index %u", qIndex);
    goto done;
  }

  if (pAdapter->ppEvq != NULL)
    pEvq = pAdapter->ppEvq[qIndex];

  if (pEvq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL event queue ptr");
    goto done;
  }

  vmk_SpinlockLock(pEvq->lock);

  if (pEvq->state != SFVMK_EVQ_STATE_STARTED) {
    vmk_SpinlockUnlock(pEvq->lock);
    SFVMK_ADAPTER_ERROR(pAdapter, "EVQ is not started");
    goto done;
  }

  pEvq->state = SFVMK_EVQ_STATE_INITIALIZED;
  pEvq->readPtr = 0;
  pEvq->exception = VMK_FALSE;
  vmk_SpinlockUnlock(pEvq->lock);

  efx_ev_qdestroy(pEvq->pCommonEvq);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_EVQ, "qIndex[%u]", qIndex);
}

/*! \brief   Create all common code EVQs.
**
** \param[in]  pAdapter  Pointer to sfvmk_adapter_t
**
** \return:  VMK_OK <success> error code <failure>
*/
VMK_ReturnStatus
sfvmk_evStart(sfvmk_adapter_t *pAdapter)
{
  int qIndex;
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_EVQ);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if (pAdapter->intr.state != SFVMK_INTR_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Interrupt is not started");
    status = VMK_FAILURE;
    goto done;
  }

  /* Initialize the event module */
  status = efx_ev_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_ev_init failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  for (qIndex = 0; qIndex < pAdapter->numEvqsAllocated; qIndex++) {
    status = sfvmk_evqStart(pAdapter, qIndex);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_evqStart(%u) failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto failed_evqstart;
    } else {
      vmk_NetPollEnable(pAdapter->ppEvq[qIndex]->netPoll);
    }
  }

  goto done;

failed_evqstart:
  while (qIndex--) {
    /* Disable netPoll */
    vmk_NetPollDisable(pAdapter->ppEvq[qIndex]->netPoll);
    sfvmk_evqStop(pAdapter, qIndex);
  }
  /* Tear down the event module */
  efx_ev_fini(pAdapter->pNic);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_EVQ);

  return status;
}

/*! \brief Destroy all common code EVQs
**
** \param[in]  pAdapter Pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_evStop(sfvmk_adapter_t *pAdapter)
{
  int qIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_EVQ);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if (pAdapter->intr.state != SFVMK_INTR_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Interrupt is not started");
    goto done;
  }

  qIndex = pAdapter->numEvqsAllocated;
  while (qIndex--)
    sfvmk_evqStop(pAdapter, qIndex);

  /* Tear down the event module */
  if (pAdapter->pNic != NULL)
    efx_ev_fini(pAdapter->pNic);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_EVQ);
}

/*! \brief  Allocate resources for a particular EVQ
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex      eventQ index
**
** \return:   VMK_OK <success> VMK_FAILURE < failure>
*/
static VMK_ReturnStatus
sfvmk_evqInit(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_evq_t *pEvq;
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_EVQ, "qIndex[%d]", qIndex);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if (qIndex >= pAdapter->numEvqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid event queue index %d", qIndex);
    goto done;
  }

  pEvq = vmk_HeapAlloc(sfvmk_modInfo.heapID, sizeof(sfvmk_evq_t));
  if(pEvq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HeapAlloc failed");
    status = VMK_NO_MEMORY;
    goto done;
  }
  vmk_Memset(pEvq, 0 , sizeof(sfvmk_evq_t));

  pEvq->index = qIndex;
  pEvq->pAdapter = pAdapter;

  /* Build an event queue with room for one event per TX and RX buffer,
   * plus some extra for link state events and MCDI completions.
   */

  if (qIndex == 0) {
    pEvq->numDesc = pAdapter->numRxqBuffDesc +
                    pAdapter->numTxqBuffDesc +
                    SFVMK_EVQ_EXTRA_EVENT_SPACE;
  } else {
    pEvq->numDesc = pAdapter->numRxqBuffDesc + pAdapter->numTxqBuffDesc;
  }
  /* Make number of descriptors to nearest power of 2 */
  pEvq->numDesc = sfvmk_pow2GE(pEvq->numDesc);

  pEvq->mem.ioElem.length = EFX_EVQ_SIZE(pEvq->numDesc);

  /* Allocate the event queue DMA buffer */
  pEvq->mem.pEsmBase = sfvmk_allocDMAMappedMem(pAdapter->dmaEngine,
                                               pEvq->mem.ioElem.length,
                                               &pEvq->mem.ioElem.ioAddr);
  if(pEvq->mem.pEsmBase == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_allocDMAMappedMem failed");
    status = VMK_NO_MEMORY;
    goto failed_dma_alloc;
  }
  pEvq->mem.esmHandle = pAdapter->dmaEngine;

  status = sfvmk_createLock(pAdapter, "evqLock",
                            SFVMK_SPINLOCK_RANK_EVQ_LOCK,
                            &pEvq->lock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_lock;
  }

  pAdapter->ppEvq[qIndex] = pEvq;
  pEvq->state = SFVMK_EVQ_STATE_INITIALIZED;

  goto done;

failed_create_lock:
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                         pEvq->mem.pEsmBase,
                         pEvq->mem.ioElem.ioAddr,
                         pEvq->mem.ioElem.length);

failed_dma_alloc:
  vmk_HeapFree(sfvmk_modInfo.heapID, pEvq);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_EVQ, "qIndex[%d]", qIndex );

  return status;
}

/*! \brief  Releases resource allocated for a particular EVQ
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex       eventQ index
**
** \return: void
*/
static void
sfvmk_evqFini(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  sfvmk_evq_t *pEvq;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_EVQ, "qIndex[%d]", qIndex);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if (qIndex >= pAdapter->numEvqsAllocated) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid event queue index %d", qIndex);
    goto done;
  }

  pEvq = pAdapter->ppEvq[qIndex];
  if (pEvq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL EVQ ptr");
    goto done;
  }

  if (pEvq->state != SFVMK_EVQ_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Event queue is not yet initialized");
    goto done;
  }

  sfvmk_destroyLock(pEvq->lock);

  /* Free DMA memory allocated for event queue */
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                         pEvq->mem.pEsmBase,
                         pEvq->mem.ioElem.ioAddr,
                         pEvq->mem.ioElem.length);

  /* Free memory allocated for event queue object */
  vmk_HeapFree(sfvmk_modInfo.heapID, pEvq);

  /* Invalidate event queue ptr */
  pAdapter->ppEvq[qIndex] = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_EVQ, "qIndex[%d]", qIndex);
}

/*! \brief  Allocate resources for all EVQs
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
VMK_ReturnStatus
sfvmk_evInit(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_BAD_PARAM;
  vmk_uint32 qIndex;
  vmk_uint32 evqArraySize;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_EVQ);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if (pAdapter->intr.state != SFVMK_INTR_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Interrupt is not yet initialized");
    status = VMK_FAILURE;
    goto done;
  }
  /* Number of EVQs can not be greater than number of  intrrupt allocated */
  pAdapter->numEvqsAllocated = MIN(pAdapter->intr.numIntrAlloc,
                                   pAdapter->numEvqsDesired);

  evqArraySize = sizeof(sfvmk_evq_t *) * pAdapter->numEvqsAllocated;
  pAdapter->ppEvq = vmk_HeapAlloc(sfvmk_modInfo.heapID, evqArraySize);
  if(pAdapter->ppEvq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HeapAlloc failed");
    status = VMK_NO_MEMORY;
    goto failed_evq_alloc;
  }
  vmk_Memset(pAdapter->ppEvq, 0, evqArraySize);

  /* Setting default event moderation */
  pAdapter->intrModeration = SFVMK_MODERATION_USEC;

  pAdapter->numRxqBuffDesc = SFVMK_NUM_RXQ_DESC;
  pAdapter->numTxqBuffDesc = SFVMK_NUM_TXQ_DESC;

  /* Initialize the event queue(s) - one per interrupt.*/
  for (qIndex = 0; qIndex < pAdapter->numEvqsAllocated; qIndex++) {
    status = sfvmk_evqInit(pAdapter, qIndex);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_evqInit(%d) failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto failed_evq_init;
    }
  }

  goto done;

failed_evq_init:
  while (qIndex > 0)
    sfvmk_evqFini(pAdapter, --qIndex);

failed_evq_alloc:
  pAdapter->numEvqsAllocated = 0;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_EVQ);

  return status;
}

/*! \brief  Releases resources for all EVQs
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_evFini(sfvmk_adapter_t *pAdapter)
{
  int qIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_EVQ);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if (pAdapter->intr.state != SFVMK_INTR_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Interrupt is not yet initialized");
    goto done;
  }

  pAdapter->intrModeration = 0;

  /* Tear down the event queue(s). */
  qIndex = pAdapter->numEvqsAllocated;
  while (qIndex > 0)
    sfvmk_evqFini(pAdapter, --qIndex);

  pAdapter->numEvqsAllocated = 0;

  vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter->ppEvq);
  pAdapter->ppEvq = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_EVQ);
}

