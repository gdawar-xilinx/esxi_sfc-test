/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

/* Wait time  for common RXQ to stop */
#define SFVMK_RXQ_STOP_POLL_TIME_USEC   VMK_USEC_PER_MSEC
#define SFVMK_RXQ_STOP_TIME_OUT_USEC    (200 * SFVMK_RXQ_STOP_POLL_TIME_USEC)

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

  pAdapter->numRxqsAllocated = MIN(pAdapter->numEvqsAllocated,
                                   pAdapter->numRxqsAllotted);

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
  pRxq->state = SFVMK_RXQ_STATE_STARTED;
  pRxq->flushState = SFVMK_FLUSH_STATE_REQUIRED;
  vmk_SpinlockUnlock(pEvq->lock);

  goto done;

failed_rx_qcreate:
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
    vmk_SpinlockUnlock(pEvq->lock);

    /* Release DMA memory. */
    sfvmk_freeDMAMappedMem(pRxq->mem.esmHandle,
                           pRxq->mem.pEsmBase,
                           pRxq->mem.ioElem.ioAddr,
                           pRxq->mem.ioElem.length);

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
  vmk_uint32 qIndex;
  VMK_ReturnStatus status = VMK_BAD_PARAM;

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
                                          pAdapter->enableRSS);

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

  efx_rx_fini(pAdapter->pNic);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX);
}

/*! \brief Set the RXQ flush state.
**
** \param[in]  pRxq       Pointer to RXQ
** \param[in]  flushState Flush state
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

