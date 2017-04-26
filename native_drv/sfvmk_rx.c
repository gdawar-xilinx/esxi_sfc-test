
#include "sfvmk_util.h"
#include "sfvmk_rx.h"
#include "sfvmk_ev.h"
#include "sfvmk_driver.h"

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

        sfvmk_memPoolFree(&rxq, rxq->size);
}

static int
sfvmk_rx_qinit(sfvmk_adapter *adapter, unsigned int index)
{
	struct sfvmk_rxq *rxq;
	struct sfvmk_evq *evq;
	efsys_mem_t *esmp;
	vmk_uint64  alloc_size;


	VMK_ASSERT(index < adapter->rxq_count);
        rxq = sfvmk_memPoolAlloc(sizeof(sfvmk_rxq), &alloc_size);
        rxq->size = alloc_size;
	
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

	/* Allocate the context array and the flow table. */
	rxq->queue = sfvmk_memPoolAlloc(sizeof(struct sfvmk_rx_sw_desc) * adapter->rxq_entries, &alloc_size);
        rxq->qdescSize = alloc_size;

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


