/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

/*! \brief     Initialize all resources required for a RXQ
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex       RXQ Index
**
** \return: VMK_OK <success> error code <failure>
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

  pRxq->numDesc = pAdapter->numRxqBuffDesc;
  if (!ISP2(pRxq->numDesc)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid descriptors count: %u", pRxq->numDesc);
    status = VMK_BAD_PARAM;
    goto failed_valid_desc;
  }

  pRxq->index = qIndex;
  pRxq->ptrMask = pRxq->numDesc - 1;

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

  status = sfvmk_createLock(pAdapter, "rxqLock",
                            SFVMK_SPINLOCK_RANK_RXQ_LOCK,
                            &pRxq->lock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock failed status  %s",
                        vmk_StatusToString(status));
    goto failed_create_lock;
  }

  pRxq->state = SFVMK_RXQ_STATE_INITIALIZED;

  pAdapter->ppRxq[qIndex] = pRxq;

  goto done;

failed_create_lock:
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                         pRxq->mem.pEsmBase,
                         pRxq->mem.ioElem.ioAddr,
                         pRxq->mem.ioElem.length);

failed_dma_alloc:
failed_valid_desc:
  vmk_HeapFree(sfvmk_modInfo.heapID, pRxq);
  pRxq->numDesc = 0;

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

  sfvmk_destroyLock(pRxq->lock);

  /* Release DMA memory. */
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                         pRxq->mem.pEsmBase,
                         pRxq->mem.ioElem.ioAddr,
                         pRxq->mem.ioElem.length);

  vmk_HeapFree(sfvmk_modInfo.heapID, pRxq);

  pAdapter->ppRxq[qIndex] = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_RX, "qIndex[%u]" , qIndex);
}

/*! \brief     Initialize all resources required for RX module
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
**
** @return: VMK_OK <success> error code <failure>
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

  goto done;

failed_rxq_init:
  /* Tear down the receive queue(s). */
  while (qIndex > 0)
    sfvmk_rxqFini(pAdapter, --qIndex);

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
