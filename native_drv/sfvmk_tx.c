
/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"
#include "sfvmk_tx.h"
#include "sfvmk_ev.h"
#include "sfvmk_utils.h"
void
sfvmk_tx_qcomplete(struct sfvmk_txq *txq, struct sfvmk_evq *evq)
{
}
static void
sfvmk_tx_qreap(sfvmk_txq *txq)
{
	//SFVMK_TXQ_LOCK_ASSERT_OWNED(txq);

	txq->reaped = txq->completed;
}

static void sfvmk_TXQStop(sfvmk_adapter *adapter, unsigned int index)
{
	sfvmk_txq *txq;
	sfvmk_evq *evq;
	unsigned int count;

//	SFVMK_ADAPTER_LOCK_ASSERT_OWNED(adapter);
	txq = adapter->txq[index];
	evq = adapter->evq[txq->evq_index];
//	SFVMK_EVQ_LOCK(evq);
//	SFVMK_TXQ_LOCK(txq);

	VMK_ASSERT(txq->init_state == SFVMK_TXQ_STARTED);

	txq->init_state = SFVMK_TXQ_INITIALIZED;

	if (txq->flush_state != SFVMK_FLUSH_DONE) {
		txq->flush_state = SFVMK_FLUSH_PENDING;
//		SFVMK_EVQ_UNLOCK(evq);
//		SFVMK_TXQ_UNLOCK(txq);
		/* Flush the transmit queue. */
		if (efx_tx_qflush(txq->common) != 0) {
		 SFVMK_ERR(adapter, "Flushing Tx queue %u failed\n", index);
			txq->flush_state = SFVMK_FLUSH_DONE;
		} else {
			count = 0;
			do {
				/* Spin for 100ms. */
                                vmk_DelayUsecs(100*1000);
				if (txq->flush_state != SFVMK_FLUSH_PENDING)
					break;
			} while (++count < 20);
		}
//		SFVMK_EVQ_LOCK(evq);
//		SFVMK_TXQ_LOCK(txq);
		VMK_ASSERT(txq->flush_state != SFVMK_FLUSH_FAILED);
		if (txq->flush_state != SFVMK_FLUSH_DONE) {
			/* Flush timeout */
			SFVMK_ERR(adapter, " Cannot flush Tx queue %u\n", index);
			txq->flush_state = SFVMK_FLUSH_DONE;
		}
	}
	txq->blocked = 0;
	txq->pending = txq->added;

	sfvmk_tx_qcomplete(txq, evq);
//	VMK_ASSERT(txq->completed == txq->added);

	sfvmk_tx_qreap(txq);
//	VMK_ASSERT(txq->reaped == txq->completed);


	txq->added = 0;
	txq->pending = 0;
	txq->completed = 0;
	txq->reaped = 0;

	/* Destroy the common code transmit queue. */
	efx_tx_qdestroy(txq->common);
	txq->common = NULL;
//	SFVMK_EVQ_UNLOCK(evq);
//	SFVMK_TXQ_UNLOCK(txq);
}

void sfvmk_TXStop(sfvmk_adapter *adapter)
{
	int index;

	index = 1;//adapter->txq_count;
	while (--index >= 0)
		sfvmk_TXQStop(adapter, index);

	/* Tear down the transmit module */
	efx_tx_fini(adapter->enp);
}

static void
sfvmk_TXQFini(sfvmk_adapter *adapter, unsigned int index)
{
	sfvmk_txq *txq;
	efsys_mem_t *esmp;

	txq = adapter->txq[index];
	esmp = &txq->mem;

	VMK_ASSERT(txq->init_state == SFXGE_TXQ_INITIALIZED);

	/* Free the context arrays. */
  sfvmk_MemPoolFree(txq->pend_desc , txq->pend_desc_size); 


  sfvmk_FreeCoherentDMAMapping(adapter,esmp->esm_base, esmp->io_elem.ioAddr, esmp->io_elem.length);
	adapter->txq[index] = NULL;
  sfvmk_MutexDestroy(txq->lock);

  sfvmk_MemPoolFree(txq,  sizeof(sfvmk_txq));

}

static int
sfvmk_TXQInit(sfvmk_adapter *adapter, unsigned int txq_index,
	       enum sfvmk_txq_type type, unsigned int evq_index)
{
	sfvmk_txq *txq;
	struct sfvmk_evq *evq;
	efsys_mem_t *esmp;

	txq = (struct sfvmk_txq *)sfvmk_MemPoolAlloc(sizeof(sfvmk_txq));
	txq->adapter = adapter;
	txq->entries = adapter->txq_entries;
	txq->ptr_mask = txq->entries - 1;
        
	adapter->txq[txq_index] = txq;
	esmp = &txq->mem;
  esmp->esm_handle  = adapter->vmkDmaEngine;
	evq = adapter->evq[evq_index];

	/* Allocate and zero DMA space for the descriptor ring. */

  esmp->esm_base = sfvmk_AllocCoherentDMAMapping(adapter, EFX_TXQ_SIZE(adapter->txq_entries),&esmp->io_elem.ioAddr);
  esmp->io_elem.length = EFX_TXQ_SIZE(adapter->txq_entries);

	/* Allocate pending descriptor array for batching writes. */
	txq->pend_desc = sfvmk_MemPoolAlloc(sizeof(efx_desc_t) * adapter->txq_entries);
        txq->pend_desc_size = sizeof(efx_desc_t) * adapter->txq_entries; 

       sfvmk_MutexInit("txq" ,VMK_MUTEX_RANK_HIGHEST - 2, &txq->lock);

	txq->type = type;
	txq->evq_index = evq_index;
	txq->txq_index = txq_index;
	txq->init_state = SFVMK_TXQ_INITIALIZED;
//	txq->hw_vlan_tci = 0;

	return (0);


}


int
sfvmk_TxInit(sfvmk_adapter *adapter)
{
	struct sfvmk_intr *intr;
	int index;
	int rc;

	intr = &adapter->intr;

	VMK_ASSERT(intr->state == SFXGE_INTR_INITIALIZED);

	adapter->txq_count = SFVMK_TXQ_NTYPES - 1 + intr->numIntrAlloc;

 
	/* Initialize the transmit queues */
	if ((rc = sfvmk_TXQInit(adapter, SFVMK_TXQ_NON_CKSUM,
	    SFVMK_TXQ_NON_CKSUM, 0)) != 0)
		goto fail;

	if ((rc = sfvmk_TXQInit(adapter, SFVMK_TXQ_IP_CKSUM,
	    SFVMK_TXQ_IP_CKSUM, 0)) != 0)
		goto fail2;

	for (index = 0;
	     index < adapter->txq_count - SFVMK_TXQ_NTYPES + 1;
	     index++) {
		if ((rc = sfvmk_TXQInit(adapter, SFVMK_TXQ_NTYPES - 1 + index,
		    SFVMK_TXQ_IP_TCP_UDP_CKSUM, index)) != 0)
			goto fail3;
	}

	return (0);

fail3:
	while (--index >= 0)
		sfvmk_TXQFini(adapter, SFVMK_TXQ_IP_TCP_UDP_CKSUM + index);

	sfvmk_TXQFini(adapter, SFVMK_TXQ_IP_CKSUM);

fail2:
	sfvmk_TXQFini(adapter, SFVMK_TXQ_NON_CKSUM);

fail:
	adapter->txq_count = 0;
	return (rc);
}




void
sfvmk_TxFini(sfvmk_adapter *adapter)
{
	int index;

	index = adapter->txq_count;
	while (--index >= 0)
		sfvmk_TXQFini(adapter, index);

	adapter->txq_count = 0;
}


static int
sfvmk_TXQStart(sfvmk_adapter *adapter, unsigned int index)
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
      adapter->txq_entries, 0, flags, evq->common,
      &txq->common, &desc_index)) != 0) {
    /* Retry if no FATSOv2 resources, otherwise fail */
    if ((rc != ENOSPC) || (~flags & EFX_TXQ_FATSOV2))
      goto fail;

    /* Looks like all FATSOv2 contexts are used */
    flags &= ~EFX_TXQ_FATSOV2;
    tso_fw_assisted &= ~SFVMK_FATSOV2;
    if ((rc = efx_tx_qcreate(adapter->enp, index, txq->type, esmp,
        adapter->txq_entries, 0, flags, evq->common,
        &txq->common, &desc_index)) != 0)
      goto fail;
  }

  /* Initialise queue descriptor indexes */
  txq->added = txq->pending = txq->completed = txq->reaped = desc_index;

  //SFVMK_TXQ_LOCK(txq);

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
  //SFVMK_TXQ_UNLOCK(txq);

  return (0);

fail:
  return (rc);
}




int sfvmk_TXStart(sfvmk_adapter *adapter)
{
  int index;
  int rc;

  /* Initialize the common code transmit module. */
  if ((rc = efx_tx_init(adapter->enp)) != 0)
    return (rc);

  for (index = 0; index < 1/*adapter->txq_count*/; index++) {
    if ((rc = sfvmk_TXQStart(adapter, index)) != 0)
      goto fail;
  }

  return (0);

fail:
  while (--index >= 0)
    sfvmk_TXQStop(adapter, index);

  efx_tx_fini(adapter->enp);

  return (rc);
}




