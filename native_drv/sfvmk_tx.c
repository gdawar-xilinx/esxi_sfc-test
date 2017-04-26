#include "sfvmk_tx.h" 
#include "sfvmk_util.h" 


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
	nmaps = adapter->txq_entries;

        //praveen free  stmp;
//	while (nmaps-- != 0)
//		bus_dmamap_destroy(txq->packet_dma_tag, txq->stmp[nmaps].map);
//	free(txq->stmp, M_SFXGE);

	/* Release DMA memory mapping. */
//	sfxge_dma_free(&txq->mem);

        sfvmk_FreeCoherentDMAMapping(adapter,esmp->esm_base, esmp->io_elem.ioAddr, esmp->io_elem.length);
	adapter->txq[index] = NULL;
        sfvmk_MutexDestroy(txq->lock);

//	SFXGE_TXQ_LOCK_DESTROY(txq);

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
        vmk_uint64 alloc_size;

	txq = (struct sfvmk_txq *)sfvmk_memPoolAlloc(sizeof(sfvmk_txq), &alloc_size);
	txq->adapter = adapter;
	txq->entries = adapter->txq_entries;
	txq->ptr_mask = txq->entries - 1;

	adapter->txq[txq_index] = txq;
	esmp = &txq->mem;

	evq = adapter->evq[evq_index];

	/* Allocate and zero DMA space for the descriptor ring. */
//	if ((rc = sfxge_dma_alloc(sc, EFX_TXQ_SIZE(sc->txq_entries), esmp)) != 0)
//		return (rc);

        esmp->esm_base = sfvmk_AllocCoherentDMAMapping(adapter, EFX_TXQ_SIZE(adapter->txq_entries),&esmp->io_elem.ioAddr);
        esmp->io_elem.length = EFX_TXQ_SIZE(adapter->txq_entries);

	/* Allocate pending descriptor array for batching writes. */
	txq->pend_desc = sfvmk_memPoolAlloc(sizeof(efx_desc_t) * adapter->txq_entries, &txq->pend_desc_size);


        #if 0 
	/* Allocate and initialise mbuf DMA mapping array. */
	txq->stmp = malloc(sizeof(struct sfxge_tx_mapping) * sc->txq_entries,
	    M_SFXGE, M_ZERO | M_WAITOK);
	for (nmaps = 0; nmaps < sc->txq_entries; nmaps++) {
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

	rc = sfxge_txq_stat_init(txq, txq_node);
	if (rc != 0)
		goto fail_txq_stat_init;
        #endif 
	txq->type = type;
	txq->evq_index = evq_index;
	txq->txq_index = txq_index;
	txq->init_state = SFVMK_TXQ_INITIALIZED;
//	txq->hw_vlan_tci = 0;

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

