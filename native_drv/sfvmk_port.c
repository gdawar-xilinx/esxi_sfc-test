/*
 * Copyright (c) 2017, Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "sfvmk_driver.h"

#define SFVMK_STATS_UPDATE_WAIT_USEC  VMK_USEC_PER_MSEC

const vmk_uint32 sfvmk_linkBaudrate[EFX_LINK_NMODES] = {
  [EFX_LINK_10HDX] = VMK_LINK_SPEED_10_MBPS,
  [EFX_LINK_10FDX] = VMK_LINK_SPEED_10_MBPS,
  [EFX_LINK_100HDX] = VMK_LINK_SPEED_100_MBPS,
  [EFX_LINK_100FDX] = VMK_LINK_SPEED_100_MBPS,
  [EFX_LINK_1000HDX] = VMK_LINK_SPEED_1000_MBPS,
  [EFX_LINK_1000FDX] = VMK_LINK_SPEED_1000_MBPS,
  [EFX_LINK_10000FDX] = VMK_LINK_SPEED_10000_MBPS,
  [EFX_LINK_25000FDX] = VMK_LINK_SPEED_25000_MBPS,
  [EFX_LINK_40000FDX] = VMK_LINK_SPEED_40000_MBPS,
  [EFX_LINK_50000FDX] = VMK_LINK_SPEED_50000_MBPS,
  [EFX_LINK_100000FDX] = VMK_LINK_SPEED_100000_MBPS
};

const vmk_uint32 sfvmk_linkDuplex[EFX_LINK_NMODES] = {
  [EFX_LINK_10HDX] = VMK_LINK_DUPLEX_HALF,
  [EFX_LINK_10FDX] = VMK_LINK_DUPLEX_FULL,
  [EFX_LINK_100HDX] = VMK_LINK_DUPLEX_HALF,
  [EFX_LINK_100FDX] = VMK_LINK_DUPLEX_FULL,
  [EFX_LINK_1000HDX] = VMK_LINK_DUPLEX_HALF,
  [EFX_LINK_1000FDX] = VMK_LINK_DUPLEX_FULL,
  [EFX_LINK_10000FDX] = VMK_LINK_DUPLEX_FULL,
  [EFX_LINK_25000FDX] = VMK_LINK_DUPLEX_FULL,
  [EFX_LINK_40000FDX] = VMK_LINK_DUPLEX_FULL,
  [EFX_LINK_50000FDX] = VMK_LINK_DUPLEX_FULL,
  [EFX_LINK_100000FDX] = VMK_LINK_DUPLEX_FULL
};

#define SFVMK_PHY_CAP_ALL_SPEEDS_MASK     \
  ((1 << EFX_PHY_CAP_AN) |              \
   (1 << EFX_PHY_CAP_100000FDX) |       \
   (1 << EFX_PHY_CAP_50000FDX) |        \
   (1 << EFX_PHY_CAP_40000FDX) |        \
   (1 << EFX_PHY_CAP_25000FDX) |        \
   (1 << EFX_PHY_CAP_10000FDX) |        \
   (1 << EFX_PHY_CAP_1000FDX) |         \
   (1 << EFX_PHY_CAP_1000HDX) |         \
   (1 << EFX_PHY_CAP_100FDX) |          \
   (1 << EFX_PHY_CAP_100HDX) |          \
   (1 << EFX_PHY_CAP_10FDX) |           \
   (1 << EFX_PHY_CAP_10HDX))

/*! \brief  update the PHY link speed
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  speed    link speed
**
** \return: VMK_OK <success> error code <failure>
**          VMK_BAD_PARAM:     Wrong speed or duplex value
**          VMK_NOT_SUPPORTED: Requested capability not supported
**          VMK_FAILURE:       Other failure
*/
VMK_ReturnStatus
sfvmk_phyLinkSpeedSet(sfvmk_adapter_t *pAdapter, vmk_LinkSpeed speed)
{
  sfvmk_port_t *pPort;
  vmk_uint32 supportedCapabilities;
  vmk_uint32 advertisedCapabilities;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  VMK_ASSERT_NOT_NULL(pAdapter);

  pPort = &pAdapter->port;

  efx_phy_adv_cap_get(pAdapter->pNic, EFX_PHY_CAP_PERM,
                      &supportedCapabilities);

  efx_phy_adv_cap_get(pAdapter->pNic, EFX_PHY_CAP_CURRENT,
                      &advertisedCapabilities);

  advertisedCapabilities &= ~SFVMK_PHY_CAP_ALL_SPEEDS_MASK;

  switch (speed) {
    case VMK_LINK_SPEED_100000_MBPS:
      advertisedCapabilities |= 1 << EFX_PHY_CAP_100000FDX;
      break;

    case VMK_LINK_SPEED_50000_MBPS:
      advertisedCapabilities |= 1 << EFX_PHY_CAP_50000FDX;
      break;

    case VMK_LINK_SPEED_40000_MBPS:
      advertisedCapabilities |= 1 << EFX_PHY_CAP_40000FDX;
      break;

    case VMK_LINK_SPEED_25000_MBPS:
      advertisedCapabilities |= 1 << EFX_PHY_CAP_25000FDX;
      break;

    case VMK_LINK_SPEED_10000_MBPS:
      advertisedCapabilities |= 1 << EFX_PHY_CAP_10000FDX;
      break;

    case VMK_LINK_SPEED_1000_MBPS:
      advertisedCapabilities |= 1 << EFX_PHY_CAP_1000FDX;
      break;

    case VMK_LINK_SPEED_AUTO:
      advertisedCapabilities |= supportedCapabilities &
                                SFVMK_PHY_CAP_ALL_SPEEDS_MASK;
      break;

    default:
      status = VMK_BAD_PARAM;
      goto end;
  }

  /* Fail if no supported speeds are advertised */
  if (!(advertisedCapabilities & SFVMK_PHY_CAP_ALL_SPEEDS_MASK &
	    supportedCapabilities)) {
    status = VMK_NOT_SUPPORTED;
    goto end;
  }

  if (pPort->advertisedCapabilities == advertisedCapabilities)
    goto end;

  vmk_LogMessage("0xdeadbeaf Set the Capabilities");
  status = efx_phy_adv_cap_set(pAdapter->pNic, advertisedCapabilities);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Failed to set PHY cap with error %s",
                        vmk_StatusToString(status));
    goto end;
  }

  pPort->advertisedCapabilities = advertisedCapabilities;
  status = VMK_OK;

end:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);
  return status;
}

/*! \brief  Get PHY link speed and AutoNeg status
**
** \param[in]   pAdapter   pointer to sfvmk_adapter_t
** \param[out]  pSpeed     link speed
** \param[out]  pAutoNeg   AutoNeg On/Off
**
** \return: void
**
*/
void
sfvmk_phyLinkSpeedGet(sfvmk_adapter_t *pAdapter,
                      vmk_LinkSpeed *pSpeed, vmk_Bool *pAutoNeg)
{
  efx_nic_t *pNic;
  sfvmk_port_t *pPort;
  vmk_UplinkSharedData *pSharedData = NULL;
  vmk_uint32 advertisedCapabilities;
  vmk_uint32 sharedReadLockVer;

  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pSpeed);
  VMK_ASSERT_NOT_NULL(pAutoNeg);

  pNic = pAdapter->pNic;
  pPort = &pAdapter->port;

  pSharedData =  &pAdapter->uplink.sharedData;

  sharedReadLockVer = vmk_VersionedAtomicBeginTryRead(&pSharedData->lock);
  *pSpeed = pSharedData->link.speed;
  vmk_VersionedAtomicEndTryRead(&pSharedData->lock, sharedReadLockVer);

  advertisedCapabilities = pPort->advertisedCapabilities;

  *pAutoNeg = VMK_FALSE;

  if (advertisedCapabilities & (1 << EFX_PHY_CAP_AN))
    *pAutoNeg = VMK_TRUE;
}

/*! \brief  Helper world queue function for link update
**
** \param[in]  vmk_AddrCookie  Pointer to sfvmk_adapter_t
**
** \return: void
*/
void sfvmk_macLinkUpdateHelper(vmk_AddrCookie data)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)data.ptr;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }
  /* TODO Adapter lock will come over here */
  sfvmk_macLinkUpdate(pAdapter);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);
}

/*! \brief  Update link mode and populate it to uplink device.
**
** \param[in]  pAdapter  Pointer to sfvmk_adapter_t
** \param[in]  linkmode  LinkMode( link up/Down , Speed)
**
** \return: void
*/
void sfvmk_macLinkUpdate(sfvmk_adapter_t *pAdapter)
{
  vmk_UplinkSharedData *pSharedData = NULL;
  vmk_UplinkSharedQueueInfo *pQueueInfo = NULL;
  efx_link_mode_t linkMode;
  vmk_uint32 index = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  linkMode = pAdapter->port.linkMode;
  pSharedData =  &pAdapter->uplink.sharedData;

  sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);

  if (linkMode < EFX_LINK_NMODES) {
    pSharedData->link.speed = sfvmk_linkBaudrate[linkMode];
    pSharedData->link.duplex = sfvmk_linkDuplex[linkMode];
  }

  switch (linkMode) {
    case EFX_LINK_10HDX:
    case EFX_LINK_10FDX:
    case EFX_LINK_100HDX:
    case EFX_LINK_100FDX:
    case EFX_LINK_1000HDX:
    case EFX_LINK_1000FDX:
    case EFX_LINK_10000FDX:
    case EFX_LINK_25000FDX:
    case EFX_LINK_40000FDX:
    case EFX_LINK_50000FDX:
    case EFX_LINK_100000FDX:
      pSharedData->link.state = VMK_LINK_STATE_UP;
      break;
    case EFX_LINK_DOWN:
      pSharedData->link.state = VMK_LINK_STATE_DOWN;
      break;
    default:
      pSharedData->link.state = VMK_LINK_STATE_DOWN;
      SFVMK_ADAPTER_ERROR(pAdapter, "Unknown link mode %d", linkMode);
      break;
  }

  sfvmk_sharedAreaEndWrite(&pAdapter->uplink);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PORT, SFVMK_LOG_LEVEL_INFO,
                      "setting uplink state to %u speed: %u duplex: %u\n",
                      pSharedData->link.state,
                      pSharedData->link.speed,
                      pSharedData->link.duplex);

  /* TODO: This should not be called if admin link status is down.
   * Check will be added along with admin link status capability in uplink.
   * Right now it is safe to call this always as by default admin link status is up */
  vmk_UplinkUpdateLinkState(pAdapter->uplink.handle, &pAdapter->uplink.sharedData.link);

  pQueueInfo = &pAdapter->uplink.queueInfo;

  for (index = 0; index < pQueueInfo->maxTxQueues; index++)
    sfvmk_updateQueueStatus(pAdapter, pSharedData->link.state?
                            VMK_UPLINK_QUEUE_STATE_STARTED:
                            VMK_UPLINK_QUEUE_STATE_STOPPED, index);
done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);
}

/*! \brief Function to read the latest stats DMAed from NIC
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK  <success>
**     Below error values are returned in case of failure,
**          VMK_NOT_READY:     Port is not yet ready
**          VMK_TIMEOUT:       Stats update timed out
**          VMK_FAILURE:       Other failure
**
*/
VMK_ReturnStatus
sfvmk_macStatsUpdate(sfvmk_adapter_t *pAdapter)
{
  sfvmk_port_t *pPort;
  efsys_mem_t *pMacStatsBuf;
  unsigned int count;
  VMK_ReturnStatus status = VMK_FAILURE;

  VMK_ASSERT_NOT_NULL(pAdapter);

  pPort = &pAdapter->port;
  if (pPort->state != SFVMK_PORT_STATE_STARTED) {
    status = VMK_NOT_READY;
    goto done;
  }

  pMacStatsBuf = &pPort->macStatsDmaBuf;

  /* If we're unlucky enough to read statistics during the DMA, wait
   * up to 10ms for it to finish (typically takes <500us) */
  for (count = 0; count < 10; ++count) {
    /* Try to update the cached counters */
    status = efx_mac_stats_update(pAdapter->pNic, pMacStatsBuf,
                                  pAdapter->adapterStats, NULL);
    if (status != VMK_RETRY)
       goto done;

    status = sfvmk_worldSleep(SFVMK_STATS_UPDATE_WAIT_USEC);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_WorldSleep failed status: %s",
                          vmk_StatusToString(status));
      /* World is dying */
      goto done;
    }
  }

  status = VMK_TIMEOUT;

done:
  return status;
}

/*! \brief Fuction to submit link update request.
**
** \param[in]  Pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
VMK_ReturnStatus
sfvmk_scheduleLinkUpdate(sfvmk_adapter_t *pAdapter)
{
  vmk_HelperRequestProps props = {0};
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Create a request and submit */
  props.requestMayBlock = VMK_FALSE;
  props.tag = (vmk_AddrCookie)NULL;
  props.cancelFunc = NULL;
  props.worldToBill = VMK_INVALID_WORLD_ID;
  status = vmk_HelperSubmitRequest(pAdapter->helper,
                                   sfvmk_macLinkUpdateHelper,
                                   (vmk_AddrCookie *)pAdapter,
                                   &props);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HelperSubmitRequest failed status: %s",
                        vmk_StatusToString(status));
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);

  return status;
}

/*! \brief  Get current link state
**
** \param[in]   pAdapter    pointer to sfvmk_adapter_t
** \param[out]  pLinkState  pointer to link state
**
** \return: void
**
*/
void
sfvmk_linkStateGet(sfvmk_adapter_t *pAdapter, vmk_LinkState *pLinkState)
{
  vmk_UplinkSharedData *pSharedData = NULL;

  VMK_ASSERT_NOT_NULL(pAdapter);

  pSharedData =  &pAdapter->uplink.sharedData;

  SFVMK_SHARED_AREA_BEGIN_READ(pAdapter);
  *pLinkState = pSharedData->link.state;
  SFVMK_SHARED_AREA_END_READ(pAdapter);
}

/*! \brief  Initialize port, filters, flow control and link status.
**
** \param[in]  pAdapter     Pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success>  error code <failure>
*/
VMK_ReturnStatus
sfvmk_portStart(sfvmk_adapter_t *pAdapter)
{
  sfvmk_port_t *pPort = NULL;
  efx_nic_t *pNic = NULL;
  size_t pdu;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_FAILURE;
    goto done;
  }

  pPort = &pAdapter->port;
  pNic = pAdapter->pNic;

  if (pPort->state != SFVMK_PORT_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Port is not yet initialized");
    status = VMK_FAILURE;
    goto done;
  }

  status = efx_filter_init(pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_filter_init failed status: %s",
                        vmk_StatusToString(status));
    goto failed_filter_init;
  }

  /* Initialize the port object in the common code. */
  status = efx_port_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_port_init failed status: %s",
                        vmk_StatusToString(status));
    goto failed_port_init;
  }

  /* Set the SDU */
  pdu = EFX_MAC_PDU(pAdapter->uplink.sharedData.mtu);
  status = efx_mac_pdu_set(pNic, pdu);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_pdu_set failed status: %s",
                        vmk_StatusToString(status));
    goto failed_mac_pdu;
  }

  /* Setting flow control */
  status = efx_mac_fcntl_set(pNic, pPort->fcRequested, B_TRUE);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_fcntl failed status: %s",
                        vmk_StatusToString(status));
    goto failed_mac_fcntl;
  }

  /* By default promiscuous, all multicast and  broadcast filters are enabled */
  status = efx_mac_filter_set(pNic, B_TRUE, 0, B_TRUE, B_TRUE);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_filter_set failed status: %s",
                        vmk_StatusToString(status));
    goto failed_mac_filter_set;
  }

  /* Update MAC stats by DMA every second.  SFVMK_STATS_UPDATE_WAIT_USEC
   * is ignored by the API. Firmware always update the stats at 1 second
   * periodicity. */
  status = efx_mac_stats_periodic(pNic, &pPort->macStatsDmaBuf,
                                  SFVMK_STATS_UPDATE_WAIT_USEC, B_FALSE);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_stats_periodic failed status: %s",
                        vmk_StatusToString(status));
    goto failed_mac_stats;
  }

  status = efx_mac_drain(pNic, B_FALSE);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_drain failed status: %s",
                        vmk_StatusToString(status));
    goto failed_mac_drain;
  }

  pPort->state = SFVMK_PORT_STATE_STARTED;

  /* Single poll in case there were missing initial events */
  status = efx_port_poll(pNic, &pPort->linkMode);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_port_poll failed status: %s",
                        vmk_StatusToString(status));
    goto failed_port_poll;
  }
  /* Safe to call directly instead of through helper as portStart gets
   * called inside adapter lock */
  sfvmk_macLinkUpdate(pAdapter);

  /* Update the current PHY settings */
  efx_phy_adv_cap_get(pAdapter->pNic, EFX_PHY_CAP_CURRENT,
                      &pPort->advertisedCapabilities);

  goto done;

failed_port_poll:
failed_mac_drain:
  (void)efx_mac_stats_periodic(pNic, &pPort->macStatsDmaBuf, 0, B_FALSE);
failed_mac_stats:
failed_mac_filter_set:
failed_mac_fcntl:
failed_mac_pdu:
  efx_port_fini(pNic);

failed_port_init:
  efx_filter_fini(pNic);

failed_filter_init:

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);
  return status;
}

/*! \brief  Destory port and remove filters.
**
** \param[in]  pAdapter     Pointer to sfvmk_adapter_t
**
** \return: void
*/
void sfvmk_portStop(sfvmk_adapter_t *pAdapter)
{
  sfvmk_port_t *pPort = NULL;
  efx_nic_t *pNic = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  pPort = &pAdapter->port;
  pNic = pAdapter->pNic;

  if (pPort->state != SFVMK_PORT_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Port is not yet started");
    goto done;
  }

  pPort->state = SFVMK_PORT_STATE_INITIALIZED;

  pPort->advertisedCapabilities = 0;

  (void)efx_mac_stats_periodic(pNic, &pPort->macStatsDmaBuf, 0, B_FALSE);

  efx_mac_drain(pNic, B_TRUE);

  pPort->linkMode = EFX_LINK_UNKNOWN;

  /* Destroy the common code pPort object. */
  efx_port_fini(pNic);

  efx_filter_fini(pNic);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);

  return;
}

/*! \brief  Allocate resource for port module.
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> Error code <failure>
*/
VMK_ReturnStatus
sfvmk_portInit(sfvmk_adapter_t *pAdapter)
{
  sfvmk_port_t *pPort;
  efsys_mem_t *pMacStatsBuf;
  vmk_uint32 numMacStats;
  size_t macStatsSize;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter error");
    status = VMK_BAD_PARAM;
    goto done;
  }

  pPort = &pAdapter->port;
  if (pPort->state != SFVMK_PORT_STATE_UNINITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Port is already initialized");
    goto done;
  }

  pMacStatsBuf = &pPort->macStatsDmaBuf;

  /* Allocate memory */
  numMacStats = efx_nic_cfg_get(pAdapter->pNic)->enc_mac_stats_nstats;
  macStatsSize = P2ROUNDUP(numMacStats * sizeof(uint64_t), EFX_BUF_SIZE);

  pMacStatsBuf->esmHandle = pAdapter->dmaEngine;
  pMacStatsBuf->ioElem.length = macStatsSize;

  /* Allocate DMA space. */
  pMacStatsBuf->pEsmBase = sfvmk_allocDMAMappedMem(pAdapter->dmaEngine,
                                                   pMacStatsBuf->ioElem.length,
                                                   &pMacStatsBuf->ioElem.ioAddr);
  if(pMacStatsBuf->pEsmBase == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter,"failed to allocate DMA memory for pMacStatsBuf enteries");
    status = VMK_NO_MEMORY;
    goto done;
  }

  memset(pAdapter->adapterStats, 0, EFX_MAC_NSTATS * sizeof(uint64_t));

  pPort->state = SFVMK_PORT_STATE_INITIALIZED;
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);

  return status;
}

/*! \brief  Releases resource for port module.
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_portFini(sfvmk_adapter_t *pAdapter)
{
  sfvmk_port_t *pPort;
  efsys_mem_t *pMacStatsBuf;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter error");
    goto done;
  }

  pPort = &pAdapter->port;
  if (pPort->state != SFVMK_PORT_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Port is not initialized");
    goto done;
  }

  pMacStatsBuf = &pPort->macStatsDmaBuf;

  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine, pMacStatsBuf->pEsmBase,
                         pMacStatsBuf->ioElem.ioAddr, pMacStatsBuf->ioElem.length);

  memset(pAdapter->adapterStats, 0, sizeof(EFX_MAC_NSTATS * sizeof(uint64_t)));

  pPort->state = SFVMK_PORT_STATE_UNINITIALIZED;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);
}

