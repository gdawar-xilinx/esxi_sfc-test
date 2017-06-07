
/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"
#include "sfvmk_port.h"
#include "sfvmk_utils.h"

/**-----------------------------------------------------------------------------
*
* sfvmk_macLinkUpdate --
*
* @brief  update link mode and populate it to uplink device.
*
* @param[in]  adapter   pointer to sfvmk_adapter_t
* @param[in]  linkmode  linkMode( link up/Down , Speed)
*
* @result: void
*
*-----------------------------------------------------------------------------*/
void sfvmk_macLinkUpdate(sfvmk_adapter_t *pAdapter,
                                  efx_link_mode_t linkMode )
{
  vmk_UplinkSharedData *pSharedData = &pAdapter->sharedData;

  if (NULL == pSharedData)
    return ;

  /* right now writing hardcoded values will populate later on */
  SFVMK_SHARED_AREA_BEGIN_WRITE(pAdapter)

  pSharedData->link.speed = VMK_LINK_SPEED_10000_MBPS;
  pSharedData->link.duplex = VMK_LINK_DUPLEX_FULL;

  if (linkMode > EFX_LINK_DOWN)
    pSharedData->link.state = VMK_LINK_STATE_UP;
  else
    pSharedData->link.state = VMK_LINK_STATE_DOWN;

  SFVMK_SHARED_AREA_END_WRITE(pAdapter)

  SFVMK_DBG(pAdapter, SFVMK_DBG_PORT, SFVMK_LOG_LEVEL_INFO,
            "sfvmk_mac_link_update: setting uplink state to %d"
            "speed: %d duplex: %d\n", pSharedData->link.state,
            pSharedData->link.speed, pSharedData->link.duplex);

  vmk_UplinkUpdateLinkState(pAdapter->uplink, &pAdapter->sharedData.link);

  return;
}
/**-----------------------------------------------------------------------------
*
* sfvmk_macPollWork --
*
* @brief poll mac and update the link accordingly.
*
* @param[in]  arg     pointer to sfvmk_adapter_t
*
* @result: VMK_OK on success, and lock created. Error code if otherwise.
*
*-----------------------------------------------------------------------------*/
static void sfvmk_macPollWork(void *arg)
{
  sfvmk_adapter_t *pAdapter;
  efx_nic_t *pNic;
  sfvmk_port_t *pPort;
  efx_link_mode_t mode;

  pAdapter = (sfvmk_adapter_t *)arg;
  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");

  pNic = pAdapter->pNic;
  pPort = &pAdapter->port;

  SFVMK_PORT_LOCK(pPort);

  if (VMK_UNLIKELY(pPort->initState != SFVMK_PORT_STARTED))
    goto sfvmk_done;

  /* This may sleep waiting for MCDI completion */
  (void)efx_port_poll(pNic, &mode);
  sfvmk_macLinkUpdate(pAdapter, mode);

sfvmk_done:
  SFVMK_PORT_UNLOCK(pPort);
  return ;
}
/**-----------------------------------------------------------------------------
*
* sfvmk_portStop --
*
* @brief  destory port and remove filters.
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
*
* @result: void
*
*-----------------------------------------------------------------------------*/
void sfvmk_portStop(sfvmk_adapter_t *pAdapter)
{
  sfvmk_port_t *pPort;
  efx_nic_t *pNic;

  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");
  SFVMK_DBG(pAdapter, SFVMK_DBG_PORT, SFVMK_LOG_LEVEL_FUNCTION,
                "Entered sfvmk_portStop");

  pPort = &pAdapter->port;
  pNic = pAdapter->pNic;

  SFVMK_PORT_LOCK(pPort);

  VMK_ASSERT_BUG(pPort->initState == SFVMK_PORT_STARTED);

  pPort->initState = SFVMK_PORT_INITIALIZED;

  /* This may call MCDI */
  (void)efx_mac_drain(pNic, B_TRUE);

  pPort->linkMode = EFX_LINK_UNKNOWN;

  /* Destroy the common code pPort object. */
  efx_port_fini(pNic);

  efx_filter_fini(pNic);

  SFVMK_PORT_UNLOCK(pPort);

  SFVMK_DBG(pAdapter, SFVMK_DBG_PORT, SFVMK_LOG_LEVEL_FUNCTION,
                "Exited sfvmk_portStop");

}
/**-----------------------------------------------------------------------------
*
* sfvmk_portStart --
*
* @brief  initialize port, filters , flow control and wait for link status.
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
*
* @result: void
*
*-----------------------------------------------------------------------------*/
int sfvmk_portStart(sfvmk_adapter_t *pAdapter)
{
  sfvmk_port_t *pPort;
  efx_nic_t *pNic;
  size_t pdu;
  int rc;

  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");

  SFVMK_DBG(pAdapter, SFVMK_DBG_PORT, SFVMK_LOG_LEVEL_FUNCTION,
                "Entered sfvmk_portStart");

  pPort = &pAdapter->port;
  pNic = pAdapter->pNic;

  SFVMK_PORT_LOCK(pPort);

  VMK_ASSERT_BUG(pPort->initState == SFVMK_PORT_INITIALIZED);

  /* Initialise the required filtering */
  if ((rc = efx_filter_init(pNic)) != 0)
    goto sfvmk_filter_init_fail;

  /* Initialize the pPort object in the common code. */
  if ((rc = efx_port_init(pAdapter->pNic)) != 0)
    goto sfvmk_port_init_fail;

  /* Set the SDU */
  pdu = EFX_MAC_PDU(pAdapter->mtu);
  if ((rc = efx_mac_pdu_set(pNic, pdu)) != 0)
    goto sfvmk_mac_pdu_fail;

  /* setting flow control */
  if ((rc = efx_mac_fcntl_set(pNic, pPort->fcRequested, B_TRUE)) != 0)
    goto sfvmk_mac_fcntl_fail;

  if ((rc = efx_mac_drain(pNic, B_FALSE)) != 0)
    goto sfvmk_mac_drain_fail;

  pPort->initState = SFVMK_PORT_STARTED;

  /* Single poll in case there were missing initial events */
  SFVMK_PORT_UNLOCK(pPort);
  sfvmk_macPollWork(pAdapter);

  SFVMK_DBG(pAdapter, SFVMK_DBG_PORT, SFVMK_LOG_LEVEL_FUNCTION,
                "Exited sfvmk_portStart");

  return (0);

sfvmk_mac_drain_fail:
sfvmk_mac_fcntl_fail:
sfvmk_mac_pdu_fail:
  efx_port_fini(pNic);
sfvmk_port_init_fail:
  efx_filter_fini(pNic);
sfvmk_filter_init_fail:
  SFVMK_PORT_UNLOCK(pPort);

  return (rc);
}
/**-----------------------------------------------------------------------------
*
* sfvmk_portInit --
*
* @brief  allocate resource for port module.
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
*
* @result: void
*
*-----------------------------------------------------------------------------*/
int
sfvmk_portInit(struct sfvmk_adapter_s *pAdapter)
{
  struct sfvmk_port_s *pPort;
  VMK_ReturnStatus status;

  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");

  SFVMK_DBG(pAdapter, SFVMK_DBG_PORT, SFVMK_LOG_LEVEL_FUNCTION,
                "Entered sfvmk_portInit");

  pPort = &pAdapter->port;

  VMK_ASSERT_BUG(pPort->initState == SFVMK_PORT_UNINITIALIZED,
              "Port already initialized");

  pPort->pAdapter = pAdapter;

  status = sfvmk_mutexInit("port", SFVMK_PORT_LOCK_RANK, &pPort->lock);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to create lock for port. err: %d", status);
    goto sfvmk_fail;
  }

  pPort->initState = SFVMK_PORT_INITIALIZED;

  SFVMK_DBG(pAdapter, SFVMK_DBG_PORT, SFVMK_LOG_LEVEL_FUNCTION,
                "Exited sfvmk_portInit");

sfvmk_fail:
  return status ;

}
/**-----------------------------------------------------------------------------
*
* sfvmk_portFini --
*
* @brief  releases resource for port module.
*
* @param[in]  adapter     pointer to sfvmk_adapter_t
*
* @result: void
*
*-----------------------------------------------------------------------------*/
void
sfvmk_portFini(struct sfvmk_adapter_s *pAdapter)
{
  struct sfvmk_port_s *pPort;

  VMK_ASSERT_BUG((NULL != pAdapter), " null adapter ptr");
  SFVMK_DBG(pAdapter, SFVMK_DBG_PORT, SFVMK_LOG_LEVEL_FUNCTION,
                "Entered sfvmk_portFini");

  pPort = &pAdapter->port;

  VMK_ASSERT_BUG(pPort->initState == SFVMK_PORT_INITIALIZED,
              "Port is not initialized");

  pPort->pAdapter = pAdapter;

  sfvmk_mutexDestroy(pPort->lock);

  SFVMK_DBG(pAdapter, SFVMK_DBG_PORT, SFVMK_LOG_LEVEL_FUNCTION,
                "Exited sfvmk_portFini");
}



