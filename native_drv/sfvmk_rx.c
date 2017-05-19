#include "sfvmk.h"
#include "sfvmk_util.h"
#include "sfvmk_rx.h"
#include "sfvmk_ev.h"
#include "sfvmk_driver.h"
#if 0 
static uint8_t toep_key[] = {
	0x6d, 0x5a, 0x56, 0xda, 0x25, 0x5b, 0x0e, 0xc2,
	0x41, 0x67, 0x25, 0x3d, 0x43, 0xa3, 0x8f, 0xb0,
	0xd0, 0xca, 0x2b, 0xcb, 0xae, 0x7b, 0x30, 0xb4,
	0x77, 0xcb, 0x2d, 0xa3, 0x80, 0x30, 0xf2, 0x0c,
	0x6a, 0x42, 0xb7, 0x3b, 0xbe, 0xac, 0x01, 0xfa
};
#endif 
#define	SFVMK_REFILL_BATCH  64



static void
sfvmk_rx_qfini(sfvmk_adapter *adapter, unsigned int index)
{
	sfvmk_rxq *rxq;
	efsys_mem_t *esmp;

	rxq = adapter->rxq[index];

	esmp = &rxq->mem;
	VMK_ASSERT(rxq->init_state == SFXGE_RXQ_INITIALIZED);

	/* Free the context array and the flow table. */
        //praveen memfree
        sfvmk_memPoolFree(&rxq->queue, rxq->qdescSize);
        //praveen lro will do it later 
	//sfxge_lro_fini(rxq);

	/* Release DMA memory. */
        sfvmk_FreeCoherentDMAMapping(adapter,esmp->esm_base, esmp->io_elem.ioAddr, esmp->io_elem.length);
	adapter->rxq[index] = NULL;

        sfvmk_memPoolFree(&rxq, sizeof(sfvmk_rxq));
}

static int
sfvmk_rx_qinit(sfvmk_adapter *adapter, unsigned int index)
{
	struct sfvmk_rxq *rxq;
	struct sfvmk_evq *evq;
	efsys_mem_t *esmp;


	VMK_ASSERT(index < adapter->rxq_count);
        rxq = sfvmk_memPoolAlloc(sizeof(sfvmk_rxq));
	
        rxq->adapter = adapter;
	rxq->index = index;
	rxq->entries = adapter->rxq_enteries;
	rxq->ptr_mask = rxq->entries - 1;
        //praveen   
	//rxq->refill_threshold = RX_REFILL_THRESHOLD(rxq->entries);

	adapter->rxq[index] = rxq;
	esmp = &rxq->mem;

	evq = adapter->evq[index];

	/* Allocate and zero DMA space. */
        esmp->esm_base = sfvmk_AllocCoherentDMAMapping(adapter, EFX_RXQ_SIZE(adapter->rxq_enteries),&esmp->io_elem.ioAddr);
        esmp->io_elem.length = EFX_RXQ_SIZE(adapter->rxq_enteries);
        esmp->esm_handle  = adapter->vmkDmaEngine;

	/* Allocate the context array and the flow table. */
	rxq->queue = sfvmk_memPoolAlloc(sizeof(struct sfvmk_rx_sw_desc) * adapter->rxq_enteries);
        rxq->qdescSize = sizeof(struct sfvmk_rx_sw_desc) * adapter->rxq_enteries;

	//praveen will do LRO later on
	//sfxge_lro_init(rxq);

	//praveen will see later on
	//callout_init(&rxq->refill_callout, 1);

	rxq->init_state = SFVMK_RXQ_INITIALIZED;
	
        return (0);
}

void
sfvmk_rx_fini(sfvmk_adapter *adapter)
{
	int index;

	index = adapter->rxq_count;
	while (--index >= 0)
		sfvmk_rx_qfini(adapter, index);

	adapter->rxq_count = 0;
}



int
sfvmk_rx_init(sfvmk_adapter *adapter)
{
	struct sfvmk_intr *intr;
	int index;
	int rc;


#if 0
#ifdef SFXGE_LRO
	if (!ISP2(lro_table_size)) {
		log(LOG_ERR, "%s=%u must be power of 2",
		    SFXGE_LRO_PARAM(table_size), lro_table_size);
		rc = EINVAL;
		goto fail_lro_table_size;
	}

	if (lro_idle_ticks == 0)
		lro_idle_ticks = hz / 10 + 1; /* 100 ms */
#endif

#endif
  	intr = &adapter->sfvmkIntrInfo;

	adapter->rxq_count = intr->numIntrAlloc;
 
	VMK_ASSERT(intr->state == SFVMK_INTR_INITIALIZED);

	/* Initialize the receive queue(s) - one per interrupt. */
	for (index = 0; index < adapter->rxq_count; index++) {
		if ((rc = sfvmk_rx_qinit(adapter, index)) != 0)
			goto fail;
	}

   // praveen will see later on
	//sfvmk_rx_stat_init(adapter);

	return (0);

fail:
	/* Tear down the receive queue(s). */
	while (--index >= 0)
		sfvmk_rx_qfini(adapter, index);

	adapter->rxq_count = 0;

#ifdef SFXGE_LRO
fail_lro_table_size:
#endif
	return (rc);
}

static void
sfvmk_rx_qstop( sfvmk_adapter *adapter, unsigned int index)
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
  
  vmk_LogMessage("calling sfvmk_rx_qfill\n");

  batch =0; 
 rxfill = rxq->added - rxq->completed;
  // VMK_ASSERT(rxfill <= EFX_RXQ_LIMIT(rxq->entries));
  max_bufs = min(EFX_RXQ_LIMIT(rxq->entries) - rxfill, num_bufs);
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
      rx_desc->pkt = newpkt;
      rx_desc->size = mblksize;
      
      addr[batch++] = mapper_out.ioAddr;
    
    
    if (batch == SFVMK_REFILL_BATCH) {
      efx_rx_qpost(rxq->common, addr, mblksize, batch,
          rxq->completed, rxq->added);
      rxq->added += batch;
      batch = 0;
    }
 }


  //praveen needs to check
  /*
  bus_dmamap_sync(rxq->mem.esm_tag, rxq->mem.esm_map,
      BUS_DMASYNC_PREWRITE);
  */
  efx_rx_qpush(rxq->common, rxq->added, &rxq->pushed);

  /* The queue could still be empty if no descriptors were actually
   * pushed, in which case there will be no event to cause the next
   * refill, so we must schedule a refill ourselves.
   */
  // praveen needs to check
  #if 0
  if(rxq->pushed == rxq->completed) {
    sfvmk_rx_schedule_refill(rxq, retrying);
  }


   /*
    * Ensure that posting buffers is the last thing done by this
    * routine to avoid racing between rx bottom-half and worker
    * (process) contexts.
    */
  #endif 
   return;
}
void sfvmk_rx_deliver(sfvmk_adapter *adapter, struct sfvmk_rx_sw_desc *rx_desc, unsigned int qindex)
{
	vmk_PktHandle *pkt= rx_desc->pkt;
	int flags = rx_desc->flags;
	
	/* Convert checksum flags */
	if (flags & EFX_CKSUM_TCPUDP)
		vmk_PktSetCsumVfd(pkt);

         vmk_PktFrameLenSet(pkt, rx_desc->size - adapter->rx_prefix_size);
	vmk_NetPollRxPktQueue(adapter->nicPoll[qindex].netPoll, pkt);

	rx_desc->flags = EFX_DISCARD;
	rx_desc->pkt = NULL;
}

void
sfvmk_rx_qcomplete(sfvmk_rxq *rxq, boolean_t eop)
{
  sfvmk_adapter *adapter = rxq->adapter;
  unsigned int index;
  struct sfvmk_evq *evq;
  unsigned int completed;
  unsigned int level;
  vmk_PktHandle *pkt ; 
  struct sfvmk_rx_sw_desc *prev = NULL;

  index = rxq->index;
  evq = adapter->evq[index];

  //SFVMK_EVQ_LOCK_ASSERT_OWNED(evq);

  completed = rxq->completed;
  while (completed != rxq->pending) {
    unsigned int id;
    struct sfvmk_rx_sw_desc *rx_desc;

    id = completed++ & rxq->ptr_mask;
    rx_desc = &rxq->queue[id];
    pkt = rx_desc->pkt;
    vmk_LogMessage(" completed %d pending %d\n", completed , rxq->pending);

    if (VMK_UNLIKELY(rxq->init_state != SFVMK_RXQ_STARTED))
      goto discard;

    if (rx_desc->flags & (EFX_ADDR_MISMATCH | EFX_DISCARD))
    
      goto discard;

    /* Read the length from the pseudo header if required */
    if (rx_desc->flags & EFX_PKT_PREFIX_LEN) {
     #if 0 
      uint16_t tmp_size;
      int rc;
      rc = efx_psuedo_hdr_pkt_length_get(adapter->enp,
                 pkt,
                 &tmp_size);
//      KASSERT(rc == 0, ("cannot get packet length: %d", rc));
      rx_desc->size = (int)tmp_size + adapter->rx_prefix_size;
     vmk_LogMessage( " temp size = %d prefix size =%d\n", tmp_size, adapter->rx_prefix_size);
     #endif 
      vmk_LogMessage("Praveen : EFX_PKT_PREFIX_LEN not supported\n");
    }

    prefetch_read_many(pkt);

    switch (rx_desc->flags & (EFX_PKT_IPV4 | EFX_PKT_IPV6)) {
    case EFX_PKT_IPV4:
        rx_desc->flags &=
            ~(EFX_CKSUM_IPV4 | EFX_CKSUM_TCPUDP);
      break;
    case EFX_PKT_IPV6:
        rx_desc->flags &= ~EFX_CKSUM_TCPUDP;
      break;
    case 0:
      /* Check for loopback packets */
       #if 0 
      {
        struct ether_header *etherhp;

        /*LINTED*/
        etherhp = mtod(m, struct ether_header *);

        if (etherhp->ether_type ==
            htons(SFVMK_ETHERTYPE_LOOPBACK)) {
          EFSYS_PROBE(loopback);

          rxq->loopback++;
          goto discard;
        }
      }
			 #endif 
      break;
			
    default:
      //KASSERT(B_FALSE,
       //   ("Rx descriptor with both IPv4 and IPv6 flags"));
      goto discard;
    }

    /* Pass packet up the stack or into LRO (pipelined) */
    if (prev != NULL) {
        sfvmk_rx_deliver(adapter, prev, rxq->index);
    }
    prev = rx_desc;
    continue;

discard:
    /* Return the packet to the pool */
    //m_free(m);
    vmk_LogMessage ("pkt is discarded \n");
    rx_desc->pkt = NULL;
  }
  rxq->completed = completed;

  level = rxq->added - rxq->completed;

  /* Pass last packet up the stack or into LRO */
  if (prev != NULL) {
      sfvmk_rx_deliver(adapter, prev, rxq->index);
  }


  /* Top up the queue if necessary */
  if (level < rxq->refill_threshold)
    sfvmk_rx_qfill(rxq, EFX_RXQ_LIMIT(rxq->entries), B_FALSE);
}


static int
sfvmk_rx_qstart( sfvmk_adapter *adapter, unsigned int index)
{

  sfvmk_rxq *rxq;
  efsys_mem_t *esmp;
  sfvmk_evq *evq;
  int rc;

  vmk_LogMessage("calling sfvmk_rx_qstart\n");
//  SFVMK_ADAPTER_LOCK_ASSERT_OWNED(adapter);

  rxq = adapter->rxq[index];
  esmp = &rxq->mem;
  evq = adapter->evq[index];

  //VMK_ASSERT(rxq->init_state == SFVMK_RXQ_INITIALIZED,
  //VMK_ASSERT(evq->init_state == SFVMK_EVQ_STARTED,

  /* Create the common code receive queue. */
  if ((rc = efx_rx_qcreate(adapter->enp, index, 0, EFX_RXQ_TYPE_DEFAULT,
      esmp, adapter->rxq_enteries, rxq->buf_base_id, evq->common,
      &rxq->common)) != 0)
    goto fail;

//  SFVMK_EVQ_LOCK(evq);

  /* Enable the receive queue. */
  efx_rx_qenable(rxq->common);

  rxq->init_state = SFVMK_RXQ_STARTED;
  rxq->flush_state = SFVMK_FLUSH_REQUIRED;

  /* Try to fill the queue from the pool. */
  vmk_LogMessage("calling sfvmk_rx_qfill\n");
  sfvmk_rx_qfill(rxq, EFX_RXQ_LIMIT(adapter->rxq_enteries), B_FALSE);

  //SFVMK_EVQ_UNLOCK(evq);

  return (0);

fail:
  return (rc);
}



int sfvmk_rx_start( sfvmk_adapter *adapter)
{
  sfvmk_intr *intr;
  const efx_nic_cfg_t *encp;
  size_t hdrlen, align, reserved;
  int index;
  int rc;

  intr = &adapter->sfvmkIntrInfo;
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
  #if 0 
  /* Select zone for packet buffers */
  if (reserved <= MCLBYTES)
    adapter->rx_cluster_size = MCLBYTES;
  else if (reserved <= MJUMPAGESIZE)
    adapter->rx_cluster_size = MJUMPAGESIZE;
  else if (reserved <= MJUM9BYTES)
    adapter->rx_cluster_size = MJUM9BYTES;
  else
    adapter->rx_cluster_size = MJUM16BYTES;
#endif 
  /*
   * Set up the scale table.  Enable all hash types and hash insertion.
   */
#if 0 
  for (index = 0; index < SFVMK_RX_SCALE_MAX; index++)
    adapter->rx_indir_table[index] = index % adapter->rxq_count;
  if ((rc = efx_rx_scale_tbl_set(adapter->enp, adapter->rx_indir_table,
               SFVMK_RX_SCALE_MAX)) != 0)
    goto fail;
  (void)efx_rx_scale_mode_set(adapter->enp, EFX_RX_HASHALG_TOEPLITZ,
      (1 << EFX_RX_HASH_IPV4) | (1 << EFX_RX_HASH_TCPIPV4) |
      (1 << EFX_RX_HASH_IPV6) | (1 << EFX_RX_HASH_TCPIPV6), B_TRUE);

  if ((rc = efx_rx_scale_key_set(adapter->enp, toep_key,
               sizeof(toep_key))) != 0)
    goto fail;
#endif 
  /* Start the receive queue(s). */
  for (index = 0; index < 1/*adapter->rxq_count*/; index++) {
    if ((rc = sfvmk_rx_qstart(adapter, index)) != 0)
      goto fail2;
  }

  // praveen needs to do after port init 
  rc = efx_mac_filter_default_rxq_set(adapter->enp, adapter->rxq[0]->common,0);
//              adapter->sfvmkIntrInfo.numIntrAlloc > 1);
  if (rc != 0)
    goto fail3;
  
 return (0);

fail3:
  vmk_LogMessage("Praveen Error in RX\"n");
fail2:
  while (--index >= 0)
    sfvmk_rx_qstop(adapter, index);
//fail:
  efx_rx_fini(adapter->enp);

  return (rc);
}



