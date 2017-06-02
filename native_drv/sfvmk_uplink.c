/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"

#define SFVMK_DEFAULT_MTU 1500
#define SFVMK_MAX_FILTER_PER_QUEUE 10;

/****************************************************************************
*                Local Functions                                            *
****************************************************************************/
static void sfvmk_updateCableType(sfvmk_adapter_t *adapter);

static VMK_ReturnStatus 
sfvmk_updateSharedQueueInfo(sfvmk_adapter_t *pAdapter);

static VMK_ReturnStatus 
sfvmk_updateRxqData(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex, vmk_ServiceAcctID serviceID);

static VMK_ReturnStatus
sfvmk_updateTxqData(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex);


static VMK_ReturnStatus
sfvmk_allocQ(sfvmk_adapter_t *pAdapter, vmk_UplinkQueueFeature feat, vmk_UplinkQueueID *pQid,
                    vmk_UplinkQueueType qType, vmk_NetPoll *pNetpoll);


static int
sfvmk_setDrvLimits( sfvmk_adapter_t *pAdapter);

/****************************************************************************
*                vmk_UplinkMessageLevelOps Handler                         *
****************************************************************************/
static VMK_ReturnStatus
sfvmk_messageLevelGet(vmk_AddrCookie cookie, vmk_uint32 *level);

static VMK_ReturnStatus
sfvmk_messageLevelSet(vmk_AddrCookie cookie, vmk_uint32 level);

static vmk_UplinkMessageLevelOps sfvmkMessageLevelOps = {
  .getMessageLevel = sfvmk_messageLevelGet,
  .setMessageLevel = sfvmk_messageLevelSet,
};

/****************************************************************************
*              vmk_UplinkRingParamsOps Handler                             *
****************************************************************************/
static VMK_ReturnStatus
sfvmk_ringParamsGet(vmk_AddrCookie cookie,
                              vmk_UplinkRingParams *params);

static VMK_ReturnStatus
sfvmk_ringParamsSet(vmk_AddrCookie cookie,
                              vmk_UplinkRingParams *params);

struct vmk_UplinkRingParamsOps sfvmkRingParamsOps = {
  .ringParamsGet = sfvmk_ringParamsGet,
  .ringParamsSet = sfvmk_ringParamsSet,
};


/****************************************************************************
 *               vmk_UplinkOps Handlers                       *
 ****************************************************************************/
static VMK_ReturnStatus sfvmk_uplinkTx(vmk_AddrCookie, vmk_PktList);
static VMK_ReturnStatus sfvmk_uplinkMTUSet(vmk_AddrCookie, vmk_uint32);
static VMK_ReturnStatus sfvmk_uplinkStateSet(vmk_AddrCookie, vmk_UplinkState);
static VMK_ReturnStatus sfvmk_uplinkStatsGet(vmk_AddrCookie, vmk_UplinkStats *);
static VMK_ReturnStatus sfvmk_uplinkAssociate(vmk_AddrCookie, vmk_Uplink);
static VMK_ReturnStatus sfvmk_uplinkDisassociate(vmk_AddrCookie);
static VMK_ReturnStatus sfvmk_uplinkCapsRegister(vmk_AddrCookie);
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


/****************************************************************************
*                vmk_UplinkMessageLevelOps 		                    *
****************************************************************************/
static VMK_ReturnStatus
sfvmk_messageLevelGet(vmk_AddrCookie cookie, vmk_uint32 *level)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  VMK_ASSERT(adapter != NULL);

  *level = vmk_LogGetCurrentLogLevel(sfvmk_ModInfo.logID);

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 2, "level: 0x%x", *level);

  return VMK_OK;
}


static VMK_ReturnStatus
sfvmk_messageLevelSet(vmk_AddrCookie cookie, vmk_uint32 level)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  VMK_ASSERT(adapter != NULL);

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 2,
                "Seting level to 0x%x (Current level : 0x%x)",
                level, vmk_LogGetCurrentLogLevel(sfvmk_ModInfo.logID));

  vmk_LogSetCurrentLogLevel(sfvmk_ModInfo.logID , level);

  return VMK_OK;
}


/****************************************************************************
*              vmk_UplinkRingParamsOps                                      *
****************************************************************************/
static VMK_ReturnStatus
sfvmk_ringParamsGet(vmk_AddrCookie cookie,
                     vmk_UplinkRingParams *params)
{

  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  VMK_ASSERT(pAdapter != NULL);

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 3, "Entry ... ");

  /*
  * txMaxPending & rxMaxPending are maximum num of entries
  * supported by Hardware
  *
  * txPending & rxPending are currently configured
  * num of entries
  *
  */
  params->txPending = params->txMaxPending = SFVMK_RX_SCALE_MAX;
  params->rxMaxPending = params->rxPending = SFVMK_RX_SCALE_MAX;

  /*
  * Ignoring Mini & Jumbo params, since firmware don't support
  */
  params->rxMiniMaxPending = 0;
  params->rxJumboMaxPending = 0;
  params->rxMiniPending = 0;
  params->rxJumboPending = 0;

  return VMK_OK;
}


static VMK_ReturnStatus
sfvmk_ringParamsSet(vmk_AddrCookie cookie,
                   vmk_UplinkRingParams *params)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  VMK_ASSERT_BUG(pAdapter != NULL);

  SFVMK_ERR(pAdapter, "RingParamsSet is not supported");

  return VMK_NOT_SUPPORTED;
}
#if 0 
static VMK_ReturnStatus
sfvmk_getCableType(vmk_AddrCookie cookie,
                            vmk_UplinkCableType *cableType)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 3, "Entry ...");

  *cableType = pAdapter->phy.cable_type;

  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_setCableType(vmk_AddrCookie cookie,
                            vmk_UplinkCableType cableType)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  SFVMK_ERR(pAdapter, "setCableType is not supported");

  return VMK_NOT_SUPPORTED;
}
#endif
/****************************************************************************
 *               vmk_UplinkOps Handlers                       *
 ****************************************************************************/
static VMK_ReturnStatus
sfvmk_registerIOCaps(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status;

  status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_SG_TX, NULL);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "SG_TX cap register failed with error 0x%x",status);
    VMK_ASSERT(0);
  }

  status = vmk_UplinkCapRegister(pAdapter->uplink,
                                  VMK_UPLINK_CAP_MULTI_PAGE_SG, NULL);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter,"MULTI_PAGE_SG cap register failed with error 0x%x",
                status);
    VMK_ASSERT(0);
  }

  status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_IPV4_CSO, NULL);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter,"IPv4_CSO cap register failed with error 0x%x", status);
    VMK_ASSERT(0);
  }

  status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_IPV4_TSO, NULL);
  if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
    SFVMK_ERR(pAdapter, "IPv4_TSO cap register failed with error 0x%x", status);
    VMK_ASSERT(0);
  }

  status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_IPV6_CSO, NULL);
  if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
    SFVMK_ERR(pAdapter, "IPv6_CSO cap register failed with error 0x%x", status);
    VMK_ASSERT(0);
  }

  status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_IPV6_TSO, NULL);
  if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
    SFVMK_ERR(pAdapter,"IPv6_TSO cap register failed with error 0x%x", status);
    VMK_ASSERT(0);
  }

  status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_VLAN_TX_INSERT, NULL);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "VLAN_TX_INSERT cap register failed with error 0x%x",
              status);
    VMK_ASSERT(0);
  }

  status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_VLAN_RX_STRIP,  NULL);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter,"VLAN_RX_STRIP cap register failed with error 0x%x",
              status);
    VMK_ASSERT(0);
  }

  status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_MESSAGE_LEVEL,
                                  &sfvmkMessageLevelOps);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Message Level cap register failed with error 0x%x ",
             status);
    VMK_ASSERT(0);
  }

  status = vmk_UplinkCapRegister(pAdapter->uplink, VMK_UPLINK_CAP_RING_PARAMS,
                                  &sfvmkRingParamsOps);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Ring Parameters cap register failed with error 0x%x",
              status);
    VMK_ASSERT(0);
  }

  return (status);
}






static VMK_ReturnStatus
sfvmk_uplinkCapsRegister(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_OK;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 5, "sfvmk_uplinkCapsRegister entered");

  VMK_ASSERT(pAdapter->uplink != NULL);

  status = sfvmk_registerIOCaps(pAdapter);

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 5, "sfvmk_uplinkCapsRegister exit");

  return status;
}




static VMK_ReturnStatus
sfvmk_uplinkAssociate(vmk_AddrCookie cookie, vmk_Uplink uplink)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  //VMK_ASSERT_BUG(uplink != NULL);
  //VMK_ASSERT_BUG(adapter->uplink == NULL);

  pAdapter->uplink = uplink;
  pAdapter->uplinkName = vmk_UplinkNameGet(pAdapter->uplink);

  return sfvmk_uplinkCapsRegister(cookie);
}


static VMK_ReturnStatus
sfvmk_uplinkDisassociate(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  //VMK_ASSERT_BUG(pAdapter->uplink != NULL);

  pAdapter->uplink = NULL;

  return VMK_OK;
}


static VMK_ReturnStatus
sfvmk_uplinkTx(vmk_AddrCookie cookie, vmk_PktList pktList)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 5, "sfvmk_uplinkTx entered ");
  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_uplinkMTUSet(vmk_AddrCookie cookie, vmk_uint32 mtu)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 5, "sfvmk_uplinkMTUSet entered ");
  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_uplinkStateSet(vmk_AddrCookie cookie, vmk_UplinkState admnState)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 5, "sfvmk_uplinkStateSet entered ");
  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_uplinkStatsGet(vmk_AddrCookie cookie, vmk_UplinkStats *amnState )
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 5, "sfvmk_uplinkStatsGet entered ");

  return VMK_OK;
}


static VMK_ReturnStatus
sfvmk_uplinkCapEnable(vmk_AddrCookie cookie, vmk_UplinkCap uplinkCap)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 5, "sfvmk_uplinkCapEnable entered ");
  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_uplinkCapDisable(vmk_AddrCookie cookie, vmk_UplinkCap uplinkCap)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 5, "sfvmk_uplinkCapDisable entered ");

  return VMK_OK;
}

static VMK_ReturnStatus 
sfvmk_uplinkReset(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 5, "sfvmk_uplinkReset entered ");

  return VMK_OK;
}

static VMK_ReturnStatus 
sfvmk_uplinkStartIO(vmk_AddrCookie cookie)
{

  VMK_ReturnStatus status ;
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *) cookie.ptr;
  int rc;

  VMK_ASSERT(pAdapter != NULL);
  if (pAdapter->initState == SFVMK_STARTED)
    return VMK_OK;

  if (pAdapter->initState != SFVMK_REGISTERED)
    return VMK_FAILURE;

  pAdapter->uplinkName = vmk_UplinkNameGet(pAdapter->uplink);
  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 0, "Received Uplink Start I/O");

  /* Set required resource limits */
  if ((rc = sfvmk_setDrvLimits(pAdapter)) != 0) {
    SFVMK_ERR(pAdapter, "Failed to set drv limit");
    status = VMK_FAILURE;
    goto sfvmk_fail;
  }

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 0, "caliing nic init");
  if ((rc = efx_nic_init(pAdapter->pNic)) != 0) {
    SFVMK_ERR(pAdapter, "Failed to init nic");
    goto sfvmk_fail;
  }

  /* Start processing interrupts. */
  status = sfvmk_intrStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to register intrrupt");
    goto sfvmk_intr_start_fail;
  }

  status = sfvmk_evStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to start eventQ module");
    goto sfvmk_ev_start_fail;
  }

  status = sfvmk_portStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to start port module");
    goto sfvmk_port_start_fail;
  }

  status = sfvmk_rxStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to start rx module");
    goto sfvmk_rx_start_fail;
  }
 
  status =  sfvmk_txStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to start tx module");
    goto sfvmk_tx_start_fail;
  }

  pAdapter->initState = SFVMK_STARTED;
  return status;

sfvmk_tx_start_fail:
  sfvmk_rxStop(pAdapter);
sfvmk_rx_start_fail:
  sfvmk_portStop(pAdapter);
sfvmk_port_start_fail:
  sfvmk_evStop(pAdapter);
sfvmk_ev_start_fail:
  /* intr clean up should have come here */
  //sfvmk_EvStop(pAdapter);
sfvmk_intr_start_fail:
  efx_nic_fini(pAdapter->pNic);
sfvmk_fail:

  return VMK_FAILURE;




}

static VMK_ReturnStatus 
sfvmk_uplinkQuiesceIO(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 5, "sfvmk_uplinkQuiesceIO entered ");


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



  return VMK_OK;
}
vmk_Bool
sfvmk_netPollCB(void *pEvq, vmk_uint32 budget)
{

  vmk_LogMessage("*************************************************\n");
  vmk_LogMessage("calling net poll callback \n");
  vmk_LogMessage("*************************************************\n");
  sfvmk_evqPoll(pEvq);
  return 0 ;
}


static void
sfvmk_updateCableType(sfvmk_adapter_t *adapter)
{
  sfvmk_phyInfo_t *phy = &adapter->phy;

  efx_phy_media_type_get(adapter->pNic, &phy->interfaceType);

  switch (phy->interfaceType) {
    case EFX_PHY_MEDIA_BASE_T:
    phy->cableType = VMK_UPLINK_CABLE_TYPE_TP;
    break;

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

static VMK_ReturnStatus
sfvmk_allocQ(sfvmk_adapter_t *pAdapter,
                    vmk_UplinkQueueFeature feat,
                    vmk_UplinkQueueID *pQid,
                    vmk_UplinkQueueType qType,
                    vmk_NetPoll *pNetpoll)
{
  vmk_UplinkSharedQueueData *qData;
  vmk_uint32 qIndex, flags, qStartIndex, maxQueue;
  void *pDefQueue, *pQueue ;
  vmk_UplinkSharedQueueInfo *pQueueInfo;


  pQueueInfo = &pAdapter->queueInfo;

  if (qType == VMK_UPLINK_QUEUE_TYPE_TX) {
    qStartIndex = pAdapter->queueInfo.maxRxQueues;
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


  for (qIndex = qStartIndex ; qIndex < maxQueue; qIndex++)
    if ((qData[qIndex].flags & VMK_UPLINK_QUEUE_FLAG_IN_USE) == 0)
      break;


  if (qType == VMK_UPLINK_QUEUE_TYPE_TX)
    vmk_UplinkQueueMkTxQueueID(pQid, qIndex, qIndex);
  else if (qType == VMK_UPLINK_QUEUE_TYPE_RX) {
    vmk_UplinkQueueMkRxQueueID(pQid, qIndex, qIndex);
    *pNetpoll = qData[qIndex].poll;
  }

  // Setup flags and qid for the allocated Rx queue
  SFVMK_SHARED_AREA_BEGIN_WRITE(pAdapter);

  if (qType == VMK_UPLINK_QUEUE_TYPE_TX)
    pQueue = (void*) pAdapter->pTxq[qIndex - qStartIndex];
  else if (qType == VMK_UPLINK_QUEUE_TYPE_RX)
    pQueue = (void*) pAdapter->pRxq[qIndex - qStartIndex];
  else
    return VMK_FAILURE;

  flags = VMK_UPLINK_QUEUE_FLAG_IN_USE;

  if (pQueue == pDefQueue)
    flags |= VMK_UPLINK_QUEUE_FLAG_DEFAULT;


  qData[qIndex].flags &= ~(VMK_UPLINK_QUEUE_FLAG_DEFAULT |
                            VMK_UPLINK_QUEUE_FLAG_IN_USE);
  qData[qIndex].flags |= flags;
  qData[qIndex].qid = *pQid;

  SFVMK_SHARED_AREA_END_WRITE(pAdapter);

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 2,"sfvmk_allocQ %u alloced", qIndex);

  return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_allocRxQueue(sfvmk_adapter_t *pAdapter,
                   vmk_UplinkQueueFeature feat,
                   vmk_UplinkQueueID *pQid,
                   vmk_NetPoll *pNetpoll)
{
  return sfvmk_allocQ(pAdapter, 0, pQid, VMK_UPLINK_QUEUE_TYPE_RX, pNetpoll);
}

static VMK_ReturnStatus
sfvmk_allocTxQueue(sfvmk_adapter_t *pAdapter, vmk_UplinkQueueID *pQid)

{
  return sfvmk_allocQ(pAdapter, 0, pQid, VMK_UPLINK_QUEUE_TYPE_TX, NULL);
}


static VMK_ReturnStatus
sfvmk_updateQData(sfvmk_adapter_t *pAdapter, vmk_UplinkQueueType qType,
                      vmk_uint32 qIndex, vmk_ServiceAcctID  serviceID)
{
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_NetPollProperties pollProp;
  VMK_ReturnStatus status = VMK_OK;

  VMK_ASSERT_BUG(NULL != pAdapter, "NULL adapter ptr" );

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
      pQueueData->supportedFeatures = 0;
      pQueueData->maxFilters *= pAdapter->rxqCount;
    }
    else
      pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_PAIR;

      SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 3, " uplink Txq =%d init", qIndex);
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
    //assert for qIndex
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

    SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 3, "uplink Rxq =%d init, poll=0x%p",
                qIndex, pQueueData->poll);
  }

  return status;

}




static VMK_ReturnStatus
sfvmk_updateRxqData(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex,  vmk_ServiceAcctID serviceID)
{

  return sfvmk_updateQData(pAdapter, VMK_UPLINK_QUEUE_TYPE_RX, qIndex,
                           serviceID);

}

static VMK_ReturnStatus
sfvmk_updateTxqData(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  return sfvmk_updateQData(pAdapter, VMK_UPLINK_QUEUE_TYPE_TX, qIndex, 0);

}



static VMK_ReturnStatus
sfvmk_updateSharedQueueInfo(sfvmk_adapter_t *pAdapter)
{

  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_ServiceAcctID serviceID;
  VMK_ReturnStatus status = VMK_OK;
  int qIndex;

  /*populating shared queue information */
  pQueueInfo = &pAdapter->queueInfo;

  pQueueInfo->supportedQueueTypes = VMK_UPLINK_QUEUE_TYPE_TX |
                                    VMK_UPLINK_QUEUE_TYPE_RX;
  pQueueInfo->supportedRxQueueFilterClasses =
                              VMK_UPLINK_QUEUE_FILTER_CLASS_MAC_ONLY;


  /* update max tx and rx queue*/
  /* needs modification when RSS and dynamic queue support is added */
  pQueueInfo->maxTxQueues =  pAdapter->txqCount;
  pQueueInfo->maxRxQueues =  pAdapter->rxqCount;

  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 2, "maxTxQs: %d maxRxQs: %d ",
                    pQueueInfo->maxTxQueues, pQueueInfo->maxRxQueues);

  pQueueInfo->activeRxQueues = 0;
  pQueueInfo->activeTxQueues = 0;

  pQueueInfo->queueData = pAdapter->queueData;


  pQueueInfo->defaultRxQueueID = 0;
  pQueueInfo->defaultTxQueueID = 0;

  status = vmk_ServiceGetID(VMK_SERVICE_ACCT_NAME_NET, &serviceID);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed in getting the service ID");
    goto sfvmk_fail;
  }

  /* updating rxqData */
  for (qIndex=0; qIndex < pQueueInfo->maxRxQueues; qIndex++) {

    status = sfvmk_updateRxqData(pAdapter, qIndex, serviceID);
    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed in updating uplink rxq[%d] data", qIndex);
      goto sfvmk_fail;
    }
    else
      SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 2, "updated uplink rxq[%d] data",
                    qIndex);

  }

  for (qIndex = pQueueInfo->maxRxQueues;
        qIndex < (pQueueInfo->maxRxQueues + pQueueInfo->maxTxQueues); qIndex++) {

    status = sfvmk_updateTxqData(pAdapter, qIndex);
    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed in updating uplink rxq[%d] data", qIndex);
      goto sfvmk_fail;
    }
  }

  status = sfvmk_allocRxQueue(pAdapter, 0, &pQueueInfo->defaultRxQueueID,
                        &pAdapter->pEvq[0]->netPoll);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed in creating uplink rxq[%d]", qIndex);
    goto sfvmk_fail;
  }


  status = sfvmk_allocTxQueue(pAdapter, &pQueueInfo->defaultTxQueueID);
  if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed in creating uplink txq[%d]", qIndex);
      goto sfvmk_fail;
  }

  return status;

sfvmk_fail:
  return status;

}




VMK_ReturnStatus
sfvmk_initUplinkData(sfvmk_adapter_t * pAdapter)
{
  vmk_UplinkSharedData *pSharedData;
  vmk_UplinkRegData *pRegData;
  VMK_ReturnStatus status;

  VMK_ASSERT_BUG(NULL != pAdapter, "NULL adapter ptr" );
  SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, 2,"sfvmk_InitUplinkData is invoked");

  VMK_ASSERT_BUG(NULL != pAdapter->pNic, "NULL Nic ptr" );
  const efx_nic_cfg_t *pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  VMK_ASSERT_BUG(NULL != pNicCfg, "NULL cfg ptr" );

  sfvmk_updateCableType(pAdapter);

  status = sfvmk_createLock(sfvmk_ModInfo.driverName.string, VMK_SPINLOCK_RANK_HIGHEST-1,
                            &pAdapter->sharedDataWriterLock);
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


  status = sfvmk_updateSharedQueueInfo(pAdapter);
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

  return (VMK_OK);

sfvmk_create_data_fail:
  sfvmk_destroyLock(pAdapter->sharedDataWriterLock);
sfvmk_create_data_lock_fail:
  return (status);
}

VMK_ReturnStatus
sfvmk_destroyUplinkData(sfvmk_adapter_t *pAdapter)
{
  vmk_int16 qIndex;
  sfvmk_evq_t *pEVQ;

  for (qIndex = 0; qIndex < pAdapter->queueInfo.maxRxQueues; qIndex++) {
    pEVQ = pAdapter->pEvq[qIndex];
    vmk_NetPollDestroy(pEVQ->netPoll);
  }

  vmk_SpinlockDestroy(pAdapter->sharedDataWriterLock);

  return (VMK_OK);
}

static int
sfvmk_setDrvLimits( sfvmk_adapter_t *pAdapter)
{
  efx_drv_limits_t limits;

  vmk_Memset(&limits, 0, sizeof(limits));

  /* Limits are strict since take into account initial estimation */
  limits.edl_min_evq_count = limits.edl_max_evq_count =
      pAdapter->intr.numIntrAlloc;
  limits.edl_min_txq_count = limits.edl_max_txq_count =
      pAdapter->intr.numIntrAlloc + SFVMK_TXQ_NTYPES - 1;
  limits.edl_min_rxq_count = limits.edl_max_rxq_count =
      pAdapter->intr.numIntrAlloc;

  return (efx_nic_set_drv_limits(pAdapter->pNic, &limits));
}

