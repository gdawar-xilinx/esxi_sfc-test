/*
* Copyright (c) 2017, Solarflare Communications Inc.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "sfvmk_driver.h"

const vmk_uint32 sfvmk_linkBaudrate[EFX_LINK_NMODES] = {
  [EFX_LINK_10HDX] = VMK_LINK_SPEED_10_MBPS,
  [EFX_LINK_10FDX] = VMK_LINK_SPEED_10_MBPS,
  [EFX_LINK_100HDX] = VMK_LINK_SPEED_100_MBPS,
  [EFX_LINK_100FDX] = VMK_LINK_SPEED_100_MBPS,
  [EFX_LINK_1000HDX] = VMK_LINK_SPEED_1000_MBPS,
  [EFX_LINK_1000FDX] = VMK_LINK_SPEED_1000_MBPS,
  [EFX_LINK_10000FDX] = VMK_LINK_SPEED_10000_MBPS,
  [EFX_LINK_40000FDX] = VMK_LINK_SPEED_40000_MBPS
};

const vmk_uint32 sfvmk_linkDuplex[EFX_LINK_NMODES] = {
  [EFX_LINK_10HDX] = VMK_LINK_DUPLEX_HALF,
  [EFX_LINK_10FDX] = VMK_LINK_DUPLEX_FULL,
  [EFX_LINK_100HDX] = VMK_LINK_DUPLEX_HALF,
  [EFX_LINK_100FDX] = VMK_LINK_DUPLEX_FULL,
  [EFX_LINK_1000HDX] = VMK_LINK_DUPLEX_HALF,
  [EFX_LINK_1000FDX] = VMK_LINK_DUPLEX_FULL,
  [EFX_LINK_10000FDX] = VMK_LINK_DUPLEX_FULL,
  [EFX_LINK_40000FDX] = VMK_LINK_DUPLEX_FULL,
};

/*! \brief  Update link mode and populate it to uplink device.
**
** \param[in]  pAdapter  Pointer to sfvmk_adapter_t
** \param[in]  linkmode  LinkMode( link up/Down , Speed)
**
** \return: void
*/
void sfvmk_macLinkUpdate(sfvmk_adapter_t *pAdapter,
                         efx_link_mode_t linkMode)
{
  vmk_UplinkSharedData *pSharedData = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  pSharedData =  &pAdapter->uplink.sharedData;
  sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);

  pSharedData->link.speed = sfvmk_linkBaudrate[linkMode];
  pSharedData->link.duplex = sfvmk_linkDuplex[linkMode];

  switch (linkMode) {
    case EFX_LINK_10HDX:
    case EFX_LINK_10FDX:
    case EFX_LINK_100HDX:
    case EFX_LINK_100FDX:
    case EFX_LINK_1000HDX:
    case EFX_LINK_1000FDX:
    case EFX_LINK_10000FDX:
    case EFX_LINK_40000FDX:
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

  pAdapter->port.linkMode = linkMode;
  /* TODO: This should not be called if admin link status is down.
   * Check will be added along with admin link status capability in uplink.
   * Right now it is safe to call this always as by default admin link status is up */
  vmk_UplinkUpdateLinkState(pAdapter->uplink.handle, &pAdapter->uplink.sharedData.link);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);

  return;
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
  efx_link_mode_t mode;
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

  vmk_SpinlockLock(pPort->lock);

  if (pPort->state != SFVMK_PORT_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Port is not yet initialized");
    status = VMK_FAILURE;
    goto done;
  }

  status = efx_filter_init(pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_filter_init failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto failed_filter_init;
  }

  /* Initialize the port object in the common code. */
  status = efx_port_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_port_init failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto failed_port_init;
  }

  /* Set the SDU */
  pdu = EFX_MAC_PDU(pAdapter->uplink.sharedData.mtu);
  status = efx_mac_pdu_set(pNic, pdu);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_pdu_set failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto failed_mac_pdu;
  }

  /* Setting flow control */
  status = efx_mac_fcntl_set(pNic, pPort->fcRequested, B_TRUE);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_fcntl failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto failed_mac_fcntl;
  }

  /* By default promiscuous, all multicast and  broadcast filters are enabled */
  status = efx_mac_filter_set(pNic, B_TRUE, 0, B_TRUE, B_TRUE);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_filter_set failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto failed_mac_filter_set;
  }

  status = efx_mac_drain(pNic, B_FALSE);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_drain failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto failed_mac_drain;
  }

  pPort->state = SFVMK_PORT_STATE_STARTED;

  vmk_SpinlockUnlock(pPort->lock);

  /* Single poll in case there were missing initial events */
  efx_port_poll(pNic, &mode);
  sfvmk_macLinkUpdate(pAdapter, mode);

  goto done;

failed_mac_drain:
failed_mac_filter_set:
failed_mac_fcntl:
failed_mac_pdu:
  efx_port_fini(pNic);

failed_port_init:
  efx_filter_fini(pNic);

failed_filter_init:
  vmk_SpinlockUnlock(pPort->lock);

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

  vmk_SpinlockLock(pPort->lock);

  if (pPort->state != SFVMK_PORT_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Port is not yet started");
    goto unlock_spinlock;
  }

  pPort->state = SFVMK_PORT_STATE_INITIALIZED;

  efx_mac_drain(pNic, B_TRUE);

  pPort->linkMode = EFX_LINK_UNKNOWN;

  /* Destroy the common code pPort object. */
  efx_port_fini(pNic);

  efx_filter_fini(pNic);

unlock_spinlock:
  vmk_SpinlockUnlock(pPort->lock);

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

  status = sfvmk_createLock(pAdapter, "portLock",
                            SFVMK_SPINLOCK_RANK_PORT_LOCK,
                            &pPort->lock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  pPort->state = SFVMK_PORT_STATE_INITIALIZED;

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

  sfvmk_destroyLock(pPort->lock);
  pPort->state = SFVMK_PORT_STATE_UNINITIALIZED;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);
}

