/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

/* default mtu size*/
#define SFVMK_DEFAULT_MTU 1500
/* hardcoded max filter needs to revisit when implement multiQ */
#define SFVMK_MAX_FILTER_PER_QUEUE 10

/****************************************************************************
*                Local Functions                                            *
****************************************************************************/
static VMK_ReturnStatus sfvmk_initSharedQueueInfo(sfvmk_adapter_t *pAdapter);

static VMK_ReturnStatus sfvmk_initRxqData(sfvmk_adapter_t *pAdapter,
                           vmk_uint32 qIndex, vmk_ServiceAcctID serviceID);

static VMK_ReturnStatus sfvmk_initTxqData(sfvmk_adapter_t *pAdapter,
                                            vmk_uint32 qIndex);

static VMK_ReturnStatus sfvmk_allocQ(sfvmk_adapter_t *pAdapter,
                                    vmk_UplinkQueueFeature feat,
                                    vmk_UplinkQueueID *pQid,
                                    vmk_UplinkQueueType qType,
                                    vmk_NetPoll *pNetpoll);

static int sfvmk_setDrvLimits( sfvmk_adapter_t *pAdapter);
static void sfvmk_updateCableType(sfvmk_adapter_t *adapter);

/****************************************************************************
*                vmk_UplinkMessageLevelOps Handler                          *
****************************************************************************/
static VMK_ReturnStatus sfvmk_messageLevelGet(vmk_AddrCookie cookie,
                                              vmk_uint32 *level);

static VMK_ReturnStatus sfvmk_messageLevelSet(vmk_AddrCookie cookie,
                                                  vmk_uint32 level);

static vmk_UplinkMessageLevelOps sfvmkMessageLevelOps = {
  .getMessageLevel = sfvmk_messageLevelGet,
  .setMessageLevel = sfvmk_messageLevelSet,
};

/****************************************************************************
*              vmk_UplinkRingParamsOps Handler                              *
****************************************************************************/
static VMK_ReturnStatus sfvmk_ringParamsGet(vmk_AddrCookie cookie,
                                          vmk_UplinkRingParams *params);

static VMK_ReturnStatus sfvmk_ringParamsSet(vmk_AddrCookie cookie,
                                           vmk_UplinkRingParams *params);

struct vmk_UplinkRingParamsOps sfvmkRingParamsOps = {
  .ringParamsGet = sfvmk_ringParamsGet,
  .ringParamsSet = sfvmk_ringParamsSet,
};

/****************************************************************************
*                vmk_UplinkCableTypeOps Handler                             *
****************************************************************************/
static VMK_ReturnStatus sfvmk_getCableType(vmk_AddrCookie cookie,
                                         vmk_UplinkCableType *cableType);

static VMK_ReturnStatus sfvmk_getSupportedCableTypes(vmk_AddrCookie cookie,
                                           vmk_UplinkCableType *cableType);

static VMK_ReturnStatus sfvmk_setCableType(vmk_AddrCookie cookie,
                                         vmk_UplinkCableType cableType);

struct vmk_UplinkCableTypeOps sfvmkCableTypeOps = {
  .getCableType = sfvmk_getCableType,
  .getSupportedCableTypes = sfvmk_getSupportedCableTypes,
  .setCableType = sfvmk_setCableType,
};

/****************************************************************************
 *               vmk_UplinkPrivStatsOps Handlers                            *
 ****************************************************************************/
static VMK_ReturnStatus sfvmk_privStatsLengthGet(vmk_AddrCookie cookie,
                                                  vmk_ByteCount *length);
static VMK_ReturnStatus sfvmk_privStatsGet(vmk_AddrCookie cookie,
                                            char *statBuf,
                                            vmk_ByteCount length);

static vmk_UplinkPrivStatsOps sfvmkPrivStatsOps = {
   .privStatsLengthGet     = sfvmk_privStatsLengthGet,
   .privStatsGet           = sfvmk_privStatsGet,
};


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
  .uplinkMTUSet = sfvmk_uplinkMTUSet,
  .uplinkStateSet = sfvmk_uplinkStateSet,
  .uplinkStatsGet = sfvmk_uplinkStatsGet,
  .uplinkAssociate = sfvmk_uplinkAssociate,
  .uplinkDisassociate = sfvmk_uplinkDisassociate,
  .uplinkCapEnable = sfvmk_uplinkCapEnable,
  .uplinkCapDisable = sfvmk_uplinkCapDisable,
  .uplinkStartIO = sfvmk_uplinkStartIO,
  .uplinkQuiesceIO = sfvmk_uplinkQuiesceIO,
  .uplinkReset = sfvmk_uplinkReset,
};

/*! \brief uplink callback function to retrieve log level.
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[out]  level    log level
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus sfvmk_messageLevelGet(vmk_AddrCookie cookie,
                                              vmk_uint32 *level)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  *level = vmk_LogGetCurrentLogLevel(sfvmk_ModInfo.logID);

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG, "level: 0x%x", *level);

  return VMK_OK;
}

/*! \brief uplink callback function to set log level.
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[in]  level    log level
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus sfvmk_messageLevelSet(vmk_AddrCookie cookie,
                                              vmk_uint32 level)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
      "Seting level to 0x%x ",vmk_LogGetCurrentLogLevel(sfvmk_ModInfo.logID));

  vmk_LogSetCurrentLogLevel(sfvmk_ModInfo.logID , level);

  return VMK_OK;
}

/*! \brief uplink callback function to get ring params
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[out]  params   ring params
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus sfvmk_ringParamsGet(vmk_AddrCookie cookie,
                                          vmk_UplinkRingParams *params)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  /* rxPending and txPending denotes currently configured enteries
  *  rxMaxPending and txMaxPending denotes max number of enteries
  *  supported by hw */
  params->txPending = params->txMaxPending = sfvmk_txRingEntries;
  params->rxMaxPending = params->rxPending = sfvmk_rxRingEntries;

  /* assuming fw does not support this */
  /* TODO: needs to revisit if fw support this */
  params->rxMiniMaxPending = 0;
  params->rxJumboMaxPending = 0;
  params->rxMiniPending = 0;
  params->rxJumboPending = 0;

  return VMK_OK;
}

/*! \brief uplink callback function to set ring params
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[out]  params   ring params
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus sfvmk_ringParamsSet(vmk_AddrCookie cookie,
                                           vmk_UplinkRingParams *params)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_ERR(pAdapter, "RingParamsSet is not supported");

  return VMK_NOT_SUPPORTED;
}

/*! \brief uplink callback function to get the cable type
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[out]  cableType  ptr to cable type
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus sfvmk_getCableType(vmk_AddrCookie cookie,
                                          vmk_UplinkCableType *cableType)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  *cableType = pAdapter->phy.cableType;

  return VMK_OK;
}

/*! \brief uplink callback function to set the cable type
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[out]  cableType  cable type
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus sfvmk_setCableType(vmk_AddrCookie cookie,
                                            vmk_UplinkCableType cableType)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_ERR(pAdapter, "setCableType is not supported");

  return VMK_NOT_SUPPORTED;
}

/*! \brief uplink callback function to get all cable type supported by interface.
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[out]  cableType  ptr to cable type
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus sfvmk_getSupportedCableTypes(vmk_AddrCookie cookie,
                                              vmk_UplinkCableType *cableType)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  /* TODO :  will revisit to return all cable type supported */
  *cableType = pAdapter->phy.cableType;

  return VMK_OK;
}

/*! \brief uplink callback function to set the link status
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[out]  linkstatus  ptr to link status
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus sfvmk_linkStatusSet(vmk_AddrCookie cookie,
                                          vmk_LinkStatus *linkStatus)
{

  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);

  /* TODO : need to implement */

  return VMK_OK;
}

/*! \brief function to register driver cap with uplink device.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus sfvmk_registerIOCaps(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);

  /* Driver supports scatter-gather transmit */
  status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_SG_TX, NULL);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "SG_TX cap register failed with error %s",
              vmk_StatusToString(status));
    VMK_ASSERT(0);
  }

  /* Driver supports scatter-gather entries spanning multiple pages */
  status = vmk_UplinkCapRegister(pAdapter->uplink,
                                VMK_UPLINK_CAP_MULTI_PAGE_SG, NULL);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter,"MULTI_PAGE_SG cap register failed with error %s",
                vmk_StatusToString(status));
    VMK_ASSERT(0);
  }
  /* TODO : following caps will be enabled in subsequent code drop */
#if 0
  /* Driver supports IPv4 TCP and UDP checksum offload */
  status = vmk_UplinkCapRegister(pAdapter->uplink,
                                VMK_UPLINK_CAP_IPV4_CSO, NULL);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter,"IPv4_CSO cap register failed with error 0x%x", status);
    VMK_ASSERT(0);
  }

  /* Driver supports IPv6 TCP and UDP checksum offload */
  status = vmk_UplinkCapRegister(pAdapter->uplink,
                                VMK_UPLINK_CAP_IPV6_CSO, NULL);
  if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
    SFVMK_ERR(pAdapter, "IPv6_CSO cap register failed with error 0x%x", status);
    VMK_ASSERT(0);
  }

  /* Driver supports IPv4 TCP segmentation offload (TSO) */
  status = vmk_UplinkCapRegister(pAdapter->uplink,
                                VMK_UPLINK_CAP_IPV4_TSO, NULL);
  if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
    SFVMK_ERR(pAdapter, "IPv4_TSO cap register failed with error 0x%x", status);
    VMK_ASSERT(0);
  }

  /* Driver supports IPv6 TCP segmentation offload (TSO) */
  status = vmk_UplinkCapRegister(pAdapter->uplink,
                                  VMK_UPLINK_CAP_IPV6_TSO, NULL);
  if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
    SFVMK_ERR(pAdapter,"IPv6_TSO cap register failed with error 0x%x", status);
    VMK_ASSERT(0);
  }

#endif
  /* Driver supports VLAN RX offload (tag stripping) */
  status = vmk_UplinkCapRegister(pAdapter->uplink,
                                  VMK_UPLINK_CAP_VLAN_RX_STRIP,  NULL);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter,"VLAN_RX_STRIP cap register failed with error 0x%x",
              status);
    VMK_ASSERT(0);
  }

  /* Driver supports VLAN TX Offload (tag insertion) */
  status = vmk_UplinkCapRegister(pAdapter->uplink,
                                VMK_UPLINK_CAP_VLAN_TX_INSERT, NULL);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "VLAN_TX_INSERT cap register failed with error 0x%x",
              status);
    VMK_ASSERT(0);
  }

  /* Driver supports getting and setting message level */
  status = vmk_UplinkCapRegister(pAdapter->uplink,
                                VMK_UPLINK_CAP_MESSAGE_LEVEL,
                                &sfvmkMessageLevelOps);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Message Level cap register failed with error %s ",
             vmk_StatusToString(status));
    VMK_ASSERT(0);
  }

  /* Driver supports getting and setting RX/TX ring params */
  status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_RING_PARAMS,
                                  &sfvmkRingParamsOps);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Ring Parameters cap register failed with error %s",
              vmk_StatusToString(status));
    VMK_ASSERT(0);
  }

  /* Driver supports changing link status */
  status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_LINK_STATUS_SET,
                                &sfvmk_linkStatusSet);
   if (status != VMK_OK) {
     SFVMK_ERR(pAdapter, "link status cap register failed with error %s",
               vmk_StatusToString(status));
     VMK_ASSERT(0);
   }

  /* Driver supports getting and setting cable type */
   status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_CABLE_TYPE,
                                  &sfvmkCableTypeOps);
   if (status != VMK_OK) {
      SFVMK_ERR(pAdapter,"Cable Type cap register failed with error %s",
                vmk_StatusToString(status));
      VMK_ASSERT(0);
   }

   /* Register private stats capability */
   status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_PRIV_STATS,
                                  &sfvmkPrivStatsOps);
   if (status != VMK_OK) {
      SFVMK_ERR(pAdapter,
                "PRIV_STATS cap register failed with error 0x%x", status);
      VMK_ASSERT(0);
   }
   SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK);

  return (status);
}

/*! \brief  uplink callback function to associate uplink device with driver and
**         driver register its cap with uplink device.
**
** \param[in]  adapter   pointer to sfvmk_adapter_t
** \param[in]  uplink    uplink device
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus sfvmk_uplinkAssociate(vmk_AddrCookie cookie,
                                               vmk_Uplink uplink)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ASSERT(uplink != NULL);
  SFVMK_NULL_PTR_CHECK(pAdapter);
  VMK_ASSERT(pAdapter->uplink == NULL);

  pAdapter->uplink = uplink;
  pAdapter->uplinkName = vmk_UplinkNameGet(pAdapter->uplink);
  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
          "%s Associted",  pAdapter->uplinkName.string);

  return sfvmk_registerIOCaps(pAdapter);
}

/*! \brief uplink call back function to disassociate uplink device from driver.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkDisassociate(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  VMK_ASSERT_BUG(pAdapter->uplink != NULL);
  pAdapter->uplink = NULL;

  return VMK_OK;
}

/*! \brief function to check if the transmit queue is stopped
**
** \param[in]  adapter pointer to sfvmk_adapter_t
** \param[in]  txq     transmit queue id
**
** \return: VMK_TRUE if txq is stopped, VMK_FALSE otherwise.
**
*/
static vmk_Bool
sfvmk_isTxqStopped(struct sfvmk_adapter_s * pAdapter, vmk_uint32 txq)
{
  vmk_UplinkSharedQueueData *pTxqData;

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);
  /* Get shared data */
  pTxqData = SFVMK_GET_TX_SHARED_QUEUE_DATA(pAdapter);

  if (pTxqData[txq].state == VMK_UPLINK_QUEUE_STATE_STOPPED) {
   SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK);
   return VMK_TRUE;
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK);
  return VMK_FALSE;
}

/*! \brief uplink callback function to transmit pkt
**
** \param[in]  adapter pointer to sfvmk_adapter_t
** \param[in]  pktList List of packets to be transmitted
**
** \return: VMK_OK on success, VMK_BUSY otherwise.
**
*/
static VMK_ReturnStatus
sfvmk_uplinkTx(vmk_AddrCookie cookie, vmk_PktList pktList)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);

  vmk_PktHandle *pkt;
  vmk_UplinkQueueID uplinkQid;
  vmk_uint32 qid;
  vmk_ByteCountSmall pktLen;
  vmk_int16 maxRxQueues;
  vmk_int16 maxTxQueues;

  maxRxQueues = pAdapter->queueInfo.maxRxQueues;
  maxTxQueues = pAdapter->queueInfo.maxTxQueues;

  /* retrieve the qid from first packet */
  pkt = vmk_PktListGetFirstPkt(pktList);
  uplinkQid = vmk_PktQueueIDGet(pkt);
  qid = vmk_UplinkQueueIDVal(uplinkQid);

  if ((!qid) || (qid >= (maxTxQueues + maxRxQueues))) {
   SFVMK_ERR(pAdapter,"Invalid QID %d, numTxQueue %d,  maxRxQueues: %d, device %s",
			   qid,
			   maxTxQueues,
			   maxRxQueues,
			   vmk_NameToString(&pAdapter->uplinkName));

	/* TODO: need to see the pkt completion context and use appropriate function call */
   vmk_PktListReleaseAllPkts(pktList);
   SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK);
	return VMK_OK;
  }

  if (VMK_UNLIKELY(sfvmk_isTxqStopped(pAdapter, qid))) {
   SFVMK_ERR(pAdapter, "sfvmk_isTxqStopped returned TRUE");
   /* TODO: need to see the pkt completion context and use appropriate function call */
   vmk_PktListReleaseAllPkts(pktList);
   SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK);
   return VMK_BUSY;
  }

  /* cross over the rx queues */
  qid -= maxRxQueues;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
            "RxQ: %d, TxQ: %d, qid: %d", maxRxQueues, maxTxQueues, qid);

  VMK_PKTLIST_ITER_STACK_DEF(iter);
  for (vmk_PktListIterStart(iter, pktList); !vmk_PktListIterIsAtEnd(iter);) {
   vmk_PktListIterRemovePkt(iter, &pkt);
   VMK_ASSERT(pkt);

   pktLen = vmk_PktFrameLenGet(pkt);
   if (pktLen > SFVMK_MAX_PKT_SIZE) {
    SFVMK_ERR(pAdapter, "Invalid len %d, device %s",
              pktLen, vmk_NameToString(&pAdapter->uplinkName));
    goto sfvmk_release_pkt;
	}

   sfvmk_transmitPkt(pAdapter, pAdapter->pTxq[qid], pkt, pktLen);
   continue;

sfvmk_release_pkt:
   vmk_PktRelease(pkt);
   continue;
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK);
  return VMK_OK;
}

/*! \brief uplink callback function to set MTU
**
** \param[in]  adapter pointer to sfvmk_adapter_t
** \param[in] mtu
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkMTUSet(vmk_AddrCookie cookie, vmk_uint32 mtu)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);
  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
            "Not Supported");
  /* TODO : feature implementation will be done later */
  return VMK_OK;
}

/*! \brief uplink callback function to set the link status.
**
** \param[in]  adapter   pointer to sfvmk_adapter_t
** \param[in]  admnState linkstate.
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkStateSet(vmk_AddrCookie cookie, vmk_UplinkState admnState)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);
  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
            "Not Implemented");
  /* TODO : feature implementation will be done later */
  return VMK_OK;
}

/*! \brief Fill the buffer with private stats
**
** \param[in]  pAdapter  Adapter pointer
** \param[out] statsBuf  stats buffer to be filled
**
** \Return: VMK_OK
*/
static VMK_ReturnStatus
sfvmk_requestPrivStats(sfvmk_adapter_t *pAdapter, char *statsBuf)
{
  uint32_t id;
  int offset, count = 0;
  const char *name;

  offset = vmk_Sprintf(statsBuf, "\n");
  statsBuf += offset;
  count += offset;
  for (id = 0; id < EFX_MAC_NSTATS; id++) {
    name = efx_mac_stat_name(pAdapter->pNic, id);
    offset = vmk_Sprintf(statsBuf, "%s: %lu\n", name, pAdapter->adapterStats[id]);
    if (offset >= SFVMK_PRIV_STATS_BUFFER_SZ) {
	return VMK_EOVERFLOW;
    }

    statsBuf += offset;
    count += offset;
  }

  //TODO: Fill the driver maintained Statisitics
  return VMK_OK;
}

/*! \brief Handler used by vmkernel to get uplink private stats length
**
** \param[in]  cookie  struct holding driverData
** \param[out] length  length of the private stats in bytes
**
** \Return: VMK_OK
*/
static VMK_ReturnStatus
sfvmk_privStatsLengthGet(vmk_AddrCookie cookie, vmk_ByteCount *length)
{
  VMK_ASSERT(length != NULL);

  *length = SFVMK_PRIV_STATS_BUFFER_SZ;

  return VMK_OK;
}

/*! \brief Handler used by vmkernel to get uplink private statistics
**
** \param[in]  cookie   struct holding driverData
** \param[out] statBuf  buffer to put device private stats
** \param[in]  length   length of stats buf in bytes
**
** \Return: VMK_OK
*/
static VMK_ReturnStatus
sfvmk_privStatsGet(vmk_AddrCookie cookie,
                    char *statsBuf, vmk_ByteCount length)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  if (statsBuf) {
    SFVMK_ADAPTER_LOCK(pAdapter);
    sfvmk_requestPrivStats(pAdapter, statsBuf);
    SFVMK_ADAPTER_UNLOCK(pAdapter);
  }

  return VMK_OK;
}

/*! \brief uplink callback function to get the NIC stats
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
** \param[out] nicStats    ptr to stats
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkStatsGet(vmk_AddrCookie cookie, vmk_UplinkStats *nicStats )
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);
  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
            "sfvmk_uplinkStatsGet entered");

  SFVMK_ADAPTER_LOCK(pAdapter);

  vmk_Memset(nicStats, 0, sizeof(*nicStats));

  nicStats->rxPkts = pAdapter->adapterStats[EFX_MAC_VADAPTER_RX_UNICAST_PACKETS] +
                     pAdapter->adapterStats[EFX_MAC_VADAPTER_RX_MULTICAST_PACKETS] +
                     pAdapter->adapterStats[EFX_MAC_VADAPTER_RX_BROADCAST_PACKETS];

  nicStats->txPkts = pAdapter->adapterStats[EFX_MAC_VADAPTER_TX_UNICAST_PACKETS] +
                     pAdapter->adapterStats[EFX_MAC_VADAPTER_TX_MULTICAST_PACKETS] +
                     pAdapter->adapterStats[EFX_MAC_VADAPTER_TX_BROADCAST_PACKETS];

  nicStats->rxBytes = pAdapter->adapterStats[EFX_MAC_VADAPTER_RX_UNICAST_BYTES] +
                      pAdapter->adapterStats[EFX_MAC_VADAPTER_RX_MULTICAST_BYTES] +
                      pAdapter->adapterStats[EFX_MAC_VADAPTER_RX_BROADCAST_BYTES];

  nicStats->txBytes = pAdapter->adapterStats[EFX_MAC_VADAPTER_TX_UNICAST_BYTES] +
                      pAdapter->adapterStats[EFX_MAC_VADAPTER_TX_MULTICAST_BYTES] +
                      pAdapter->adapterStats[EFX_MAC_VADAPTER_TX_BROADCAST_BYTES];

  nicStats->rxMulticastPkts = pAdapter->adapterStats[EFX_MAC_VADAPTER_RX_MULTICAST_PACKETS];
  nicStats->txMulticastPkts = pAdapter->adapterStats[EFX_MAC_VADAPTER_TX_MULTICAST_PACKETS];
  nicStats->rxBroadcastPkts = pAdapter->adapterStats[EFX_MAC_VADAPTER_RX_BROADCAST_PACKETS];
  nicStats->txBroadcastPkts = pAdapter->adapterStats[EFX_MAC_VADAPTER_TX_BROADCAST_PACKETS];
  nicStats->rxErrors = pAdapter->adapterStats[EFX_MAC_RX_ERRORS];
  nicStats->txErrors = pAdapter->adapterStats[EFX_MAC_TX_ERRORS];
  nicStats->collisions = pAdapter->adapterStats[EFX_MAC_TX_SGL_COL_PKTS] +
                         pAdapter->adapterStats[EFX_MAC_TX_MULT_COL_PKTS] +
                         pAdapter->adapterStats[EFX_MAC_TX_EX_COL_PKTS] +
                         pAdapter->adapterStats[EFX_MAC_TX_LATE_COL_PKTS];

  nicStats->rxFrameAlignErrors = pAdapter->adapterStats[EFX_MAC_RX_ALIGN_ERRORS];
  nicStats->rxDrops = pAdapter->adapterStats[EFX_MAC_RX_NODESC_DROP_CNT];
  nicStats->rxOverflowErrors = pAdapter->adapterStats[EFX_MAC_VADAPTER_RX_OVERFLOW];
  nicStats->rxCRCErrors = pAdapter->adapterStats[EFX_MAC_RX_ALIGN_ERRORS];
  nicStats->rxLengthErrors = pAdapter->adapterStats[EFX_MAC_RX_FCS_ERRORS] +
                             pAdapter->adapterStats[EFX_MAC_RX_JABBER_PKTS];
  SFVMK_ADAPTER_UNLOCK(pAdapter);
  return VMK_OK;
}

/*! \brief uplink callback function to enable cap
**
** \param[in]  cookie      pointer to sfvmk_adapter_t
** \param[in]  uplinkCap   uplink capability to be enabled
**
* @return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkCapEnable(vmk_AddrCookie cookie, vmk_UplinkCap uplinkCap)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);
  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
            "Not Supported");
  return VMK_OK;
}

/*! \brief uplink callback function to disable cap
**
** \param[in]  cookie     pointer to sfvmk_adapter_t
** \param[in]  uplinkCap  uplink cap to be disabled
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkCapDisable(vmk_AddrCookie cookie, vmk_UplinkCap uplinkCap)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);
  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
            "Not Supported");

  return VMK_OK;
}

/*! \brief uplink callback function to reset the adapter.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkReset(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
            "Not Supported");

  /* TODO : will be done later */
  return VMK_OK;
}

/*! \brief function to update the queue state.
**
** \param[in]  pAdapter	pointer to sfvmk_adapter_t
** \param[in]  qState   queue state STOPPED or STARTED
**
** \return:    Nothing
**
*/
void sfvmk_updateQueueStatus(struct sfvmk_adapter_s *pAdapter, vmk_UplinkQueueState qState)
{
  vmk_UplinkSharedQueueData *queueData;
  vmk_uint16 i;

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);
  SFVMK_SHARED_AREA_BEGIN_WRITE(pAdapter);
  queueData = SFVMK_GET_TX_SHARED_QUEUE_DATA(pAdapter);

  for (i=0; i<pAdapter->queueInfo.maxTxQueues; i++) {
    SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_FUNCTION,
             "queueData flags: %x", queueData[i].flags);
   if (queueData[i].flags & (VMK_UPLINK_QUEUE_FLAG_IN_USE|VMK_UPLINK_QUEUE_FLAG_DEFAULT)) {
    queueData[i].state = qState;
   }
  }

  SFVMK_SHARED_AREA_END_WRITE(pAdapter);

  for (i=0; i<pAdapter->queueInfo.maxTxQueues; i++) {
   if (queueData[i].flags & (VMK_UPLINK_QUEUE_FLAG_IN_USE|VMK_UPLINK_QUEUE_FLAG_DEFAULT)) {
    if (qState == VMK_UPLINK_QUEUE_STATE_STOPPED) {
      vmk_UplinkQueueStop(pAdapter->uplink, queueData[i].qid);
    }
    else {
       vmk_UplinkQueueStart(pAdapter->uplink, queueData[i].qid);
    }
   }
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK);
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
  VMK_ReturnStatus status ;
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *) cookie.ptr;
  int rc;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);

  if (pAdapter->initState == SFVMK_STARTED)
    return VMK_OK;

  if (pAdapter->initState != SFVMK_REGISTERED)
    return VMK_FAILURE;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
            "Received Uplink Start I/O");

  /* Set required resource limits */
  if ((rc = sfvmk_setDrvLimits(pAdapter)) != 0) {
    SFVMK_ERR(pAdapter, "Failed to set drv limit");
    status = VMK_FAILURE;
    goto sfvmk_fail;
  }

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
            "caliing nic init");
  if ((rc = efx_nic_init(pAdapter->pNic)) != 0) {
    SFVMK_ERR(pAdapter, "Failed to init nic");
    goto sfvmk_fail;
  }

  /* Start processing interrupts. */
  status = sfvmk_intrStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to register intrrupt with error %s",
              vmk_StatusToString(status));
    goto sfvmk_intr_start_fail;
  }

  status = sfvmk_evStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to start eventQ module with error %s",
              vmk_StatusToString(status));
    goto sfvmk_ev_start_fail;
  }

  status = sfvmk_portStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to start port module with error %s",
              vmk_StatusToString(status));
    goto sfvmk_port_start_fail;
  }

  status = sfvmk_rxStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to start rx module with error %s",
              vmk_StatusToString(status));
    goto sfvmk_rx_start_fail;
  }

  status =  sfvmk_txStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to start tx module with error %s",
              vmk_StatusToString(status));
    goto sfvmk_tx_start_fail;
  }

  pAdapter->initState = SFVMK_STARTED;

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK);

  return status;

sfvmk_tx_start_fail:
  sfvmk_rxStop(pAdapter);
sfvmk_rx_start_fail:
  sfvmk_portStop(pAdapter);
sfvmk_port_start_fail:
  sfvmk_evStop(pAdapter);
sfvmk_ev_start_fail:
  sfvmk_intrStop(pAdapter);
sfvmk_intr_start_fail:
  efx_nic_fini(pAdapter->pNic);
sfvmk_fail:

  return VMK_FAILURE;
}

/*! \brief uplink callback function to  Quiesce IO operations.
**
** \param[in]  cookie  pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_uplinkQuiesceIO(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);

  if (pAdapter->initState != SFVMK_STARTED)
    return VMK_FAILURE;

  pAdapter->initState = SFVMK_REGISTERED;

  /* Stop the transmitter. */
  sfvmk_txStop(pAdapter);

  /* Stop the receiver. */
  sfvmk_rxStop(pAdapter);

  /* Stop the port. */
  sfvmk_portStop(pAdapter);

  /* Stop processing events. */
  sfvmk_evStop(pAdapter);

  /* Stop processing interrupts. */
  sfvmk_intrStop(pAdapter);

  efx_nic_fini(pAdapter->pNic);

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK);

  return VMK_OK;
}

/*! \brief It creates a spin lock with specified name and lock rank.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
vmk_Bool sfvmk_netPollCB(void *pEvq, vmk_uint32 budget)
{
  /* TODO : budget needs to be handled. */
  sfvmk_evqPoll(pEvq);
  return 0;
}

/*! \brief  update cable type
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
static void sfvmk_updateCableType(sfvmk_adapter_t *pAdapter)
{
  SFVMK_NULL_PTR_CHECK(pAdapter);
  sfvmk_phyInfo_t *phy = &pAdapter->phy;

  efx_phy_media_type_get(pAdapter->pNic, &phy->interfaceType);

  switch (phy->interfaceType) {
    case EFX_PHY_MEDIA_BASE_T:
      phy->cableType = VMK_UPLINK_CABLE_TYPE_TP;
      break;
    /* TODO: cable type could be fiber or DA */
    /* needs to populate this information properly */
    case EFX_PHY_MEDIA_SFP_PLUS:
    case EFX_PHY_MEDIA_QSFP_PLUS:
    case EFX_PHY_MEDIA_XFP:
      phy->cableType = VMK_UPLINK_CABLE_TYPE_FIBRE;
      break;

    default:
      phy->cableType = VMK_UPLINK_CABLE_TYPE_OTHER;
      break;
  }
  return;
}

/*! \brief  allocate  queues for uplink device
**
** \param[in]  adapter pointer to sfvmk_adapter_t
** \param[in]  feat      Q feature
** \param[out]  pQid     ptr to uplink Q ID
** \param[in]  qType     q type (rx or tx)
** \param[out]  pNetPoll ptr to netpoll registered with this Q
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus sfvmk_allocQ(sfvmk_adapter_t *pAdapter,
                                    vmk_UplinkQueueFeature feat,
                                    vmk_UplinkQueueID *pQid,
                                    vmk_UplinkQueueType qType,
                                    vmk_NetPoll *pNetpoll)
{
  vmk_UplinkSharedQueueData *qData;
  vmk_uint32 qIndex, flags, qStartIndex, maxQueue;
  void *pDefQueue, *pQueue ;
  vmk_UplinkSharedQueueInfo *pQueueInfo;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);

  pQueueInfo = &pAdapter->queueInfo;

  /* tx queue is just after the rx queue in qdata*/
  if (qType == VMK_UPLINK_QUEUE_TYPE_TX) {
    qStartIndex = pAdapter->queueInfo.maxRxQueues;
    /*first queue is default queue */
    pDefQueue = (void *)pAdapter->pTxq[0];
    maxQueue = qStartIndex + pAdapter->queueInfo.maxTxQueues;
  }
  else  if (qType == VMK_UPLINK_QUEUE_TYPE_RX) {
    qStartIndex = 0;
    pDefQueue = (void *)pAdapter->pRxq[0];
    maxQueue = qStartIndex + pAdapter->queueInfo.maxRxQueues;
  }
  else
    return VMK_FAILURE;

  qData = &pAdapter->queueData[qStartIndex];

  /* check if queue is free*/
  for (qIndex = qStartIndex ; qIndex < maxQueue; qIndex++)
    if ((qData[qIndex].flags & VMK_UPLINK_QUEUE_FLAG_IN_USE) == 0)
      break;

  /* make queue with uplink device */
  if (qType == VMK_UPLINK_QUEUE_TYPE_TX)
    vmk_UplinkQueueMkTxQueueID(pQid, qIndex, qIndex);
  else if (qType == VMK_UPLINK_QUEUE_TYPE_RX) {
    vmk_UplinkQueueMkRxQueueID(pQid, qIndex, qIndex);
    *pNetpoll = qData[qIndex].poll;
  }

  /* Setup flags and qid for the allocated Rx queue */
  SFVMK_SHARED_AREA_BEGIN_WRITE(pAdapter);

  if (qType == VMK_UPLINK_QUEUE_TYPE_TX)
    pQueue = (void*) pAdapter->pTxq[qIndex - qStartIndex];
  else if (qType == VMK_UPLINK_QUEUE_TYPE_RX)
    pQueue = (void*) pAdapter->pRxq[qIndex - qStartIndex];
  else
    return VMK_FAILURE;

  flags = VMK_UPLINK_QUEUE_FLAG_IN_USE;

  /* set the default flag if the queue is default Q */
  if (pQueue == pDefQueue)
    flags |= VMK_UPLINK_QUEUE_FLAG_DEFAULT;

  qData[qIndex].flags &= ~(VMK_UPLINK_QUEUE_FLAG_DEFAULT |
                            VMK_UPLINK_QUEUE_FLAG_IN_USE);
  qData[qIndex].flags |= flags;
  qData[qIndex].qid = *pQid;

  SFVMK_SHARED_AREA_END_WRITE(pAdapter);

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK);

  return VMK_OK;
}

/*! \brief  allocate rx queue for uplink device
**
** \param[in]  adapter pointer to sfvmk_adapter_t
** \param[in]  feat      Q feature
** \param[in]  pQid      ptr to uplink rxQ ID
** \param[in]  pNetPoll  ptr to netpoll registered with this Q
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_allocRxQueue(sfvmk_adapter_t *pAdapter,
                   vmk_UplinkQueueFeature feat,
                   vmk_UplinkQueueID *pQid,
                   vmk_NetPoll *pNetpoll)
{
  return sfvmk_allocQ(pAdapter, 0, pQid, VMK_UPLINK_QUEUE_TYPE_RX, pNetpoll);
}

/*! \brief  allocate tx queue for uplink device
**
** \param[in]  adapter pointer to sfvmk_adapter_t
** \param[in]  pQid      uplink txQ ID
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_allocTxQueue(sfvmk_adapter_t *pAdapter, vmk_UplinkQueueID *pQid)

{
  return sfvmk_allocQ(pAdapter, 0, pQid, VMK_UPLINK_QUEUE_TYPE_TX, NULL);
}

/*! \brief update qData for a queue (tx or rx)
**
** \param[in]  adapter    pointer to sfvmk_adapter_t
** \param[in]  qIndex     queue Index
** \param[in]  qType      queue type (tx or rx)
** \param[in]  serviceID  service to which work has to be charged
**
** \return: VMK_OK <success> error code <failure>
**
*/

static VMK_ReturnStatus
sfvmk_initQData(sfvmk_adapter_t *pAdapter, vmk_UplinkQueueType qType,
                      vmk_uint32 qIndex, vmk_ServiceAcctID  serviceID)
{
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_NetPollProperties pollProp;
  VMK_ReturnStatus status = VMK_OK;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK, "qIndex[%d]", qIndex);

  /* populate qInfo information */
  pQueueInfo = &pAdapter->queueInfo;
  pQueueData = &pAdapter->queueData[qIndex];
  pQueueData->type =  qType;
  pQueueData->state = VMK_UPLINK_QUEUE_STATE_STOPPED;
  pQueueData->flags = VMK_UPLINK_QUEUE_FLAG_UNUSED;

  pQueueData->activeFeatures = VMK_UPLINK_QUEUE_FEAT_NONE;

  pQueueData->dmaEngine = pAdapter->dmaEngine;
  pQueueData->activeFilters = 0;
  pQueueData->maxFilters = SFVMK_MAX_FILTER_PER_QUEUE ;

  if (qType == VMK_UPLINK_QUEUE_TYPE_TX) {
    if (qIndex == pQueueInfo->maxRxQueues) {
      pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_NONE;
      pQueueData->maxFilters *= pAdapter->rxqCount;
    }
    else
      pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_PAIR;

    SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
              "uplink Txq =%d init", qIndex);
  }
  else if (qType == VMK_UPLINK_QUEUE_TYPE_RX) {
    if (qIndex == 0) {
      pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_NONE;
      pQueueData->maxFilters *= pAdapter->rxqCount;
    }
    else
      pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_PAIR ;

    /* poll properties for crreting netPoll*/
    pollProp.poll = sfvmk_netPollCB;
    pollProp.priv.ptr = pAdapter->pEvq[qIndex];
    pollProp.deliveryCallback = NULL;
    pollProp.features = VMK_NETPOLL_NONE;

    status = vmk_NetPollCreate(&pollProp, serviceID, vmk_ModuleCurrentID,
                                &pAdapter->pEvq[qIndex]->netPoll);
    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed to create net poll for Q[%d]", qIndex);
      for (qIndex--; qIndex >= 0; qIndex--)
        vmk_NetPollDestroy(pAdapter->pEvq[qIndex]->netPoll);
      return VMK_FAILURE;
    }

    pQueueData->poll = pAdapter->pEvq[qIndex]->netPoll;
    pAdapter->pEvq[qIndex]->vector = pAdapter->intr.intrCookies[qIndex];

    SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
              "uplink Rxq =%d init, poll=0x%p", qIndex, pQueueData->poll);
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK, "qIndex[%d]", qIndex);

  return status;

}

/*! \brief update qData for a rx queue
**
** \param[in]  adapter     pointer to sfvmk_adapter_t
** \param[in]  qIndex      shared rx queue Index
** \param[in]  serviceID   service to which work has to be charged
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_initRxqData(sfvmk_adapter_t *pAdapter,
                      vmk_uint32 qIndex, vmk_ServiceAcctID serviceID)
{
  return sfvmk_initQData(pAdapter, VMK_UPLINK_QUEUE_TYPE_RX, qIndex,
                           serviceID);
}

/*! \brief update qData for a tx queue
**
** \param[in]  adapter pointer to sfvmk_adapter_t
** \param[in]  qIndex    tx queue Index
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_initTxqData(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  return sfvmk_initQData(pAdapter, VMK_UPLINK_QUEUE_TYPE_TX, qIndex, 0);

}

/*! \brief update the shared queue info which will be used by uplink device.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
static VMK_ReturnStatus
sfvmk_initSharedQueueInfo(sfvmk_adapter_t *pAdapter)
{

  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_ServiceAcctID serviceID;
  VMK_ReturnStatus status = VMK_OK;
  int qIndex;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);

  /*populating shared queue information */
  pQueueInfo = &pAdapter->queueInfo;
  pQueueInfo->supportedQueueTypes = VMK_UPLINK_QUEUE_TYPE_TX |
                                    VMK_UPLINK_QUEUE_TYPE_RX;
  /* TODO: this will be extended as and when we add a new functinality */
  pQueueInfo->supportedRxQueueFilterClasses =
                              VMK_UPLINK_QUEUE_FILTER_CLASS_MAC_ONLY;

  /* update max tx and rx queue*/
  /* needs modification when RSS and dynamic queue support is added */
  pQueueInfo->maxTxQueues =  pAdapter->txqCount;
  pQueueInfo->maxRxQueues =  pAdapter->rxqCount;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
            "maxTxQs: %d maxRxQs: %d ", pQueueInfo->maxTxQueues,
            pQueueInfo->maxRxQueues);

  pQueueInfo->activeRxQueues = 0;
  pQueueInfo->activeTxQueues = 0;
  pQueueInfo->queueData = pAdapter->queueData;
  pQueueInfo->defaultRxQueueID = 0;
  pQueueInfo->defaultTxQueueID = 0;

  status = vmk_ServiceGetID(VMK_SERVICE_ACCT_NAME_NET, &serviceID);
  VMK_ASSERT_BUG(status == VMK_OK);

  /* updating rxqData */
  for (qIndex = 0; qIndex < pQueueInfo->maxRxQueues; qIndex++) {

    status = sfvmk_initRxqData(pAdapter, qIndex, serviceID);
    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed in updating uplink rxq[%d] data", qIndex);
      goto sfvmk_fail;
    }
    else
      SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                "updated uplink rxq[%d] data", qIndex);

  }

  for (qIndex = pQueueInfo->maxRxQueues;
        qIndex < (pQueueInfo->maxRxQueues + pQueueInfo->maxTxQueues); qIndex++) {

    status = sfvmk_initTxqData(pAdapter, qIndex);
    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed in updating uplink rxq[%d] data", qIndex);
      goto sfvmk_fail;
    }
  }

  /* default RX queue is being allocated */
  status = sfvmk_allocRxQueue(pAdapter, 0, &pQueueInfo->defaultRxQueueID,
                        &pAdapter->pEvq[0]->netPoll);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed in creating uplink rxq[%d]", qIndex);
    goto sfvmk_fail;
  }

  /* default TX queue is being allocated */
  status = sfvmk_allocTxQueue(pAdapter, &pQueueInfo->defaultTxQueueID);
  if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed in creating uplink txq[%d]", qIndex);
      goto sfvmk_fail;
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK);

  return status;

sfvmk_fail:
  return status;

}

/*! \brief  allocate the resource required for uplink device and initalize all
**         required information.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/

VMK_ReturnStatus
sfvmk_initUplinkData(sfvmk_adapter_t * pAdapter)
{
  vmk_UplinkSharedData *pSharedData;
  vmk_UplinkRegData *pRegData;
  VMK_ReturnStatus status;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_UPLINK);

  SFVMK_NULL_PTR_CHECK(pAdapter->pNic);
  const efx_nic_cfg_t *pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  SFVMK_NULL_PTR_CHECK(pNicCfg);

  sfvmk_updateCableType(pAdapter);

  status = sfvmk_createLock(sfvmk_ModInfo.driverName.string, VMK_SPINLOCK_RANK_HIGHEST-1,
                            &pAdapter->shareDataLock);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to create writer lock for shared data");
    goto sfvmk_create_data_lock_fail;
  }

  /* Initialize shared data area */
  pSharedData = &pAdapter->sharedData;
  pSharedData->supportedModes = pAdapter->supportedModes;
  pSharedData->supportedModesArraySz = pAdapter->supportedModesArraySz;

  pSharedData->link.state = VMK_LINK_STATE_DOWN;
  pSharedData->link.speed = 0;
  pSharedData->link.duplex = VMK_LINK_DUPLEX_FULL;

  pSharedData->flags = 0;
  pSharedData->state = VMK_UPLINK_STATE_ENABLED;

  pSharedData->mtu = pAdapter->mtu = SFVMK_DEFAULT_MTU;

  pSharedData->queueInfo = &pAdapter->queueInfo;

  vmk_Memcpy(pSharedData->macAddr, pNicCfg->enc_mac_addr, VMK_ETH_ADDR_LENGTH);
  vmk_Memcpy(pSharedData->hwMacAddr, pNicCfg->enc_mac_addr,VMK_ETH_ADDR_LENGTH);

  status = sfvmk_initSharedQueueInfo(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed in updating shared queues information");
    goto sfvmk_create_data_fail;
  }

  pRegData = &pAdapter->regData;
  pRegData->ops = sfvmkUplinkOps;
  pRegData->moduleID = vmk_ModuleCurrentID;
  pRegData->apiRevision = VMKAPI_REVISION;
  pRegData->sharedData = pSharedData;
  pRegData->driverData.ptr = pAdapter;

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_UPLINK);

  return (VMK_OK);

sfvmk_create_data_fail:
  sfvmk_destroyLock(pAdapter->shareDataLock);
sfvmk_create_data_lock_fail:
  return (status);
}

/*! \brief function to destroy resource created for uplink device.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK success, Error code  otherwise.
*/
VMK_ReturnStatus
sfvmk_destroyUplinkData(sfvmk_adapter_t *pAdapter)
{
  vmk_int16 qIndex;
  sfvmk_evq_t *pEVQ;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  for (qIndex = 0; qIndex < pAdapter->queueInfo.maxRxQueues; qIndex++) {
    pEVQ = pAdapter->pEvq[qIndex];
    vmk_NetPollDestroy(pEVQ->netPoll);
  }

  vmk_SpinlockDestroy(pAdapter->shareDataLock);

  return (VMK_OK);
}

/*! \brief function to set the driver limit in fw.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: 0 success, Error code otherwise.
*/
static int
sfvmk_setDrvLimits( sfvmk_adapter_t *pAdapter)
{
  efx_drv_limits_t limits;

  vmk_Memset(&limits, 0, sizeof(limits));

  /* putting more strict limit by making max and min same */
  limits.edl_min_evq_count = limits.edl_max_evq_count =
      pAdapter->intr.numIntrAlloc;
  limits.edl_min_txq_count = limits.edl_max_txq_count =
      pAdapter->intr.numIntrAlloc + SFVMK_TXQ_NTYPES - 1;
  limits.edl_min_rxq_count = limits.edl_max_rxq_count =
      pAdapter->intr.numIntrAlloc;

  return (efx_nic_set_drv_limits(pAdapter->pNic, &limits));
}
