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

/* Default mtu size*/
#define SFVMK_DEFAULT_MTU 1500

/* Max number of filter supported by default RX queue.
 * HW supports total 8192 filters.
 * TODO: Using a smaller number of filters in driver as
 * supporting only single queue right now.
 * Needs to revisit during multiQ implementation.
 */
#define SFVMK_MAX_FILTER 2048

/****************************************************************************
 *               vmk_UplinkOps Handlers                                     *
 ****************************************************************************/
static VMK_ReturnStatus sfvmk_uplinkTx(vmk_AddrCookie, vmk_PktList);
static VMK_ReturnStatus sfvmk_uplinkMTUSet(vmk_AddrCookie, vmk_uint32);
static VMK_ReturnStatus sfvmk_uplinkStateSet(vmk_AddrCookie, vmk_UplinkState);
static VMK_ReturnStatus sfvmk_uplinkStatsGet(vmk_AddrCookie, vmk_UplinkStats *);
static VMK_ReturnStatus sfvmk_uplinkAssociate(vmk_AddrCookie, vmk_Uplink);
static VMK_ReturnStatus sfvmk_uplinkDisassociate(vmk_AddrCookie);
static VMK_ReturnStatus sfvmk_uplinkCapEnable(vmk_AddrCookie, vmk_UplinkCap);
static VMK_ReturnStatus sfvmk_uplinkCapDisable(vmk_AddrCookie, vmk_UplinkCap);
static VMK_ReturnStatus sfvmk_uplinkStartIO(vmk_AddrCookie);
static VMK_ReturnStatus sfvmk_uplinkQuiesceIO(vmk_AddrCookie);
static VMK_ReturnStatus sfvmk_uplinkReset(vmk_AddrCookie);

static vmk_UplinkOps sfvmkUplinkOps = {
  .uplinkTx = sfvmk_uplinkTx,
  .uplinkReset = sfvmk_uplinkReset,
  .uplinkMTUSet = sfvmk_uplinkMTUSet,
  .uplinkStateSet = sfvmk_uplinkStateSet,
  .uplinkStatsGet = sfvmk_uplinkStatsGet,
  .uplinkStartIO = sfvmk_uplinkStartIO,
  .uplinkQuiesceIO = sfvmk_uplinkQuiesceIO,
  .uplinkCapEnable = sfvmk_uplinkCapEnable,
  .uplinkCapDisable = sfvmk_uplinkCapDisable,
  .uplinkAssociate = sfvmk_uplinkAssociate,
  .uplinkDisassociate = sfvmk_uplinkDisassociate,
};

/*! \brief  Uplink callback function to associate uplink device with driver and
**          driver register its cap with uplink device.
**
** \param[in]  cookie     vmk_AddrCookie
** \param[in]  uplink     uplink device
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkAssociate(vmk_AddrCookie cookie, vmk_Uplink uplinkHandle)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if ((pAdapter == NULL) || (uplinkHandle == NULL)) {
    SFVMK_ERROR("Invalid argument(s)");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pAdapter->uplink.handle != NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Already associated with uplink");
    status = VMK_FAILURE;
    goto done;
  }

  pAdapter->uplink.handle = uplinkHandle;
  pAdapter->uplink.name = vmk_UplinkNameGet(uplinkHandle);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "%s associated",  pAdapter->uplink.name.string);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Uplink callback function to disassociate uplink device from driver.
**
** \param[in]  cookie     vmk_AddrCookie
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkDisassociate(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pAdapter->uplink.handle == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Not associated with uplink");
    status = VMK_FAILURE;
    goto done;
  }

  pAdapter->uplink.handle = NULL;
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Uplink callback function to transmit pkt
**
** \param[in]  cookie     vmk_AddrCookie
** \param[in]  pktList    List of packets to be transmitted
**
** \return: VMK_OK on success, VMK_BUSY otherwise.
**
*/
static VMK_ReturnStatus
sfvmk_uplinkTx(vmk_AddrCookie cookie, vmk_PktList pktList)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_NOT_SUPPORTED;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);
  /* TODO: Add implementation */
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Uplink callback function to set MTU
**
** \param[in]  cookie  vmk_AddrCookie
** \param[in]  mtu
**
** \return: VMK_OK <success>
** \return: VMK_FAILURE <failure>
** \return: VMK_BAD_PARAM <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkMTUSet(vmk_AddrCookie cookie, vmk_uint32 mtu)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_NOT_SUPPORTED;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);
  /* TODO: Add implementation */
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Uplink callback function to set the link status.
**         success of this API enable vmkernel to call
**         startIO uplink callback.
**
** \param[in]  cookie    vmk_AddrCookie
** \param[in]  admnState linkstate.
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkStateSet(vmk_AddrCookie cookie, vmk_UplinkState admnState)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);
  status = VMK_OK;
  /* TODO: Add implementation */
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Uplink callback function to get the NIC stats
**
** \param[in]  cookie  pointer to vmk_AddrCookie
** \param[out] pNicStats    ptr to stats
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkStatsGet(vmk_AddrCookie cookie, vmk_UplinkStats *pNicStats)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_NOT_SUPPORTED;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);
  /* TODO: Add implementation */
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Uplink callback function to enable cap
**
** \param[in]  cookie      vmk_AddrCookie
** \param[in]  uplinkCap   uplink capability to be enabled
**
* @return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkCapEnable(vmk_AddrCookie cookie, vmk_UplinkCap uplinkCap)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_NOT_SUPPORTED;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);
  /* TODO: Add implementation */
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to disable cap
**
** \param[in]  cookie     vmk_AddrCookie
** \param[in]  uplinkCap  uplink cap to be disabled
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkCapDisable(vmk_AddrCookie cookie, vmk_UplinkCap uplinkCap)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_NOT_SUPPORTED;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);
  /* TODO: Add implementation */
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to reset the adapter.
**
** \param[in]  cookie  vmk_AddrCookie
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkReset(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_NOT_SUPPORTED;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);
  /* TODO: Add implementation */
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Set the driver limit in fw.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_setDrvLimits(sfvmk_adapter_t *pAdapter)
{
  efx_drv_limits_t limits;

  vmk_Memset(&limits, 0, sizeof(limits));

  /* The resource limits configured here use the allotted resource values
   * obtained when the NIC was probed. Request fixed sizes here to ensure
   * that the allocated resources do not change across an MC reset.
   */
  limits.edl_min_evq_count = limits.edl_max_evq_count = pAdapter->numEvqsAllotted;
  limits.edl_min_txq_count = limits.edl_max_txq_count = pAdapter->numTxqsAllotted;
  limits.edl_min_rxq_count = limits.edl_max_rxq_count = pAdapter->numRxqsAllotted;

  return efx_nic_set_drv_limits(pAdapter->pNic, &limits);
}

/*! \brief uplink callback function to start the IO operations.
**
** \param[in]  cookie  pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkStartIO(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *) cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto failed_adapter_ptr;
  }

  vmk_MutexLock(pAdapter->lock);

  if (pAdapter->state == SFVMK_ADAPTER_STATE_STARTED) {
    status = VMK_FAILURE;
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter IO is already started");
    goto done;
  }

  /* Set required resource limits */
  status = sfvmk_setDrvLimits(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_setDrvLimits failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  status = efx_nic_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_init failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  status = sfvmk_intrStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_intrStart failed status: %s",
                        vmk_StatusToString(status));
    goto failed_intr_start;
  }

  status = sfvmk_evStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_evStart failed status: %s",
                        vmk_StatusToString(status));
    goto failed_ev_start;
  }

  status = sfvmk_portStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_portStart failed status: %s",
                        vmk_StatusToString(status));
    goto failed_port_start;
  }

  status = sfvmk_rxStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_rxStart failed status: %s",
                        vmk_StatusToString(status));
    goto failed_rx_start;
  }

  status = sfvmk_txStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_txStart failed status: %s",
                        vmk_StatusToString(status));
    goto failed_tx_start;
  }

  pAdapter->state = SFVMK_ADAPTER_STATE_STARTED;
  status = VMK_OK;
  goto done;

failed_tx_start:
  sfvmk_rxStop(pAdapter);

failed_rx_start:
  sfvmk_portStop(pAdapter);

failed_port_start:
  sfvmk_evStop(pAdapter);

failed_ev_start:
  sfvmk_intrStop(pAdapter);

failed_intr_start:
  efx_nic_fini(pAdapter->pNic);

done:
  vmk_MutexUnlock(pAdapter->lock);

failed_adapter_ptr:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to  Quiesce IO operations.
**
** \param[in]  cookie  pointer to vmk_AddrCookie
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkQuiesceIO(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto failed_adapter_ptr;
  }

  vmk_MutexLock(pAdapter->lock);

  if (pAdapter->state != SFVMK_ADAPTER_STATE_STARTED) {
    status = VMK_FAILURE;
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter IO is not yet started");
    goto done;
  }

  pAdapter->state = SFVMK_ADAPTER_STATE_REGISTERED;

  sfvmk_txStop(pAdapter);

  sfvmk_rxStop(pAdapter);

  sfvmk_portStop(pAdapter);

  sfvmk_evStop(pAdapter);

  status = sfvmk_intrStop(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_intrStop failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  efx_nic_fini(pAdapter->pNic);

  status = VMK_OK;

done:
  vmk_MutexUnlock(pAdapter->lock);

failed_adapter_ptr:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Update supported link modes for reporting to VMK interface
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_updateSupportedCap(sfvmk_adapter_t *pAdapter)
{
  efx_phy_cap_type_t cap;
  vmk_uint32 supportedCaps;
  vmk_uint32 index = 0;
  vmk_UplinkSupportedMode *pSupportedModes = NULL;
  vmk_LinkMediaType defaultMedia;
  efx_phy_media_type_t mediumType;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  efx_phy_media_type_get(pAdapter->pNic, &mediumType);

  switch (mediumType) {
    case EFX_PHY_MEDIA_KX4:
      defaultMedia = VMK_LINK_MEDIA_BASE_KX4;
      break;
    case EFX_PHY_MEDIA_QSFP_PLUS:
    case EFX_PHY_MEDIA_SFP_PLUS:
      defaultMedia = VMK_LINK_MEDIA_UNKNOWN;
      break;
    default:
      defaultMedia = VMK_LINK_MEDIA_UNKNOWN;
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                          "Unknown media = %d", mediumType);
      break;
  }

  efx_phy_adv_cap_get(pAdapter->pNic, EFX_PHY_CAP_DEFAULT, &supportedCaps);

  pSupportedModes = pAdapter->uplink.supportedModes;

  for (cap = EFX_PHY_CAP_10HDX; cap < EFX_PHY_CAP_NTYPES; cap++) {
    if ((supportedCaps & (1U << cap)) == 0)
      continue;

    pSupportedModes[index].media = defaultMedia;

    switch (cap) {
      case EFX_PHY_CAP_10HDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_10_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_HALF;
        break;

      case EFX_PHY_CAP_10FDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_10_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_FULL;
        break;

      case EFX_PHY_CAP_100HDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_100_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_HALF;
        break;

      case EFX_PHY_CAP_100FDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_100_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_FULL;
        break;

      case EFX_PHY_CAP_1000HDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_1000_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_HALF;
        break;

      case EFX_PHY_CAP_1000FDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_1000_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_FULL;
        break;

      case EFX_PHY_CAP_10000FDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_10000_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_FULL;
        break;

      case EFX_PHY_CAP_40000FDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_40000_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_FULL;
        break;

      default:
        SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                            "Unsupported cap = %d", cap);
        continue;
    }
    index++;
  }
  pAdapter->uplink.numSupportedModes = index;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "No of supported modes = %u", index);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

/****************************************************************************
 * Utility function to design TX/RX queue layout                            *
 ****************************************************************************/
#define SFVMK_UPLINK_RXQ_START_INDEX    0
#define SFVMK_DEFAULT_UPLINK_RXQ        SFVMK_UPLINK_RXQ_START_INDEX

/*! \brief Get number of uplink TXQs
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: Number of uplink TXQs
*/
static inline vmk_uint32 sfvmk_getNumUplinkTxq(sfvmk_adapter_t *pAdapter)
{
  return pAdapter->numTxqsAllocated;
}

/*! \brief Get number of uplink RXQs
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: Number of uplink RXQs
*/
static inline vmk_uint32 sfvmk_getNumUplinkRxq(sfvmk_adapter_t *pAdapter)
{
  return pAdapter->numRxqsAllocated;
}

/*! \brief Get number of shared uplink queues
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: Number of shared uplink queues
*/
static inline vmk_uint32 sfvmk_getNumUplinkQueues(sfvmk_adapter_t *pAdapter)
{
  return (sfvmk_getNumUplinkRxq(pAdapter) + sfvmk_getNumUplinkTxq(pAdapter));
}

/*! \brief Check if an uplink queue is already in use
**
** \param[in]  pUplink  pointer to uplink structure
** \param[in]  qIndex   uplink queue index
**
** \return: VMK_TRUE    If it is free
** \return: VMK_FALSE   Otherwise
*/
static inline vmk_Bool sfvmk_isQueueFree(sfvmk_uplink_t *pUplink, vmk_uint32 qIndex)
{
  return ((pUplink->queueInfo.queueData[qIndex].flags & VMK_UPLINK_QUEUE_FLAG_IN_USE) == 0);
}

/*! \brief Get the start index of uplink RXQs in vmk_UplinkSharedQueueData array
**
** \param[in]  pUplink  pointer to uplink structure
**
** \return: index from where RXQs is starting in vmk_UplinkSharedQueueData array
*/
static inline vmk_uint32 sfvmk_getUplinkRxqStartIndex(sfvmk_uplink_t *pUplink)
{
  return SFVMK_UPLINK_RXQ_START_INDEX;
}

/*! \brief Get the start index of uplink TXQs in vmk_UplinkSharedQueueData array
**
** \param[in]  pUplink  pointer to uplink structure
**
** \return: index from where TXQs is starting in vmk_UplinkSharedQueueData array
*/
static inline vmk_uint32 sfvmk_getUplinkTxqStartIndex(sfvmk_uplink_t *pUplink)
{
  return (SFVMK_UPLINK_RXQ_START_INDEX + pUplink->queueInfo.maxRxQueues);
}

/*! \brief Check if given uplink RXQ is a default RXQ
**
** \param[in]  pUplink  pointer to uplink structure
**
** \return: VMK_TRUE    If it is a default RXQ
** \return: VMK_FALSE   otherwise
*/
static inline vmk_Bool sfvmk_isDefaultUplinkRxq(sfvmk_uplink_t *pUplink, vmk_uint32 qIndex)
{
  return (qIndex == SFVMK_DEFAULT_UPLINK_RXQ);
}

/*! \brief Check if given uplink TXQ is a default TXQ
**
** \param[in]  pUplink  pointer to uplink structure
**
** \return: VMK_TRUE    If it is a default TXQ
** \return: VMK_FALSE   otherwise
*/
static inline vmk_Bool sfvmk_isDefaultUplinkTxq(sfvmk_uplink_t *pUplink, vmk_uint32 qIndex)
{
  return (qIndex == pUplink->queueInfo.maxRxQueues);
}

/*! \brief  The poll thread calls this callback function for polling packets.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_TRUE      <Completion is pending>
**          VMK_FALSE      <No pending completion>
*/
static vmk_Bool
sfvmk_netPollCB(void *pEvq, vmk_uint32 budget)
{
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_UPLINK);

  sfvmk_evqPoll(pEvq);

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_UPLINK);
  return VMK_FALSE;
}

/*! \brief  Create uplink RXQ
**
** \param[in]   pAdapter pointer to sfvmk_adapter_t
** \param[out]  pQid     ptr to uplink Q ID
** \param[out]  pNetPoll ptr to netpoll registered with this Q
**
** \return:  VMK_BAD_PARAM   Input arguments are not valid
** \return:  VMK_OK          Able to create uplink queue
*/
static VMK_ReturnStatus
sfvmk_createUplinkRxq(sfvmk_adapter_t *pAdapter,
                      vmk_UplinkQueueID *pQid,
                      vmk_NetPoll *pNetPoll)
{
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_uint32 queueIndex;
  vmk_uint32 flags;
  vmk_uint32 queueStartIndex;
  vmk_uint32 queueEndIndex;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  pQueueInfo = &pAdapter->uplink.queueInfo;

  queueStartIndex = sfvmk_getUplinkRxqStartIndex(&pAdapter->uplink);
  queueEndIndex = queueStartIndex + pAdapter->uplink.queueInfo.maxRxQueues - 1;

  pQueueData = &pQueueInfo->queueData[0];

  if (sfvmk_getNumUplinkQueues(pAdapter) < queueEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", queueEndIndex);
    status = VMK_FAILURE;
    goto done;
  }

  /* Check if queue is free */
  for (queueIndex = queueStartIndex; queueIndex <= queueEndIndex; queueIndex++)
    if (sfvmk_isQueueFree(&pAdapter->uplink, queueIndex))
      break;

  if (queueIndex > queueEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "All Qs are in use");
    status = VMK_FAILURE;
    goto done;
  }

  /* Make uplink queue */
  flags = VMK_UPLINK_QUEUE_FLAG_IN_USE;
  vmk_UplinkQueueMkRxQueueID(pQid, queueIndex, queueIndex);
  if (pNetPoll != NULL)
    *pNetPoll = pQueueData[queueIndex].poll;

  if (sfvmk_isDefaultUplinkRxq(&pAdapter->uplink, queueIndex))
    flags |= VMK_UPLINK_QUEUE_FLAG_DEFAULT;

  /* Setup flags and qid for the allocated queue */
  sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);
  pQueueData[queueIndex].flags &= ~(VMK_UPLINK_QUEUE_FLAG_DEFAULT |
                               VMK_UPLINK_QUEUE_FLAG_IN_USE);
  pQueueData[queueIndex].flags |= flags;
  pQueueData[queueIndex].qid = *pQid;
  sfvmk_sharedAreaEndWrite(&pAdapter->uplink);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Create uplink TXQ
**
** \param[in]   pAdapter pointer to sfvmk_adapter_t
** \param[out]  pQid     ptr to uplink Q ID
**
** \return:  VMK_BAD_PARAM   Input arguments are not valid
** \return:  VMK_OK          Able to create uplink queue
*/
static VMK_ReturnStatus
sfvmk_createUplinkTxq(sfvmk_adapter_t *pAdapter,
                      vmk_UplinkQueueID *pQid)
{
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_uint32 queueIndex;
  vmk_uint32 flags;
  vmk_uint32 queueStartIndex;
  vmk_uint32 queueEndIndex;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  pQueueInfo = &pAdapter->uplink.queueInfo;

  queueStartIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);
  queueEndIndex = queueStartIndex + pAdapter->uplink.queueInfo.maxTxQueues - 1;

  pQueueData = &pQueueInfo->queueData[0];

  if (sfvmk_getNumUplinkQueues(pAdapter) < queueEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", queueEndIndex);
    status = VMK_FAILURE;
    goto done;
  }

  /* Check if queue is free */
  for (queueIndex = queueStartIndex; queueIndex <= queueEndIndex; queueIndex++)
    if (sfvmk_isQueueFree(&pAdapter->uplink, queueIndex))
      break;

  if (queueIndex > queueEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "All Qs are in use");
    status = VMK_FAILURE;
    goto done;
  }

  /* Make uplink queue */
  flags = VMK_UPLINK_QUEUE_FLAG_IN_USE;
  vmk_UplinkQueueMkTxQueueID(pQid, queueIndex, queueIndex);

  if (sfvmk_isDefaultUplinkTxq(&pAdapter->uplink, queueIndex))
    flags |= VMK_UPLINK_QUEUE_FLAG_DEFAULT;

  /* Setup flags and qid for the allocated queue */
  sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);
  pQueueData[queueIndex].flags &= ~(VMK_UPLINK_QUEUE_FLAG_DEFAULT |
                               VMK_UPLINK_QUEUE_FLAG_IN_USE);
  pQueueData[queueIndex].flags |= flags;
  pQueueData[queueIndex].qid = *pQid;
  sfvmk_sharedAreaEndWrite(&pAdapter->uplink);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Update vmk_UplinkSharedQueueData for all uplink RXQs
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  serviceID   service to which work has to be charged
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_rxqDataInit(sfvmk_adapter_t *pAdapter, vmk_ServiceAcctID serviceID)
{
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_NetPollProperties pollProp = {0};
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_evq_t *pEvq = NULL;
  vmk_uint32 queueIndex;
  vmk_uint32 queueStartIndex;
  vmk_uint32 queueEndIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Populate qInfo information */
  pQueueInfo = &pAdapter->uplink.queueInfo;

  queueStartIndex = sfvmk_getUplinkRxqStartIndex(&pAdapter->uplink);
  queueEndIndex = queueStartIndex + sfvmk_getNumUplinkRxq(pAdapter) - 1;

  if (sfvmk_getNumUplinkQueues(pAdapter) < queueEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", queueEndIndex);
    status = VMK_FAILURE;
    goto done;
  }

  for (queueIndex = queueStartIndex; queueIndex <= queueEndIndex; queueIndex++) {
    pQueueData = &pQueueInfo->queueData[queueIndex];
    if (pQueueData == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL shared queue data ptr");
      status = VMK_FAILURE;
      goto failed_valid_queue_data;
    }

    pQueueData->type =  VMK_UPLINK_QUEUE_TYPE_RX;
    pQueueData->state = VMK_UPLINK_QUEUE_STATE_STOPPED;
    pQueueData->flags = VMK_UPLINK_QUEUE_FLAG_UNUSED;
    pQueueData->dmaEngine = pAdapter->dmaEngine;
    pQueueData->activeFilters = 0;
    pQueueData->activeFeatures = VMK_UPLINK_QUEUE_FEAT_NONE;

    if (sfvmk_isDefaultUplinkRxq(&pAdapter->uplink, queueIndex)) {
      pQueueData->maxFilters = SFVMK_MAX_FILTER;
      pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_NONE;
    } else {
      pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_PAIR;
      pQueueData->maxFilters = SFVMK_MAX_FILTER / pQueueInfo->maxRxQueues;
    }

    pEvq = pAdapter->ppEvq[queueIndex];
    if (pEvq == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL event queue ptr");
      status = VMK_BAD_PARAM;
      goto done;
    }

    /* Poll properties for creating netpoll*/
    pollProp.poll = sfvmk_netPollCB;
    pollProp.features = VMK_NETPOLL_NONE;
    pollProp.priv.ptr = pEvq;
    pollProp.deliveryCallback = NULL;

    status = vmk_NetPollCreate(&pollProp, serviceID, vmk_ModuleCurrentID,
                               &pEvq->netPoll);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_NetPollCreate(Q[%u]) failed status: %s",
                          queueIndex, vmk_StatusToString(status));
      goto failed_net_poll_create;
    }
    pQueueData->poll = pEvq->netPoll;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "Successful initialization of uplink RXQ %u", queueIndex);

  status = VMK_OK;
  goto done;

failed_net_poll_create:
failed_valid_queue_data:
  while(queueIndex > queueStartIndex) {
    vmk_NetPollDestroy(pAdapter->ppEvq[queueIndex]->netPoll);
    pAdapter->ppEvq[queueIndex]->netPoll = NULL;
    queueIndex--;
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Update vmk_UplinkSharedQueueData for all uplink TXQs
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_txqDataInit(sfvmk_adapter_t *pAdapter)
{
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 queueIndex;
  vmk_uint32 queueStartIndex;
  vmk_uint32 queueEndIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Populate qInfo information */
  pQueueInfo = &pAdapter->uplink.queueInfo;

  queueStartIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);
  queueEndIndex = queueStartIndex + sfvmk_getNumUplinkTxq(pAdapter) - 1;

  if (sfvmk_getNumUplinkQueues(pAdapter) < queueEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", queueEndIndex);
    status = VMK_FAILURE;
    goto done;
  }

  for (queueIndex = queueStartIndex; queueIndex <= queueEndIndex; queueIndex++) {
    pQueueData = &pQueueInfo->queueData[queueIndex];
    if (pQueueData == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL shared queue data ptr");
      status = VMK_FAILURE;
      goto done;
    }

    pQueueData->type =  VMK_UPLINK_QUEUE_TYPE_TX;
    pQueueData->state = VMK_UPLINK_QUEUE_STATE_STOPPED;
    pQueueData->flags = VMK_UPLINK_QUEUE_FLAG_UNUSED;
    pQueueData->dmaEngine = pAdapter->dmaEngine;
    pQueueData->activeFeatures = VMK_UPLINK_QUEUE_FEAT_NONE;
    pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_PAIR;

    /* Check if it is a default queue */
    if (sfvmk_isDefaultUplinkTxq(&pAdapter->uplink, queueIndex)) {
      pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_NONE;
    }

  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "Successful initialization of uplink TXQ %u", queueIndex);
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Deallocate resource acquired by all uplink RXQs
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t
**
** \return: void
**
*/
static void
sfvmk_rxqDataFini(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 qIndex;
  vmk_uint32 qStartIndex;
  vmk_uint32 qEndIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  qStartIndex = sfvmk_getUplinkRxqStartIndex(&pAdapter->uplink);
  qEndIndex = qStartIndex + sfvmk_getNumUplinkRxq(pAdapter);

  if (pAdapter->numEvqsAllocated < qEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", qEndIndex);
    goto done;
  }

  for (qIndex = qStartIndex; qIndex < qEndIndex; qIndex++) {
    if (pAdapter->ppEvq[qIndex] != NULL) {
      if (pAdapter->ppEvq[qIndex]->netPoll != NULL)
        vmk_NetPollDestroy(pAdapter->ppEvq[qIndex]->netPoll);
    }
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

/*! \brief Initialize shared queue info which will be used by uplink device.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_sharedQueueInfoInit(sfvmk_adapter_t *pAdapter)
{
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_ServiceAcctID serviceID;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 numSharedQueue;
  vmk_uint32 queueDataArraySize;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Populate shared queue information */
  pQueueInfo = &pAdapter->uplink.queueInfo;
  pQueueInfo->supportedQueueTypes = VMK_UPLINK_QUEUE_TYPE_TX |
                                    VMK_UPLINK_QUEUE_TYPE_RX;

  /* Populate shared filter class with MAC filter */
  pQueueInfo->supportedRxQueueFilterClasses =
                              VMK_UPLINK_QUEUE_FILTER_CLASS_MAC_ONLY;

  /* Update max TX and RX queue*/
  pQueueInfo->maxTxQueues =  sfvmk_getNumUplinkTxq(pAdapter);
  pQueueInfo->maxRxQueues =  sfvmk_getNumUplinkRxq(pAdapter);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "maxTxQs: %d maxRxQs: %d ", pQueueInfo->maxTxQueues,
                      pQueueInfo->maxRxQueues);

  numSharedQueue = pQueueInfo->maxTxQueues + pQueueInfo->maxRxQueues;
  queueDataArraySize = numSharedQueue * sizeof(vmk_UplinkSharedQueueData);

  pQueueInfo->queueData = vmk_HeapAlloc(sfvmk_modInfo.heapID, queueDataArraySize);
  if (pQueueInfo->queueData == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HeapAlloc failed");
    status = VMK_NO_MEMORY;
    goto done;
  }
  vmk_Memset(pQueueInfo->queueData, 0, queueDataArraySize);

  pQueueInfo->activeRxQueues = 0;
  pQueueInfo->activeTxQueues = 0;

  status = vmk_ServiceGetID(VMK_SERVICE_ACCT_NAME_NET, &serviceID);
  if (status != VMK_OK) {
    goto failed_get_service_id;
  }

  /* Update shared RXQData */
  status = sfvmk_rxqDataInit(pAdapter, serviceID);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_rxqDataInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_rxqdata_init;
  }

  /* Update shared TXQData */
  status = sfvmk_txqDataInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_txqDataInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_txqdata_init;
  }

  /* Create default RX uplink queue */
  status = sfvmk_createUplinkRxq(pAdapter, &pQueueInfo->defaultRxQueueID, NULL);
  if (pQueueInfo->queueData == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createUplinkRxq failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_uplink_rxq;
  }

  /* Create default TX uplink queue */
  status = sfvmk_createUplinkTxq(pAdapter, &pQueueInfo->defaultTxQueueID);
  if (pQueueInfo->queueData == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createUplinkTxq failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_uplink_txq;
  }

  goto done;

failed_create_uplink_txq:
failed_create_uplink_rxq:
failed_txqdata_init:
  sfvmk_rxqDataFini(pAdapter);

failed_rxqdata_init:
failed_get_service_id:
  vmk_HeapFree(sfvmk_modInfo.heapID, pQueueInfo->queueData);
  pQueueInfo->queueData = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Deallocate resource acquired by the shared queue
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
**
*/
static void
sfvmk_sharedQueueInfoFini(sfvmk_adapter_t *pAdapter)
{
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  /* Deallocate queueData for RXQ */
  sfvmk_rxqDataFini(pAdapter);

  vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter->uplink.queueInfo.queueData);
  pAdapter->uplink.queueInfo.queueData = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

/*! \brief  allocate the resource required for uplink device and initalize all
**          required information.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
VMK_ReturnStatus
sfvmk_uplinkDataInit(sfvmk_adapter_t * pAdapter)
{
  vmk_UplinkSharedData *pSharedData = NULL;
  vmk_UplinkRegData *pRegData = NULL;
  VMK_ReturnStatus status = VMK_BAD_PARAM;
  const efx_nic_cfg_t *pNicCfg;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pAdapter->pNic == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL NIC ptr");
    status = VMK_FAILURE;
    goto done;
  }

  /* Create lock to serialize multiple writers's access to protected sharedData area */
  status = sfvmk_createLock(pAdapter, "uplinkLock", SFVMK_SPINLOCK_RANK_UPLINK_LOCK,
                            &pAdapter->uplink.shareDataLock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_lock;
  }

  sfvmk_updateSupportedCap(pAdapter);

  /* Initialize shared data area */
  pSharedData = &pAdapter->uplink.sharedData;
  vmk_VersionedAtomicInit(&pSharedData->lock);

  pSharedData->supportedModes = pAdapter->uplink.supportedModes;
  pSharedData->supportedModesArraySz = pAdapter->uplink.numSupportedModes;

  pSharedData->link.state = VMK_LINK_STATE_DOWN;
  pSharedData->link.speed = 0;
  pSharedData->link.duplex = VMK_LINK_DUPLEX_FULL;

  pSharedData->flags = 0;
  pSharedData->state = VMK_UPLINK_STATE_ENABLED;

  pSharedData->mtu = SFVMK_DEFAULT_MTU;

  pSharedData->queueInfo = &pAdapter->uplink.queueInfo;

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_cfg_get failed");
    status = VMK_FAILURE;
    goto failed_nic_cfg_get;
  }

  vmk_Memcpy(pSharedData->macAddr, pNicCfg->enc_mac_addr, VMK_ETH_ADDR_LENGTH);
  vmk_Memcpy(pSharedData->hwMacAddr, pNicCfg->enc_mac_addr,VMK_ETH_ADDR_LENGTH);

  status = sfvmk_sharedQueueInfoInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_sharedQueueInfoInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_init_shared_qdata;
  }

  pRegData = &pAdapter->uplink.regData;
  pRegData->ops = sfvmkUplinkOps;
  pRegData->moduleID = vmk_ModuleCurrentID;
  pRegData->apiRevision = VMKAPI_REVISION;
  pRegData->sharedData = pSharedData;
  pRegData->driverData.ptr = pAdapter;

  status = VMK_OK;
  goto done;

failed_init_shared_qdata:
failed_nic_cfg_get:
  sfvmk_destroyLock(pAdapter->uplink.shareDataLock);

failed_create_lock:
done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Dealloocate resources acquired by uplink device
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
**
*/
void
sfvmk_uplinkDataFini(sfvmk_adapter_t * pAdapter)
{

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  sfvmk_sharedQueueInfoFini(pAdapter);
  sfvmk_destroyLock(pAdapter->uplink.shareDataLock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

