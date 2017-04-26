#include "sfvmk_uplink.h"



vmk_UplinkOps sfvmkUplinkOps = {NULL};
/*
 ***********************************************************************
 *
 * elxnet_netPollCB
 *
 *      Callback invoked by vmkernel net poll thread to poll for
 *      Tx/Rx/MCC  completions
 *
 *
 *      param[in] nicPoll    pointer to elxnet_nicPoll object
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
sfvmk_netPollCB(void *nicPoll, vmk_uint32 budget)
{
    return 0 ; 
}


/*
 ***********************************************************************
 *
 * SFVMK_allociTxQueue
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
      i = ELXNET_RSS_START_INDEX(driver);
      if (qData[i].flags & VMK_UPLINK_QUEUE_FLAG_IN_USE) {
         ELXNET_LOG_ERR(driver, "RSS leading queue already allocated");
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

   //SFVMK_DBG(driver, ELXNET_DBG_QUEUE, 2,
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
   if ((ELXNET_IS_DEV_STATE(adapter, RESET))) {
      goto sfvmk_shared_data_init;
   }
*/
   /* Create sharedData writer lock to sync among
      multiple writers in driver */
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

   sharedData->supportedModes = adapter->supportedModes;
   sharedData->supportedModesArraySz = adapter->supportedModesArraySz;

   sharedData->flags = 0;
   sharedData->state = VMK_UPLINK_STATE_ENABLED;
   sharedData->link.state = VMK_LINK_STATE_DOWN;
   sharedData->link.speed = 0;
   sharedData->link.duplex = VMK_LINK_DUPLEX_HALF;

   sharedData->mtu = adapter->mtu;

   /* populate queue info */
   sharedData->queueInfo = queueInfo = &adapter->queueInfo;
   queueInfo->supportedQueueTypes = VMK_UPLINK_QUEUE_TYPE_TX |
      					VMK_UPLINK_QUEUE_TYPE_RX;
   queueInfo->supportedRxQueueFilterClasses =
       VMK_UPLINK_QUEUE_FILTER_CLASS_MAC_ONLY;

#if 0
   if (ELXNET_IS_VXLAN_OFFLOAD_ENABLED(adapter)) {
      queueInfo->supportedRxQueueFilterClasses |=
	     VMK_UPLINK_QUEUE_FILTER_CLASS_VXLAN;
   }
#endif
#if 0
   if (ELXNET_IS_RSS_ENABLED(adapter)) {
      queueInfo->maxTxQueues =
         ((ELXNET_GET_NUM_QUEUES(adapter) - ELXNET_RSS_RING_SETS + 1));

      queueInfo->maxRxQueues =
         (ELXNET_GET_NUM_QUEUES(adapter) - ELXNET_RSS_RING_SETS + 1);
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
      queueInfo->maxTxQueues = adapter->txq_count;
      queueInfo->maxRxQueues = adapter->rxq_count;
   }
   SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 2, "maxTxQs: %d maxRxQs: %d ",
                  queueInfo->maxTxQueues, queueInfo->maxRxQueues);

   vmk_LogMessage("max tx queue = %d max rx queue %d\n", queueInfo->maxTxQueues, queueInfo->maxRxQueues);
   queueInfo->activeRxQueues = 0;
   queueInfo->activeTxQueues = 0;

   status = vmk_ServiceGetID(VMK_SERVICE_ACCT_NAME_NET, &serviceID);
   VMK_ASSERT(status == VMK_OK);

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
         if (!ELXNET_IS_RSS_ENABLED(adapter)) {
            queueData->supportedFeatures |= VMK_UPLINK_QUEUE_FEAT_DYNAMIC;
         }
         /* Enable RSS Feature support for RSS Qs*/
         if (ELXNET_IS_RSS_ENABLED(adapter) &&
            (i >= ELXNET_RSS_START_INDEX(adapter))) {
            queueData->supportedFeatures |= VMK_UPLINK_QUEUE_FEAT_RSS_DYN;
         }
#else
         /* Enable RSS Feature support for RSS Qs*/
         if (ELXNET_IS_RSS_ENABLED(adapter) &&
            (i >= ELXNET_RSS_START_INDEX(adapter))) {
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
      if ((ELXNET_IS_DEV_STATE(adapter, RESET))) {
         goto sfvmk_update_nicPoll;
      }
       */
      nicPoll->adapter = (struct sfvmk_adapter*)adapter;
      nicPoll->queueData = queueData;
      nicPoll->rxo = &adapter->rx_obj[i];

      pollProp.poll = sfvmk_netPollCB;
      pollProp.priv.ptr = nicPoll;
      pollProp.deliveryCallback = NULL;
      pollProp.features = VMK_NETPOLL_NONE;
      status = vmk_NetPollCreate(&pollProp, serviceID, vmk_ModuleCurrentID,
                                 &nicPoll->netPoll);
      if (status != VMK_OK) {
         for (i--; i >= 0; i--) {
            nicPoll = &adapter->nicPoll[i];
            vmk_NetPollDestroy(nicPoll->netPoll);
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
   if (ELXNET_IS_RSS_ENABLED(adapter)) {
      for (i = queueInfo->maxRxQueues;
           i < (queueInfo->maxRxQueues + ELXNET_RSS_RING_SETS - 1); i++) {

         vmk_NetPollProperties pollProp;
         sfvmk_nicPoll *nicPoll;

         nicPoll = &adapter->nicPoll[i];
         if ((ELXNET_IS_DEV_STATE(adapter, RESET))) {
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
                                    elxnetDriver.moduleID,
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

         SFVMK_DBG(adapter, ELXNET_DBG_DRIVER, 3, "RXq=%d, poll=0x%p",
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
   VMK_ASSERT(status == VMK_OK);
   rxQueue = sfvmk_getRxQueueByQID(adapter, queueInfo->defaultRxQueueID);
   VMK_ASSERT(sfvmk_isDefaultRxQueue(adapter, rxQueue));
   VMK_ASSERT(vmk_UplinkQueueIDType(queueInfo->defaultRxQueueID) ==
              VMK_UPLINK_QUEUE_TYPE_RX);
   VMK_ASSERT(vmk_UplinkQueueIDVal(queueInfo->defaultRxQueueID) ==
              sfvmk_getQIDValByQData(adapter, rxQueue->qData));

   queueInfo->defaultTxQueueID = 0;
   status = sfvmk_allocTxQueue(adapter, &queueInfo->defaultTxQueueID);
   VMK_ASSERT(status == VMK_OK);
   txQueue = sfvmk_getTxQueueByQID(adapter, queueInfo->defaultTxQueueID);
   VMK_ASSERT(sfvmk_isDefaultTxQueue(adapter, txQueue));
   VMK_ASSERT(vmk_UplinkQueueIDType(queueInfo->defaultTxQueueID) ==
              VMK_UPLINK_QUEUE_TYPE_TX);
   VMK_ASSERT(vmk_UplinkQueueIDVal(queueInfo->defaultTxQueueID) ==
              sfvmk_getQIDValByQData(adapter, txQueue->qData));

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


