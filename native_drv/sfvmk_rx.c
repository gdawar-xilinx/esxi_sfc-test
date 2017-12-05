/*
 * Copyright (c) 2017, Solarflare Communications Inc.
 * All rights reserved.
 *  
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "sfvmk_driver.h"

/* value for batch processing */
#define SFVMK_REFILL_BATCH  64

/* polling time for flush to be completed */
#define SFVMK_RXQ_STOP_POLL_WAIT 100*SFVMK_ONE_MILISEC
#define SFVMK_RXQ_STOP_POLL_TIMEOUT 20

/* refill low watermark */
#define RX_REFILL_THRESHOLD(_entries)   (EFX_RXQ_LIMIT(_entries) * 9 / 10)
#define SFVMK_MIN_PKT_SIZE 60

/* hash key */
static uint8_t sfvmk_toepKey[] = {
  0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
  0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
  0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
  0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
  0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa
};

void sfvmk_rxDeliver(sfvmk_adapter_t *pAdapter,
                     struct sfvmk_rxSwDesc_s *pRxDesc,
                     unsigned int qIndex);
void sfvmk_rxqComplete(sfvmk_rxq_t *pRxq, boolean_t eop);
void sfvmk_rxqFill(sfvmk_rxq_t *pRxq, unsigned int  numBufs);
static VMK_ReturnStatus sfvmk_rxqInit(sfvmk_adapter_t *pAdapter,
                                       unsigned int qIndex);
static void sfvmk_rxqFini(sfvmk_adapter_t *pAdapter, unsigned int qIndex);
static int sfvmk_rxqStart( sfvmk_adapter_t *pAdapter, unsigned int qIndex);
static void sfvmk_rxqStop( sfvmk_adapter_t *pAdapter, unsigned int qIndex);

/*! \brief      software implementation to strip vlan hdr from pkt
**
** \param[in]   pAdapter     pointer to sfvmk_adapter_t
** \param[in]   pkt          pointer to packet handle
**
** \return: void
*/

static void
sfvmk_stripVlanHdr(sfvmk_adapter_t *pAdapter, vmk_PktHandle *pPkt)
{
  uint8_t *pPtr=NULL;
  uint16_t thisTag=0;
  uint16_t *pTag=NULL;
  vmk_VlanID vlanId;
  vmk_VlanPriority vlanPrio;
  vmk_uint8 *pFrameVa;
  VMK_ReturnStatus ret;

  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_DBG,
            "Mapped len: %d, frame len: %d",
            vmk_PktFrameMappedLenGet(pPkt), vmk_PktFrameLenGet(pPkt));

  /*TODO: check with VMware for expected handling of VLAN stacking */

  pFrameVa = (vmk_uint8 *)vmk_PktFrameMappedPointerGet(pPkt);
  pPtr = pFrameVa + SFVMK_VLAN_HDR_START_OFFSET;
  pTag = (uint16_t *)pPtr;
#ifdef DEBUG_VLAN
  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_DBG,
            "pFrameVa: %p, tci: %x", pFrameVa, sfvmk_swapBytes(*pTag));

  if(sfvmk_swapBytes(*pTag) == VMK_ETH_TYPE_VLAN)
    SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_DBG, "check passed");
#endif
  pPtr += SFVMK_ETH_TYPE_SIZE;

  /*fetch the TCI now */
  thisTag = sfvmk_swapBytes(*(uint16_t *)pPtr);
  vlanId = thisTag & SFVMK_VLAN_VID_MASK;
  vlanPrio = (thisTag & SFVMK_VLAN_PRIO_MASK) >> SFVMK_VLAN_PRIO_SHIFT;

  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_DBG,
            "vlanid: %d , vlanPrio: %d", vlanId, vlanPrio);
  ret = vmk_PktVlanIDSet(pPkt, vlanId);
  VMK_ASSERT_BUG((ret == VMK_OK),"(ret == VMK_OK) is False");
  ret = vmk_PktPrioritySet(pPkt, vlanPrio);
  VMK_ASSERT_BUG((ret == VMK_OK),"(ret == VMK_OK) is False");

  /* strip off the vlan header */
  vmk_Memmove(pFrameVa + SFVMK_VLAN_HDR_SIZE, pFrameVa, SFVMK_VLAN_HDR_START_OFFSET);
  ret = vmk_PktPushHeadroom(pPkt, SFVMK_VLAN_HDR_SIZE);
  if(ret != VMK_OK) {
    SFVMK_ERR(pAdapter, "vmk_PktPushHeadroom failed with err %s",
              vmk_StatusToString(ret));
  }

#ifdef DEBUG_VLAN
  pFrameVa = (vmk_uint8 *)vmk_PktFrameMappedPointerGet(pPkt);
  SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_DBG, "pFrameVa: %p", pFrameVa);
#endif
}

/*! \brief      function to deliver rx pkt to uplink layer
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
** \param[in]  rxDesc      rx buffer desc
** \param[in]  qIndex      rxq Index
**
** \return: void
*/
void sfvmk_rxDeliver(sfvmk_adapter_t *pAdapter, struct
                            sfvmk_rxSwDesc_s *pRxDesc, unsigned int qIndex)
{
  vmk_PktHandle *pPkt = NULL;
  vmk_VA pFrameVa  ;
  int flags ;
  VMK_ReturnStatus status;
  vmk_SgElem elem;

  if (qIndex >=  pAdapter->intr.numIntrAlloc)
    return ;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_NULL_PTR_CHECK(pRxDesc);
  pPkt = pRxDesc->pPkt;
  SFVMK_NULL_PTR_CHECK(pPkt);
  flags = pRxDesc->flags;

  elem.ioAddr = pRxDesc->ioAddr;
  elem.length = pRxDesc->size;
  status = vmk_DMAUnmapElem(pAdapter->dmaEngine, VMK_DMA_DIRECTION_TO_MEMORY, &elem);
  VMK_ASSERT_BUG((status == VMK_OK),"(status == VMK_OK) is False");

  /* Initialize the pkt len for vmk_PktPushHeadroom to work */
  vmk_PktFrameLenSet(pPkt, pRxDesc->size);

  /* if prefix header is present, remove it */
  status = vmk_PktPushHeadroom(pPkt, pAdapter->rxPrefixSize);
  if(status != VMK_OK) {
    SFVMK_ERR(pAdapter, "vmk_PktPushHeadroom failed with err %s",
              vmk_StatusToString(status));
  }
  else {
    /* pRxDesc->size update can be avoided but doing it for debug purpose */
    pRxDesc->size -= pAdapter->rxPrefixSize;
  }

  if(flags & (EFX_PKT_VLAN_TAGGED  | EFX_CHECK_VLAN)) {
    sfvmk_stripVlanHdr(pAdapter, pPkt);
    pRxDesc->size -= SFVMK_VLAN_HDR_SIZE;
  }

  /* pad the pPkt if it is shorter than 60 bytes */
  if (pRxDesc->size < SFVMK_MIN_PKT_SIZE) {
    pFrameVa = vmk_PktFrameMappedPointerGet(pPkt);
    SFVMK_NULL_PTR_CHECK(((vmk_uint8 *)pFrameVa));
    VMK_ASSERT_BUG(vmk_PktIsBufDescWritable(pPkt) == VMK_TRUE);
    vmk_Memset((vmk_uint8 *)pFrameVa + pRxDesc->size, 0,
               SFVMK_MIN_PKT_SIZE - pRxDesc->size);
    pRxDesc->size = SFVMK_MIN_PKT_SIZE;
    vmk_PktFrameLenSet(pPkt, pRxDesc->size);
  }

  /* Convert checksum flags */
  if (flags & EFX_CKSUM_TCPUDP)
    vmk_PktSetCsumVfd(pPkt);

  if(VMK_UNLIKELY(pAdapter->pEvq[qIndex]->pktList != NULL)) {
    VMK_ASSERT_BUG(vmk_SystemCheckState(VMK_SYSTEM_STATE_PANIC),
                   "pktList valid in normal system state");
    vmk_PktListAppendPkt(pAdapter->pEvq[qIndex]->pktList, pPkt);
  }
  else {
    /* deliver the pkt to uplink layer */
    vmk_NetPollRxPktQueue(pAdapter->pEvq[qIndex]->netPoll, pPkt);
  }
  pRxDesc->flags = EFX_DISCARD;
  pRxDesc->pPkt = NULL;

  return;
}

/*! \brief      read the pkt from the queue and pass it to uplink layer
**              or discard it
**
** \param[in]  pRxq     pointer to rxQ
** \param[in]  eop
**
** \return: void
*/
void sfvmk_rxqComplete(sfvmk_rxq_t *pRxq, boolean_t eop)
{
  sfvmk_adapter_t *pAdapter = pRxq->pAdapter;
  unsigned int index;
  struct sfvmk_evq_s *pEvq;
  unsigned int completed;
  unsigned int level;
  vmk_PktHandle *pPkt ;
  struct sfvmk_rxSwDesc_s *prev = NULL;
  vmk_SgElem elem;
  VMK_ReturnStatus status;

  SFVMK_NULL_PTR_CHECK(pRxq);

  index = pRxq->index;
  pEvq = pAdapter->pEvq[index];

  SFVMK_NULL_PTR_CHECK(pEvq);

  completed = pRxq->completed;
  while (completed != pRxq->pending) {
    unsigned int id;
    struct sfvmk_rxSwDesc_s *pRxDesc;

    id = completed++ & pRxq->ptrMask;
    pRxDesc = &pRxq->pQueue[id];
    pPkt = pRxDesc->pPkt;

    if (VMK_UNLIKELY(pRxq->initState != SFVMK_RXQ_STARTED))
      goto sfvmk_discard;

    if (pRxDesc->flags & (EFX_ADDR_MISMATCH | EFX_DISCARD))
      goto sfvmk_discard;

    /* Read the length from the pseudo header for fragmented pkt */
    if (pRxDesc->flags & EFX_PKT_PREFIX_LEN) {
      int rc;
      vmk_uint16 len=0;

      rc = efx_pseudo_hdr_pkt_length_get(pRxq->pCommonRxq,
              (vmk_uint8 *)vmk_PktFrameMappedPointerGet(pPkt),
                 &len);
      VMK_ASSERT_BUG(rc == 0, "cannot get packet length: %d", rc);
      pRxDesc->size = len + pAdapter->rxPrefixSize;
      SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_DBG,
                "rx_desc_size: %d", pRxDesc->size);
    }

    switch (pRxDesc->flags & (EFX_PKT_IPV4 | EFX_PKT_IPV6)) {
      case EFX_PKT_IPV4:
        if(!pAdapter->isRxCsumEnabled)
          pRxDesc->flags &=  ~(EFX_CKSUM_IPV4 | EFX_CKSUM_TCPUDP);
        break;
      case EFX_PKT_IPV6:
        if(!pAdapter->isRxCsumEnabled)
          pRxDesc->flags &= ~EFX_CKSUM_TCPUDP;
        break;
      case 0:
        break;
      default:
        SFVMK_ERR(pAdapter, "Rx Desc with both ipv4 and ipv6 flags");
        goto sfvmk_discard;
    }

    /* Pass packet up the stack  */
    if (prev != NULL) {
      sfvmk_rxDeliver(pAdapter, prev, pRxq->index);
    }

    prev = pRxDesc;
    continue;

sfvmk_discard:
    /* Return the packet to the pool */
    elem.ioAddr = pRxDesc->ioAddr;
    elem.length = pRxDesc->size;
    status = vmk_DMAUnmapElem(pAdapter->dmaEngine, VMK_DMA_DIRECTION_TO_MEMORY, &elem);
    VMK_ASSERT_BUG((status == VMK_OK),"(status == VMK_OK) is False");

    vmk_PktRelease(pPkt);
    pRxDesc->pPkt = NULL;

    SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_DBG, "completed = %d"
              "pending = %d", completed, pRxq->pending);
  }

  pRxq->completed = completed;
  level = pRxq->added - pRxq->completed;

  /* Pass last packet up the stack or into LRO */
  if (prev != NULL) {
    sfvmk_rxDeliver(pAdapter, prev, pRxq->index);
  }

  if (level < pRxq->refillThreshold)
    sfvmk_rxqFill(pRxq, EFX_RXQ_LIMIT(pRxq->entries));
}

/*! \brief      fill RX buffer desc with pkt information.
**
** \param[in]  pRxq      pointer to rxQ
** \param[in]  numBuf    number of buf desc to be filled in
**
** \return: void
*/
void sfvmk_rxqFill(sfvmk_rxq_t *pRxq, unsigned int  numBufs)
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

  SFVMK_NULL_PTR_CHECK(pRxq);
  pAdapter = pRxq->pAdapter;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  if (pRxq->initState != SFVMK_RXQ_STARTED)
    return;

  batch = 0;
  rxfill = pRxq->added - pRxq->completed;

  maxBuf = MIN(EFX_RXQ_LIMIT(pRxq->entries) - rxfill, numBufs);
  mblkSize = pAdapter->rxBufferSize - pAdapter->rxBufferAlign;

  for (posted = 0; posted < maxBuf ; posted++) {

    status = vmk_PktAllocForDMAEngine(mblkSize, pAdapter->dmaEngine, &pNewpkt);
    if (VMK_UNLIKELY(status != VMK_OK)) {
      SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_INFO,
                "failed to allocate pkt. err: %s", vmk_StatusToString(status));
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
      SFVMK_DBG(pAdapter, SFVMK_DBG_RX, SFVMK_LOG_LEVEL_INFO,
                "Failed to map %p size %d to IO address, %s.",
                pNewpkt, mblkSize,vmk_DMAMapErrorReasonToString(dmaMapErr.reason));
      vmk_PktRelease(pNewpkt);
      break;
    }

    id = (pRxq->added + batch) & pRxq->ptrMask;

    rxDesc = &pRxq->pQueue[id];
    rxDesc->flags = EFX_DISCARD;
    rxDesc->ioAddr = mapperOut.ioAddr;
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

  if (batch !=0) {
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

/*! \brief     initialize all the resource required for a rxQ module
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex      rxq Index
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus sfvmk_rxqInit(sfvmk_adapter_t *pAdapter,
                                              unsigned int qIndex)
{
  sfvmk_rxq_t *pRxq;
  efsys_mem_t *pRxqMem;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  VMK_ASSERT_BUG(qIndex < pAdapter->rxqCount, " invalid qindex");

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_RX, "qIndex[%d]" , qIndex);

  pRxq = sfvmk_memPoolAlloc(sizeof(sfvmk_rxq_t));
  if(NULL == pRxq) {
    SFVMK_ERR(pAdapter,"failed to allocate memory for rxq object");
    goto sfvmk_rx_alloc_fail;
  }

  pRxq->pAdapter = pAdapter;
  pRxq->index = qIndex;
  pRxq->entries = pAdapter->rxqEntries;
  pRxq->ptrMask = pRxq->entries - 1;
  pRxq->refillThreshold = RX_REFILL_THRESHOLD(pRxq->entries);


  pAdapter->pRxq[qIndex] = pRxq;

  pRxqMem = &pRxq->mem;

  /* Allocate and zero DMA space. */
  pRxqMem->ioElem.length = EFX_RXQ_SIZE(pAdapter->rxqEntries);
  pRxqMem->pEsmBase = sfvmk_allocCoherentDMAMapping(pAdapter->dmaEngine,
                                                    pRxqMem->ioElem.length,
                                                    &pRxqMem->ioElem.ioAddr);
  if (pRxqMem->pEsmBase == NULL) {
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

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_RX, "qIndex[%d]" , qIndex);

  return VMK_OK;
sfvmk_desc_alloc_fail:
  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine,
                                pRxqMem->pEsmBase,
                                pRxqMem->ioElem.ioAddr,
                                pRxqMem->ioElem.length);
sfvmk_dma_alloc_fail:
  sfvmk_memPoolFree(pRxq, sizeof(sfvmk_rxq_t));
sfvmk_rx_alloc_fail:
  return VMK_FAILURE;
}

/*! \brief     destroy all the resources required for a rxQ module
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex      rxq Index
**
** \return: void
*/
static void sfvmk_rxqFini(sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_rxq_t *pRxq;
  efsys_mem_t *pRxqMem;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_RX, "qIndex[%d]" , qIndex);

  pRxq = pAdapter->pRxq[qIndex];
  SFVMK_NULL_PTR_CHECK(pRxq);

  pRxqMem = &pRxq->mem;
  VMK_ASSERT_BUG(pRxq->initState == SFVMK_RXQ_INITIALIZED,
                    "rxq->initState != SFVMK_RXQ_INITIALIZED");

  sfvmk_memPoolFree(pRxq->pQueue, pRxq->qdescSize);
  /* Release DMA memory. */
  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pRxqMem->pEsmBase,
                              pRxqMem->ioElem.ioAddr, pRxqMem->ioElem.length);
  pAdapter->pRxq[qIndex] = NULL;

  sfvmk_memPoolFree(pRxq, sizeof(sfvmk_rxq_t));

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_RX, "qIndex[%d]" , qIndex);

}

/*! \brief      initialize all the resource required for all rxQ module
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
**
** @return: VMK_OK <success> error code <failure>
**
*/
VMK_ReturnStatus sfvmk_rxInit(sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  int qIndex;
  VMK_ReturnStatus status;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_RX);

  pIntr = &pAdapter->intr;

  pAdapter->rxqCount = pIntr->numIntrAlloc;
  pAdapter->isRxCsumEnabled = VMK_TRUE;

  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_INITIALIZED,
                  "intr->state != SFXGE_INTR_INITIALIZED");

  /* Initialize the receive queue(s) - one per interrupt. */
  for (qIndex = 0; qIndex < pAdapter->rxqCount; qIndex++) {
    if ((status = sfvmk_rxqInit(pAdapter, qIndex)) != VMK_OK) {
      SFVMK_ERR(pAdapter,"failed to init rxq[%d]", qIndex);
      goto sfvmk_fail;
    }
  }
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_RX);

  return status;

sfvmk_fail:
/* Tear down the receive queue(s). */
  while (--qIndex >= 0)
    sfvmk_rxqFini(pAdapter, qIndex);

  pAdapter->rxqCount = 0;

  return status;
}

/*! \brief      destroy all the resources required for all rxQ module
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
**
** \return: void
*/
void sfvmk_rxFini(sfvmk_adapter_t *pAdapter)
{
  int qIndex;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_RX);

  qIndex = pAdapter->rxqCount;
  while (--qIndex >= 0)
    sfvmk_rxqFini(pAdapter, qIndex);

  pAdapter->rxqCount = 0;

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_RX);
}

/*! \brief     create rxQ module and fill the rxdesc for a given queue.
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex      rxq Index
**
** \return: void
*/
static int sfvmk_rxqStart( sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{

  sfvmk_rxq_t *pRxq;
  efsys_mem_t *pRxqMem;
  sfvmk_evq_t *pEvq;
  int rc;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_RX, "qIndex[%d]" , qIndex);

  pRxq = pAdapter->pRxq[qIndex];
  SFVMK_NULL_PTR_CHECK(pRxq);

  pRxqMem = &pRxq->mem;
  pEvq = pAdapter->pEvq[qIndex];
  SFVMK_NULL_PTR_CHECK(pEvq);

  /* Create the common code receive queue. */
  if ((rc = efx_rx_qcreate(pAdapter->pNic, qIndex, 0, EFX_RXQ_TYPE_DEFAULT,
                            pRxqMem, pAdapter->rxqEntries, 0, EFX_RXQ_FLAG_NONE,
                            pEvq->pCommonEvq, &pRxq->pCommonRxq)) != 0) {
    SFVMK_ERR(pAdapter, "Failed to create rxQ %u", qIndex);
    goto sfvmk_rxq_create_fail;
  }

  SFVMK_EVQ_LOCK(pEvq);

  /* Enable the receive queue. */
  efx_rx_qenable(pRxq->pCommonRxq);

  pRxq->initState = SFVMK_RXQ_STARTED;
  pRxq->flushState = SFVMK_FLUSH_REQUIRED;

  /* Try to fill the queue from the pool. */
  sfvmk_rxqFill(pRxq, EFX_RXQ_LIMIT(pAdapter->rxqEntries));

  SFVMK_EVQ_UNLOCK(pEvq);
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_RX, "qIndex[%d]" , qIndex);

  return VMK_OK;

sfvmk_rxq_create_fail:
  return VMK_FAILURE;
}

/*! \brief     flush and destroy rxq Module for a given queue.
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex      rxq Index
**
** \return: void
*/
static void sfvmk_rxqStop( sfvmk_adapter_t *pAdapter, unsigned int qIndex)
{
  sfvmk_rxq_t *pRxq;
  sfvmk_evq_t *pEvq;
  vmk_uint32 count;
  vmk_uint32 retry = 3;
  vmk_uint32 spinTime = SFVMK_ONE_MILISEC;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_RX, "qIndex[%d]" , qIndex);

  pRxq = pAdapter->pRxq[qIndex];
  SFVMK_NULL_PTR_CHECK(pRxq);
  pEvq = pAdapter->pEvq[qIndex];
  SFVMK_NULL_PTR_CHECK(pEvq);

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
      vmk_WorldSleep(spinTime);
      spinTime *= 2;

      /* MAX Spin will not be more than SFVMK_RXQ_STOP_POLL_WAIT */
      spinTime = MIN(spinTime, SFVMK_RXQ_STOP_POLL_WAIT);

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

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_RX, "qIndex[%d]" , qIndex);
}

/*! \brief      initialize rxq module and set the RSS params.
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
**
** \return: 0 <success> error code <failure>
*/

int sfvmk_rxStart( sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  const efx_nic_cfg_t *pEfxNicCfg;
  size_t hdrlen, align, reserved;
  int qIndex;
  int rc;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_RX);

  pIntr = &pAdapter->intr;

  /* Initialize the common code receive module. */
  if ((rc = efx_rx_init(pAdapter->pNic)) != 0)
  return (rc);

  pEfxNicCfg = efx_nic_cfg_get(pAdapter->pNic);

  pAdapter->rxBufferSize = EFX_MAC_PDU(pAdapter->sharedData.mtu);

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

  /* Setup rss if max rss channel is conifgured to greater than 1 */
  if (pAdapter->maxRssChannels > 1) {
    /* Set up the scale table.  Enable all hash types and hash insertion. */
    for (qIndex = 0; qIndex < SFVMK_RX_SCALE_MAX; qIndex++)
      pAdapter->rxIndirTable[qIndex] = qIndex % pAdapter->rxqCount;

    if ((rc = efx_rx_scale_tbl_set(pAdapter->pNic, EFX_RSS_CONTEXT_DEFAULT,
                                   pAdapter->rxIndirTable, SFVMK_RX_SCALE_MAX)) != 0) {
      SFVMK_ERR(pAdapter, "Failed to set RSS indirection table, status: %s", vmk_StatusToString(rc));
      goto sfvmk_rxq_rss_fail;
    }

    rc = efx_rx_scale_mode_set(pAdapter->pNic,
                          EFX_RSS_CONTEXT_DEFAULT, EFX_RX_HASHALG_TOEPLITZ,
                          (1 << EFX_RX_HASH_IPV4) | (1 << EFX_RX_HASH_TCPIPV4) |
                          (1 << EFX_RX_HASH_IPV6) | (1 << EFX_RX_HASH_TCPIPV6), B_TRUE);
    if (rc) {
      SFVMK_ERR(pAdapter, "failed to set RSS scale mode, status: %s", vmk_StatusToString(rc));
      goto sfvmk_rxq_rss_fail;
    }

    rc = efx_rx_scale_key_set(pAdapter->pNic, EFX_RSS_CONTEXT_DEFAULT,
			    sfvmk_toepKey, sizeof(sfvmk_toepKey));
    if (rc) {
      SFVMK_ERR(pAdapter, "failed to set RSS Key, status: %s", vmk_StatusToString(rc));
      goto sfvmk_rxq_rss_fail;
    }
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
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_RX);

  return rc;

sfvmk_default_rxq_set_fail:
sfvmk_rxq_start_fail:
  while (--qIndex >= 0)
    sfvmk_rxqStop(pAdapter, qIndex);
sfvmk_rxq_rss_fail:
  efx_rx_fini(pAdapter->pNic);
  return (rc);
}

/*! \brief      destroy rxq module
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
**
** \return: void
*/
void sfvmk_rxStop(sfvmk_adapter_t *pAdapter)
{
  int qIndex;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_RX);

  efx_mac_filter_default_rxq_clear(pAdapter->pNic);

  /* Stop the receive queue(s) */
  qIndex = pAdapter->rxqCount;
  while (--qIndex >= 0)
    sfvmk_rxqStop(pAdapter, qIndex);

  pAdapter->rxPrefixSize = 0;
  pAdapter->rxBufferSize = 0;

  efx_rx_fini(pAdapter->pNic);

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_RX);
}

/*! \brief     change flush state to done
**
** \param[in]  pRxq     pointer to rxQ
**
** \return: void
*/

void
sfvmk_rxqFlushDone(struct sfvmk_rxq_s *pRxq)
{
  pRxq->flushState = SFVMK_FLUSH_DONE;
}

/*! \brief     change flush state to failed
**
** \param[in]  pRxq     pointer to rxQ
**
** \return: void
*/
void
sfvmk_rxqFlushFailed(struct sfvmk_rxq_s *pRxq)
{
  pRxq->flushState = SFVMK_FLUSH_FAILED;
}
