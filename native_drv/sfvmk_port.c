#include "sfvmk_driver.h"
#include "sfvmk_util.h"
#include "efx.h"

const uint64_t sfvmk_link_baudrate[EFX_LINK_NMODES] = {
	[EFX_LINK_10HDX]	= VMK_LINK_SPEED_10_MBPS,
	[EFX_LINK_10FDX]	= VMK_LINK_SPEED_10_MBPS,
	[EFX_LINK_100HDX]	= VMK_LINK_SPEED_100_MBPS,
	[EFX_LINK_100FDX]	= VMK_LINK_SPEED_100_MBPS,
	[EFX_LINK_1000HDX]	= VMK_LINK_SPEED_1000_MBPS,
	[EFX_LINK_1000FDX]	= VMK_LINK_SPEED_1000_MBPS,
	[EFX_LINK_10000FDX]	= VMK_LINK_SPEED_10000_MBPS,
	[EFX_LINK_40000FDX]	= VMK_LINK_SPEED_40000_MBPS
};

const uint8_t sfvmk_link_duplex[EFX_LINK_NMODES] = {
	[EFX_LINK_10HDX]	= VMK_LINK_DUPLEX_HALF,
	[EFX_LINK_10FDX]	= VMK_LINK_DUPLEX_FULL,
	[EFX_LINK_100HDX]	= VMK_LINK_DUPLEX_HALF,
	[EFX_LINK_100FDX]	= VMK_LINK_DUPLEX_FULL,
	[EFX_LINK_1000HDX]	= VMK_LINK_DUPLEX_HALF,
	[EFX_LINK_1000FDX]	= VMK_LINK_DUPLEX_FULL,
	[EFX_LINK_10000FDX]	= VMK_LINK_DUPLEX_FULL,
	[EFX_LINK_40000FDX]	= VMK_LINK_DUPLEX_FULL,
};



void
sfvmk_mac_link_update(sfvmk_adapter *adapter,efx_link_mode_t link_mode )
{
   vmk_UplinkSharedData *sharedData = &adapter->sharedData;

   sharedData->link.speed = sfvmk_link_baudrate[link_mode];
   sharedData->link.duplex = sfvmk_link_duplex[link_mode];

    if (link_mode > EFX_LINK_DOWN)
      sharedData->link.state = VMK_LINK_STATE_UP;
    else
      sharedData->link.state = VMK_LINK_STATE_DOWN;
  
    vmk_LogMessage("sfvmk_mac_link_update: setting uplink state to %d speed: %d duplex: %d\n", 
		sharedData->link.state, sharedData->link.speed, sharedData->link.duplex);
   
    vmk_UplinkUpdateLinkState(adapter->uplink, &adapter->sharedData.link);

   return;
}

VMK_ReturnStatus sfvmk_MacStatsLock(sfvmk_adapter *adapter)
{
  sfvmk_port *port = &adapter->port;
  efsys_lock_t *mac_stats_lock = &port->lock;
  VMK_ReturnStatus rc;

  rc = vmk_SpinlockLock(mac_stats_lock->lock);
  return rc;
}

void sfvmk_MacStatsUnlock(sfvmk_adapter *adapter)
{
  sfvmk_port *port = &adapter->port;
  efsys_lock_t *mac_stats_lock = &port->lock;

  vmk_SpinlockUnlock(mac_stats_lock->lock);
}

int
sfvmk_MacStatsUpdate(sfvmk_adapter *adapter)
{
  sfvmk_port *port = &adapter->port;
  efsys_mem_t *esmp = &(port->mac_stats.dma_buf);
  unsigned int count;
  int rc;

  if (port->init_state != SFVMK_PORT_STARTED) {
    rc = 0;
    goto out;
  }

  /* If we're unlucky enough to read statistics wduring the DMA, wait
   * up to 10ms for it to finish (typically takes <500us) */
  for (count = 0; count < 5; ++count) {
    EFSYS_PROBE1(wait, unsigned int, count);

    /* Try to update the cached counters */
    if ((rc = efx_mac_stats_update(adapter->enp, esmp,
       port->mac_stats.decode_buf, NULL)) != EAGAIN)
        goto out;

      vmk_DelayUsecs(10000);
  }

  rc = ETIMEDOUT;
out:
  return (rc);
}

static void
sfvmk_mac_poll_work(void *arg, int npending)
{
	sfvmk_adapter *adapter;
	efx_nic_t *enp;
	struct sfvmk_port *port;
	efx_link_mode_t mode;

       adapter = (sfvmk_adapter *)arg;
	enp = adapter->enp;
	port = &adapter->port;

//	SFXGE_PORT_LOCK(port);

	if (VMK_UNLIKELY(port->init_state != SFVMK_PORT_STARTED))
		goto done;

	/* This may sleep waiting for MCDI completion */
	(void)efx_port_poll(enp, &mode);
	sfvmk_mac_link_update(adapter, mode);

done:
  return ; 
//	SFXGE_PORT_UNLOCK(port);
}


int sfvmk_port_start(sfvmk_adapter *adapter)
{
//	uint8_t mac_addr[ETHER_ADDR_LEN];
	sfvmk_port *port;
	efx_nic_t *enp;
	size_t pdu;
	int rc;
//	uint32_t phy_cap_mask;

	port = &adapter->port;
	enp = adapter->enp;

//	SFXGE_PORT_LOCK(port);

//	VMK_ASSERT(port->init_state == SFVMK_PORT_INITIALIZED)


	/* Initialise the required filtering */
	if ((rc = efx_filter_init(enp)) != 0)
		goto fail_filter_init;

	/* Initialize the port object in the common code. */
	if ((rc = efx_port_init(adapter->enp)) != 0)
		goto fail;

	/* Set the SDU */
	pdu = EFX_MAC_PDU(adapter->mtu);
	if ((rc = efx_mac_pdu_set(enp, pdu)) != 0)
		goto fail2;


        // praveen setting the FC to true right now but needs to check in future 
	if ((rc = efx_mac_fcntl_set(enp, B_TRUE, B_TRUE))
			!= 0)
		goto fail3;

	//sfvmk_mac_filter_set_locked(adapter);

	/* Update MAC stats by DMA every second */
	if ((rc = efx_mac_stats_periodic(enp, &port->mac_stats.dma_buf,
					 1000, B_FALSE)) != 0)
		goto fail3;

	if ((rc = efx_mac_drain(enp, B_FALSE)) != 0)
		goto fail8;

#if 0 
	if ((rc = sfvmk_phy_cap_mask(adapter, adapter->media.ifm_cur->ifm_media,
						 &phy_cap_mask)) != 0)
		goto fail9;

	if ((rc = efx_phy_adv_cap_set(adapter->enp, phy_cap_mask)) != 0)
		goto fail10;

#endif 
	port->init_state = SFVMK_PORT_STARTED;
        efx_mac_filter_set(adapter->enp, 1,0, 0 , 1);
	/* Single poll in case there were missing initial events */
//	SFVMK_PORT_UNLOCK(port);
	sfvmk_mac_poll_work(adapter, 0);

	return (0);

//fail10:
//fail9:
	(void)efx_mac_drain(enp, B_TRUE);
fail8:
	(void)efx_mac_stats_periodic(enp, &port->mac_stats.dma_buf, 0, B_FALSE);
//fail6:
//fail4:
fail3:

fail2:
	efx_port_fini(enp);
fail:
	efx_filter_fini(enp);
fail_filter_init:
//	SFVMK_PORT_UNLOCK(port);

	return (rc);
}

int
sfvmk_PortInit(sfvmk_adapter *adapter)
{
  sfvmk_port *port;
  efsys_lock_t *mac_stats_lock; 
  efsys_mem_t *mac_stats_buf;

  port = &adapter->port;
  mac_stats_buf = &port->mac_stats.dma_buf;

	//KASSERT(port->init_state == SFXGE_PORT_UNINITIALIZED,
	//    ("Port already initialized"));

  port->sc = adapter;

  mac_stats_lock = &port->lock; 
  sfvmk_CreateLock("mac_stats", VMK_SPINLOCK_RANK_HIGHEST-3, &mac_stats_lock->lock);

#ifdef SFXGE_HAVE_PAUSE_MEDIAOPTS
  /* If flow control cannot be configured or reported through
   * ifmedia, provide sysctls for it. */
  port->wanted_fc = EFX_FCNTL_RESPOND | EFX_FCNTL_GENERATE;
#endif

  port->mac_stats.decode_buf = sfvmk_memPoolAlloc(EFX_MAC_NSTATS * sizeof(uint64_t));

  /* Allocate DMA space. */
  mac_stats_buf->esm_base = sfvmk_AllocCoherentDMAMapping(adapter, EFX_MAC_STATS_SIZE, &mac_stats_buf->io_elem.ioAddr);
  mac_stats_buf->io_elem.length = EFX_MAC_STATS_SIZE;
  mac_stats_buf->esm_handle = adapter->vmkDmaEngine;

  //TODO
  //port->stats_update_period_ms = sfxge_port_stats_update_period_ms(sc);
  //sfxge_mac_stat_init(sc);
  memset(adapter->mac_stats, 0, sizeof(EFX_MAC_NSTATS * sizeof(uint64_t)));
  port->init_state = SFVMK_PORT_INITIALIZED;

  return (0);
#if 0
fail:
	free(port->phy_stats.decode_buf, M_SFXGE);
	SFXGE_PORT_LOCK_DESTROY(port);
	port->sc = NULL;
	DBGPRINT(sc->dev, "failed %d", rc);
	return (rc);
#endif
}


