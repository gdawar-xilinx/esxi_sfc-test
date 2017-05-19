
/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"
#include "sfvmk_ev.h"
#include "sfvmk_utils.h"

static boolean_t sfvmk_ev_initialized(void *arg);
static boolean_t sfvmk_ev_rx(void *arg, uint32_t label, uint32_t id, uint32_t size,
				      uint16_t flags);

static boolean_t sfvmk_ev_tx(void *arg, uint32_t label, uint32_t id);
static boolean_t sfvmk_ev_exception(void *arg, uint32_t code, uint32_t data);
static boolean_t sfvmk_ev_rxq_flush_done(void *arg, uint32_t rxq_index);
static boolean_t sfvmk_ev_rxq_flush_failed(void *arg, uint32_t rxq_index);
static boolean_t sfvmk_ev_txq_flush_done(void *arg, uint32_t txq_index);
static boolean_t sfvmk_ev_software(void *arg, uint16_t magic);
static boolean_t sfvmk_ev_sram(void *arg, uint32_t code);
static boolean_t sfvmk_ev_timer(void *arg, uint32_t index);
static boolean_t sfvmk_ev_wake_up(void *arg, uint32_t index);
static boolean_t sfvmk_ev_link_change(void *arg, efx_link_mode_t link_mode);




static const efx_ev_callbacks_t sfvmk_ev_callbacks = {
  .eec_initialized  = sfvmk_ev_initialized,
  .eec_rx     = sfvmk_ev_rx,
  .eec_tx     = sfvmk_ev_tx,
  .eec_exception    = sfvmk_ev_exception,
  .eec_rxq_flush_done = sfvmk_ev_rxq_flush_done,
  .eec_rxq_flush_failed = sfvmk_ev_rxq_flush_failed,
  .eec_txq_flush_done = sfvmk_ev_txq_flush_done,
  .eec_software   = sfvmk_ev_software,
  .eec_sram   = sfvmk_ev_sram,
  .eec_wake_up    = sfvmk_ev_wake_up,
  .eec_timer    = sfvmk_ev_timer,
  .eec_link_change  = sfvmk_ev_link_change,
};


static boolean_t
sfvmk_ev_initialized(void *arg)
{
  struct sfvmk_evq *evq;

  evq = (struct sfvmk_evq *)arg;
  SFVMK_EVQ_LOCK_ASSERT_OWNED(evq);

  /* Init done events may be duplicated on 7xxx */
//  VMK_ASSERT(evq->init_state == SFVMK_EVQ_STARTING ||
//    evq->init_state == SFVMK_EVQ_STARTED);

  vmk_LogMessage("event queue is initialized\n");
  evq->init_state = SFVMK_EVQ_STARTED;

  return (0);
}


static boolean_t
sfvmk_ev_link_change(void *arg, efx_link_mode_t link_mode)
{
  #if 0
  sfvmk_evq *evq;
  sfvmk_adapter *adapter;

  evq = (struct sfvmk_evq *)arg;
  SFVMK_EVQ_LOCK_ASSERT_OWNED(evq);

  adapter = evq->adapter;

  sfvmk_mac_link_update(adapter, link_mode);
  #endif

  vmk_LogMessage("calling sfvmk_ev_link_change\n");
  return (0);
}

static boolean_t
sfvmk_ev_rx(void *arg, uint32_t label, uint32_t id, uint32_t size,
      uint16_t flags)
{

  #if 0
  sfvmk_evq *evq;
  sfvmk_adapter *adapter;
  sfvmk_rxq *rxq;
  unsigned int stop;
  unsigned int delta;
  struct sfvmk_rx_sw_desc *rx_desc;

  evq = arg;
  SFVMK_EVQ_LOCK_ASSERT_OWNED(evq);

  adapter = evq->adapter;

  if (evq->exception)
    goto done;

  rxq = sfvmk_get_rxq_by_label(evq, label);
  if ((rxq->init_state != SFVMK_RXQ_STARTED))
    goto done;

  stop = (id + 1) & rxq->ptr_mask;
  id = rxq->pending & rxq->ptr_mask;
  delta = (stop >= id) ? (stop - id) : (rxq->entries - id + stop);
  rxq->pending += delta;

  if (delta != 1) {
    if ((delta <= 0) ||
        (delta > efx_nic_cfg_get(adapter->enp)->enc_rx_batch_max)) {
      evq->exception = B_TRUE;

      //device_printf(adapter->dev, "RX completion out of order"
      //        " (id=%#x delta=%u flags=%#x); resetting\n",
      //        id, delta, flags);
      //sfvmk_schedule_reset(adapter);

      goto done;
    }
  }

  rx_desc = &rxq->queue[id];

  prefetch_read_many(rx_desc->mbuf);

  for (; id != stop; id = (id + 1) & rxq->ptr_mask) {
    rx_desc = &rxq->queue[id];
    VMK_ASSERT(rx_desc->flags == EFX_DISCARD,
        ("rx_desc->flags != EFX_DISCARD"));
    rx_desc->flags = flags;

    VMK_ASSERT(size < (1 << 16), ("size > (1 << 16)"));
    rx_desc->size = (uint16_t)size;
  }

  evq->rx_done++;

  if (rxq->pending - rxq->completed >= SFVMK_RX_BATCH)
    sfvmk_ev_qcomplete(evq, B_FALSE);

done:
  return (evq->rx_done >= SFVMK_EV_BATCH);

  #endif

   return  B_FALSE ;
}

static boolean_t
sfvmk_ev_exception(void *arg, uint32_t code, uint32_t data)
{
  sfvmk_evq *evq;
  sfvmk_adapter *adapter;

  evq = (struct sfvmk_evq *)arg;
  SFVMK_EVQ_LOCK_ASSERT_OWNED(evq);

  adapter = evq->adapter;

#if 0
  DBGPRINT(adapter->dev, "[%d] %s", evq->index,
        (code == EFX_EXCEPTION_RX_RECOVERY) ? "RX_RECOVERY" :
        (code == EFX_EXCEPTION_RX_DSC_ERROR) ? "RX_DSC_ERROR" :
        (code == EFX_EXCEPTION_TX_DSC_ERROR) ? "TX_DSC_ERROR" :
        (code == EFX_EXCEPTION_UNKNOWN_SENSOREVT) ? "UNKNOWN_SENSOREVT" :
        (code == EFX_EXCEPTION_FWALERT_SRAM) ? "FWALERT_SRAM" :
        (code == EFX_EXCEPTION_UNKNOWN_FWALERT) ? "UNKNOWN_FWALERT" :
        (code == EFX_EXCEPTION_RX_ERROR) ? "RX_ERROR" :
        (code == EFX_EXCEPTION_TX_ERROR) ? "TX_ERROR" :
        (code == EFX_EXCEPTION_EV_ERROR) ? "EV_ERROR" :
        "UNKNOWN");
#endif

  evq->exception = B_TRUE;

  if (code != EFX_EXCEPTION_UNKNOWN_SENSOREVT) {
  #if 0
    device_printf(adapter->dev,
            "hardware exception (code=%u); resetting\n",
            code);

    sfvmk_schedule_reset(adapter);
  #endif
   vmk_LogMessage ("Got exception ................\n");
  }

  return (B_FALSE);
}

static boolean_t
sfvmk_ev_rxq_flush_done(void *arg, uint32_t rxq_index)
{
  #if 0
  sfvmk_evq *evq;
  sfvmk_adapter *adapter;
  sfvmk_rxq *rxq;
  unsigned int index;
  uint16_t magic;

  evq = (struct sfvmk_evq *)arg;
  SFVMK_EVQ_LOCK_ASSERT_OWNED(evq);

  adapter = evq->adapter;
  rxq = adapter->rxq[rxq_index];

  VMK_ASSERT(rxq != NULL, ("rxq == NULL"));

  /* Resend a software event on the correct queue */
  index = rxq->index;
  if (index == evq->index) {
    sfvmk_rx_qflush_done(rxq);
    return (B_FALSE);
  }

  evq = adapter->evq[index];
  magic = sfvmk_sw_ev_rxq_magic(SFVMK_SW_EV_RX_QFLUSH_DONE, rxq);

  VMK_ASSERT(evq->init_state == SFVMK_EVQ_STARTED,
      ("evq not started"));
  efx_ev_qpost(evq->common, magic);
#endif
  return (B_FALSE);
}

static boolean_t
sfvmk_ev_rxq_flush_failed(void *arg, uint32_t rxq_index)
{
  #if 0
  sfvmk_evq *evq;
  sfvmk_adapter *adapter;
  sfvmk_rxq *rxq;
  unsigned int index;
  uint16_t magic;

  evq = (struct sfvmk_evq *)arg;
  SFVMK_EVQ_LOCK_ASSERT_OWNED(evq);

  adapter = evq->adapter;
  rxq = adapter->rxq[rxq_index];

//  VMK_ASSERT(rxq != NULL);

  /* Resend a software event on the correct queue */
  index = rxq->index;
  evq = adapter->evq[index];
  magic = sfvmk_sw_ev_rxq_magic(SFVMK_SW_EV_RX_QFLUSH_FAILED, rxq);

//  VMK_ASSERT(evq->init_state == SFVMK_EVQ_STARTED);

  efx_ev_qpost(evq->common, magic);
  #endif
  return (B_FALSE);
}
#if 0
static struct sfvmk_txq *
sfvmk_get_txq_by_label(struct sfvmk_evq *evq, enum sfvmk_txq_type label)
{
  unsigned int index;

//  VMK_ASSERT((evq->index == 0 && label < SFVMK_TXQ_NTYPES) ||
//      (label == SFVMK_TXQ_IP_TCP_UDP_CKSUM));

  index = (evq->index == 0) ? label : (evq->index - 1 + SFVMK_TXQ_NTYPES);
  return (evq->adapter->txq[index]);
}
#endif
static boolean_t
sfvmk_ev_tx(void *arg, uint32_t label, uint32_t id)
{

  #if 0
  sfvmk_evq *evq;
  sfvmk_txq *txq;
  unsigned int stop;
  unsigned int delta;

  evq = (struct sfvmk_evq *)arg;
  SFVMK_EVQ_LOCK_ASSERT_OWNED(evq);

  txq = sfvmk_get_txq_by_label(evq, label);

  VMK_ASSERT(txq != NULL, ("txq == NULL"));
  VMK_ASSERT(evq->index == txq->evq_index,
      ("evq->index != txq->evq_index"));

  if ((txq->init_state != SFVMK_TXQ_STARTED))
    goto done;

  stop = (id + 1) & txq->ptr_mask;
  id = txq->pending & txq->ptr_mask;

  delta = (stop >= id) ? (stop - id) : (txq->entries - id + stop);
  txq->pending += delta;

  evq->tx_done++;

  if (txq->next == NULL &&
      evq->txqs != &(txq->next)) {
    *(evq->txqs) = txq;
    evq->txqs = &(txq->next);
  }

  if (txq->pending - txq->completed >= SFVMK_TX_BATCH)
    sfvmk_tx_qcomplete(txq, evq);

done:
  return (evq->tx_done >= SFVMK_EV_BATCH);
  #endif

  return B_FALSE;
}

static boolean_t
sfvmk_ev_txq_flush_done(void *arg, uint32_t txq_index)
{
  #if 0
  sfvmk_evq *evq;
  sfvmk_adapter *adapter;
  sfvmk_txq *txq;
  uint16_t magic;

  evq = (struct sfvmk_evq *)arg;
  SFVMK_EVQ_LOCK_ASSERT_OWNED(evq);

  adapter = evq->adapter;
  txq = adapter->txq[txq_index];

  VMK_ASSERT(txq != NULL, ("txq == NULL"));
  VMK_ASSERT(txq->init_state == SFVMK_TXQ_INITIALIZED,
      ("txq not initialized"));

  if (txq->evq_index == evq->index) {
    sfvmk_txq_flush_done(txq);
    return (B_FALSE);
  }

  /* Resend a software event on the correct queue */
  evq = adapter->evq[txq->evq_index];
  magic = sfvmk_sw_ev_txq_magic(SFVMK_SW_EV_TX_QFLUSH_DONE, txq);

  VMK_ASSERT(evq->init_state == SFVMK_EVQ_STARTED,
      ("evq not started"));
  efx_ev_qpost(evq->common, magic);
#endif
  return (B_FALSE);
}

static boolean_t
sfvmk_ev_software(void *arg, uint16_t magic)
{


  return (B_FALSE);
}


static boolean_t
sfvmk_ev_sram(void *arg, uint32_t code)
{


  return (B_FALSE);
}


static boolean_t
sfvmk_ev_timer(void *arg, uint32_t index)
{
  (void)arg;
  (void)index;

  return (B_FALSE);
}

static boolean_t
sfvmk_ev_wake_up(void *arg, uint32_t index)
{
  (void)arg;
  (void)index;

  return (B_FALSE);
}



static void
sfvmk_EVQFini(sfvmk_adapter *adapter, unsigned int index);
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
void sfvmk_ev_qcomplete(sfvmk_evq *evq, boolean_t eop)
{

   #if 0
  sfvmk_adapter *adapter;
  unsigned int index;
  sfvmk_rxq *rxq;
  sfvmk_txq *txq;

//  SFVMK_EVQ_LOCK_ASSERT_OWNED(evq);

  adapter = evq->adapter;
  index = evq->index;
  rxq = (sfvmk_rxq *)adapter->rxq[index];

  if ((txq = evq->txq) != NULL) {
    evq->txq = NULL;
    evq->txqs = &(evq->txq);

    do {
      sfvmk_txq *next;

      next = txq->next;
      txq->next = NULL;

//      VMK_ASSERT(txq->evq_index == index);

      if (txq->pending != txq->completed)
               ;
    //    sfvmk_tx_qcomplete(txq, evq);

      txq = next;
    } while (txq != NULL);
  }

  if (rxq->pending != rxq->completed)
    //sfvmk_rx_qcomplete(rxq, eop);
    ;
 #endif
  return ;
}


int
sfvmk_ev_qpoll(sfvmk_evq *evq)
{

	int rc;

//	SFVMK_EVQ_LOCK(evq);

        vmk_LogMessage("calling sfvmk_ev_qpoll\n");
	if ((evq->init_state != SFVMK_EVQ_STARTING &&
			    evq->init_state != SFVMK_EVQ_STARTED)) {
        vmk_LogMessage("event queus state is %d\n", evq->init_state );
		rc = EINVAL;
		goto fail;
	}

	/* Synchronize the DMA memory for reading */

       //praveen code for syncing
	//bus_dmamap_sync(evq->mem.esm_tag, evq->mem.esm_map,
	//    BUS_DMASYNC_POSTREAD);

//	VMK_ASSERT(evq->rx_done == 0);
//	VMK_ASSERT(evq->tx_done == 0);
//	VMK_ASSERT(evq->txq == NULL);
//	VMK_ASSERT(evq->txqs == &evq->txq);

	/* Poll the queue */

	efx_ev_qpoll(evq->common, &evq->read_ptr, &sfvmk_ev_callbacks, evq);

	evq->rx_done = 0;
	evq->tx_done = 0;

	/* Perform any pending completion processing */
	sfvmk_ev_qcomplete(evq, B_TRUE);

	/* Re-prime the event queue for interrupts */
	if ((rc = efx_ev_qprime(evq->common, evq->read_ptr)) != 0)
		goto fail;


//	SFVMK_EVQ_UNLOCK(evq);
	return (0);

fail:
//	SFVMK_EVQ_UNLOCK(evq);
	return (rc);
}



static int
sfvmk_EVQinit(sfvmk_adapter *adapter, unsigned int index)
{
	struct sfvmk_evq *evq;
	efsys_mem_t *esmp;

	VMK_ASSERT(index < SFVMK_RX_SCALE_MAX);


	evq = (struct sfvmk_evq *)sfvmk_MemPoolAlloc(sizeof(sfvmk_evq));
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
	esmp->esm_handle = adapter->vmkDmaEngine; 

	
  // praveen needs to check the name 
  sfvmk_MutexInit("evq" ,VMK_MUTEX_RANK_HIGHEST - 1, &evq->lock);

	evq->init_state = SFVMK_EVQ_INITIALIZED;

	return (0);
}




int
sfvmk_EvInit(sfvmk_adapter *adapter)
{

  // praveen
	sfvmk_intr *intr;
	int index;
	int rc;

	intr = &adapter->intr;

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
		if ((rc = sfvmk_EVQinit(adapter, index)) != 0)
                 {
                     vmk_LogMessage(" failed createing evq %d\n", index);
			goto fail;
                 }
		else
                     vmk_LogMessage(" createing evq %d\n", index);
	}

	return (0);

fail:
	while (--index >= 0)
		sfvmk_EVQFini(adapter, index);

	adapter->evq_count = 0;
	return (rc);
}



static void
sfvmk_EVQFini(sfvmk_adapter *adapter, unsigned int index)
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
        
        sfvmk_MemPoolFree(evq, sizeof(sfvmk_evq));
}

void
sfvmk_EvFini(sfvmk_adapter *adapter)
{
	sfvmk_intr *intr;
	int index;

	intr = &adapter->intr;

	VMK_ASSERT(intr->state == SFVMK_INTR_INITIALIZED);

	adapter->ev_moderation = 0;

	/* Tear down the event queue(s). */
	index = adapter->evq_count;
	while (--index >= 0)
		sfvmk_EVQFini(adapter, index);

	adapter->evq_count = 0;
}
static void
sfvmk_EVQStop(sfvmk_adapter *adapter, unsigned int index)
{
  sfvmk_evq *evq;

  evq = adapter->evq[index];

//  VMK_ASSERT(evq->init_state == SFVMK_EVQ_STARTED)


  //SFVMK_EVQ_LOCK(evq);
  evq->init_state = SFVMK_EVQ_INITIALIZED;
  evq->read_ptr = 0;
  evq->exception = B_FALSE;


  efx_ev_qdestroy(evq->common);
  //SFVMK_EVQ_UNLOCK(evq);
}
void
sfvmk_EVStop(sfvmk_adapter *adapter)
{
        sfvmk_intr *intr;
        efx_nic_t *enp;
        int index;

        intr = &adapter->intr;
        enp = adapter->enp;

        //VMK_ASSERT(intr->state == SFVMK_INTR_STARTED);

        /* Stop the event queue(s) */
        index = 2;//adapter->evq_count;
        while (--index >= 0)
                sfvmk_EVQStop(adapter, index);

        /* Tear down the event module */
        efx_ev_fini(enp);
}

static int
sfvmk_EVQStart(sfvmk_adapter *adapter, unsigned int index)
{
  sfvmk_evq *evq;
  efsys_mem_t *esmp;
  int count;
  int rc;

  evq = adapter->evq[index];
  esmp = &evq->mem;

//  VMK_ASSERT(evq->init_state == SFVMK_EVQ_INITIALIZED);


  /* Clear all events. */
  (void)vmk_Memset(esmp->esm_base, 0xff, EFX_EVQ_SIZE(evq->entries));

  /* Create the common code event queue. */
  if ((rc = efx_ev_qcreate(adapter->enp, index, esmp, evq->entries,0 , adapter->ev_moderation,
                           EFX_EVQ_FLAGS_NOTIFY_INTERRUPT, &evq->common)) != 0)
    goto sfvmk_qcreate_fail;

//  SFVMK_EVQ_LOCK(evq);

  /* Prime the event queue for interrupts */
  if ((rc = efx_ev_qprime(evq->common, evq->read_ptr)) != 0)
    goto sfvmk_qprime_fail;

  evq->init_state = SFVMK_EVQ_STARTING;

  //SFVMK_EVQ_UNLOCK(evq);

  /* Wait for the initialization event */
  count = 0;
  do {
    vmk_DelayUsecs(100*1000);

    /* Check to see if the test event has been processed */
    if (evq->init_state == SFVMK_EVQ_STARTED)
      goto done;

  } while (++count < 20);
  vmk_LogMessage( "event queue is not yet started \n");

  rc = ETIMEDOUT;
  goto sfvmk_qstart_fail;

done:

  vmk_LogMessage( "event queue is started \n");
  return (0);

sfvmk_qstart_fail:
//  SFVMK_EVQ_LOCK(evq);
  evq->init_state = SFVMK_EVQ_INITIALIZED;
sfvmk_qprime_fail:
    efx_ev_qdestroy(evq->common);
sfvmk_qcreate_fail:
  return (rc);
}

int
sfvmk_EVStart(sfvmk_adapter *adapter)
{

  sfvmk_intr *intr;
  int index;
  int rc;

  intr = &adapter->intr;

//  VMK_ASSERT(intr->state == SFVMK_INTR_STARTED);

  /* Initialize the event module */
  if ((rc = efx_ev_init(adapter->enp)) != 0)
    return (rc);

  /* Start the event queues */
  // praveen initializing only one event queue
  for (index = 0; index < 2 /*adapter->evq_count*/; index++) {
    if ((rc = sfvmk_EVQStart(adapter, index)) != 0)
      goto sfvmk_qstart_fail;
    else
       vmk_NetPollEnable(adapter->evq[index]->netPoll);
  }

  return (0);

sfvmk_qstart_fail:
  vmk_LogMessage("failed to start event queue %d\n", index);
  /* Stop the event queue(s) */
  while (--index >= 0)
    sfvmk_EVQStop(adapter, index);

  /* Tear down the event module */
  efx_ev_fini(adapter->enp);

  return (rc);
}


