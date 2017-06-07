/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"

/* value for batch processing */
#define SFVMK_REFILL_BATCH  64

/* polling time for flush to be completed */
#define SFVMK_RXQ_STOP_POLL_WAIT 100*SFVMK_ONE_MILISEC
#define SFVMK_RXQ_STOP_POLL_TIMEOUT 20


/* hash key */
static uint8_t sfvmk_toepKey[] = {
  0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
  0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
  0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
  0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
  0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa
};


void sfvmk_rxDeliver(sfvmk_adapter_t *pAdapter, struct
                            sfvmk_rxSwDesc_s *pRxDesc, unsigned int qIndex);
void sfvmk_rxqComplete(sfvmk_rxq_t *pRxq, boolean_t eop);
void sfvmk_rxqFill(sfvmk_rxq_t *pRxq, unsigned int  numBufs,
                          boolean_t retrying);
static VMK_ReturnStatus sfvmk_rxqInit(sfvmk_adapter_t *pAdapter,
                                              unsigned int qIndex);
static void sfvmk_rxqFini(sfvmk_adapter_t *pAdapter, unsigned int qIndex);
static int sfvmk_rxqStart( sfvmk_adapter_t *pAdapter, unsigned int qIndex);
static void sfvmk_rxqStop( sfvmk_adapter_t *pAdapter, unsigned int qIndex);



/**-----------------------------------------------------------------------------
*
* sfvmk_rxDeliver --
*
* @brief      function to deliver rx pkt to uplink layer
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
* @param[in]  rxDesc      rx buffer desc
* @param[in]  qIndex      rxq Index
*
* @result: void
*
*-----------------------------------------------------------------------------*/
void sfvmk_rxDeliver(sfvmk_adapter_t *pAdapter, struct
                            sfvmk_rxSwDesc_s *pRxDesc, unsigned int qIndex)
{
  return ;
}
/**-----------------------------------------------------------------------------
* sfvmk_rxqComplete --
*
* @brief      read the pkt from the queue and pass it to uplink layer
*             or discard it
*
* @param[in]  pRxq     pointer to rxQ
* @param[in]  eop
*
* @result: void
*
*-----------------------------------------------------------------------------*/
void sfvmk_rxqComplete(sfvmk_rxq_t *pRxq, boolean_t eop)
{
  sfvmk_adapter_t *pAdapter = pRxq->pAdapter;
  unsigned int index;
  struct sfvmk_evq_s *pEvq;
  unsigned int completed;
  unsigned int level;
  vmk_PktHandle *pPkt ;
  struct sfvmk_rxSwDesc_s *prev = NULL;

  VMK_ASSERT_BUG(NULL != pRxq, " NULL rxQ ptr");

  index = pRxq->index;
  pEvq = pAdapter->pEvq[index];

  completed = pRxq->completed;
  while (completed != pRxq->pending) {
    unsigned int id;
    struct sfvmk_rxSwDesc_s *rxDesc;

    id = completed++ & pRxq->ptrMask;
    rxDesc = &pRxq->pQueue[id];
    pPkt = rxDesc->pPkt;

    if (VMK_UNLIKELY(pRxq->initState != SFVMK_RXQ_STARTED))
      goto sfvmk_discard;

    if (rxDesc->flags & (EFX_ADDR_MISMATCH | EFX_DISCARD))
      goto sfvmk_discard;

    switch (rxDesc->flags & (EFX_PKT_IPV4 | EFX_PKT_IPV6)) {
      case EFX_PKT_IPV4:
        rxDesc->flags &=
        ~(EFX_CKSUM_IPV4 | EFX_CKSUM_TCPUDP);
        break;
      case EFX_PKT_IPV6:
        rxDesc->flags &= ~EFX_CKSUM_TCPUDP;
        break;
      case 0:
        /* Check for loopback packets */
        break;

      default:
        SFVMK_ERR(pAdapter, "Rx Desc with both ipv4 and ipv6 flags");
        goto sfvmk_discard;
    }

    /* Pass packet up the stack  */
    if (prev != NULL) {
      sfvmk_rxDeliver(pAdapter, prev, pRxq->index);
    }

    prev = rxDesc;
    continue;

sfvmk_discard:
    /* Return the packet to the pool */
    vmk_PktRelease(pPkt);
    rxDesc->pPkt = NULL;
    SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_DBG, "completed = %d"
              "pending = %d", completed, pRxq->pending);
  }

  pRxq->completed = completed;
  level = pRxq->added - pRxq->completed;

  /* Pass last packet up the stack or into LRO */
  if (prev != NULL) {
    sfvmk_rxDeliver(pAdapter, prev, pRxq->index);
  }
}
/**-----------------------------------------------------------------------------
*
* sfvmk_rxqFill --
*
* @brief      fill RX buffer desc with pkt information.
*
* @param[in]  pRxq      pointer to rxQ
* @param[in]  numBuf    number of buf desc to be filled in
*
* @result: void
*
*-----------------------------------------------------------------------------*/
void sfvmk_rxqFill(sfvmk_rxq_t *pRxq, unsigned int  numBufs,
                          boolean_t retrying)
{
  sfvmk_adapter_t *pAdapter = NULL;
  struct sfvmk_rxSwDesc_s *rxDesc;
  efsys_dma_addr_t addr[SFVMK_REFILL_BATCH];
  vmk_PktHandle *pNewpkt = NULL;
  const vmk_SgElem *pFrag;
  VMK_ReturnStatus status;
  vmk_DMAMapErrorInfo dmaMapErr;
  vmk_SgElem mapperIN, mapperOut;
  vmk_uint32 posted , maxBuf;
  vmk_uint32 batch;
  vmk_uint32 rxfill;
  vmk_uint32 mblkSize;
  vmk_uint32 id;

  VMK_ASSERT_BUG(NULL != pRxq, " NULL Rxq ptr");
  pAdapter = pRxq->pAdapter;
  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");


  batch = 0;
  rxfill = pRxq->added - pRxq->completed;

  maxBuf = MIN(EFX_RXQ_LIMIT(pRxq->entries) - rxfill, numBufs);
  mblkSize = pAdapter->rxBufferSize - pAdapter->rxBufferAlign;

  for (posted = 0; posted < maxBuf ; posted++) {

    status = vmk_PktAllocForDMAEngine(mblkSize, pAdapter->dmaEngine, &pNewpkt);
    if (VMK_UNLIKELY(status != VMK_OK)) {
      SFVMK_ERR(pAdapter, "failed to allocate pkt. err: %d", status);
      break;
    }
    pFrag = vmk_PktSgElemGet(pNewpkt, 0);
    if (pFrag == NULL) {
      vmk_PktRelease(pNewpkt);
      break;
    }
    /* map it io dma address*/
    mapperIN.addr = pFrag->addr;
    mapperIN.length = pFrag->length;
    status = vmk_DMAMapElem(pAdapter->dmaEngine, VMK_DMA_DIRECTION_TO_MEMORY,
                            &mapperIN, VMK_TRUE, &mapperOut, &dmaMapErr);
    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed to map %p size %d to IO address, %s.",
                pNewpkt, mblkSize,vmk_DMAMapErrorReasonToString(dmaMapErr.reason));
      vmk_PktRelease(pNewpkt);
      break;
    }

    id = (pRxq->added + batch) & pRxq->ptrMask;

    rxDesc = &pRxq->pQueue[id];
    rxDesc->flags = EFX_DISCARD;
    rxDesc->pPkt = pNewpkt;
    rxDesc->size = mblkSize;
    addr[batch++] = mapperOut.ioAddr;


    if (batch == SFVMK_REFILL_BATCH) {
      /* post buffer to rxq module */
      efx_rx_qpost(pRxq->pCommonRxq, addr, mblkSize, batch,
                    pRxq->completed, pRxq->added);
      pRxq->added += batch;
      batch = 0;
    }
  }

  if (batch == SFVMK_REFILL_BATCH) {
    /* post buffer to rxq module */
    efx_rx_qpost(pRxq->pCommonRxq, addr, mblkSize, batch,
                  pRxq->completed, pRxq->added);
    pRxq->added += batch;
    batch = 0;
  }

  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_DBG,
            " No of allocated buffer = %d", posted);

  /* push entries in queue*/
  efx_rx_qpush(pRxq->pCommonRxq, pRxq->added, &pRxq->pushed);

  return;
}

/**-----------------------------------------------------------------------------
*
* sfvmk_rxqInit --
*
* @brief     initialize all the resource required for a rxQ module
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
* @param[in]  qIndex      rxq Index
*
* @result: void
*
*-----------------------------------------------------------------------------*/
static VMK_ReturnStatus sfvmk_rxqInit(sfvmk_adapter_t *pAdapter,
                                              unsigned int qIndex)
{
  sfvmk_rxq_t *pRxq;
  sfvmk_evq_t *pEvq;
  efsys_mem_t *pRxqMem;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");
  VMK_ASSERT_BUG(qIndex < pAdapter->rxqCount, " invalid qindex");

  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "entered sfvmk_rxqInit[%u]", qIndex);

  pRxq = sfvmk_memPoolAlloc(sizeof(sfvmk_rxq_t));
  if(NULL == pRxq) {
    SFVMK_ERR(pAdapter,"failed to allocate memory for rxq object");
    goto sfvmk_rx_alloc_fail;
  }

  pRxq->pAdapter = pAdapter;
  pRxq->index = qIndex;
  pRxq->entries = pAdapter->rxqEntries;
  pRxq->ptrMask = pRxq->entries - 1;

  pAdapter->pRxq[qIndex] = pRxq;

  pRxqMem = &pRxq->mem;

  pEvq = pAdapter->pEvq[qIndex];

  /* Allocate and zero DMA space. */
  pRxqMem->io_elem.length = EFX_RXQ_SIZE(pAdapter->rxqEntries);
  pRxqMem->esm_base = sfvmk_allocCoherentDMAMapping(pAdapter->dmaEngine, pRxqMem->io_elem.length                        ,&pRxqMem->io_elem.ioAddr);
  if (pRxqMem->esm_base == NULL) {
    SFVMK_ERR(pAdapter,"failed to allocate memory for rxq enteries");
    goto sfvmk_dma_alloc_fail;
  }
  pRxqMem->esmHandle  = pAdapter->dmaEngine;

  /* Allocate the context array and the flow table. */
  pRxq->pQueue = sfvmk_memPoolAlloc(sizeof( sfvmk_rxSwDesc_t) * pAdapter->rxqEntries);
  if (NULL == pRxq->pQueue)
    goto sfvmk_desc_alloc_fail;

  pRxq->qdescSize = sizeof(sfvmk_rxSwDesc_t) * pAdapter->rxqEntries;
  pRxq->initState = SFVMK_RXQ_INITIALIZED;

  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "exited sfvmk_rxqInit[%u]", qIndex);

  return VMK_OK;
sfvmk_desc_alloc_fail:
  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pRxqMem->esm_base,
                                pRxqMem->io_elem.ioAddr, pRxqMem->io_elem.length);
sfvmk_dma_alloc_fail:
  sfvmk_memPoolFree(pRxq, sizeof(sfvmk_rxq_t));
sfvmk_rx_alloc_fail:
  return VMK_FAILURE;
}
/**-----------------------------------------------------------------------------
*
* sfvmk_rxqFini --
*
* @brief     destroy all the resources required for a rxQ module
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
* @param[in]  qIndex      rxq Index
*
* @result: void
*
*-----------------------------------------------------------------------------*/
static void sfvmk_rxqFini(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_rxq_t *pRxq;
  efsys_mem_t *pRxqMem;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");

  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "entered sfvmk_rxqFini[%d]", qIndex);

  pRxq = pAdapter->pRxq[qIndex];

  pRxqMem = &pRxq->mem;
  VMK_ASSERT_BUG(pRxq->initState == SFVMK_RXQ_INITIALIZED,
                    "rxq->initState != SFVMK_RXQ_INITIALIZED");

  sfvmk_memPoolFree(pRxq->pQueue, pRxq->qdescSize);
  /* Release DMA memory. */
  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pRxqMem->esm_base,
                              pRxqMem->io_elem.ioAddr, pRxqMem->io_elem.length);
  pAdapter->pRxq[qIndex] = NULL;

  sfvmk_memPoolFree(pRxq, sizeof(sfvmk_rxq_t));

  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "exited sfvmk_rxqFini[%u]", qIndex);

}
/**-----------------------------------------------------------------------------
*
* sfvmk_rxInit --
*
* @brief      initialize all the resource required for all rxQ module
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
*
* @result: void
*
*-----------------------------------------------------------------------------*/
int sfvmk_rxInit(sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  int qIndex;
  VMK_ReturnStatus status;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");

  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "entered sfvmk_rxInit");

  pIntr = &pAdapter->intr;

  pAdapter->rxqCount = pIntr->numIntrAlloc;

  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_INITIALIZED,
                  "intr->state != SFXGE_INTR_INITIALIZED");

  /* Initialize the receive queue(s) - one per interrupt. */
  for (qIndex = 0; qIndex < pAdapter->rxqCount; qIndex++) {
    if ((status = sfvmk_rxqInit(pAdapter, qIndex)) != VMK_OK) {
      SFVMK_ERR(pAdapter,"failed to init rxq[%d]", qIndex);
      goto sfvmk_fail;
    }
  }
  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "exited sfvmk_rxInit");

  return status;

sfvmk_fail:
/* Tear down the receive queue(s). */
  while (--qIndex >= 0)
    sfvmk_rxqFini(pAdapter, qIndex);

  pAdapter->rxqCount = 0;

  return status;
}
/**-----------------------------------------------------------------------------
*
* sfvmk_rxFini --
*
* @brief      destroy all the resources required for all rxQ module
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
*
* @result: void
*
*-----------------------------------------------------------------------------*/
void sfvmk_rxFini(sfvmk_adapter_t *pAdapter)
{
  int qIndex;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");
  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "entered sfvmk_rxFini");

  qIndex = pAdapter->rxqCount;
  while (--qIndex >= 0)
    sfvmk_rxqFini(pAdapter, qIndex);

  pAdapter->rxqCount = 0;

  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "exited sfvmk_rxFini");
}
/**-----------------------------------------------------------------------------
*
* sfvmk_rxqStart --
*
* @brief     create rxQ module and fill the rxdesc for a given queue.
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
* @param[in]  qIndex      rxq Index
*
* @result: void
*
*-----------------------------------------------------------------------------*/
static int sfvmk_rxqStart( sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{

  sfvmk_rxq_t *pRxq;
  efsys_mem_t *pRxqMem;
  sfvmk_evq_t *pEvq;
  int rc;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");
  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "entered sfvmk_rxqStart[%u]", qIndex);

  pRxq = pAdapter->pRxq[qIndex];
  pRxqMem = &pRxq->mem;
  pEvq = pAdapter->pEvq[qIndex];

  /* Create the common code receive queue. */
  if ((rc = efx_rx_qcreate(pAdapter->pNic, qIndex, 0, EFX_RXQ_TYPE_DEFAULT,
                            pRxqMem, pAdapter->rxqEntries, 0, pEvq->pCommonEvq,
                            &pRxq->pCommonRxq)) != 0) {
    SFVMK_ERR(pAdapter, "Failed to create rxQ %u", qIndex);
    goto sfvmk_rxq_create_fail;
  }

  SFVMK_EVQ_LOCK(pEvq);

  /* Enable the receive queue. */
  efx_rx_qenable(pRxq->pCommonRxq);

  pRxq->initState = SFVMK_RXQ_STARTED;
  pRxq->flushState = SFVMK_FLUSH_REQUIRED;

  /* Try to fill the queue from the pool. */
  sfvmk_rxqFill(pRxq, EFX_RXQ_LIMIT(pAdapter->rxqEntries), B_FALSE);

  SFVMK_EVQ_UNLOCK(pEvq);
  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "exited sfvmk_rxqStart[%u]", qIndex);

  return VMK_OK;

sfvmk_rxq_create_fail:
  return VMK_FAILURE;
}
/**-----------------------------------------------------------------------------
*
* sfvmk_rxqStop --
*
* @brief     flush and destroy rxq Module for a given queue.
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
* @param[in]  qIndex      rxq Index
*
* @result: void
*
*-----------------------------------------------------------------------------*/
static void sfvmk_rxqStop( sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_rxq_t *pRxq;
  sfvmk_evq_t *pEvq;
  vmk_uint32 count;
  vmk_uint32 retry = 3;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");

  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "entered sfvmk_rxqStop[%u]", qIndex);

  pRxq = pAdapter->pRxq[qIndex];
  pEvq = pAdapter->pEvq[qIndex];

  SFVMK_EVQ_LOCK(pEvq);

  pRxq->initState = SFVMK_RXQ_INITIALIZED;

  while (pRxq->flushState != SFVMK_FLUSH_DONE && retry != 0) {
          pRxq->flushState = SFVMK_FLUSH_PENDING;

    SFVMK_EVQ_UNLOCK(pEvq);

    /* Flush the receive queue */
    if (efx_rx_qflush(pRxq->pCommonRxq) != 0) {
      SFVMK_EVQ_LOCK(pEvq);
      pRxq->flushState = SFVMK_FLUSH_FAILED;
      break;
    }

    count = 0;
    do {
      /* Spin for 100 ms */
      vmk_WorldSleep(SFVMK_RXQ_STOP_POLL_WAIT);

      if (pRxq->flushState != SFVMK_FLUSH_PENDING)
        break;

    } while (++count < SFVMK_RXQ_STOP_POLL_TIMEOUT);

    SFVMK_EVQ_LOCK(pEvq);

    if (pRxq->flushState == SFVMK_FLUSH_PENDING) {
      SFVMK_ERR(pAdapter, "Could not flush RxQ %u", qIndex);
      pRxq->flushState = SFVMK_FLUSH_DONE;
    }
    retry--;
  }

  if (pRxq->flushState == SFVMK_FLUSH_FAILED) {
    SFVMK_ERR(pAdapter, "Flushing Rxq Failed RxQ %u", qIndex);
    pRxq->flushState = SFVMK_FLUSH_DONE;
  }

  pRxq->pending = pRxq->added;

  sfvmk_rxqComplete(pRxq, B_TRUE);

  pRxq->added = 0;
  pRxq->pushed = 0;
  pRxq->pending = 0;
  pRxq->completed = 0;
  pRxq->loopback = 0;

  /* Destroy the common code receive queue. */
  efx_rx_qdestroy(pRxq->pCommonRxq);


  SFVMK_EVQ_UNLOCK(pEvq);

  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "exited sfvmk_rxqStop[%u]", qIndex);
}

/**-----------------------------------------------------------------------------
*
* sfvmk_rxStart --
*
* @brief      initialize rxq module and set the RSS params.
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
*
* @result: void
*
*-----------------------------------------------------------------------------*/

int sfvmk_rxStart( sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  const efx_nic_cfg_t *pEfxNicCfg;
  size_t hdrlen, align, reserved;
  int qIndex;
  int rc;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");
  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "entered sfvmk_rxStart");

  pIntr = &pAdapter->intr;

  /* Initialize the common code receive module. */
  if ((rc = efx_rx_init(pAdapter->pNic)) != 0)
  return (rc);

  pEfxNicCfg = efx_nic_cfg_get(pAdapter->pNic);

  //praveen needs to check
  pAdapter->rxBufferSize = EFX_MAC_PDU(pAdapter->mtu);

  /* Calculate the receive packet buffer size. */
  pAdapter->rxPrefixSize = pEfxNicCfg->enc_rx_prefix_size;

  /* Ensure IP headers are 32bit aligned */
  hdrlen = pAdapter->rxPrefixSize + sizeof (vmk_EthHdr);
  pAdapter->rxBufferAlign = P2ROUNDUP(hdrlen, 4) - hdrlen;

  pAdapter->rxBufferSize += pAdapter->rxBufferAlign;

  /* Align end of packet buffer for RX DMA end padding */
  align = MAX(1, pEfxNicCfg->enc_rx_buf_align_end);
  EFSYS_ASSERT(ISP2(align));

  pAdapter->rxBufferSize = P2ROUNDUP(pAdapter->rxBufferSize, align);

  /* we need extra space to align to the cache line */

  reserved = pAdapter->rxBufferSize + CACHE_LINE_SIZE;
  /* Set up the scale table.  Enable all hash types and hash insertion. */

  for (qIndex = 0; qIndex < SFVMK_RX_SCALE_MAX; qIndex++)
    pAdapter->rxIndirTable[qIndex] = qIndex % pAdapter->rxqCount;

  if ((rc = efx_rx_scale_tbl_set(pAdapter->pNic, pAdapter->rxIndirTable,
                                  SFVMK_RX_SCALE_MAX)) != 0) {
    SFVMK_ERR(pAdapter, "failed to RSS Indirection table");
    goto sfvmk_fail;
  }
  (void)efx_rx_scale_mode_set(pAdapter->pNic, EFX_RX_HASHALG_TOEPLITZ,
                (1 << EFX_RX_HASH_IPV4) | (1 << EFX_RX_HASH_TCPIPV4) |
                (1 << EFX_RX_HASH_IPV6) | (1 << EFX_RX_HASH_TCPIPV6), B_TRUE);

  rc = efx_rx_scale_key_set(pAdapter->pNic, sfvmk_toepKey,
                            sizeof(sfvmk_toepKey));
  if (rc) {
    SFVMK_ERR(pAdapter, "failed to set RSS Key");
    goto sfvmk_fail;
  }

  /* Start the receive queue(s). */
  for (qIndex = 0; qIndex < pAdapter->rxqCount ; qIndex++) {
    if ((rc = sfvmk_rxqStart(pAdapter, qIndex)) != 0) {
      SFVMK_ERR(pAdapter, "failed to start RxQ[%u]", qIndex);
      goto sfvmk_rxq_start_fail;
    }
  }

  rc = efx_mac_filter_default_rxq_set(pAdapter->pNic, pAdapter->pRxq[0]->pCommonRxq,
                                      pAdapter->intr.numIntrAlloc > 1);

  if (rc != 0) {
    SFVMK_ERR(pAdapter, "failed to set default rxq filter");
    goto sfvmk_default_rxq_set_fail;
  }
  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "exited sfvmk_rxStart");
  return (0);

sfvmk_default_rxq_set_fail:
sfvmk_rxq_start_fail:
  while (--qIndex >= 0)
    sfvmk_rxqStop(pAdapter, qIndex);
sfvmk_fail:
  efx_rx_fini(pAdapter->pNic);

  return (rc);
}


/**-----------------------------------------------------------------------------
*
* sfvmk_rxStop --
*
* @brief      destroy rxq module
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
*
* @result: void
*
*-----------------------------------------------------------------------------*/
void sfvmk_rxStop(sfvmk_adapter_t *pAdapter)
{
  int qIndex;

  VMK_ASSERT_BUG(NULL != pAdapter, " NULL adapter ptr");
  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "entered sfvmk_rxStop");

  efx_mac_filter_default_rxq_clear(pAdapter->pNic);

  /* Stop the receive queue(s) */
  qIndex = pAdapter->rxqCount;
  while (--qIndex >= 0)
    sfvmk_rxqStop(pAdapter, qIndex);

  pAdapter->rxPrefixSize = 0;
  pAdapter->rxBufferSize = 0;

  efx_rx_fini(pAdapter->pNic);

  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_FUNCTION,
            "exited sfvmk_rxStop");
}
/**-----------------------------------------------------------------------------
*
* sfvmk_rxqFlushDone --
*
* @brief     change flush state to done
*
* @param[in]  pRxq     pointer to rxQ
*
* @result: void
*
*-----------------------------------------------------------------------------*/

void
sfvmk_rxqFlushDone(struct sfvmk_rxq_s *pRxq)
{
  pRxq->flushState = SFVMK_FLUSH_DONE;
}
/**-----------------------------------------------------------------------------
*
* sfvmk_rxqFlushFailed --
*
* @brief     change flush state to failed
*
* @param[in]  pRxq     pointer to rxQ
*
* @result: void
*
*-----------------------------------------------------------------------------*/
void
sfvmk_rxqFlushFailed(struct sfvmk_rxq_s *pRxq)
{
  pRxq->flushState = SFVMK_FLUSH_FAILED;
}
