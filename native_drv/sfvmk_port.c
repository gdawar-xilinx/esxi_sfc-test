#include "sfvmk_driver.h"
#include "efx.h"
void sfvmk_PortStop(sfvmk_adapter *adapter)
{
	sfvmk_port *port;
	efx_nic_t *enp;

	port = &adapter->port;
	enp = adapter->enp;

//	SFVMK_PORT_LOCK(port);

//	VMK_ASSERT(port->init_state == SFVMK_PORT_STARTED);

	port->init_state = SFVMK_PORT_INITIALIZED;

//	port->mac_stats.update_time = 0;

	/* This may call MCDI */
	(void)efx_mac_drain(enp, B_TRUE);

//	(void)efx_mac_stats_periodic(enp, &port->mac_stats.dma_buf, 0, B_FALSE);

	port->link_mode = EFX_LINK_UNKNOWN;

	/* Destroy the common code port object. */
	efx_port_fini(enp);

	efx_filter_fini(enp);

//	SFVMK_PORT_UNLOCK(port);
}



int sfvmk_PortStart(sfvmk_adapter *adapter)
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
        #if 0 
	if ((rc = efx_mac_stats_periodic(enp, &port->mac_stats.dma_buf,
					 1000, B_FALSE)) != 0)
		goto fail6;
        #endif 
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

	/* Single poll in case there were missing initial events */
//	SFVMK_PORT_UNLOCK(port);
//	sfvmk_mac_poll_work(adapter, 0);

	return (0);

//fail10:
//fail9:
	(void)efx_mac_drain(enp, B_TRUE);
fail8:
//	(void)efx_mac_stats_periodic(enp, &port->mac_stats.dma_buf, 0, B_FALSE);
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




