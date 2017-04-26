#include "sfvmk_ev.h"
#include "sfvmk_driver.h"
#include "sfvmk_util.h"



int roundup_pow_of_two(unsigned int _n)	
{                      
   unsigned int index=0;
  _n = _n -1 ;       
   do{                 
     _n >>=1;      
     index++;      
   }while(_n);      
  return (1ULL << index);    
}

static int
sfvmk_ev_qinit(sfvmk_adapter *adapter, unsigned int index)
{
	struct sfvmk_evq *evq;
	efsys_mem_t *esmp;
	vmk_uint64 alloc_size;

	VMK_ASSERT(index < SFVMK_RX_SCALE_MAX);


	evq = (struct sfvmk_evq *)sfvmk_memPoolAlloc(sizeof(sfvmk_evq), &alloc_size);
        evq->size = alloc_size;
        vmk_Memset(evq, 0 , sizeof(sfvmk_evq));

	evq->adapter = adapter;
	evq->index = index;

	adapter->evq[index] = evq;
	esmp = &evq->mem;

	/* Build an event queue with room for one event per tx and rx buffer,
	 * plus some extra for link state events and MCDI completions.
	 * There are three tx queues in the first event queue and one in
	 * other.
	 */
	if (index == 0)
		evq->entries =
			roundup_pow_of_two(adapter->rxq_entries +
					   3 * adapter->txq_entries +
					   128);
	else
		evq->entries =
			roundup_pow_of_two(adapter->rxq_entries +
					   adapter->txq_entries +
					   128);

	/* Initialise TX completion list */
	evq->txqs = &evq->txq;

	/* Allocate DMA space. */
        esmp->esm_base = sfvmk_AllocCoherentDMAMapping(adapter, EFX_EVQ_SIZE(evq->entries),&esmp->io_elem.ioAddr);
        esmp->io_elem.length = EFX_EVQ_SIZE(evq->entries);
        // praveen needs to check the name 
        sfvmk_MutexInit("evq" ,VMK_MUTEX_RANK_HIGHEST - 1, &evq->lock);

	evq->init_state = SFVMK_EVQ_INITIALIZED;

	return (0);
}

static void
sfvmk_ev_qfini(sfvmk_adapter *adapter, unsigned int index)
{
	struct sfvmk_evq *evq;
	efsys_mem_t *esmp;

	evq = adapter->evq[index];

	esmp = &evq->mem;
	VMK_ASSERT(evq->init_state == SFXGE_EVQ_INITIALIZED);
	VMK_ASSERT(evq->txqs == &evq->txq);
        //praveen

        sfvmk_FreeCoherentDMAMapping(adapter,esmp->esm_base, esmp->io_elem.ioAddr, EFX_RXQ_SIZE(adapter->rxq_entries));
	adapter->evq[index] = NULL;

        sfvmk_MutexDestroy(evq->lock);

  //praveen free evq structure
	//free(evq, M_SFXGE);
}


void
sfvmk_ev_fini(sfvmk_adapter *adapter)
{
	sfvmk_intr *intr;
	int index;

	intr = &adapter->sfvmkIntrInfo;

	VMK_ASSERT(intr->state == SFVMK_INTR_INITIALIZED);

	adapter->ev_moderation = 0;

	/* Tear down the event queue(s). */
	index = adapter->evq_count;
	while (--index >= 0)
		sfvmk_ev_qfini(adapter, index);

	adapter->evq_count = 0;
}

int
sfvmk_ev_init(sfvmk_adapter *adapter)
{

  // praveen
	//struct sysctl_ctx_list *sysctl_ctx = device_get_sysctl_ctx(sc->dev);
	//struct sysctl_oid *sysctl_tree = device_get_sysctl_tree(sc->dev);
	sfvmk_intr *intr;
	int index;
	int rc;

	intr = &adapter->sfvmkIntrInfo;

	adapter->evq_count = intr->numIntrAlloc;
        vmk_LogMessage("Number of event queue %d\n" , adapter->evq_count);
	VMK_ASSERT(intr->state == SFVMK_INTR_INITIALIZED);

	/* Set default interrupt moderation; add a sysctl to
	 * read and change it.
	 */
	adapter->ev_moderation = SFVMK_MODERATION;

  //praveen needs to check
  /*
	SYSCTL_ADD_PROC(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree),
			OID_AUTO, "int_mod", CTLTYPE_UINT|CTLFLAG_RW,
			sc, 0, sfxge_int_mod_handler, "IU",
			"sfxge interrupt moderation (us)");
 */
	/*
	 * Initialize the event queue(s) - one per interrupt.
	 */
	for (index = 0; index < adapter->evq_count; index++) {
		if ((rc = sfvmk_ev_qinit(adapter, index)) != 0)
			goto fail;
	}

	return (0);

fail:
	while (--index >= 0)
		sfvmk_ev_qfini(adapter, index);

	adapter->evq_count = 0;
	return (rc);
}



