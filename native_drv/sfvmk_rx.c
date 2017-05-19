

/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"
#include "sfvmk_rx.h"
#include "sfvmk_ev.h"
#include "sfvmk_utils.h"

static uint8_t toep_key[] = {
	0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
	0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
	0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
	0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
	0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa
};

#define	SFVMK_REFILL_BATCH  64


static int
sfvmk_RXQInit(sfvmk_adapter *adapter, unsigned int index)
{
	struct sfvmk_rxq *rxq;
	struct sfvmk_evq *evq;
	efsys_mem_t *esmp;


	VMK_ASSERT(index < adapter->rxq_count);
        rxq = sfvmk_MemPoolAlloc(sizeof(sfvmk_rxq));
	
        rxq->adapter = adapter;
	rxq->index = index;
	rxq->entries = adapter->rxq_entries;
	rxq->ptr_mask = rxq->entries - 1;
        //praveen   
	//rxq->refill_threshold = RX_REFILL_THRESHOLD(rxq->entries);

	adapter->rxq[index] = rxq;
	esmp = &rxq->mem;

	evq = adapter->evq[index];

	/* Allocate and zero DMA space. */
  esmp->esm_base = sfvmk_AllocCoherentDMAMapping(adapter, EFX_RXQ_SIZE(adapter->rxq_entries),&esmp->io_elem.ioAddr);
  esmp->io_elem.length = EFX_RXQ_SIZE(adapter->rxq_entries);
  esmp->esm_handle  = adapter->vmkDmaEngine;

	/* Allocate the context array and the flow table. */
	rxq->queue = sfvmk_MemPoolAlloc(sizeof(struct sfvmk_rx_sw_desc) * adapter->rxq_entries);
        if (NULL != rxq->queue)
        {
            rxq->qdescSize = sizeof(struct sfvmk_rx_sw_desc) * adapter->rxq_entries;
           vmk_LogMessage("allocate memory for qdesc %d\n", rxq->index);
        }
        else
           vmk_LogMessage("failed to allocate memory for qdesc\n");


	rxq->init_state = SFVMK_RXQ_INITIALIZED;
	
        return (0);
}

static void
sfvmk_RXQFini(sfvmk_adapter *adapter, unsigned int index)
{
	sfvmk_rxq *rxq;
	efsys_mem_t *esmp;

	rxq = adapter->rxq[index];

	esmp = &rxq->mem;
	VMK_ASSERT(rxq->init_state == SFXGE_RXQ_INITIALIZED);

	/* Free the context array and the flow table. */
        //praveen memfree
         //if (NULL != rxq->queue)
             sfvmk_MemPoolFree(rxq->queue, rxq->qdescSize);
         //else
         //   vmk_LogMessage("error in allocation memory %d\n", rxq->index);

	/* Release DMA memory. */
        sfvmk_FreeCoherentDMAMapping(adapter,esmp->esm_base, esmp->io_elem.ioAddr, esmp->io_elem.length);
	adapter->rxq[index] = NULL;

        sfvmk_MemPoolFree(rxq, sizeof(sfvmk_rxq));
}




int sfvmk_RxInit(sfvmk_adapter *adapter)
{
	struct sfvmk_intr *intr;
	int index;
	int rc;


	intr = &adapter->intr;

	adapter->rxq_count = intr->numIntrAlloc;
 
	VMK_ASSERT(intr->state == SFVMK_INTR_INITIALIZED);

	/* Initialize the receive queue(s) - one per interrupt. */
	for (index = 0; index < adapter->rxq_count; index++) {
		if ((rc = sfvmk_RXQInit(adapter, index)) != 0)
			goto fail;
	}

	return (0);

fail:
	/* Tear down the receive queue(s). */
	while (--index >= 0)
		sfvmk_RXQFini(adapter, index);

	adapter->rxq_count = 0;

	return (rc);
}
void
sfvmk_RxFini(sfvmk_adapter *adapter)
{
	int index;

	index = adapter->rxq_count;
	while (--index >= 0)
		sfvmk_RXQFini(adapter, index);

	adapter->rxq_count = 0;
}
void
sfvmk_rx_qfill(sfvmk_rxq *rxq, unsigned int  num_bufs , boolean_t retrying)
{
   sfvmk_adapter *adapter = rxq->adapter;
   struct sfvmk_rx_sw_desc *rx_desc;
   efsys_dma_addr_t addr[SFVMK_REFILL_BATCH];
   vmk_PktHandle *newpkt = NULL;
   const vmk_SgElem *frag;
   VMK_ReturnStatus status;
   vmk_DMAMapErrorInfo dmaMapErr;
   vmk_SgElem mapper_in, mapper_out;
   vmk_uint32 posted , max_bufs;
   unsigned int batch;
   unsigned int rxfill;
   unsigned int mblksize;
   unsigned int id;
	 unsigned int old_added;

  vmk_LogMessage("calling sfvmk_rx_qfill\n");

  batch =0;
 rxfill = rxq->added - rxq->completed;
 old_added = rxq->added;
  // VMK_ASSERT(rxfill <= EFX_RXQ_LIMIT(rxq->entries));
  max_bufs = MIN(EFX_RXQ_LIMIT(rxq->entries) - rxfill, num_bufs);
  //VMK_ASSERT(ntodo <= EFX_RXQ_LIMIT(rxq->entries));

   mblksize = adapter->rx_buffer_size - adapter->rx_buffer_align;

   for (posted = 0; posted < max_bufs ; posted++) {

      status = vmk_PktAllocForDMAEngine(mblksize, adapter->vmkDmaEngine, &newpkt);

      if (VMK_UNLIKELY(status != VMK_OK)) {
         //rxo->stats.rx_post_fail++;
         break;
      }

      frag = vmk_PktSgElemGet(newpkt, 0);
      if (frag == NULL) {
         vmk_PktRelease(newpkt);
         break;
      }

      mapper_in.addr = frag->addr;
      mapper_in.length = frag->length;
      status = vmk_DMAMapElem(adapter->vmkDmaEngine, VMK_DMA_DIRECTION_TO_MEMORY,
                              &mapper_in, VMK_TRUE, &mapper_out, &dmaMapErr);

      if (status != VMK_OK) {
         SFVMK_ERR(adapter,
                        "Failed to map %p size %d to IO address, %s.",
                        newpkt, mblksize,
                        vmk_DMAMapErrorReasonToString(dmaMapErr.reason));
        // rxo->stats.rx_post_fail++;
         vmk_PktRelease(newpkt);
         break;
      }

      id = (rxq->added + batch) & rxq->ptr_mask;
      rx_desc = &rxq->queue[id];
      rx_desc->flags = EFX_DISCARD;
      //rx_desc->pkt = newpkt;
      rx_desc->size = mblksize;

      addr[batch++] = mapper_out.ioAddr;


    if (batch == SFVMK_REFILL_BATCH) {
      efx_rx_qpost(rxq->common, addr, mblksize, batch,
          rxq->completed, rxq->added);
      rxq->added += batch;
      batch = 0;
    }
 }

 if (posted != max_bufs)
 {
     posted = posted%SFVMK_REFILL_BATCH;
		 for (posted--; posted>=0 ; posted--)
		 {
			 vmk_SgElem elem;
			 VMK_ReturnStatus status;



			 id = (rxq->added + posted) & rxq->ptr_mask;
			  rx_desc = &rxq->queue[id];
				elem.ioAddr = addr[posted];
			 elem.length = mblksize;

			 status = vmk_DMAUnmapElem(adapter->vmkDmaEngine, VMK_DMA_DIRECTION_TO_MEMORY, &elem);
			 //vmk_PktRelease(rx_desc->pkt);

		 	}
 	}
  //praveen needs to check
  /*
  bus_dmamap_sync(rxq->mem.esm_tag, rxq->mem.esm_map,
      BUS_DMASYNC_PREWRITE);
  */
  efx_rx_qpush(rxq->common, rxq->added, &rxq->pushed);

   return;
}


static int
sfvmk_RXQStart( sfvmk_adapter *adapter, unsigned int index)
{

  sfvmk_rxq *rxq;
  efsys_mem_t *esmp;
  sfvmk_evq *evq;
  int rc;

  vmk_LogMessage("calling sfvmk_rx_qstart\n");

  rxq = adapter->rxq[index];
  esmp = &rxq->mem;
  evq = adapter->evq[index];

  //VMK_ASSERT(rxq->init_state == SFVMK_RXQ_INITIALIZED,
  //VMK_ASSERT(evq->init_state == SFVMK_EVQ_STARTED,

  /* Create the common code receive queue. */
  if ((rc = efx_rx_qcreate(adapter->enp, index, 0, EFX_RXQ_TYPE_DEFAULT,
      esmp, adapter->rxq_entries, 0, evq->common,
      &rxq->common)) != 0)
    goto sfvmk_rxq_create_fail;

//  SFVMK_EVQ_LOCK(evq);

  /* Enable the receive queue. */
  efx_rx_qenable(rxq->common);

  rxq->init_state = SFVMK_RXQ_STARTED;
  rxq->flush_state = SFVMK_FLUSH_REQUIRED;

  /* Try to fill the queue from the pool. */
   sfvmk_rx_qfill(rxq, EFX_RXQ_LIMIT(adapter->rxq_entries), B_FALSE);

  //SFVMK_EVQ_UNLOCK(evq);

  return (0);

sfvmk_rxq_create_fail:
  return (rc);
}

static void
sfvmk_RXQStop( sfvmk_adapter *adapter, unsigned int index)
{
   sfvmk_rxq *rxq;
   sfvmk_evq *evq;
  unsigned int count;
  unsigned int retry = 3;

  //SFVMK_ADAPTER_LOCK_ASSERT_OWNED(adapter);

  rxq = adapter->rxq[index];
  evq = adapter->evq[index];

  //SFVMK_EVQ_LOCK(evq);

  //VMK_ASSERT(rxq->init_state == SFVMK_RXQ_STARTED);
  
  rxq->init_state = SFVMK_RXQ_INITIALIZED;

  //praveen needs to check 
  //callout_stop(&rxq->refill_callout);

  while (rxq->flush_state != SFVMK_FLUSH_DONE && retry != 0) {
    rxq->flush_state = SFVMK_FLUSH_PENDING;

    //SFVMK_EVQ_UNLOCK(evq);

    /* Flush the receive queue */
    if (efx_rx_qflush(rxq->common) != 0) {
      //SFVMK_EVQ_LOCK(evq);
      rxq->flush_state = SFVMK_FLUSH_FAILED;
      break;
    }

    count = 0;
    do {
      /* Spin for 100 ms */
      vmk_DelayUsecs(100*1000);


      if (rxq->flush_state != SFVMK_FLUSH_PENDING)
        break;

    } while (++count < 20);

    //SFVMK_EVQ_LOCK(evq);

    if (rxq->flush_state == SFVMK_FLUSH_PENDING) {
      /* Flush timeout - neither done nor failed */
/*      log(LOG_ERR, "%s: Cannot flush Rx queue %u\n",
          device_get_nameunit(adapter->dev), index);*/
      rxq->flush_state = SFVMK_FLUSH_DONE;
    }
    retry--;
  }
  if (rxq->flush_state == SFVMK_FLUSH_FAILED) {
/*    log(LOG_ERR, "%s: Flushing Rx queue %u failed\n",
        device_get_nameunit(adapter->dev), index);*/
    rxq->flush_state = SFVMK_FLUSH_DONE;
  }

  rxq->pending = rxq->added;

   //praveen will call later
//  sfvmk_rx_qcomplete(rxq, B_TRUE);

  //VMK_ASSERT(rxq->completed == rxq->pending,

  rxq->added = 0;
  rxq->pushed = 0;
  rxq->pending = 0;
  rxq->completed = 0;
  rxq->loopback = 0;

  /* Destroy the common code receive queue. */
  efx_rx_qdestroy(rxq->common);


  //SFVMK_EVQ_UNLOCK(evq);
}


int sfvmk_RXStart( sfvmk_adapter *adapter)
{
  sfvmk_intr *intr;
  const efx_nic_cfg_t *encp;
  size_t hdrlen, align, reserved;
  int index;
  int rc;

  intr = &adapter->intr;
  vmk_LogMessage("calling sfvmk_rx_start\n");
  /* Initialize the common code receive module. */
  if ((rc = efx_rx_init(adapter->enp)) != 0)
    return (rc);

  encp = efx_nic_cfg_get(adapter->enp);

  //praveen needs to check
  adapter->rx_buffer_size = EFX_MAC_PDU(adapter->mtu);

  /* Calculate the receive packet buffer size. */
  adapter->rx_prefix_size = encp->enc_rx_prefix_size;

  /* Ensure IP headers are 32bit aligned */
  hdrlen = adapter->rx_prefix_size + sizeof (vmk_EthHdr);
  adapter->rx_buffer_align = P2ROUNDUP(hdrlen, 4) - hdrlen;

  adapter->rx_buffer_size += adapter->rx_buffer_align;

  /* Align end of packet buffer for RX DMA end padding */
  align = MAX(1, encp->enc_rx_buf_align_end);
  EFSYS_ASSERT(ISP2(align));
  adapter->rx_buffer_size = P2ROUNDUP(adapter->rx_buffer_size, align);

  /*
   * Standard mbuf zones only guarantee pointer-size alignment;
   * we need extra space to align to the cache line
   */
  reserved = adapter->rx_buffer_size + CACHE_LINE_SIZE;
  /*
   * Set up the scale table.  Enable all hash types and hash insertion.
   */
  for (index = 0; index < SFVMK_RX_SCALE_MAX; index++)
    adapter->rx_indir_table[index] = index % adapter->rxq_count;
  if ((rc = efx_rx_scale_tbl_set(adapter->enp, adapter->rx_indir_table,
               SFVMK_RX_SCALE_MAX)) != 0)
    goto sfvmk_fail;
  (void)efx_rx_scale_mode_set(adapter->enp, EFX_RX_HASHALG_TOEPLITZ,
      (1 << EFX_RX_HASH_IPV4) | (1 << EFX_RX_HASH_TCPIPV4) |
      (1 << EFX_RX_HASH_IPV6) | (1 << EFX_RX_HASH_TCPIPV6), B_TRUE);

  if ((rc = efx_rx_scale_key_set(adapter->enp, toep_key,
               sizeof(toep_key))) != 0)
    goto sfvmk_fail;
  /* Start the receive queue(s). */
  for (index = 0; index < 1; index++) {
    if ((rc = sfvmk_RXQStart(adapter, index)) != 0)
      goto sfvmk_rxq_start_fail;
  }

  rc = efx_mac_filter_default_rxq_set(adapter->enp, adapter->rxq[0]->common,
              adapter->intr.numIntrAlloc > 1);

  if (rc != 0)
    goto sfvmk_default_rxq_set_fail;

 return (0);

sfvmk_default_rxq_set_fail:
sfvmk_rxq_start_fail:
  while (--index >= 0)
    sfvmk_RXQStop(adapter, index);
sfvmk_fail:
  efx_rx_fini(adapter->enp);

  return (rc);
}



void
sfvmk_RXStop(sfvmk_adapter *adapter)
{
        int index;

        efx_mac_filter_default_rxq_clear(adapter->enp);

        /* Stop the receive queue(s) */
        index = 1;//adapter->rxq_count;
        while (--index >= 0)
                sfvmk_RXQStop(adapter, index);

        adapter->rx_prefix_size = 0;
        adapter->rx_buffer_size = 0;

        efx_rx_fini(adapter->enp);
}

