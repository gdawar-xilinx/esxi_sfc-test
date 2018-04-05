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

/* EVQ budget (in descriptors) to allow for MCDI and Rx/Tx error events. */
#define SFVMK_EVQ_EXTRA_EVENT_SPACE   128

/* Wait time  for common EVQ to start */
#define SFVMK_EVQ_START_POLL_TIME_USEC   VMK_USEC_PER_MSEC
#define SFVMK_EVQ_START_TIME_OUT_USEC    (200 *  VMK_USEC_PER_MSEC)

/* TODO: Needs to revisit when dynamic interrupt moderation support
 * is added and performance tuning is done */
#define SFVMK_MODERATION_USEC         30

/* Number of RX desc processed per batch */
#define SFVMK_RX_BATCH                128
/* Number of events processed per batch */
#define SFVMK_EV_BATCH                16384
/* Number of TX descriptors processed per batch */
#define SFVMK_TX_BATCH                64

/* Call back functions which needs to be registered with common EVQ module */
static boolean_t sfvmk_evInitialized(void *arg);
static boolean_t sfvmk_evException(void *arg, uint32_t code, uint32_t data);
static boolean_t sfvmk_evLinkChange(void *arg, efx_link_mode_t linkMode);
static boolean_t sfvmk_evRxqFlushDone(void *arg, uint32_t rxq_index);
static boolean_t sfvmk_evRxqFlushFailed(void *arg, uint32_t rxqIndex);
void sfvmk_evqComplete(sfvmk_evq_t *pEvq);
static boolean_t sfvmk_evTxqFlushDone(void *arg, uint32_t txqIndex);
static boolean_t sfvmk_evSoftware(void *arg, uint16_t magic);
static boolean_t sfvmk_evRX(void *arg, uint32_t label, uint32_t id,
                            uint32_t size, uint16_t flags);
static boolean_t sfvmk_evTx(void *arg, uint32_t label, uint32_t id);

static const efx_ev_callbacks_t sfvmk_evCallbacks = {
  .eec_tx = sfvmk_evTx,
  .eec_rx = sfvmk_evRX,
  .eec_software	= sfvmk_evSoftware,
  .eec_exception = sfvmk_evException,
  .eec_link_change = sfvmk_evLinkChange,
  .eec_initialized = sfvmk_evInitialized,
  .eec_txq_flush_done = sfvmk_evTxqFlushDone,
  .eec_rxq_flush_done = sfvmk_evRxqFlushDone,
  .eec_rxq_flush_failed = sfvmk_evRxqFlushFailed,
};

/*! \brief Called when a RX event received on eventQ
**
** \param[in] arg      Ptr to event queue
** \param[in] label    Queue label
** \param[in] id       Last RX desc id to process.
** \param[in] size     Pkt size
** \param[in] flags    Pkt metadeta info
**
** \return: VMK_FALSE  In a single interrupt if number of RX events processed
**                     is less than SFVMK_EV_BATCH
** \return: VMK_True   Failure
*/
static boolean_t
sfvmk_evRX(void *arg, uint32_t label, uint32_t id, uint32_t size, uint16_t flags)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;
  sfvmk_adapter_t *pAdapter = NULL;
  sfvmk_rxq_t *pRxq = NULL;
  vmk_uint32 stop;
  vmk_uint32 delta;
  sfvmk_rxSwDesc_t *pRxDesc = NULL;
  const efx_nic_cfg_t *pNicCfg;
  VMK_ReturnStatus status;

  VMK_ASSERT_NOT_NULL(pEvq);

  vmk_SpinlockAssertHeldByWorld(pEvq->lock);

  pAdapter = pEvq->pAdapter;
  VMK_ASSERT_NOT_NULL(pAdapter);

  if (pEvq->exception) {
    goto fail;
  }

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL nic cfg ptr");
    goto fail;
  }

  /* Get corresponding RXQ */
  VMK_ASSERT_NOT_NULL(pAdapter->ppRxq);
  pRxq = pAdapter->ppRxq[pEvq->index];

  VMK_ASSERT_NOT_NULL(pRxq);

  if (VMK_UNLIKELY(pRxq->state != SFVMK_RXQ_STATE_STARTED)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RXQ[%u] is not yet started", pRxq->index);
    goto fail;
  }

  stop = (id + 1) & pRxq->ptrMask;
  id = pRxq->pending & pRxq->ptrMask;
  delta = (stop >= id) ? (stop - id) : (pRxq->numDesc - id + stop);
  pRxq->pending += delta;

  if ((delta == 0) || (delta > pNicCfg->enc_rx_batch_max)) {
    pEvq->exception = B_TRUE;
    SFVMK_ADAPTER_ERROR(pAdapter, "RXQ[%u] completion out of order", pRxq->index);
    if ((status = sfvmk_scheduleReset(pAdapter)) != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_scheduleReset failed with error %s",
                          vmk_StatusToString(status));
    }

    goto fail;
  }

  /* Update RX desc */
  for (; id != stop; id = (id + 1) & pRxq->ptrMask) {
    pRxDesc = &pRxq->pQueue[id];
    pRxDesc->flags = flags;
    pRxDesc->size = (uint16_t)size;
  }

  pEvq->rxDone++;
  SFVMK_ADAPTER_DEBUG_IO(pAdapter, SFVMK_DEBUG_EVQ, SFVMK_LOG_LEVEL_IO,
                         "pending %u, completed %u", pRxq->pending, pRxq->completed);

  if (pRxq->pending - pRxq->completed >= SFVMK_RX_BATCH)
    sfvmk_evqComplete(pEvq);

  return (pEvq->rxDone >= pEvq->rxBudget);

fail:
  return VMK_TRUE;
}

/*! \brief called when a TX event received on eventQ
**
** \param[in] arg      ptr to event queue
** \param[in] label    queue label
** \param[in] id       last buffer descriptor index to process
**
** \return: VMK_FALSE if number of Tx events processed in single interrupt
            is less than SFVMK_EV_BATCH, VMK_TRUE otherwise
*/
static boolean_t
sfvmk_evTx(void *arg, uint32_t label, uint32_t id)
{
  struct sfvmk_evq_s *pEvq;
  struct sfvmk_txq_s *pTxq;
  struct sfvmk_adapter_s *pAdapter;
  unsigned int stop;
  unsigned int delta;
  vmk_Bool status = VMK_TRUE;
  sfvmk_pktCompCtx_t compCtx = {
    .type = SFVMK_PKT_COMPLETION_NETPOLL,
  };

  pEvq = (sfvmk_evq_t *)arg;
  VMK_ASSERT_NOT_NULL(pEvq);

  vmk_SpinlockAssertHeldByWorld(pEvq->lock);
  pAdapter = pEvq->pAdapter;
  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pAdapter->ppTxq);

  /* Process only default transmit queue when system is in panic state */
  if (VMK_UNLIKELY(vmk_SystemCheckState(VMK_SYSTEM_STATE_PANIC) == VMK_TRUE) &&
     (pEvq != pAdapter->ppEvq[0])) {
    SFVMK_ADAPTER_DEBUG_IO(pAdapter, SFVMK_DEBUG_TX, SFVMK_LOG_LEVEL_IO,
                           "System in panic state, returning");
    status = VMK_FALSE;
    goto done;
  }

  pTxq = pAdapter->ppTxq[pEvq->index];
  VMK_ASSERT_NOT_NULL(pTxq);

  vmk_SpinlockLock(pTxq->lock);

  if (VMK_UNLIKELY(pTxq->state != SFVMK_TXQ_STATE_STARTED)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid TXQ state[%d]", pTxq->state);
    vmk_SpinlockUnlock(pTxq->lock);
    goto done;
  }

  stop = (id + 1) & pTxq->ptrMask;
  id = pTxq->pending & pTxq->ptrMask;

  delta = (stop >= id) ? (stop - id) : (pTxq->numDesc - id + stop);
  pTxq->pending += delta;

  pEvq->txDone++;

  if (pTxq->pending - pTxq->completed >= SFVMK_TX_BATCH) {
    compCtx.netPoll = pEvq->netPoll;
    sfvmk_txqComplete(pTxq, pEvq, &compCtx);
  }
  vmk_SpinlockUnlock(pTxq->lock);

  status = (pEvq->txDone >= SFVMK_EV_BATCH);

done:
  return status;
}

/*! \brief Gets called when initialized event comes for an eventQ.
**
** \param[in] arg        Pointer to eventQ
**
** \return: VMK_FALSE [success]
** \return: VMK_TRUE  [failure]
*/
static boolean_t
sfvmk_evInitialized(void *arg)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;

  if (pEvq == NULL) {
    SFVMK_ERROR("NULL event queue ptr");
    goto fail;
  }

  vmk_SpinlockAssertHeldByWorld(pEvq->lock);

  if (pEvq->state != SFVMK_EVQ_STATE_STARTING) {
    SFVMK_ADAPTER_ERROR(pEvq->pAdapter, "Invalid EVQ state(%u)", pEvq->state);
    goto fail;
  }

  pEvq->state = SFVMK_EVQ_STATE_STARTED;

  SFVMK_ADAPTER_DEBUG(pEvq->pAdapter, SFVMK_DEBUG_EVQ, SFVMK_LOG_LEVEL_DBG,
                      "EventQ %u is started", pEvq->index);

  return VMK_FALSE;

fail:
  return VMK_TRUE;
}

/*! \brief Gets called when an exception received on eventQ.
**
** \param[in] arg      Pointer to event queue
** \param[in] code     Exception code
** \param[in] data     Exception data
**
** \return: VMK_FALSE [success]
** \return: VMK_TRUE  [failure]
*/
static boolean_t
sfvmk_evException(void *arg, uint32_t code, uint32_t data)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;
  VMK_ReturnStatus status;

  if (pEvq == NULL) {
    SFVMK_ERROR("NULL event queue ptr");
    goto fail;
  }

  vmk_SpinlockAssertHeldByWorld(pEvq->lock);

  if (pEvq->state != SFVMK_EVQ_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pEvq->pAdapter, "Invalid EVQ state(%u)", pEvq->state);
    goto fail;
  }

  pEvq->exception = VMK_TRUE;

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

  if (code != EFX_EXCEPTION_UNKNOWN_SENSOREVT) {
    if ((status = sfvmk_scheduleReset(pEvq->pAdapter)) != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pEvq->pAdapter, "sfvmk_scheduleReset failed with error %s",
                          vmk_StatusToString(status));
    }
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
** \return: VMK_FALSE [success]
** \return: VMK_True  [failure]
*/
static boolean_t
sfvmk_evLinkChange(void *arg, efx_link_mode_t linkMode)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;
  sfvmk_adapter_t *pAdapter = NULL;
  VMK_ReturnStatus status;

  if (pEvq == NULL) {
    SFVMK_ERROR("NULL event queue ptr");
    goto fail;
  }

  vmk_SpinlockAssertHeldByWorld(pEvq->lock);

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

  pAdapter->port.linkMode = linkMode;
  status = sfvmk_scheduleLinkUpdate(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_scheduleLinkUpdate failed status: %s",
                        vmk_StatusToString(status));
    goto fail;
  }

done:
  return VMK_FALSE;

fail:
  return VMK_TRUE;

}

/*! \brief Check flushed event received for a RXQ.
**
** \param[in] arg        Pointer to event queue
** \param[in] rxqIndex   RXQ index
** \param[in] flushState flush state
**
** \return: VMK_FALSE [success]
** \return: VMK_TRUE  [failure]
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

  vmk_SpinlockAssertHeldByWorld(pEvq->lock);

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

  if (pRxq->index == pEvq->index) {
    sfvmk_setRxqFlushState(pRxq, flushState);
    goto done;
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
** \return: VMK_FALSE [success]
** \return: VMK_TRUE  [failure]
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
** \return: VMK_FALSE [success]
** \return: VMK_TRUE  [failure]
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
** \return: VMK_FALSE [success]
*/
static boolean_t
sfvmk_evTxqFlushDone(void *arg, uint32_t txqIndex)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;
  sfvmk_txq_t *pTxq = NULL;
  sfvmk_adapter_t *pAdapter = NULL;

  VMK_ASSERT_NOT_NULL(pEvq);

  pAdapter = pEvq->pAdapter;
  VMK_ASSERT_NOT_NULL(pAdapter);

  if (pAdapter->ppTxq != NULL)
    pTxq = pAdapter->ppTxq[txqIndex];

  VMK_ASSERT_NOT_NULL(pTxq);

  if (pTxq->index == pEvq->index)
    sfvmk_txqFlushDone(pTxq);
  else
    goto fail;

  return VMK_FALSE;

fail:
  return VMK_TRUE;

}

/*! \brief Gets called when a SW event comes
**
** \param[in] arg     Pointer to event queue
** \param[in] magic   Magic number to ideintify sw events
**
** \return: VMK_FALSE Success
** \return: VMK_True  Failure
*/
static boolean_t
sfvmk_evSoftware(void *arg, uint16_t magic)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;;
  sfvmk_adapter_t *pAdapter;
  sfvmk_rxq_t *pRxq;
  sfvmk_pktCompCtx_t compCtx = {
    .type = SFVMK_PKT_COMPLETION_NETPOLL,
  };

  VMK_ASSERT_NOT_NULL(pEvq);

  pAdapter = pEvq->pAdapter;
  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pAdapter->ppRxq);

  pRxq = pAdapter->ppRxq[pEvq->index];
  VMK_ASSERT_NOT_NULL(pRxq);

  magic &= ~SFVMK_MAGIC_RESERVED;

  if (magic == SFVMK_SW_EV_RX_QREFILL) {
    compCtx.netPoll = pEvq->netPoll;
    sfvmk_rxqFill(pRxq, &compCtx);
  }

  return VMK_FALSE;
}

/*! \brief  Poll event from eventQ and process it. function should be called in thread
**          context only.
**
** \param[in] pEvq     Pointer to event queue
**
** \return: VMK_FALSE [success]
** \return: VMK_TRUE  [failure]
*/
VMK_ReturnStatus
sfvmk_evqPoll(sfvmk_evq_t *pEvq)
{
  VMK_ReturnStatus status = VMK_FAILURE;

  if (pEvq == NULL) {
    SFVMK_ERROR("NULL event queue ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  vmk_SpinlockLock(pEvq->lock);

  if ((pEvq->state != SFVMK_EVQ_STATE_STARTING) &&
      (pEvq->state != SFVMK_EVQ_STATE_STARTED)) {
    status = VMK_FAILURE;
    goto done;
  }

  pEvq->rxDone = 0;
  pEvq->txDone = 0;

  /* Poll the queue */
  efx_ev_qpoll(pEvq->pCommonEvq, &pEvq->readPtr, &sfvmk_evCallbacks, pEvq);

  /* Perform any pending completion processing */
  sfvmk_evqComplete(pEvq);

  /* Re-prime the event queue for interrupts */
  status = efx_ev_qprime(pEvq->pCommonEvq, pEvq->readPtr);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pEvq->pAdapter, "efx_ev_qprime failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

done:
  vmk_SpinlockUnlock(pEvq->lock);

  return status;
}

/*! \brief   Perform any pending completion processing
**
** \param[in] pEvq     ptr to event queue
**
** \return: void
*/
void sfvmk_evqComplete(sfvmk_evq_t *pEvq)
{
  sfvmk_adapter_t *pAdapter = NULL;
  sfvmk_rxq_t *pRxq = NULL;
  sfvmk_txq_t *pTxq = NULL;
  sfvmk_pktCompCtx_t compCtx = {
    .type = SFVMK_PKT_COMPLETION_NETPOLL,
  };

  VMK_ASSERT_NOT_NULL(pEvq);

  pAdapter = pEvq->pAdapter;
  VMK_ASSERT_NOT_NULL(pAdapter);

  VMK_ASSERT_NOT_NULL(pAdapter->ppRxq);
  pRxq = pAdapter->ppRxq[pEvq->index];

  /* Call RX module's fn to process RX data */
  if (pRxq->pending != pRxq->completed) {
    compCtx.netPoll = pEvq->netPoll;
    sfvmk_rxqComplete(pRxq, &compCtx);
  }

  VMK_ASSERT_NOT_NULL(pAdapter->ppTxq);
  pTxq = pAdapter->ppTxq[pEvq->index];

  vmk_SpinlockLock(pTxq->lock);
  if (VMK_UNLIKELY(pTxq->state != SFVMK_TXQ_STATE_STARTED)) {
    goto done;
  }
  if (pTxq->pending != pTxq->completed) {
    compCtx.netPoll = pEvq->netPoll;
    sfvmk_txqComplete(pTxq, pEvq, &compCtx);
  }

done:
  vmk_SpinlockUnlock(pTxq->lock);

  return;
}

/*! \brief    Create common code EVQ and wait for initilize event from the fw.
**
** \param[in]  pAdapter    Pointer to sfvmk_adapter_t
** \param[in]  qIndex      EventQ index
**
** \return: 0 [success] error code [failure]
*/
static VMK_ReturnStatus
sfvmk_evqStart(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  sfvmk_evq_t *pEvq = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint64 timeout, currentTime;

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

  /* Lock has been released here as there is no need to have a lock when
   * state is set to initialized.
   */
  vmk_SpinlockUnlock(pEvq->lock);

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

  vmk_Memset(pEvq->mem.pEsmBase, 0xff, EFX_EVQ_SIZE(pEvq->numDesc));

  /* Create common code event queue. */
  status = efx_ev_qcreate(pAdapter->pNic, qIndex, &pEvq->mem, pEvq->numDesc, 0,
                          pAdapter->intrModeration, EFX_EVQ_FLAGS_NOTIFY_INTERRUPT,
                          &pEvq->pCommonEvq);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_ev_qcreate failed status: %s",
                        vmk_StatusToString(status));
    goto failed_ev_qcreate;
  }

  vmk_SpinlockLock(pEvq->lock);

  pEvq->state = SFVMK_EVQ_STATE_STARTING;

  /* Enable netpoll to process events */
  vmk_NetPollEnable(pEvq->netPoll);

  /* Prime the event queue for interrupts */
  status = efx_ev_qprime(pEvq->pCommonEvq, pEvq->readPtr);
  if (status != VMK_OK) {
    vmk_SpinlockUnlock(pEvq->lock);
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_ev_qprime failed status: %s",
                        vmk_StatusToString(status));
    goto failed_ev_qprime;
  }

  vmk_SpinlockUnlock(pEvq->lock);

  sfvmk_getTime(&currentTime);
  timeout = currentTime + SFVMK_EVQ_START_TIME_OUT_USEC;

  while (currentTime < timeout) {
    status = vmk_WorldSleep(SFVMK_EVQ_START_POLL_TIME_USEC);
    if ((status != VMK_OK) && (status != VMK_WAIT_INTERRUPTED)) {
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
    sfvmk_getTime(&currentTime);
  }

  SFVMK_ADAPTER_ERROR(pAdapter, "Event queue[%u] is not started", qIndex);
  status = VMK_TIMEOUT;

failed_world_sleep:
failed_ev_qprime:
  vmk_NetPollDisable(pEvq->netPoll);
  efx_ev_qdestroy(pEvq->pCommonEvq);

failed_ev_qcreate:
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                         pEvq->mem.pEsmBase,
                         pEvq->mem.ioElem.ioAddr,
                         pEvq->mem.ioElem.length);

  vmk_SpinlockLock(pEvq->lock);
  pEvq->state = SFVMK_EVQ_STATE_INITIALIZED;
  vmk_SpinlockUnlock(pEvq->lock);

failed_dma_alloc:
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

  vmk_NetPollDisable(pEvq->netPoll);
  efx_ev_qdestroy(pEvq->pCommonEvq);

  /* Free DMA memory allocated for event queue */
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                         pEvq->mem.pEsmBase,
                         pEvq->mem.ioElem.ioAddr,
                         pEvq->mem.ioElem.length);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_EVQ, "qIndex[%u]", qIndex);
}

/*! \brief   Create all common code EVQs.
**
** \param[in]  pAdapter  Pointer to sfvmk_adapter_t
**
** \return:  VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_evStart(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 qIndex;
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
    }
  }

  goto done;

failed_evqstart:
  while (qIndex--) {
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
** \return:   VMK_OK [success] VMK_FAILURE < failure>
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

  status = sfvmk_createLock(pAdapter, "evqLock",
                            SFVMK_SPINLOCK_RANK_EVQ_LOCK,
                            &pEvq->lock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_lock;
  }

  pEvq->panicPktList = NULL;
  pAdapter->ppEvq[qIndex] = pEvq;
  pEvq->state = SFVMK_EVQ_STATE_INITIALIZED;

  goto done;

failed_create_lock:
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
** \return: VMK_OK [success] error code [failure]
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

  /* Setting default rx & tx ring params */
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

/*! \brief  Fucntion to configure evq interrupt moderation
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex       event queue index
** \param[in]  uSec         interrupt moderaion value in uSec
**
** \return:    VMK_OK or error code
*/
VMK_ReturnStatus
sfvmk_evqModerate(sfvmk_adapter_t *pAdapter,
                  unsigned int qIndex,
                  unsigned int uSec)
{
  sfvmk_evq_t *pEvq = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_EVQ);

  VMK_ASSERT_NOT_NULL(pAdapter);

  VMK_ASSERT_LT(qIndex, pAdapter->numEvqsAllocated);

  if (pAdapter->ppEvq != NULL)
    pEvq = pAdapter->ppEvq[qIndex];

  if (pEvq == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL event queue ptr");
    status = VMK_FAILURE;
    goto done;
  }

  if (pEvq->state != SFVMK_EVQ_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid EVQ state(%u)", pEvq->state);
    status = VMK_FAILURE;
    goto done;
  }

  status = efx_ev_qmoderate(pEvq->pCommonEvq, uSec);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_ev_qmoderate failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_EVQ);

  return status;
}
