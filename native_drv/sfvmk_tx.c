#include "sfvmk_tx.h" 
#include "sfvmk_ev.h" 
#include "sfvmk_util.h" 
#include "sfvmk_uplink.h"

static void
sfvmk_tx_qfini(sfvmk_adapter *adapter, unsigned int index)
{
	sfvmk_txq *txq;
	unsigned int nmaps;
	efsys_mem_t *esmp;

	txq = adapter->txq[index];
	esmp = &txq->mem;

	VMK_ASSERT(txq->init_state == SFXGE_TXQ_INITIALIZED);


        //praveen will do it later on 
//	if (txq->type == SFXGE_TXQ_IP_TCP_UDP_CKSUM)
//		tso_fini(txq);

	/* Free the context arrays. */
        sfvmk_memPoolFree(txq->pend_desc , txq->pend_desc_size); 
	nmaps = adapter->txq_enteries;

        //praveen free  stmp;
//	while (nmaps-- != 0)
//		bus_dmamap_destroy(txq->packet_dma_tag, txq->stmp[nmaps].map);
//	free(txq->stmp, M_SFXGE);

	/* Release DMA memory mapping. */
//	sfxge_dma_free(&txq->mem);

        sfvmk_FreeCoherentDMAMapping(adapter,esmp->esm_base, esmp->io_elem.ioAddr, esmp->io_elem.length);
	adapter->txq[index] = NULL;
        sfvmk_MutexDestroy(txq->lock);

	//SFVMK_TXQ_LOCK_DESTROY(txq);

        //praveen free
	//free(txq, M_SFXGE);
}

static int
sfvmk_tx_qinit(sfvmk_adapter *adapter, unsigned int txq_index,
	       enum sfvmk_txq_type type, unsigned int evq_index)
{
	//char name[16];
	sfvmk_txq *txq;
	struct sfvmk_evq *evq;
	//struct sfvmk_tx_dpl *stdp;
	efsys_mem_t *esmp;

	txq = (struct sfvmk_txq *)sfvmk_memPoolAlloc(sizeof(sfvmk_txq));
	txq->adapter = adapter;
	txq->entries = adapter->txq_enteries;
	txq->ptr_mask = txq->entries - 1;
        
	adapter->txq[txq_index] = txq;
	esmp = &txq->mem;
        esmp->esm_handle  = adapter->vmkDmaEngine;
	evq = adapter->evq[evq_index];

	/* Allocate and zero DMA space for the descriptor ring. */

        esmp->esm_base = sfvmk_AllocCoherentDMAMapping(adapter, EFX_TXQ_SIZE(adapter->txq_enteries),&esmp->io_elem.ioAddr);
        esmp->io_elem.length = EFX_TXQ_SIZE(adapter->txq_enteries);

	/* Allocate pending descriptor array for batching writes. */
	txq->pend_desc = sfvmk_memPoolAlloc(sizeof(efx_desc_t) * adapter->txq_enteries);
        txq->pend_desc_size = sizeof(efx_desc_t) * adapter->txq_enteries; 

	/* Allocate and initialise pkt DMA mapping array. */
	txq->stmp = sfvmk_memPoolAlloc(sizeof(struct sfvmk_tx_mapping) * adapter->txq_enteries);
	#if 0
	for (nmaps = 0; nmaps < adapter->txq_entries; nmaps++) {
		rc = bus_dmamap_create(txq->packet_dma_tag, 0,
				       &txq->stmp[nmaps].map);
		if (rc != 0)
			goto fail2;
	}

	snprintf(name, sizeof(name), "%u", txq_index);
	txq_node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(sc->txqs_node),
				   OID_AUTO, name, CTLFLAG_RD, NULL, "");
	if (txq_node == NULL) {
		rc = ENOMEM;
		goto fail_txq_node;
	}

	if (type == SFXGE_TXQ_IP_TCP_UDP_CKSUM &&
	    (rc = tso_init(txq)) != 0)
		goto fail3;

	if (sfxge_tx_dpl_get_max <= 0) {
		log(LOG_ERR, "%s=%d must be greater than 0",
		    SFXGE_PARAM_TX_DPL_GET_MAX, sfxge_tx_dpl_get_max);
		rc = EINVAL;
		goto fail_tx_dpl_get_max;
	}
	if (sfxge_tx_dpl_get_non_tcp_max <= 0) {
		log(LOG_ERR, "%s=%d must be greater than 0",
		    SFXGE_PARAM_TX_DPL_GET_NON_TCP_MAX,
		    sfxge_tx_dpl_get_non_tcp_max);
		rc = EINVAL;
		goto fail_tx_dpl_get_max;
	}
	if (sfxge_tx_dpl_put_max < 0) {
		log(LOG_ERR, "%s=%d must be greater or equal to 0",
		    SFXGE_PARAM_TX_DPL_PUT_MAX, sfxge_tx_dpl_put_max);
		rc = EINVAL;
		goto fail_tx_dpl_put_max;
	}

	/* Initialize the deferred packet list. */
	stdp = &txq->dpl;
	stdp->std_put_max = sfxge_tx_dpl_put_max;
	stdp->std_get_max = sfxge_tx_dpl_get_max;
	stdp->std_get_non_tcp_max = sfxge_tx_dpl_get_non_tcp_max;
	stdp->std_getp = &stdp->std_get;
        #endif 

        sfvmk_MutexInit("txq" ,VMK_MUTEX_RANK_HIGHEST - 2, &txq->lock);
        #if 0 
	dpl_node = SYSCTL_ADD_NODE(ctx, SYSCTL_CHILDREN(txq_node), OID_AUTO,
				   "dpl", CTLFLAG_RD, NULL,
				   "Deferred packet list statistics");
	if (dpl_node == NULL) {
		rc = ENOMEM;
		goto fail_dpl_node;
	}

	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(dpl_node), OID_AUTO,
			"get_count", CTLFLAG_RD | CTLFLAG_STATS,
			&stdp->std_get_count, 0, "");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(dpl_node), OID_AUTO,
			"get_non_tcp_count", CTLFLAG_RD | CTLFLAG_STATS,
			&stdp->std_get_non_tcp_count, 0, "");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(dpl_node), OID_AUTO,
			"get_hiwat", CTLFLAG_RD | CTLFLAG_STATS,
			&stdp->std_get_hiwat, 0, "");
	SYSCTL_ADD_UINT(ctx, SYSCTL_CHILDREN(dpl_node), OID_AUTO,
			"put_hiwat", CTLFLAG_RD | CTLFLAG_STATS,
			&stdp->std_put_hiwat, 0, "");

	rc = sfvmk_txq_stat_init(txq, txq_node);
	if (rc != 0)
		goto fail_txq_stat_init;
        #endif 
	txq->type = type;
	txq->evq_index = evq_index;
	txq->txq_index = txq_index;
	txq->init_state = SFVMK_TXQ_INITIALIZED;
	txq->hw_vlan_tci = 0;

	return (0);


}






int
sfvmk_tx_init(sfvmk_adapter *adapter)
{
	struct sfvmk_intr *intr;
	int index;
	int rc;

	intr = &adapter->sfvmkIntrInfo;

	VMK_ASSERT(intr->state == SFXGE_INTR_INITIALIZED);

	adapter->txq_count = SFVMK_TXQ_NTYPES - 1 + intr->numIntrAlloc;
	vmk_LogMessage("txq_count: %d, intr->numIntrAlloc: %d",adapter->txq_count, intr->numIntrAlloc);

        //praveen willl do it later 
       /*
	sc->tso_fw_assisted = sfxge_tso_fw_assisted;
	if ((~encp->enc_features & EFX_FEATURE_FW_ASSISTED_TSO) ||
	    (!encp->enc_fw_assisted_tso_enabled))
		sc->tso_fw_assisted &= ~SFXGE_FATSOV1;
	if ((~encp->enc_features & EFX_FEATURE_FW_ASSISTED_TSO_V2) ||
	    (!encp->enc_fw_assisted_tso_v2_enabled))
		sc->tso_fw_assisted &= ~SFXGE_FATSOV2;

	sc->txqs_node = SYSCTL_ADD_NODE(
		device_get_sysctl_ctx(sc->dev),
		SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)),
		OID_AUTO, "txq", CTLFLAG_RD, NULL, "Tx queues");
	if (sc->txqs_node == NULL) {
		rc = ENOMEM;
		goto fail_txq_node;
	}
        */


	/* Initialize the transmit queues */
	if ((rc = sfvmk_tx_qinit(adapter, SFVMK_TXQ_NON_CKSUM,
	    SFVMK_TXQ_NON_CKSUM, 0)) != 0)
		goto fail;

	if ((rc = sfvmk_tx_qinit(adapter, SFVMK_TXQ_IP_CKSUM,
	    SFVMK_TXQ_IP_CKSUM, 0)) != 0)
		goto fail2;

	for (index = 0;
	     index < adapter->txq_count - SFVMK_TXQ_NTYPES + 1;
	     index++) {
		if ((rc = sfvmk_tx_qinit(adapter, SFVMK_TXQ_NTYPES - 1 + index,
		    SFVMK_TXQ_IP_TCP_UDP_CKSUM, index)) != 0)
			goto fail3;
	}

//	sfxge_tx_stat_init(sc);

	return (0);

fail3:
	while (--index >= 0)
		sfvmk_tx_qfini(adapter, SFVMK_TXQ_IP_TCP_UDP_CKSUM + index);

	sfvmk_tx_qfini(adapter, SFVMK_TXQ_IP_CKSUM);

fail2:
	sfvmk_tx_qfini(adapter, SFVMK_TXQ_NON_CKSUM);

fail:
	adapter->txq_count = 0;
	return (rc);
}
static int
sfvmk_tx_qstart(sfvmk_adapter *adapter, unsigned int index)
{
  sfvmk_txq *txq;
  efsys_mem_t *esmp;
  uint16_t flags;
  unsigned int tso_fw_assisted;
  sfvmk_evq *evq;
  unsigned int desc_index;
  int rc;

  //SFVMK_ADAPTER_LOCK_ASSERT_OWNED(adapter);

  txq = adapter->txq[index];
  esmp = &txq->mem;
  evq = adapter->evq[txq->evq_index];

//  VMK_ASSERT(txq->init_state == SFVMK_TXQ_INITIALIZED);
//    VMK_ASSERT(evq->init_state == SFVMK_EVQ_STARTED);


  /* Determine the kind of queue we are creating. */
  tso_fw_assisted = 0;
  switch (txq->type) {
  case SFVMK_TXQ_NON_CKSUM:
    flags = 0;
    break;
  case SFVMK_TXQ_IP_CKSUM:
    flags = EFX_TXQ_CKSUM_IPV4;
    break;
  case SFVMK_TXQ_IP_TCP_UDP_CKSUM:
    flags = EFX_TXQ_CKSUM_IPV4 | EFX_TXQ_CKSUM_TCPUDP;
    tso_fw_assisted = adapter->tso_fw_assisted;
    if (tso_fw_assisted & SFVMK_FATSOV2)
      flags |= EFX_TXQ_FATSOV2;
    break;
  default:
    //VMK_ASSERT(0);
    flags = 0;
    break;
  }

  //praveen 
  //  adapter->tso_fw_assisted needs to be initialized 

  /* Create the common code transmit queue. */
  if ((rc = efx_tx_qcreate(adapter->enp, index, txq->type, esmp,
      adapter->txq_enteries, txq->buf_base_id, flags, evq->common,
      &txq->common, &desc_index)) != 0) {
    /* Retry if no FATSOv2 resources, otherwise fail */
    if ((rc != ENOSPC) || (~flags & EFX_TXQ_FATSOV2))
      goto fail;

    /* Looks like all FATSOv2 contexts are used */
    flags &= ~EFX_TXQ_FATSOV2;
    tso_fw_assisted &= ~SFVMK_FATSOV2;
    if ((rc = efx_tx_qcreate(adapter->enp, index, txq->type, esmp,
        adapter->txq_enteries, txq->buf_base_id, flags, evq->common,
        &txq->common, &desc_index)) != 0)
      goto fail;
  }

  /* Initialise queue descriptor indexes */
  txq->added = txq->pending = txq->completed = txq->reaped = desc_index;

  SFVMK_TXQ_LOCK(txq);

  /* Enable the transmit queue. */
  efx_tx_qenable(txq->common);

  txq->init_state = SFVMK_TXQ_STARTED;
  txq->flush_state = SFVMK_FLUSH_REQUIRED;
  txq->tso_fw_assisted = tso_fw_assisted;

  // will see later 
  /*
  txq->max_pkt_desc = sfvmk_tx_max_pkt_desc(adapter, txq->type,
              tso_fw_assisted);
  */
  txq->hw_vlan_tci = 0;
  SFVMK_TXQ_UNLOCK(txq);

  return (0);

fail:
  return (rc);
}




int sfvmk_tx_start(sfvmk_adapter *adapter)
{
  int index;
  int rc;

  /* Initialize the common code transmit module. */
  if ((rc = efx_tx_init(adapter->enp)) != 0)
    return (rc);

  for (index = 0; index < 1/*adapter->txq_count*/; index++) {
    if ((rc = sfvmk_tx_qstart(adapter, index)) != 0)
      goto fail;
  }

  return (0);

fail:
//  while (--index >= 0)
//    sfvmk_tx_qstop(adapter, index);

  efx_tx_fini(adapter->enp);

  return (rc);
}

static void
sfvmk_tx_qreap(struct sfvmk_txq *txq)
{
	SFVMK_TXQ_LOCK_ASSERT_OWNED(txq);

	txq->reaped = txq->completed;
}


void
sfvmk_tx_qlist_post(struct sfvmk_txq *txq)
{
	unsigned int old_added;
	unsigned int block_level;
	unsigned int level;
	int rc;

	SFVMK_TXQ_LOCK_ASSERT_OWNED(txq);

	KASSERT(txq->n_pend_desc != 0, ("txq->n_pend_desc == 0"));
	KASSERT(txq->n_pend_desc <= txq->max_pkt_desc,
		("txq->n_pend_desc too large"));
	KASSERT(!txq->blocked, ("txq->blocked"));

	old_added = txq->added;
	vmk_LogMessage("old_added: %d, reaped: %d", old_added, txq->reaped);

	/* Post the fragment list. */
	rc = efx_tx_qdesc_post(txq->common, txq->pend_desc, txq->n_pend_desc,
			  txq->reaped, &txq->added);
	KASSERT(rc == 0, ("efx_tx_qdesc_post() failed"));

	/* If efx_tx_qdesc_post() had to refragment, our information about
	 * buffers to free may be associated with the wrong
	 * descriptors.
	 */
	vmk_LogMessage("added: %d, reaped: %d", txq->added, txq->reaped);
	KASSERT(txq->added - old_added == txq->n_pend_desc,
		("efx_tx_qdesc_post() refragmented descriptors"));

	level = txq->added - txq->reaped;
	KASSERT(level <= txq->entries, ("overfilled TX queue"));

	/* Clear the fragment list. */
	txq->n_pend_desc = 0;

	/*
	 * Set the block level to ensure there is space to generate a
	 * large number of descriptors for TSO.
	 */
	block_level = EFX_TXQ_LIMIT(txq->entries) - txq->max_pkt_desc;
	vmk_LogMessage("TXQ_LIMIT: %d, max_pkt_dec: %d, block_level: %d", EFX_TXQ_LIMIT(txq->entries), txq->max_pkt_desc, block_level);

	/* Have we reached the block level? */
	if (level < block_level)
		return;

	/* Reap, and check again */
	sfvmk_tx_qreap(txq);
	level = txq->added - txq->reaped;
	if (level < block_level)
		return;

	txq->blocked = 1;
	/* Mark the queue as stopped */
	sfvmk_updateQueueStatus(txq->adapter, VMK_UPLINK_QUEUE_STATE_STOPPED);
	vmk_LogMessage("Marked queue as stopped as level: %d", level);

	/*
	 * Avoid a race with completion interrupt handling that could leave
	 * the queue blocked.
	 */
	//mb();
	vmk_CPUMemFenceWrite();
	sfvmk_tx_qreap(txq);
	level = txq->added - txq->reaped;
	if (level < block_level) {
	        vmk_CPUMemFenceWrite();
		//mb();
		/* Mark the queue as started */
		txq->blocked = 0;
		sfvmk_updateQueueStatus(txq->adapter, VMK_UPLINK_QUEUE_STATE_STARTED);
		vmk_LogMessage("Marked queue as started as level: %d", level);
	}
}

static inline void
sfvmk_next_stmp(struct sfvmk_txq *txq, struct sfvmk_tx_mapping **pstmp)
{
	if (VMK_UNLIKELY(*pstmp == &txq->stmp[txq->ptr_mask]))
		*pstmp = &txq->stmp[0];
	else
		(*pstmp)++;

	vmk_LogMessage("stmp id: %ld", *pstmp - &txq->stmp[0]);
}



static void
sfvmk_tx_qunblock(struct sfvmk_txq *txq)
{
	/*
	 * If evq pointer is really required, it may be passed as the function
	 * argument.
	 */
	SFVMK_EVQ_LOCK_ASSERT_OWNED(txq->adapter->evq[txq->evq_index]);

	if (VMK_UNLIKELY(txq->init_state != SFVMK_TXQ_STARTED))
		return;

	SFVMK_TXQ_LOCK(txq);

	if (txq->blocked) {
		unsigned int level;

		level = txq->added - txq->completed;
   		vmk_LogMessage("completed: %d, added: %d, level: %d", txq->completed, txq->added, level);
		if (level <= SFVMK_TXQ_UNBLOCK_LEVEL(txq->entries)) {
			/* reaped must be in sync with blocked */
			sfvmk_tx_qreap(txq);
			txq->blocked = 0;
		}
	}

	SFVMK_TXQ_UNLOCK(txq);
	//TODO: we don't have pkt handle to service here
	//sfvmk_tx_qdpl_service(txq);
	/* note: lock has been dropped */
}


void
sfvmk_tx_qcomplete(struct sfvmk_txq *txq, struct sfvmk_evq *evq)
{
	unsigned int completed;

	SFVMK_EVQ_LOCK_ASSERT_OWNED(evq);

	completed = txq->completed;
	while (completed != txq->pending) {
		struct sfvmk_tx_mapping *stmp;
		unsigned int id;

		id = completed++ & txq->ptr_mask;
   		vmk_LogMessage("completed: %d, pending: %d, id: %d", completed, txq->pending, id);

		stmp = &txq->stmp[id];
		if (stmp->sgelem.ioAddr != 0) {
   			vmk_LogMessage("Unmapping frag at addr: %lx", stmp->sgelem.ioAddr);
			vmk_DMAUnmapElem(txq->adapter->vmkDmaEngine, VMK_DMA_DIRECTION_FROM_MEMORY,
					&stmp->sgelem);
		}

		if (stmp->pkt) {
   			vmk_LogMessage("Calling vmk_NetPollQueueCompPkt");
			vmk_NetPollQueueCompPkt(txq->adapter->nicPoll[0].netPoll, stmp->pkt);
		} 
	}

	txq->completed = completed;

   	vmk_LogMessage("completed: %d, pending: %d, blocked: %d", txq->completed, txq->pending, txq->blocked);
	/* Check whether we need to unblock the queue. */
	//mb();
	vmk_CPUMemFenceWrite();
	if (txq->blocked) {
		unsigned int level;

		level = txq->added - txq->completed;
   		vmk_LogMessage("added: %d, completed: %d, level: %d", txq->added, txq->completed, level);
		if (level <= SFVMK_TXQ_UNBLOCK_LEVEL(txq->entries))
			sfvmk_tx_qunblock(txq);
	}
}

VMK_ReturnStatus sfvmk_insert_vlan_hdr(vmk_PktHandle **ppPkt, vmk_uint16 vlanTag)
{
   VMK_ReturnStatus ret=VMK_OK;
   vmk_uint8 vlanHdrSize=4;
   vmk_PktHandle *pkt = *ppPkt;
   vmk_uint16 *ptr = NULL;
   vmk_uint8 *frameVA=NULL;

   /*
    *  See if we have enough headroom in the packet to insert vlan hdr
    */
   ret = vmk_PktPullHeadroom(pkt, vlanHdrSize);
   if (ret != VMK_OK) {
      vmk_LogMessage("vmk_PktPullHeadroom failed: %d", ret);
      vmk_PktHandle *pNewPkt = NULL;

      ret = vmk_PktPartialCopyWithHeadroom(pkt, SFVMK_VLAN_HDR_START_OFFSET,
                                              vlanHdrSize, &pNewPkt);
      if (VMK_UNLIKELY(ret != VMK_OK)) {
         vmk_LogMessage("vmk_PktPartialCopyWithHeadroom failed: %d", ret);
       	 return ret;                 
      }

      ret = vmk_PktPullHeadroom(pNewPkt, vlanHdrSize);
      if (VMK_UNLIKELY(ret != VMK_OK)) {
         vmk_LogMessage("vmk_PktPullHeadroom failed: %d",ret);
         vmk_PktRelease(pNewPkt);
	 return ret;
      }

      /* TODO: We don't release the original packet ?? */
      *ppPkt = pkt = pNewPkt;
   }

   frameVA = (vmk_uint8 *) vmk_PktFrameMappedPointerGet(pkt);
   vmk_LogMessage("pkt VA: %p, frame len: %d", frameVA, vmk_PktFrameLenGet(pkt));

   /* pull the mac headers to create space for vlan hdr */
   vmk_Memmove(frameVA, (frameVA + vlanHdrSize), SFVMK_VLAN_HDR_START_OFFSET);

   frameVA += SFVMK_VLAN_HDR_START_OFFSET;

   ptr = (vmk_uint16 *)frameVA;
   *ptr++ = bswap16(VMK_ETH_TYPE_VLAN); 
   *ptr = bswap16(vlanTag);
   frameVA += vlanHdrSize;

   /* Invalidate cache entries */
   vmk_PktHeaderInvalidateAll(pkt);

   return ret;
}

static int sfvmk_tx_maybe_insert_tag(struct sfvmk_txq *txq, vmk_PktHandle **ppPkt, vmk_ByteCountSmall *pPktLen)
{
	uint16_t thisTag=0;
	vmk_VlanID vlanId;
	vmk_VlanPriority vlanPrio;
	vmk_PktHandle *pkt = *ppPkt;

	if(vmk_PktMustVlanTag(pkt)) {
		const efx_nic_cfg_t *pNicCfg = efx_nic_cfg_get(txq->adapter->enp);
		vlanId = vmk_PktVlanIDGet(pkt);
		vlanPrio = vmk_PktPriorityGet(pkt);

		thisTag = (vlanId & SFVMK_VLAN_VID_MASK) | (vlanPrio << SFVMK_VLAN_PRIO_SHIFT);

		vmk_LogMessage("vlan_id: %d, prio: %d, tci: %d, hw_vlan_tci: %d", 
				vlanId, vlanPrio, thisTag, txq->hw_vlan_tci);


		if(pNicCfg->enc_hw_tx_insert_vlan_enabled) {
			vmk_LogMessage("FW assisted tag insertion.."); 
			if (thisTag == txq->hw_vlan_tci)
				return (0);

			efx_tx_qdesc_vlantci_create(txq->common,
					bswap16(thisTag),
					&txq->pend_desc[0]);
			txq->n_pend_desc = 1;
			txq->hw_vlan_tci = thisTag;
			return (1);
		}
		else {
			vmk_LogMessage("software implementation for vlan tag insertion");
			sfvmk_insert_vlan_hdr(&pkt, thisTag);
			*ppPkt = pkt;
			*pPktLen = *pPktLen+4;
			return 0;
		}
	}
	return 0;
}

void sfvmk_MakeTxDescriptor(sfvmk_adapter *adapter,sfvmk_txq *txq,vmk_PktHandle *pkt, vmk_ByteCountSmall pktLen)
{
   int i=0;
   int id=0;
   vmk_uint16 num_frags=0;
   vmk_uint16 vlan_tagged=0;
   const vmk_SgElem *frag;
   efx_desc_t *desc;
   int eop;
   vmk_SgElem mapper_in, mapper_out;
   vmk_DMAMapErrorInfo dmaMapErr;
   vmk_ByteCountSmall frag_length;
   int start_id = id = (txq->added) & txq->ptr_mask;
   struct sfvmk_tx_mapping *stmp = &txq->stmp[id];

   /* VLAN handling */
   vlan_tagged = sfvmk_tx_maybe_insert_tag(txq, &pkt, &pktLen);
   if (vlan_tagged) {
	   sfvmk_next_stmp(txq, &stmp);
   }


   num_frags = vmk_PktSgArrayGet(pkt)->numElems;
   vmk_LogMessage("num frags: %d", num_frags);

   for (i = 0; i < num_frags; i++) {
      VMK_ReturnStatus status;

      /* Get MA of fragment and its length. */
      frag = vmk_PktSgElemGet(pkt, i);
      VMK_ASSERT(frag != NULL);

      frag_length = frag->length;
      vmk_LogMessage("frag %d len: %d, pkt len: %d", i+1, frag_length, pktLen);
      if (pktLen < frag_length) {
         frag_length = pktLen;
      }


      pktLen -= frag_length;

      mapper_in.addr = frag->addr;
      mapper_in.length = frag_length;
      status = vmk_DMAMapElem(adapter->vmkDmaEngine,
                              VMK_DMA_DIRECTION_FROM_MEMORY,
                              &mapper_in, VMK_TRUE, &mapper_out, &dmaMapErr);

      if (status != VMK_OK) {
                     vmk_LogMessage("Failed to map %p size %d to IO address, %s.",
                        pkt, vmk_PktFrameLenGet(pkt),
                        vmk_DMAMapErrorReasonToString(dmaMapErr.reason));
         goto dma_err;
      }
      else
         vmk_LogMessage("Mapped frag addr %lx len: %d to IO addr: %lx, len: %d", 
		mapper_in.addr, mapper_in.length, mapper_out.addr, mapper_out.length);

      /* update the list of in-flight packets */
      id = (txq->added + i + vlan_tagged) & txq->ptr_mask;
      vmk_LogMessage("added: %d stmp: %p, id: %d, &txq->stmp[id]: %p", txq->added, stmp, id, &txq->stmp[id]);
      //stmp = &txq->stmp[id];
      vmk_Memcpy(&stmp->sgelem, &mapper_out, sizeof(vmk_SgElem));
      stmp->pkt=NULL;

      desc = &txq->pend_desc[i + vlan_tagged];
      eop = (i == num_frags - 1);
      efx_tx_qdesc_dma_create(txq->common,
		      mapper_out.ioAddr ,
		      frag_length,
		      eop,
		      desc);
      if(!eop)
      	sfvmk_next_stmp(txq, &stmp);
    }

   /* fill the pkt handle in the last mapping area */
   if(stmp)
   {
      vmk_LogMessage("Filling in pkt handle %p for id: %d", pkt, id);
      stmp->pkt = pkt;
   }

   txq->n_pend_desc = num_frags + vlan_tagged;
   /* Post the fragment list. */
   sfvmk_tx_qlist_post(txq);

   return;
dma_err:
   //check for successfully mapped elements and unmap them
   for (i = start_id; i < id; i++) {
	   stmp = &txq->stmp[i];
	   if (stmp->sgelem.ioAddr != 0) {
		   vmk_LogMessage("Unmapping frag at addr: %lx", stmp->sgelem.ioAddr);
		   vmk_DMAUnmapElem(txq->adapter->vmkDmaEngine, VMK_DMA_DIRECTION_FROM_MEMORY,
				   &stmp->sgelem);
	   }
   }
}

void sfvmk_queuedPkt(sfvmk_adapter *adapter,  sfvmk_txq *txq, vmk_PktHandle *pkt, vmk_ByteCountSmall pktLen)
{
   vmk_PktHeaderEntry* l3_header;
   VMK_ReturnStatus status;
   //vmk_Bool is_vxlan = VMK_FALSE;
   unsigned int pushed = txq->added;
   vmk_uint8 proto = 0, ip_ver = 0;
   //int rc;

   /* Find layer 3 header and populate L3 parameters */
   status = vmk_PktHeaderL3Find(pkt, &l3_header, NULL);
   if (status == VMK_OK) {
      if (l3_header->type == VMK_PKT_HEADER_L3_IPv4) {
         ip_ver = 4;
         proto = l3_header->nextHdrProto;
      	 vmk_LogMessage("proto: %d", proto);
         //vmk_findInnerL4ProtoForVxlanPkt(adapter, pkt, &proto, &is_vxlan);
	}
   }

   /* TODO: TSO handling */

   if(!txq->common)
   {
	   vmk_LogMessage("sfvmk_queuedPkt: txq common not initialized yet"); 
	   return;
   }
   sfvmk_MakeTxDescriptor(adapter, txq, pkt, pktLen);

   if (txq->blocked)
	   return;

   /* Push the fragments to the hardware in batches. */
   vmk_LogMessage("added: %d, pushed: %d", txq->added , pushed);
   if (txq->added - pushed >= SFVMK_TX_BATCH) {
	   efx_tx_qpush(txq->common, txq->added, pushed);
	   pushed = txq->added;
   }

   if (txq->added != pushed)
	   efx_tx_qpush(txq->common, txq->added, pushed);

}

