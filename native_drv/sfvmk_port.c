
/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"
#include "sfvmk_port.h"
#include "sfvmk_utils.h"


void
sfvmk_macLinkUpdate(sfvmk_adapter_t *pAdapter, efx_link_mode_t linkMode )
{
  vmk_UplinkSharedData *pSharedData = &pAdapter->sharedData;

  pSharedData->link.speed = VMK_LINK_SPEED_10000_MBPS;
  pSharedData->link.duplex = VMK_LINK_DUPLEX_FULL;

  if (linkMode > EFX_LINK_DOWN)
    pSharedData->link.state = VMK_LINK_STATE_UP;
  else
    pSharedData->link.state = VMK_LINK_STATE_DOWN;

  vmk_LogMessage("sfvmk_mac_link_update: setting uplink state to %d speed: %d duplex: %d\n",
                  pSharedData->link.state, pSharedData->link.speed, pSharedData->link.duplex);

  vmk_UplinkUpdateLinkState(pAdapter->uplink, &pAdapter->sharedData.link);

  return;
}
#if 0 
static void
sfvmk_macPollWork(void *arg, int npending)
{
  sfvmk_adapter_t *pAdapter;
  efx_nic_t *pNic;
  sfvmk_port_t *pPort;
  efx_link_mode_t mode;

  pAdapter = (sfvmk_adapter_t *)arg;
  pNic = pAdapter->pNic;
  pPort = &pAdapter->port;

  //SFVMK_PORT_LOCK(port);

  if (VMK_UNLIKELY(pPort->initState != SFVMK_PORT_STARTED))
    goto done;

  /* This may sleep waiting for MCDI completion */
  (void)efx_port_poll(pNic, &mode);
  sfvmk_macLinkUpdate(pAdapter, mode);

  done:
  return ;
  //SFVMK_PORT_UNLOCK(port);
}
#endif



void sfvmk_portStop(sfvmk_adapter_t *pAdapter)
{
	sfvmk_port_t *pPort;
	efx_nic_t *pNic;

	pPort = &pAdapter->port;
	pNic = pAdapter->pNic;

//	SFVMK_PORT_LOCK(pPort);

	VMK_ASSERT(pPort->initState == SFVMK_PORT_STARTED);

	pPort->initState = SFVMK_PORT_INITIALIZED;

//	pPort->macStatsUpdateTime = 0;

	/* This may call MCDI */
	(void)efx_mac_drain(pNic, B_TRUE);

//	(void)efx_mac_stats_periodic(pNic, &pPort->mac_stats.dma_buf, 0, B_FALSE);

//	pPort->linkMode = EFX_LINK_UNKNOWN;

//        efx_phy_media_type_get(pAdapter->pNic, &medium_type);


	/* Destroy the common code pPort object. */
	efx_port_fini(pNic);

	efx_filter_fini(pNic);

//	SFVMK_PORT_UNLOCK(pPort);

}



int sfvmk_portStart(sfvmk_adapter_t *pAdapter)
{

//	uint8_t mac_addr[ETHER_ADDR_LEN];
	sfvmk_port_t *pPort;
	efx_nic_t *pNic;
	size_t pdu;
	int rc;
//	uint32_t phy_cap_mask;

	pPort = &pAdapter->port;
	pNic = pAdapter->pNic;

	//SFVMK_PORT_LOCK(pPort);

	VMK_ASSERT(pPort->initState == SFVMK_PORT_INITIALIZED)


	/* Initialise the required filtering */
	if ((rc = efx_filter_init(pNic)) != 0)
		goto fail_filter_init;

	/* Initialize the pPort object in the common code. */
	if ((rc = efx_port_init(pAdapter->pNic)) != 0)
		goto fail;

	/* Set the SDU */
	pdu = EFX_MAC_PDU(pAdapter->mtu);
	if ((rc = efx_mac_pdu_set(pNic, pdu)) != 0)
		goto fail2;

  // praveen setting the FC to true right now but needs to check in future
	if ((rc = efx_mac_fcntl_set(pNic, B_TRUE, B_TRUE))
			!= 0)
		goto fail3;

//	sfvmk_mac_filter_set_locked(pAdapter);

	/* Update MAC stats by DMA every second */
        #if 0
	if ((rc = efx_mac_stats_periodic(pNic, &pPort->mac_stats.dma_buf,
					 1000, B_FALSE)) != 0)
		goto fail6;
        #endif

	if ((rc = efx_mac_drain(pNic, B_FALSE)) != 0)
		goto fail8;

#if 0
	if ((rc = sfvmk_phy_cap_mask(pAdapter, pAdapter->media.ifm_cur->ifm_media,
						 &phy_cap_mask)) != 0)
		goto fail9;

	if ((rc = efx_phy_adv_cap_set(pAdapter->pNic, phy_cap_mask)) != 0)
		goto fail10;

#endif
	pPort->initState = SFVMK_PORT_STARTED;

	/* Single poll in case there were missing initial events */

	//SFVMK_PORT_UNLOCK(pPort);
	//sfvmk_macPollWork(pAdapter, 0);

	return (0);

//fail10:
//fail9:
	(void)efx_mac_drain(pNic, B_TRUE);
fail8:
//	(void)efx_mac_stats_periodic(pNic, &pPort->mac_stats.dma_buf, 0, B_FALSE);
//fail6:
//fail4:
fail3:

fail2:
	efx_port_fini(pNic);
fail:
	efx_filter_fini(pNic);
fail_filter_init:
//	SFVMK_PORT_UNLOCK(pPort);

	return (rc);
}





