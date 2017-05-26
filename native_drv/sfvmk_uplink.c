#include "sfvmk_uplink.h"
#include "sfvmk_util.h"
#include "sfvmk_port.h"
#include "sfvmk_ev.h"
#include "sfvmk_rx.h"
#include "sfvmk_tx.h"
#if 1 
/****************************************************************************
 *                     vmk_UplinkQueueOps Handlers                          *
 ****************************************************************************/
static VMK_ReturnStatus sfvmk_allocQueue(vmk_AddrCookie, vmk_UplinkQueueType,
                                          vmk_UplinkQueueID *, vmk_NetPoll *);
static VMK_ReturnStatus sfvmk_allocQueueWithAttr(vmk_AddrCookie,
                                                  vmk_UplinkQueueType,
                                                  vmk_uint16,
                                                  vmk_UplinkQueueAttr *,
                                                  vmk_UplinkQueueID *,
                                                  vmk_NetPoll *);
static VMK_ReturnStatus sfvmk_freeQueue(vmk_AddrCookie, vmk_UplinkQueueID);
static VMK_ReturnStatus sfvmk_quiesceQueue(vmk_AddrCookie,
                                            vmk_UplinkQueueID);
static VMK_ReturnStatus sfvmk_startQueue(vmk_AddrCookie, vmk_UplinkQueueID);
static VMK_ReturnStatus sfvmk_applyQueueFilter(vmk_AddrCookie,
                                                vmk_UplinkQueueID,
                                                vmk_UplinkQueueFilter *,
                                                vmk_UplinkQueueFilterID *,
                                                vmk_uint32 *);
static VMK_ReturnStatus sfvmk_removeQueueFilter(vmk_AddrCookie,
                                                 vmk_UplinkQueueID,
                                                 vmk_UplinkQueueFilterID);
static VMK_ReturnStatus sfvmk_getQueueStats(vmk_AddrCookie,
                                             vmk_UplinkQueueID,
                                             vmk_UplinkStats *);
static VMK_ReturnStatus sfvmk_toggleQueueFeature(vmk_AddrCookie,
                                                  vmk_UplinkQueueID,
                                                  vmk_UplinkQueueFeature,
                                                  vmk_Bool);
static VMK_ReturnStatus sfvmk_setQueueTxPriority(vmk_AddrCookie,
                                                  vmk_UplinkQueueID,
                                                  vmk_UplinkQueuePriority);
static VMK_ReturnStatus sfvmk_setQueueCoalesceParams(vmk_AddrCookie,
                                                  vmk_UplinkQueueID,
                                                  vmk_UplinkCoalesceParams *);

static vmk_UplinkQueueOps sfvmkQueueOps = {
   .queueAlloc             = sfvmk_allocQueue,
   .queueAllocWithAttr     = sfvmk_allocQueueWithAttr,
   .queueFree              = sfvmk_freeQueue,
   .queueQuiesce           = sfvmk_quiesceQueue,
   .queueStart             = sfvmk_startQueue,
   .queueApplyFilter       = sfvmk_applyQueueFilter,
   .queueRemoveFilter      = sfvmk_removeQueueFilter,
   .queueGetStats          = sfvmk_getQueueStats,
   .queueToggleFeature     = sfvmk_toggleQueueFeature,
   .queueSetPriority       = sfvmk_setQueueTxPriority,
   .queueSetCoalesceParams = sfvmk_setQueueCoalesceParams,
};

#endif 




#if ESX_VERSION_NUMBER >= ESX_VERSION(2015)
/****************************************************************************
*                vmk_UplinkNetDumpOps Handler                              *
****************************************************************************/
static VMK_ReturnStatus sfvmk_getCableType(vmk_AddrCookie cookie,
                                                    vmk_UplinkCableType *cableType);

static VMK_ReturnStatus sfvmk_getSupportedCableTypes(vmk_AddrCookie cookie,
                                                                    vmk_UplinkCableType *ct);

static VMK_ReturnStatus sfvmk_setCableType(vmk_AddrCookie cookie,
                                                    vmk_UplinkCableType cableType);

struct vmk_UplinkCableTypeOps sfvmkCableTypeOps = {
  .getCableType = sfvmk_getCableType,
  .getSupportedCableTypes = sfvmk_getSupportedCableTypes,
  .setCableType = sfvmk_setCableType,
};

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

/****************************************************************************/

/****************************************************************************
*              vmk_UplinkTransceiverTypeOps Handler                         *
****************************************************************************/
static VMK_ReturnStatus
sfvmk_getTransceiverType(vmk_AddrCookie cookie,
                                      vmk_UplinkTransceiverType *type);

static VMK_ReturnStatus
sfvmk_setTransceiverType(vmk_AddrCookie cookie,
                                      vmk_UplinkTransceiverType type);

struct vmk_UplinkTransceiverTypeOps  sfvmkTransceiverTypeOps = {
  .getTransceiverType = sfvmk_getTransceiverType,
  .setTransceiverType = sfvmk_setTransceiverType,
};
/****************************************************************************/

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
/****************************************************************************/

/****************************************************************************
*              vmk_UplinkPhyAddressOps Handler                             *
****************************************************************************/
static VMK_ReturnStatus
sfvmk_getPhyAddress(vmk_AddrCookie cookie,
                              vmk_uint8* phyAddr);

static VMK_ReturnStatus
sfvmk_setPhyAddress(vmk_AddrCookie cookie,
                              vmk_uint8 phyAddr);

struct vmk_UplinkPhyAddressOps sfvmkPhyAddressOps = {
  .getPhyAddress = sfvmk_getPhyAddress,
  .setPhyAddress = sfvmk_setPhyAddress,
};
/****************************************************************************/
#endif

static VMK_ReturnStatus
sfvmk_linkStatusSet(vmk_AddrCookie cookie, vmk_LinkStatus *linkStatus)
{
  vmk_LogMessage("calling sfvmk_linkStatusSet\n");
  return VMK_OK;
}

/****************************************************************************
 *               vmk_UplinkCoalesceParamsOps Handlers                       *
 ****************************************************************************/
static VMK_ReturnStatus sfvmk_coalesceParamsGet(vmk_AddrCookie,
                                                 vmk_UplinkCoalesceParams *);
static VMK_ReturnStatus sfvmk_coalesceParamsSet(vmk_AddrCookie,
                                                 vmk_UplinkCoalesceParams *);

static vmk_UplinkCoalesceParamsOps sfvmkCoalesceParamsOps = {
   .getParams = sfvmk_coalesceParamsGet,
   .setParams = sfvmk_coalesceParamsSet,
};


 static VMK_ReturnStatus
 sfvmk_coalesceParamsSet(vmk_AddrCookie cookie,
													vmk_UplinkCoalesceParams *params)
 {
   vmk_LogMessage( "calling sfvmk_coalesceParamsSet\n");
   return VMK_NOT_SUPPORTED;

}


static VMK_ReturnStatus
sfvmk_coalesceParamsGet(vmk_AddrCookie cookie,
			vmk_UplinkCoalesceParams *params)
{
   vmk_LogMessage( "calling sfvmk_coalesceParamsGet\n");
   return VMK_NOT_SUPPORTED;

}


/****************************************************************************
 *                vmk_UplinkNetDumpOps Handler                              *
 ****************************************************************************/

static VMK_ReturnStatus sfvmk_panicTx(vmk_AddrCookie cookie,
                                       vmk_PktList pktList);
static VMK_ReturnStatus sfvmk_panicPoll(vmk_AddrCookie cookie,
                                         vmk_PktList pktList);
static VMK_ReturnStatus sfvmk_panicInfoGet(vmk_AddrCookie cookie,
                                            vmk_UplinkPanicInfo *panicInfo);

static vmk_UplinkNetDumpOps sfvmkNetDumpOps = {
   .panicTx = sfvmk_panicTx,
   .panicPoll = sfvmk_panicPoll,
   .panicInfoGet = sfvmk_panicInfoGet,
};
static VMK_ReturnStatus sfvmk_panicTx(vmk_AddrCookie cookie,
                                       vmk_PktList pktList)

{
   vmk_LogMessage( "calling sfvmk_panicTx\n");
    return VMK_NOT_SUPPORTED;
} 
static VMK_ReturnStatus sfvmk_panicPoll(vmk_AddrCookie cookie,
                                         vmk_PktList pktList)


{
   vmk_LogMessage( "calling sfvmk_panicPoll\n");
    return VMK_NOT_SUPPORTED;
} 

static VMK_ReturnStatus sfvmk_panicInfoGet(vmk_AddrCookie cookie,
                                            vmk_UplinkPanicInfo *panicInfo)
	
	{
   vmk_LogMessage( "calling sfvmk_panicPoll\n");
 SFVMK_DBG((sfvmk_adapter *)cookie.ptr, SFVMK_DBG_UPLINK, 3,
                  "Driver panicInfoGet");

   /* Provide the intrCookie corresponding to the default Rx Queue */
   panicInfo->clientData = cookie;
#if ESX_VERSION_NUMBER < ESX_VERSION(2015)
   panicInfo->intrCookie = ((sfvmk_adapter *) cookie.ptr)->sfvmkIntrInfo.intrCookies[0];
#endif
   return VMK_OK;
	} 

#if 1 
static VMK_ReturnStatus sfvmk_allocQueue(vmk_AddrCookie var1, vmk_UplinkQueueType var2,
                                          vmk_UplinkQueueID *var3, vmk_NetPoll *var4)
{
   return VMK_NOT_SUPPORTED;
}
static VMK_ReturnStatus sfvmk_allocQueueWithAttr(vmk_AddrCookie var1,
                                                  vmk_UplinkQueueType var2,
                                                  vmk_uint16 var3,
                                                  vmk_UplinkQueueAttr *var4,
                                                  vmk_UplinkQueueID *var5,
                                                  vmk_NetPoll *var6)
{
   return VMK_NOT_SUPPORTED;
}
static VMK_ReturnStatus sfvmk_freeQueue(vmk_AddrCookie var1, vmk_UplinkQueueID var2)
{
   return VMK_NOT_SUPPORTED;
}
static VMK_ReturnStatus sfvmk_quiesceQueue(vmk_AddrCookie var1,
                                            vmk_UplinkQueueID var2)
{
   return VMK_NOT_SUPPORTED;
}
static VMK_ReturnStatus sfvmk_startQueue(vmk_AddrCookie var1, vmk_UplinkQueueID var2)
{
   return VMK_NOT_SUPPORTED;
}
static VMK_ReturnStatus sfvmk_applyQueueFilter(vmk_AddrCookie var1,
                                                vmk_UplinkQueueID var2,
                                                vmk_UplinkQueueFilter *var3,
                                                vmk_UplinkQueueFilterID *var4,
                                                vmk_uint32 *var5)
{
   return VMK_NOT_SUPPORTED;
}
static VMK_ReturnStatus sfvmk_removeQueueFilter(vmk_AddrCookie var1,
                                                 vmk_UplinkQueueID var2,
                                                 vmk_UplinkQueueFilterID var3)
{
   return VMK_NOT_SUPPORTED;
}
static VMK_ReturnStatus sfvmk_getQueueStats(vmk_AddrCookie var1,
                                             vmk_UplinkQueueID var2,
                                             vmk_UplinkStats *var3)
{
   return VMK_NOT_SUPPORTED;
}
static VMK_ReturnStatus sfvmk_toggleQueueFeature(vmk_AddrCookie var1,
                                                  vmk_UplinkQueueID var2,
                                                  vmk_UplinkQueueFeature var3,
                                                  vmk_Bool var4)
{
   return VMK_NOT_SUPPORTED;
}
static VMK_ReturnStatus sfvmk_setQueueTxPriority(vmk_AddrCookie var1,
                                                  vmk_UplinkQueueID var2,
                                                  vmk_UplinkQueuePriority var3)
{
   return VMK_NOT_SUPPORTED;
}
static VMK_ReturnStatus sfvmk_setQueueCoalesceParams(vmk_AddrCookie var1,
                                                  vmk_UplinkQueueID var2,
                                                  vmk_UplinkCoalesceParams *var3)
{
   return VMK_NOT_SUPPORTED;
}
#endif 

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


static int
sfxge_set_drv_limits( sfvmk_adapter *adapter)
{
  efx_drv_limits_t limits;

  vmk_Memset(&limits, 0, sizeof(limits));

  /* Limits are strict since take into account initial estimation */
  limits.edl_min_evq_count = limits.edl_max_evq_count =
      adapter->sfvmkIntrInfo.numIntrAlloc;
  limits.edl_min_txq_count = limits.edl_max_txq_count =
      adapter->sfvmkIntrInfo.numIntrAlloc + SFVMK_TXQ_NTYPES - 1;
  limits.edl_min_rxq_count = limits.edl_max_rxq_count =
      adapter->sfvmkIntrInfo.numIntrAlloc;

  return (efx_nic_set_drv_limits(adapter->enp, &limits));
}

int
sfvmk_intr_start(sfvmk_adapter *adapter)
{
	struct sfvmk_intr *intr;
        VMK_ReturnStatus status;
        //int index;  

	//efsys_mem_t *esmp;
	int rc;

	intr = &adapter->sfvmkIntrInfo;
	//esmp = &intr->status;

	//VMK_ASSERT(intr->state == SFVMK_INTR_INITIALIZED);

	/* Zero the memory. */
	//vmk_Memset(esmp->esm_base, 0, EFX_INTR_SIZE);

	/* Initialize common code interrupt bits. */
	(void)efx_intr_init(adapter->enp, intr->type, NULL);
         

          vmk_LogMessage("efx_intr_init\n");

	 /* Register all the interrupts */
   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 3,
                  "sfvmk register interrupts");
   status = sfvmk_setup_Interrupts(adapter);
          vmk_LogMessage("sfvmk_setup_Interrupts\n");
   if (status != VMK_OK) {
      vmk_WarningMessage("%s sfvmk driver: Failed to register interrupts",
                         adapter->pciDeviceName.string);
      status = VMK_FAILURE;
      goto sfvmk_intr_registration_fail;
   }
	 
	intr->state = SFVMK_INTR_STARTED;

	/* Enable interrupts at the NIC */
	efx_intr_enable(adapter->enp);

          vmk_LogMessage("efx_intr_enable\n");


#if 0 

	for (index = 0; index < 30; index++) {
		rc = efx_intr_trigger(adapter->enp, index);
                if (0 != rc)
                  vmk_LogMessage("Failed in triggering interrupt\n");
                    
	}
#endif 
	return (0);

sfvmk_intr_registration_fail:
	/* Tear down common code interrupt bits. */
	efx_intr_fini(adapter->enp);

	intr->state = SFVMK_INTR_INITIALIZED;

	return (rc);
}


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


/*
 ***********************************************************************
 *
 * sfvmk_isTxQueueStopped
 *
 *      Local function that is invoked from sfvmk_pollTx and
 *      and sfvmk_uplinkTx
 *
 *
 *      param[in] driverData       pointer to uplink dev
 *      param[in] txQueue          tx queue id
 *
 * Results:
 *      retval: VMK_TRUE        Tx Queue has stopped
 *      retval: VMK_FALSE        Tx Queue  is not stopped
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

vmk_Bool
sfvmk_isTxQueueStopped(sfvmk_adapter * devData, vmk_uint32 txQueue)
{
   vmk_UplinkSharedQueueData *txQueueData;

   /* Get shared data */
   txQueueData = SFVMK_GET_TX_SHARED_QUEUE_DATA(devData);

   if (txQueueData[txQueue].state == VMK_UPLINK_QUEUE_STATE_STOPPED) {
      return VMK_TRUE;
   }

   return VMK_FALSE;
}

static VMK_ReturnStatus sfvmk_uplinkTx(vmk_AddrCookie cookie, vmk_PktList pktList)
{
   sfvmk_adapter *devData = (sfvmk_adapter *)cookie.ptr;
   vmk_PktHandle *pkt;
   vmk_UplinkQueueID vmkQid;
   vmk_uint32 txq_ind = 0;
   VMK_ReturnStatus status = VMK_OK;
   vmk_ByteCountSmall pktLen;
   vmk_Bool isFirstPkt = VMK_TRUE;
   vmk_int16 maxRxQueues = 0, maxTxQueues = 0;
            
   vmk_LogMessage("sfvmk_uplinkTx called");

   VMK_PKTLIST_ITER_STACK_DEF(iter);
   sfvmk_for_all_pkts_in_list(iter, pktList) {
      vmk_PktListIterRemovePkt(iter, &pkt);
      VMK_ASSERT(pkt);

      maxRxQueues = devData->queueInfo.maxRxQueues;
      maxTxQueues = devData->queueInfo.maxTxQueues;
   
      vmk_LogMessage("maxRxQueues: %d, maxTxQueues: %d", maxRxQueues, maxTxQueues);

      if (isFirstPkt) {

         /*
          * Get QId from first packet and use it for the rest of the
          * packets in that list.
          */
         vmkQid = vmk_PktQueueIDGet(pkt);
         txq_ind = vmk_UplinkQueueIDVal(vmkQid);
      

         vmk_LogMessage("txq_ind: %d", txq_ind);
         if ((!txq_ind) ||
             (txq_ind >= (maxTxQueues + maxRxQueues))) {
#if 0
            static vmk_uint32 sfvmk_txQidThrottle = 0;
            SFVMK_THROTTLED_LOG(sfvmk_txQidThrottle,
                                 "Invalid QID %d, numTxQueue %d,  device %s",
                                 txq_ind,
                                 maxTxQueues,
                                 vmk_NameToString(&devData->uplinkName));
            devData->drv_stats.count_invalid_txq_mapping++;
#endif
            vmk_LogMessage("Invalid QID %d, numTxQueue %d,  device %s",
                                 txq_ind,
                                 maxTxQueues,
                                 vmk_NameToString(&devData->uplinkName));
            goto sfvmk_tx_drop_cont;
         }
         txq_ind -= maxRxQueues;
         isFirstPkt = VMK_FALSE;
      }

      if (sfvmk_isTxQueueStopped(devData, txq_ind)) {
         vmk_LogMessage("sfvmk_isTxQueueStopped returned TRUE");
         status = VMK_BUSY;
         goto sfvmk_tx_put_back_bail;
      }

      pktLen = vmk_PktFrameLenGet(pkt);
      vmk_LogMessage("pktLen: %d", pktLen);
      if (pktLen > SFVMK_MAX_PKT_SIZE) {
#if 0
         static vmk_uint32 sfvmk_txLenThrottle = 0;
         SFVMK_THROTTLED_LOG(sfvmk_txLenThrottle,
                              "Invalid len %d, device %s",
                              pktLen, vmk_NameToString(&devData->uplinkName));
         devData->drv_stats.tso_gr_than_64k++;
#endif
         vmk_LogMessage("Invalid len %d, device %s",
                              pktLen, vmk_NameToString(&devData->uplinkName));
         goto sfvmk_tx_drop_cont;
      }

      vmk_LogMessage("Calling sfvmk_queuedPkt");
      sfvmk_queuedPkt(devData, devData->txq[txq_ind], pkt, pktLen);

      continue;

sfvmk_tx_drop_cont:
      vmk_PktRelease(pkt);
      continue;
sfvmk_tx_put_back_bail:
      vmk_PktListIterInsertPktBefore(iter, pkt);
      break;
   }
   return (status);
}

static VMK_ReturnStatus sfvmk_uplinkMTUSet(vmk_AddrCookie var1, vmk_uint32 var2)
	{
	
        vmk_LogMessage(" calling  sfvmk_uplinkMTUSet ");
		return VMK_OK;
	}

static VMK_ReturnStatus sfvmk_uplinkStateSet(vmk_AddrCookie var1 ,
                                              vmk_UplinkState var2)
	{
	
        vmk_LogMessage(" calling  sfvmk_uplinkStateSet");
		return VMK_OK;
	}

static VMK_ReturnStatus sfvmk_uplinkStatsGet(vmk_AddrCookie cookie,
                                              vmk_UplinkStats *stats )
{
  sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;
  uint64_t *mac_stats;
  
  vmk_LogMessage(" calling  sfvmk_uplinkStatsGet >>>>> ");
  sfvmk_MacStatsUpdate(adapter);
  vmk_Memset(stats, 0, sizeof(*stats));

  mac_stats = (uint64_t *)adapter->port.mac_stats.decode_buf;
  
  stats->rxPkts = mac_stats[EFX_MAC_RX_PKTS];
  stats->rxErrors = mac_stats[EFX_MAC_RX_ERRORS];
  stats->txPkts = mac_stats[EFX_MAC_TX_PKTS];
  stats->txErrors = mac_stats[EFX_MAC_TX_ERRORS];
  stats->collisions = 
	    mac_stats[EFX_MAC_TX_SGL_COL_PKTS] +
	    mac_stats[EFX_MAC_TX_MULT_COL_PKTS] +
	    mac_stats[EFX_MAC_TX_EX_COL_PKTS] +
	    mac_stats[EFX_MAC_TX_LATE_COL_PKTS];
  stats->rxBytes = mac_stats[EFX_MAC_RX_OCTETS]; 
  stats->txBytes = mac_stats[EFX_MAC_TX_OCTETS]; 
  stats->rxMulticastPkts = mac_stats[EFX_MAC_RX_MULTICST_PKTS];
  stats->txMulticastPkts = mac_stats[EFX_MAC_TX_MULTICST_PKTS];
  stats->rxBroadcastPkts = mac_stats[EFX_MAC_RX_BRDCST_PKTS];
  stats->txBroadcastPkts = mac_stats[EFX_MAC_TX_BRDCST_PKTS];
  stats->rxFrameAlignErrors = mac_stats[EFX_MAC_RX_ALIGN_ERRORS];
  stats->rxDrops = mac_stats[EFX_MAC_RX_NODESC_DROP_CNT];

  /* Update driver copy of stats*/
  memcpy(adapter->mac_stats, mac_stats, sizeof(EFX_MAC_NSTATS * sizeof(uint64_t)));
  return VMK_OK;
}



static VMK_ReturnStatus sfvmk_uplinkCapEnable(vmk_AddrCookie var1, vmk_UplinkCap var2)

{

        vmk_LogMessage(" calling sfvmk_uplinkCapEnable ");
	return VMK_OK;
}

static VMK_ReturnStatus sfvmk_uplinkCapDisable(vmk_AddrCookie var1 ,
                                                vmk_UplinkCap var2)

{

        vmk_LogMessage(" calling  sfvmk_uplinkCapDisable ");
	return VMK_OK;
}

void sfvmk_updateQueueStatus(sfvmk_adapter *adapter, vmk_UplinkQueueState qState)
{
   vmk_UplinkSharedQueueData *queueData;
   struct sfvmk_txq *txq;
   vmk_uint16 i;

   vmk_LogMessage("sfvmk_updateQueueStatus called for state: %d", qState);
   SFVMK_SHARED_AREA_BEGIN_WRITE(adapter);
   queueData = SFVMK_GET_TX_SHARED_QUEUE_DATA(adapter);
   for (i=0; i<adapter->queueInfo.maxTxQueues; i++) {
      vmk_LogMessage("queueData flags: %x", queueData[i].flags);
      if (queueData[i].flags & (VMK_UPLINK_QUEUE_FLAG_IN_USE|VMK_UPLINK_QUEUE_FLAG_DEFAULT)) {
	 txq = adapter->txq[i];

	 /* TODO: if the queue is nearly full, don't honor the queue start request */
   	 queueData[i].state = qState;
      }
   }
   SFVMK_SHARED_AREA_END_WRITE(adapter);

   for (i=0; i<adapter->queueInfo.maxTxQueues; i++) {
      if (queueData[i].flags & (VMK_UPLINK_QUEUE_FLAG_IN_USE|VMK_UPLINK_QUEUE_FLAG_DEFAULT)) {
	 txq = adapter->txq[i];

	 /* TODO: if the queue is nearly full, don't honor the queue start request */
         if (qState == VMK_UPLINK_QUEUE_STATE_STOPPED) {
            vmk_UplinkQueueStop(adapter->uplink, queueData[i].qid);
         } else {
            vmk_UplinkQueueStart(adapter->uplink, queueData[i].qid);
         }

      }
   }
}

static VMK_ReturnStatus sfvmk_uplinkStartIO(void * driverData) 
{


//  VMK_ReturnStatus status = VMK_OK;
  //vmk_int32 nic_status;
  //struct sfvmk_txObj *txo;
  //struct sfvmk_rxObj *rxo;
  //struct sfvmk_eqObj *eqo;
 // vmk_int16 i;
  sfvmk_adapter *adapter = (sfvmk_adapter *) driverData;
 // sfvmk_nicPoll *nicPoll;
  int rc;

  //VMK_ASSERT(adapter != NULL);

  adapter->uplinkName = vmk_UplinkNameGet(adapter->uplink);
  SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 0, "Received Uplink Start I/O");

  vmk_LogMessage( "calling sfvmk_uplinkStatrtIO\n");

#if 0 

  if (adapter->init_state == SFVMK_STARTED)
    return (0);

  if (adapter->init_state != SFVMK_REGISTERED) {
    rc = EINVAL;
    goto fail;
  }
#endif 
  /* Set required resource limits */
  if ((rc = sfxge_set_drv_limits(adapter)) != 0)
    goto fail;
 
  efx_nic_fini(adapter->enp);
  if ((rc = efx_nic_init(adapter->enp)) != 0)
    goto fail;
  vmk_LogMessage( "calling efx_nic_init\n");

  /* Start processing interrupts. */
  if ((rc = sfvmk_intr_start(adapter)) != 0)
	goto fail2;

  vmk_LogMessage(" calling  sfvmk_ev_start\n ");

   sfvmk_ev_start(adapter);

  vmk_LogMessage(" calling  sfvmk_port_start\n ");
   sfvmk_port_start(adapter);

  vmk_LogMessage(" calling  sfvmk_rx_start\n ");
   sfvmk_rx_start(adapter);
  
  vmk_LogMessage(" calling  sfvmk_tx_start\n ");
   sfvmk_tx_start(adapter);
  vmk_LogMessage(" Exiting  sfvmk_uplinkStartIO \n");


  /*Start/Stop the queues */
  sfvmk_updateQueueStatus(adapter,
        (adapter->sharedData.link.state = VMK_LINK_STATE_DOWN)?VMK_UPLINK_QUEUE_STATE_STOPPED:VMK_UPLINK_QUEUE_STATE_STARTED);


	return VMK_OK;
fail2:
  efx_nic_fini(adapter->enp);
fail:
    return VMK_FAILURE;

}

static VMK_ReturnStatus sfvmk_uplinkQuiesceIO(vmk_AddrCookie var1)

{

        vmk_LogMessage(" calling sfvmk_uplinkQuiesceIO");
	return VMK_OK;
}

static VMK_ReturnStatus sfvmk_uplinkReset(vmk_AddrCookie var1) 

{


        vmk_LogMessage(" calling sfvmk_uplinkReset");
	return VMK_OK;
}





 /*
  **************************************************************************
  *
  * sfvmk_updateCableType
  *
  *      Local function invoked from sfvmk_cmdUpdatePhyInfo
  *
  *      param[in] adapter       pointer to uplink dev
  *
  * Results:
  *      retval:  None
  *
  * Side effects:
  *      None
  *
  **************************************************************************
  */
 void
 sfvmk_updateCableType(sfvmk_adapter *adapter)
 {
    struct sfvmk_adptrPhyInfo *phy = &adapter->phy;


    efx_phy_media_type_get(adapter->enp, &phy->interface_type);
 
    switch (phy->interface_type) {
       case EFX_PHY_MEDIA_BASE_T:
          phy->cable_type = VMK_UPLINK_CABLE_TYPE_TP;
          break;
          
       case EFX_PHY_MEDIA_SFP_PLUS:
       case EFX_PHY_MEDIA_QSFP_PLUS:
       case EFX_PHY_MEDIA_XFP:
          phy->cable_type = VMK_UPLINK_CABLE_TYPE_FIBRE;
          break;
          
       default:
          phy->cable_type = VMK_UPLINK_CABLE_TYPE_OTHER;
    }
 
    return;
 }
 



/*
 ***********************************************************************
 *
 * sfvmk_netPollCB
 *
 *      Callback invoked by vmkernel net poll thread to poll for
 *      Tx/Rx/MCC  completions
 *
 *
 *      param[in] nicPoll    pointer to sfvmk_nicPoll object
 *                           registered with vmkernel.
 *      param[in] budget     Indicates the number of packets to be
 *                           processed in each invocation
 *
 * Results:
 *      retval: VMK_TRUE       Completions are still pending.
 *                             Indicate to vmkernel to
 *                             Continue to invoke this callback
 *              VMK_FALSE      No completion pending. Poll
 *                             should be stopped
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

vmk_Bool
sfvmk_netPollCB(void *evq, vmk_uint32 budget)
{

    vmk_LogMessage("*************************************************\n");
    vmk_LogMessage("calling net poll callback \n");
    vmk_LogMessage("*************************************************\n");
    sfvmk_ev_qpoll(evq);
    return 0 ; 
}


/*
 ***********************************************************************
 *
 * SFVMK_allocTxQueue
 *
 *      Local function to allocate Tx Queue
 *
 *
 *      param[in] driver        Struct holding driverData
 *      param[in] qid           qid value
 *
 * Results:
 *      retval: VMK_OK           Successfully set the state
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_allocTxQueue(sfvmk_adapter *driver,
                    vmk_UplinkQueueID *qid)
{
   vmk_UplinkSharedQueueData *qData;
   struct sfvmk_txObj *txQueue;
   vmk_uint32 i, flags, qidVal;

   qData = SFVMK_GET_TX_SHARED_QUEUE_DATA(driver);
   for (i=0; i<driver->queueInfo.maxTxQueues; i++) {
      if ((qData[i].flags & VMK_UPLINK_QUEUE_FLAG_IN_USE) == 0) {
         break;
      }
   }

   if (i >= driver->queueInfo.maxTxQueues) {
      SFVMK_ERR(driver, "No free tx queues");
      return VMK_FAILURE;
   }

   // Make Tx Queue ID
   qidVal = sfvmk_getQIDValByQData(driver, &qData[i]);
   vmk_UplinkQueueMkTxQueueID(qid, qidVal, qidVal);

   // Setup flags and qid for the allocated Tx queue
   SFVMK_SHARED_AREA_BEGIN_WRITE(driver);
   txQueue = sfvmk_getTxQueueByQID(driver, *qid);
   flags = sfvmk_isDefaultTxQueue(driver, txQueue) ?
      VMK_UPLINK_QUEUE_FLAG_IN_USE | VMK_UPLINK_QUEUE_FLAG_DEFAULT :
      VMK_UPLINK_QUEUE_FLAG_IN_USE;
   qData[i].flags &= ~(VMK_UPLINK_QUEUE_FLAG_DEFAULT |
      VMK_UPLINK_QUEUE_FLAG_IN_USE);
   qData[i].flags |= flags;
   qData[i].qid = *qid;
   SFVMK_SHARED_AREA_END_WRITE(driver);

   return VMK_OK;
}




/*
 ***********************************************************************
 *
 * sfvmk_allocRxQueue
 *
 *      Local function to allocate Rx Queue
 *
 *
 *      param[in] driver        Struct holding driverData
 *      param[in] feat          queue feature
 *      param[in] qid           qid value
 *      param[in] netpoll       Netpoll
 *
 * Results:
 *      retval: VMK_OK           Successfully set.
 *              VMK_FAILURE      Operation failed
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_allocRxQueue(sfvmk_adapter *driver,
                    vmk_UplinkQueueFeature feat,
                    vmk_UplinkQueueID *qid,
                    vmk_NetPoll *netpoll)
{
   vmk_UplinkSharedQueueData *qData;
   struct sfvmk_rxObj *rxQueue;
   vmk_uint32 i, flags, qidVal;

   qData = SFVMK_GET_RX_SHARED_QUEUE_DATA(driver);

    #if 0
   /* If the Queue is for RSS feature - use RSS leading Q */
   if (feat & (VMK_UPLINK_QUEUE_FEAT_RSS |
              VMK_UPLINK_QUEUE_FEAT_RSS_DYN)) {
      i = SFVMK_RSS_START_INDEX(driver);
      if (qData[i].flags & VMK_UPLINK_QUEUE_FLAG_IN_USE) {
         SFVMK_LOG_ERR(driver, "RSS leading queue already allocated");
         return VMK_FAILURE;
      }
   } else
   #endif
   {
      /* When looking for Non-RSS queues, Search for free
         queue index among the net queues. */
      for (i=0; i < SFVMK_RSS_START_INDEX(driver); i++) {
         if ((qData[i].flags & VMK_UPLINK_QUEUE_FLAG_IN_USE) == 0) {
            break;
         }
      }
      if (i >= SFVMK_RSS_START_INDEX(driver)) {
         return VMK_FAILURE;
      }
   }

   // Make Rx QID
   qidVal = sfvmk_getQIDValByQData(driver, &qData[i]);
   vmk_UplinkQueueMkRxQueueID(qid, qidVal, qidVal);
   *netpoll = qData[i].poll;

   // Setup flags and qid for the allocated Rx queue
   SFVMK_SHARED_AREA_BEGIN_WRITE(driver);
   rxQueue = sfvmk_getRxQueueByQID(driver, *qid);
   flags = sfvmk_isDefaultRxQueue(driver, rxQueue) ?
           VMK_UPLINK_QUEUE_FLAG_IN_USE | VMK_UPLINK_QUEUE_FLAG_DEFAULT :
           VMK_UPLINK_QUEUE_FLAG_IN_USE;
   qData[i].flags &= ~(VMK_UPLINK_QUEUE_FLAG_DEFAULT |
                       VMK_UPLINK_QUEUE_FLAG_IN_USE);
   qData[i].flags |= flags;
   qData[i].qid = *qid;
   SFVMK_SHARED_AREA_END_WRITE(driver);

   //SFVMK_DBG(driver, SFVMK_DBG_QUEUE, 2,
     //             "sfvmk_allocRxQueue %u alloced", qidVal);

   return VMK_OK;
}



VMK_ReturnStatus
sfvmk_createUplinkData(sfvmk_adapter * adapter)
{
   vmk_UplinkSharedData *sharedData;
   vmk_UplinkRegData *regData;
   vmk_UplinkSharedQueueInfo *queueInfo;
   vmk_UplinkSharedQueueData *queueData;
   vmk_ServiceAcctID serviceID;
   vmk_SpinlockCreateProps spinLockProps;
   vmk_NetPoll netpoll;
   VMK_ReturnStatus status;
   struct sfvmk_txObj *txQueue;
   struct sfvmk_rxObj *rxQueue;
   vmk_int16 i;

   SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 2,"sfvmk_CreateUplinkData Entered");
   vmk_LogMessage("sfvmk_CreateUplinkData Entered");

   //praveen will do it later on
   //sfvmk_cmdUpdatePhyInfo(adapter);
   //sfvmk_updateSpeedFromPhy(adapter);
/*
   if ((SFVMK_IS_DEV_STATE(adapter, RESET))) {
      goto sfvmk_shared_data_init;
   }
*/
   /* Create sharedData writer lock to sync among
      multiple writers in driver */


   const efx_nic_cfg_t *encp = efx_nic_cfg_get(adapter->enp);


   sfvmk_updateCableType(adapter);
   spinLockProps.moduleID = vmk_ModuleCurrentID;
   spinLockProps.heapID = sfvmk_ModInfo.heapID;
   spinLockProps.type = VMK_SPINLOCK;
   spinLockProps.domain = sfvmk_ModInfo.lockDomain;
   spinLockProps.rank = 1;
   vmk_NameCopy(&spinLockProps.name, &sfvmk_ModInfo.driverName);

   status = vmk_SpinlockCreate(&spinLockProps,
                               &adapter->sharedDataWriterLock);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Failed to create writer lock for shared data area");
                     vmk_LogMessage("Failed to create writer lock for shared data area");
      goto sfvmk_create_data_lock_fail;
   }

//sfvmk_shared_data_init:

   /* Initialize shared data area */
   sharedData = &adapter->sharedData;
   adapter->supportedModes[0].speed = 1000;
   adapter->supportedModes[1].speed = 10000;

   adapter->supportedModes[0].duplex = VMK_LINK_DUPLEX_FULL;
   adapter->supportedModes[1].duplex = VMK_LINK_DUPLEX_FULL;

   adapter->supportedModesArraySz =2 ;

   vmk_VersionedAtomicInit(&sharedData->lock); 

   sharedData->supportedModes = adapter->supportedModes;
   sharedData->supportedModesArraySz = adapter->supportedModesArraySz;

   sharedData->flags = 0;
   sharedData->state = VMK_UPLINK_STATE_ENABLED;
   sharedData->link.state = VMK_LINK_STATE_DOWN;
   sharedData->link.speed = 0;
   sharedData->link.duplex = VMK_LINK_DUPLEX_HALF;

   sharedData->mtu = adapter->mtu = 1500;

   vmk_Memcpy(adapter->sharedData.macAddr, encp->enc_mac_addr, VMK_ETH_ADDR_LENGTH);
   vmk_Memcpy(adapter->sharedData.hwMacAddr, encp->enc_mac_addr,VMK_ETH_ADDR_LENGTH);

   vmk_LogMessage("value of interrupt linit is %d\n",encp->enc_intr_limit);
   /* populate queue info */
   sharedData->queueInfo = queueInfo = &adapter->queueInfo;
   queueInfo->supportedQueueTypes = VMK_UPLINK_QUEUE_TYPE_TX |
      					VMK_UPLINK_QUEUE_TYPE_RX;
   queueInfo->supportedRxQueueFilterClasses =
       VMK_UPLINK_QUEUE_FILTER_CLASS_MAC_ONLY;

#if 0
   if (SFVMK_IS_VXLAN_OFFLOAD_ENABLED(adapter)) {
      queueInfo->supportedRxQueueFilterClasses |=
	     VMK_UPLINK_QUEUE_FILTER_CLASS_VXLAN;
   }
#endif
#if 0
   if (SFVMK_IS_RSS_ENABLED(adapter)) {
      queueInfo->maxTxQueues =
         ((SFVMK_GET_NUM_QUEUES(adapter) - SFVMK_RSS_RING_SETS + 1));

      queueInfo->maxRxQueues =
         (SFVMK_GET_NUM_QUEUES(adapter) - SFVMK_RSS_RING_SETS + 1);
   } else
#endif
   {
      /*
       * BE2 has a constraint when using  converged traffic
       * because of sharing of Tx queues with storage function.
       * Only one Tx Q can be supported per PF in BE2.
       * Hence Tx Netqueues are disabled for BE2.
       * Tx traffic will use a single Tx Queue i.e default Q.
       */
      queueInfo->maxTxQueues = 1;// adapter->txq_count;
      queueInfo->maxRxQueues = 1 ; //adapter->rxq_count;
   }
   SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 2, "maxTxQs: %d maxRxQs: %d ",
                  queueInfo->maxTxQueues, queueInfo->maxRxQueues);

   vmk_LogMessage("max tx queue = %d max rx queue %d\n", queueInfo->maxTxQueues, queueInfo->maxRxQueues);
   queueInfo->activeRxQueues = 0;
   queueInfo->activeTxQueues = 0;

   status = vmk_ServiceGetID(VMK_SERVICE_ACCT_NAME_NET, &serviceID);
   //VMK_ASSERT(status == VMK_OK);

   queueInfo->queueData = adapter->queueData;

   /* populate RX queues */
   for (i = 0; i < queueInfo->maxRxQueues; i++) {
      vmk_NetPollProperties pollProp;
      sfvmk_nicPoll *nicPoll;

      queueData = &adapter->queueData[i];
      queueData->flags = VMK_UPLINK_QUEUE_FLAG_UNUSED;
      queueData->type =  VMK_UPLINK_QUEUE_TYPE_RX;
      queueData->state = VMK_UPLINK_QUEUE_STATE_STOPPED;

      if (i == 0) {
         queueData->supportedFeatures = 0;
      }
			else {
         queueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_PAIR |
				 																VMK_UPLINK_QUEUE_FEAT_DYNAMIC;

#if 0
#if ESX_VERSION_NUMBER >= ESX_VERSION(2015)
         /* DYNAMIC Net Queues will not be supported when RSS is enabled. */
         if (!SFVMK_IS_RSS_ENABLED(adapter)) {
            queueData->supportedFeatures |= VMK_UPLINK_QUEUE_FEAT_DYNAMIC;
         }
         /* Enable RSS Feature support for RSS Qs*/
         if (SFVMK_IS_RSS_ENABLED(adapter) &&
            (i >= SFVMK_RSS_START_INDEX(adapter))) {
            queueData->supportedFeatures |= VMK_UPLINK_QUEUE_FEAT_RSS_DYN;
         }
#else
         /* Enable RSS Feature support for RSS Qs*/
         if (SFVMK_IS_RSS_ENABLED(adapter) &&
            (i >= SFVMK_RSS_START_INDEX(adapter))) {
            queueData->supportedFeatures |= VMK_UPLINK_QUEUE_FEAT_RSS;
         }
#endif
#endif

      }
      queueData->activeFeatures = 0;

			//praveen not sure what to fill over here
			/*
      if (i == 0) {
         queueData->maxFilters = adapter->fwRes.max_uc_mac;
      } else {
	     queueData->maxFilters = adapter->fwRes.max_uc_mac/adapter->num_rx_qs;
	     if (adapter->fwRes.max_uc_mac % adapter->num_rx_qs) {
	        queueData->maxFilters++;
	     }

      }
      */
      queueData->dmaEngine = adapter->vmkDmaEngine;

      queueData->activeFilters = 0;
      adapter->rx_obj[i].qData = queueData;

      nicPoll = &adapter->nicPoll[i];

			//praveen needs to check
			/*
      if ((SFVMK_IS_DEV_STATE(adapter, RESET))) {
         goto sfvmk_update_nicPoll;
      }
       */
      nicPoll->adapter = (struct sfvmk_adapter*)adapter;
      nicPoll->queueData = queueData;
      nicPoll->rxo = &adapter->rx_obj[i];

      pollProp.poll = sfvmk_netPollCB;
      pollProp.priv.ptr = adapter->evq[i];
      pollProp.deliveryCallback = NULL;
      pollProp.features = VMK_NETPOLL_NONE;
      status = vmk_NetPollCreate(&pollProp, serviceID, vmk_ModuleCurrentID,
                                 &nicPoll->netPoll);
      if (status != VMK_OK) {
         for (i--; i >= 0; i--) {
            nicPoll = &adapter->nicPoll[i];
            vmk_NetPollDestroy(nicPoll->netPoll);
            vmk_LogMessage("error in creating netpoll\n");
         }
         goto sfvmk_create_data_fail;
      }

      queueData->poll = nicPoll->netPoll;
      nicPoll->state = 1;

//sfvmk_update_nicPoll:
      nicPoll->vector = adapter->sfvmkIntrInfo.intrCookies[i];

      SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 3, "RXq=%d, poll=0x%p, "
                     "flag=0x%x", i, queueData->poll, queueData->flags);
     vmk_LogMessage("RXq=%d, poll=0x%p, "
                     "flag=0x%x", i, queueData->poll, queueData->flags);
      /* TBD: queueData->coalesceParams: Add support when AIC is enabled */

   }
#if 0
   /* RSS Qs are hidden from the kernel. So only creat the NetPoll */
   if (SFVMK_IS_RSS_ENABLED(adapter)) {
      for (i = queueInfo->maxRxQueues;
           i < (queueInfo->maxRxQueues + SFVMK_RSS_RING_SETS - 1); i++) {

         vmk_NetPollProperties pollProp;
         sfvmk_nicPoll *nicPoll;

         nicPoll = &adapter->nicPoll[i];
         if ((SFVMK_IS_DEV_STATE(adapter, RESET))) {
            goto sfvmk_update_rss_nicPoll;
         }

         nicPoll->adapter = adapter;
         nicPoll->queueData = NULL;
         nicPoll->rxo = &adapter->rx_obj[i];

         pollProp.poll = sfvmk_netPollCB;
         pollProp.priv.ptr = nicPoll;
         pollProp.deliveryCallback = NULL;
         pollProp.features = VMK_NETPOLL_NONE;
         status = vmk_NetPollCreate(&pollProp, serviceID,
                                    sfvmkDriver.moduleID,
                                    &nicPoll->netPoll);
         if (status != VMK_OK) {
            for (i--; i >= 0; i--) {
               nicPoll = &adapter->nicPoll[i];
               vmk_NetPollDestroy(nicPoll->netPoll);
            }
            goto sfvmk_create_data_fail;
         }
         nicPoll->state = 1;

sfvmk_update_rss_nicPoll:
         nicPoll->vector = adapter->intrCookies[i];

         SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 3, "RXq=%d, poll=0x%p",
                        i, nicPoll->netPoll);
         /* TBD: queueData->coalesceParams: Add support when AIC is enabled */
      }
   }
#endif

   /* Populate TX queues */
   for (i = queueInfo->maxRxQueues;
        i < (queueInfo->maxRxQueues + queueInfo->maxTxQueues); i++) {
      queueData = &adapter->queueData[i];
      queueData->flags = VMK_UPLINK_QUEUE_FLAG_UNUSED;
      queueData->type = VMK_UPLINK_QUEUE_TYPE_TX;
      queueData->state = VMK_UPLINK_QUEUE_STATE_STOPPED;
      if (i == queueInfo->maxRxQueues) {
         queueData->supportedFeatures = 0;
      } else {
         queueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_PAIR;
      }
      queueData->activeFeatures =  0;
      queueData->dmaEngine = adapter->vmkDmaEngine;
      queueData->priority = VMK_VLAN_PRIORITY_MINIMUM;
      adapter->tx_obj[i-queueInfo->maxRxQueues].qData = queueData;
      SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 3, "txQ=%d,flag=0x%x",
                     i, queueData->flags);
      vmk_LogMessage("txQ=%d,flag=0x%x",
                     i, queueData->flags);
      /* TBD: queueData->coalesceParams: Add support when AIC is enabled */
   }

   /*
    * Allocate default RX and TX queues, but don't activate them;
    * activation will be done in UplinkStartIO callback.
    */
   queueInfo->defaultRxQueueID = 0;
   status = sfvmk_allocRxQueue(adapter, 0, &queueInfo->defaultRxQueueID,
                                &netpoll);
   //VMK_ASSERT(status == VMK_OK);
   rxQueue = sfvmk_getRxQueueByQID(adapter, queueInfo->defaultRxQueueID);
   //VMK_ASSERT(sfvmk_isDefaultRxQueue(adapter, rxQueue));
   //VMK_ASSERT(vmk_UplinkQueueIDType(queueInfo->defaultRxQueueID) ==
   //           VMK_UPLINK_QUEUE_TYPE_RX);
   //VMK_ASSERT(vmk_UplinkQueueIDVal(queueInfo->defaultRxQueueID) ==
   //           sfvmk_getQIDValByQData(adapter, rxQueue->qData));

   queueInfo->defaultTxQueueID = 0;
   status = sfvmk_allocTxQueue(adapter, &queueInfo->defaultTxQueueID);
   //VMK_ASSERT(status == VMK_OK);
   txQueue = sfvmk_getTxQueueByQID(adapter, queueInfo->defaultTxQueueID);
  #if 0 
   VMK_ASSERT(sfvmk_isDefaultTxQueue(adapter, txQueue));
   VMK_ASSERT(vmk_UplinkQueueIDType(queueInfo->defaultTxQueueID) ==
              VMK_UPLINK_QUEUE_TYPE_TX);
   VMK_ASSERT(vmk_UplinkQueueIDVal(queueInfo->defaultTxQueueID) ==
              sfvmk_getQIDValByQData(adapter, txQueue->qData));
#endif
   regData = &adapter->regData;
   regData->apiRevision = VMKAPI_REVISION;
   regData->moduleID = vmk_ModuleCurrentID;
   regData->ops = sfvmkUplinkOps;
   regData->sharedData = sharedData;
   regData->driverData.ptr = adapter;

   return (VMK_OK);

sfvmk_create_data_fail:
   vmk_SpinlockDestroy(adapter->sharedDataWriterLock);
sfvmk_create_data_lock_fail:
   return (status);
}


/*
 ***********************************************************************
 *
 * sfvmk_registerIOCaps
 *
 *      Local function that is invoked from sfvmk_uplinkCapsRegister
 *
 *      Registers supported capabilities with vmkernel
 *
 *      param[in] driverData       pointer to uplink dev
 *
 * Results:
 *      retval: VMK_OK       Register uplink caps with vmkernel
 *                           succeeded
 *              VMK_FAILURE  Register uplink caps failed
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_registerIOCaps(sfvmk_adapter * adapter)
{
   VMK_ReturnStatus status;

   status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_SG_TX, NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter, "SG_TX cap register failed with error 0x%x",status);
      //VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
                                  VMK_UPLINK_CAP_MULTI_PAGE_SG, NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,"MULTI_PAGE_SG cap register failed with error 0x%x",
                  status);
      //VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
	 																VMK_UPLINK_CAP_IPV4_CSO, NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,"IPv4_CSO cap register failed with error 0x%x",
                     status);
      //VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
	 																VMK_UPLINK_CAP_IPV4_TSO, NULL);
   if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
      SFVMK_ERR(adapter, "IPv4_TSO cap register failed with error 0x%x",
                     status);
      //VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_IPV6_CSO, NULL);
   if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
      SFVMK_ERR(adapter, "IPv6_CSO cap register failed with error 0x%x",
                     status);
      //VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
	 																	VMK_UPLINK_CAP_IPV6_TSO, NULL);
   if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
      SFVMK_ERR(adapter,"IPv6_TSO cap register failed with error 0x%x",
                     status);
      //VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
	 																VMK_UPLINK_CAP_VLAN_TX_INSERT, NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter, "VLAN_TX_INSERT cap register failed with error 0x%x",
                status);
      //VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
	 																VMK_UPLINK_CAP_VLAN_RX_STRIP,	NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,"VLAN_RX_STRIP cap register failed with error 0x%x",
                 status);
      //VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,VMK_UPLINK_CAP_COALESCE_PARAMS, &sfvmkCoalesceParamsOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter, "COALESCE_PARAMS cap register failed with error 0x%x",
                status);
      //VMK_ASSERT(0);
   }
   status = vmk_UplinkCapRegister(adapter->uplink,
                                     VMK_UPLINK_CAP_MULTI_QUEUE,&sfvmkQueueOps);





#if 0 
   if (adapter->msix_enabled) {
      status = vmk_UplinkCapRegister(adapter->uplink,
                                     VMK_UPLINK_CAP_MULTI_QUEUE,&sfvmkQueueOps);
      if (status != VMK_OK) {
         SFVMK_ERR(adapter,
                        "MULTI_QUEUE cap register failed with error 0x%x",
                        status);
         VMK_ASSERT(0);
      }
   }
	 else {
      SFVMK_ERR(adapter, "Skipping register of VMK_UPLINK_CAP_"
                     "MULTI_QUEUE as device does not have msix enabled");
   }
#endif
#if 0
#if ESX_VERSION_NUMBER >= ESX_VERSION(2015)
   if (SFVMK_IS_RSS_ENABLED(adapter)) {
      status = vmk_UplinkQueueRegisterFeatureOps(adapter->uplink,
                                                VMK_UPLINK_QUEUE_FEAT_RSS_DYN,
                                                &sfvmkUplinkQueueRssDynOps);
      if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
         SFVMK_ERR(adapter,
                        "RSS Queue Feature register failed for %s"
                        "with error 0x%x",
                        vmk_NameToString(&adapter->uplinkName), status);
         VMK_ASSERT(0);
      }
   }
#endif




   /* Register private stats capability */
   status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_PRIV_STATS,
                                  &sfvmkPrivStatsOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "PRIV_STATS cap register failed "
                     "with error 0x%x", status);
      VMK_ASSERT(0);
   }
#endif
   /* Link Status Set capability */
 
   status = vmk_UplinkCapRegister(adapter->uplink,
   																VMK_UPLINK_CAP_LINK_STATUS_SET,
                                  &sfvmk_linkStatusSet);
   /* */
   vmk_LogMessage("VMK_UPLINK_CAP_LINK_STATUS_SET set to sfvmk_linkStatusSet\n");

   //VMK_ASSERT(status == VMK_OK);
 
#if 0
   /* Register  WOL Capability */
   if (SFVMK_IS_WOL_SUPPORTED(adapter)) {
      status = vmk_UplinkCapRegister(adapter->uplink,
                                     VMK_UPLINK_CAP_WAKE_ON_LAN,
                                     &sfvmkLinkWolOps);
      if (status != VMK_OK) {
         SFVMK_ERR(adapter,
                    "WOL cap register failed with error 0x%x", status);
	     VMK_ASSERT(0);
      }
   }
	 else {
      SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 0, "Does not support WOL");

   }
	 #endif

    /* Register  NETWORK_DUMP Capability */
   status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_NETWORK_DUMP,
                                  &sfvmkNetDumpOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "NetDump cap register failed with error 0x%x", status);
      //VMK_ASSERT(0);
   }
#if 0
   if (adapter->num_vfs_enabled) {
      status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_SRIOV,
                                     NULL);
      if (status != VMK_OK) {
         SFVMK_ERR(adapter,
                        "SRIOV cap register failed with error 0x%x", status);
         VMK_ASSERT(0);
      }
   }

#endif

#if 0
   if (SFVMK_IS_VXLAN_OFFLOAD_ENABLED(adapter)) {
      SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 2, "Register vxlan offload");

      status = vmk_UplinkCapRegister(adapter->uplink,
                                     VMK_UPLINK_CAP_ENCAP_OFFLOAD,
                                     &sfvmkEncapOffloadOps);
      if (status != VMK_OK) {
         SFVMK_ERR(adapter,
                        "VXLAN OFFLOAD cap register failed with error 0x%x",
                        status);
         VMK_ASSERT(0);
      }
   }

#endif
   status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_MOD_TX_HDRS,
                                  NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Modify Tx Hdrs cap register failed with error 0x%x",
                     status);
      //VMK_ASSERT(0);
   }
#if 0 
   status = vmk_UplinkCapRegister(adapter->uplink,
                                  VMK_UPLINK_CAP_PAUSE_PARAMS,
                                  NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Flow Control cap register failed with error 0x%x",
                     status);
      VMK_ASSERT(0);
   }

#endif 
#if ESX_VERSION_NUMBER >= ESX_VERSION(2015)
   status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_CABLE_TYPE,
                                  &sfvmkCableTypeOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Cable Type cap register failed "
                     "with error 0x%x", status);
      //VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
                                  VMK_UPLINK_CAP_MESSAGE_LEVEL,
                                  &sfvmkMessageLevelOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Message Level cap register failed "
                     "with error 0x%x", status);
      //VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
                                  VMK_UPLINK_CAP_TRANSCEIVER_TYPE,
                                  &sfvmkTransceiverTypeOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Transceiver Type cap register failed "
                     " with error 0x%x", status);
      //VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
                                  VMK_UPLINK_CAP_RING_PARAMS,
                                  &sfvmkRingParamsOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Ring Parameters cap register failed "
                     " with error 0x%x", status);
      //VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
                                  VMK_UPLINK_CAP_PHY_ADDRESS,
                                  &sfvmkPhyAddressOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Phy Address cap register failed "
                     " with error 0x%x", status);
      VMK_ASSERT(0);
   }
#endif

   return (status);
}


/*
 ***********************************************************************
 *
 * sfvmk_uplinkCapsRegister
 *
 *      Function to register uplink device capabilities such as TSO and CSO.
 *
 *      param[in] cookie     Struct holding driverData
 *
 * Results:
 *      retval: VMK_OK       Register uplink caps with vmkernel
 *                           succeeded
 *              VMK_FAILURE  Register uplink caps failed
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */
static VMK_ReturnStatus
sfvmk_uplinkCapsRegister(vmk_AddrCookie cookie)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;
   VMK_ReturnStatus status = VMK_OK;

   vmk_LogMessage("calling sfvmk_uplinkCapsRegister \n");
   //VMK_ASSERT(adapter->uplink != NULL);

   status = sfvmk_registerIOCaps(adapter);

   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 2, "driverData: %p", adapter);

   return status;
}


/*
 ***********************************************************************
 *
 * sfvmk_uplinkAssociate
 *
 *      Callback invoked by vmkernel to notify uplink is associated with
 *      device
 *
 *      param[in] cookie     Struct holding driverData
 *      param[in] uplink     Handle to uplink oject
 *
 * Results:
 *      retval: VMK_OK       Device backup uplink object succeeded
 *              VMK_FAILURE  Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */
static VMK_ReturnStatus
sfvmk_uplinkAssociate(vmk_AddrCookie cookie, vmk_Uplink uplink)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;
   vmk_LogMessage("calling sfvmk_uplinkAssociate \n");
   /* Backup uplink object in sfvmk device data */
   //VMK_ASSERT(uplink != NULL);
   //VMK_ASSERT(adapter->uplink == NULL);

   adapter->uplink = uplink;
   adapter->uplinkName = vmk_UplinkNameGet(adapter->uplink);

   return sfvmk_uplinkCapsRegister(cookie);
}



/*
 ***********************************************************************
 *
 * sfvmk_uplinkDisassociate
 *
 *      Callback invoked by vmkernel to notify uplink is disassociated
 *      from device
 *
 *      param[in] cookie     Struct holding driverData
 *
 * Results:
 *      retval: VMK_OK       Device forget uplink object succeeded
 *              VMK_FAILURE  Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_uplinkDisassociate(vmk_AddrCookie cookie)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   /* forget uplink object in sfvmk device data */
   //VMK_ASSERT(adapter->uplink != NULL);

    adapter->uplink = NULL;

   return VMK_OK;
}
#if ESX_VERSION_NUMBER >= ESX_VERSION(2015)
/*
 ***********************************************************************
 * sfvmk_messageLevelGet
 *
 *      Handler used by vmkernel to get an uplink's message level.
 *
 *      param[in]  cookie      struct holding driverData
 *      param[out] level       Pointer to be filled in with the messsage
 *                             level for this uplink.
 *
 * Results:
 *      retval: VMK_OK       Cable type is set successfully.
 *              VMK_FAILURE  Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_messageLevelGet(vmk_AddrCookie cookie, vmk_uint32 *level)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   //VMK_ASSERT(adapter != NULL);

   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 2, "debugMask: 0x%x",
                  adapter->debugMask);

   *level = adapter->debugMask;

   return VMK_OK;
}



/*
 ***********************************************************************
 * sfvmk_messageLevelGet
 *
 *      Handler used by vmkernel to set an uplink's message level.
 *
 *      param[in]  cookie      struct holding driverData
 *      param[in]  level       message level to set.
 *
 * Results:
 *      retval: VMK_OK       Cable type is set successfully.
 *              VMK_FAILURE  Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_messageLevelSet(vmk_AddrCookie cookie, vmk_uint32 level)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   //VMK_ASSERT(adapter != NULL);

   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 2,
                  "Seting debugMask to 0x%x (Current mask: 0x%x)",
                  level, adapter->debugMask);

   adapter->debugMask = level;
   return VMK_OK;
}



/*
 ************************************************************************
 * sfvmk_getTransceiverType
 *
 *   Handler used by vmkernel to get an uplink's transceiver type.
 *
 *   param[in]  cookie             struct holding driverData
 *   param[out]  transceiverType   transceiver type to get for this uplink.
 *
 * Results:
 *      retval: VMK_OK       Transceiver type is returned successfully.
 *              VMK_FAILURE  Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_getTransceiverType(vmk_AddrCookie cookie,
                          vmk_UplinkTransceiverType *transceiverType)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   //VMK_ASSERT(transceiverType != NULL);
   //VMK_ASSERT(adapter != NULL);

   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 3, "Entry ... ");

   return VMK_OK;
}



/*
 ***********************************************************************
 * sfvmk_setTransceiverType
 *
 *      Handler used by vmkernel to set an uplink's transciever type.
 *
 *      param[in]  cookie            struct holding driverData
 *      param[in]  transceiverType   transceiver type to set.
 *
 * Results:
 *      retval: VMK_OK            transceiver type is set successfully.
 *              VMK_NOT_SUPPORTED set tansceiver type is not supported.
 *              VMK_FAILURE       Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_setTransceiverType(vmk_AddrCookie cookie,
                          vmk_UplinkTransceiverType transceiverType)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   //VMK_ASSERT(adapter != NULL);

   SFVMK_ERR(adapter, "setTransceiverType is not supported");

   return VMK_NOT_SUPPORTED;
}



/*
 ***********************************************************************
 * sfvmk_ringParamsGet
 *
 *      Handler used by vmkernel to get an uplink's RX & TX ring size.
 *
 *      param[in]  cookie            struct holding driverData
 *      param[out]  params           RX & TX ring size to get for this uplink
 *
 * Results:
 *      retval: VMK_OK            ring size get is success.
 *              VMK_FAILURE       Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_ringParamsGet(vmk_AddrCookie cookie,
                     vmk_UplinkRingParams *params)
{

   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   //VMK_ASSERT(adapter != NULL);

   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 3, "Entry ... ");

   #if 0 


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
#endif 
   return VMK_OK;
}



/*
 ***********************************************************************
 * sfvmk_ringParamsSet
 *
 *      Handler used by vmkernel to set an uplink's RX/TX ring size.
 *
 *      param[in]  cookie            struct holding driverData
 *      param[in]  params            RX/TX ring size to set
 *
 * Results:
 *      retval: VMK_OK            ring size is set successfully.
 *              VMK_NOT_SUPPORTED set RX/TX ring size is not supported.
 *              VMK_FAILURE       Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_ringParamsSet(vmk_AddrCookie cookie,
                     vmk_UplinkRingParams *params)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   //VMK_ASSERT(adapter != NULL);

   SFVMK_ERR(adapter, "RingParamsSet is not supported");

   return VMK_NOT_SUPPORTED;
}



/*
 ***********************************************************************
 * sfvmk_getPhyAddress
 *
 *      Handler used by vmkernel to get an uplink's PHY address.
 *
 *      param[in]  cookie            struct holding driverData
 *      param[out] phyAddr           PHY address for this uplink.
 *
 * Results:
 *      retval: VMK_OK            PHY address get is success.
 *              VMK_FAILURE       Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_getPhyAddress(vmk_AddrCookie cookie,
                     vmk_uint8 *phyAddr)
{

   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   //VMK_ASSERT(adapter != NULL);

   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 3, "Entry ... ");

  // *phyAddr = (vmk_uint8) adapter->port_num;

   return VMK_OK;
}



/*
 ***********************************************************************
 * sfvmk_setPhyAddress
 *
 * Handler used by vmkernel to set an uplink's PHY address.
 *
 *
 *      param[in]  cookie            struct holding driverData
 *      param[in]  phyAddr           PHY address to set.
 *
 * Results:
 *      retval: VMK_OK            PHY address set is success.
 *              VMK_NOT_SUPPORTED set PHY address is not supported.
 *              VMK_FAILURE       Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_setPhyAddress(vmk_AddrCookie cookie,
                     vmk_uint8 phyAddr)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   //VMK_ASSERT(adapter != NULL);

   SFVMK_ERR(adapter, "PHY address set is not supported");

   return VMK_NOT_SUPPORTED;
}
/*
***********************************************************************
* sfvmk_getCableType
*
*      Handler used by vmkernel to get an uplink's cable type.
*
*      param[in]  cookie      struct holding driverData
*      param[out] cableType   cable type for this uplink.
*
* Results:
*      retval: VMK_OK       Cable type is returned successfully.
*              VMK_FAILURE  Otherwise
* Side effects:
*      None
*
***********************************************************************
*/

static VMK_ReturnStatus
sfvmk_getCableType(vmk_AddrCookie cookie,
                            vmk_UplinkCableType *cableType)
{
  sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

  SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 3, "Entry ...");

  *cableType = adapter->phy.cable_type;

  return VMK_OK;
}



/*
***********************************************************************
* sfvmk_getSupportedCableTypes
*
*      Handler used by vmkernel to get an uplink supported cable types.
*
*      param[in]  cookie      struct holding driverData
*      param[out] cableType   supported cable types for this uplink.
*
* Results:
*      retval: VMK_OK       Cable types is returned successfully.
*              VMK_FAILURE  Otherwise
*
* Side effects:
*      None
*
***********************************************************************
*/

static VMK_ReturnStatus
sfvmk_getSupportedCableTypes(vmk_AddrCookie cookie,
                                            vmk_UplinkCableType *cableType)
{
  sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

  SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 3, "Entry ... ");

  *cableType = adapter->phy.cable_type;

  return VMK_OK;
}



/*
***********************************************************************
* sfvmk_setCableType
*
*      Handler used by vmkernel to set an uplink's cable type.
*
*      param[in]  cookie      struct holding driverData
*      param[in]  cableType   cable type to set.
*
* Results:
*      retval: VMK_OK       Cable type is set successfully.
*              VMK_FAILURE  Otherwise
*
* Side effects:
*      None
*
***********************************************************************
*/

static VMK_ReturnStatus
sfvmk_setCableType(vmk_AddrCookie cookie,
                            vmk_UplinkCableType cableType)
{
  sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

  SFVMK_ERR(adapter, "setCableType is not supported");

  return VMK_NOT_SUPPORTED;
}





#endif 
