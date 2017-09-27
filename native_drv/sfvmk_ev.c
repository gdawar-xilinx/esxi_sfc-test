/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

/* EVQ budget (in descriptors) to allow for MCDI and Rx/Tx error events. */
#define SFVMK_EVQ_EXTRA_EVENT_SPACE   128

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

  /* Build an event queue with room for one event per TX and RX buffer,
   * plus some extra for link state events and MCDI completions.
   * There are three tx queues of type (NON_CKSUM, IP_CKSUM, IP_TCP_UDP_CKSUM)
   * in the first event queue and one in other. */

  if (qIndex == 0) {
    pEvq->numDesc = pAdapter->numRxqBuffDesc +
                    (SFVMK_TXQ_NTYPES * pAdapter->numTxqBuffDesc) +
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

