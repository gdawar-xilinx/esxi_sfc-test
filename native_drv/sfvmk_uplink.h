



/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#ifndef __SFVMK_UPLINK_H__
#define __SFVMK_UPLINK_H__

VMK_ReturnStatus
sfvmk_CreateUplinkData(sfvmk_adapter * adapter);
VMK_ReturnStatus
sfvmk_DestroyUplinkData(sfvmk_adapter *pAdapter);

#define SFVMK_DEFAULT_RX_QUEUE_INDEX 0
#define SFVMK_DEFAULT_TX_QUEUE_INDEX 0

#define SFVMK_RX_SHARED_QUEUE_START_INDEX 0                       \

#define SFVMK_GET_TX_SHARED_QUEUE_START_INDEX(adapter)                      \
               adapter->queueInfo.maxRxQueues;

#define SFVMK_SHARED_AREA_BEGIN_WRITE(adapter)                       \
{											 \
	vmk_SpinlockLock(adapter->sharedDataWriterLock);				 \
	vmk_VersionedAtomicBeginWrite(&adapter->sharedData.lock); 				 \
}

#define SFVMK_SHARED_AREA_END_WRITE(adapter)                         \
 {											 \
		vmk_VersionedAtomicEndWrite(&adapter->sharedData.lock); 					 \
		vmk_SpinlockUnlock(adapter->sharedDataWriterLock);				 \
 }

 /* MultiQueue Helper Routines */
 static inline struct sfvmk_rxq *
 sfvmk_GetRxQueue(sfvmk_adapter *pAdapter, vmk_UplinkQueueID qid)
 {
		vmk_uint32 qd_index = vmk_UplinkQueueIDVal(qid);
		int q_index =  qd_index - SFVMK_RX_SHARED_QUEUE_START_INDEX;
		struct sfvmk_rxq *queue = pAdapter->rxq[q_index];

		VMK_ASSERT(vmk_UplinkQueueIDType(qid) == VMK_UPLINK_QUEUE_TYPE_RX);
		return queue;
 }

 static inline struct sfvmk_txq *
 sfvmk_GetTxQueue(sfvmk_adapter *adapter, vmk_UplinkQueueID qid)
 {
		vmk_uint32 qd_index = vmk_UplinkQueueIDVal(qid);
		int q_index = qd_index - SFVMK_GET_TX_SHARED_QUEUE_START_INDEX(adapter) ;
		struct sfvmk_txq *queue = adapter->txq[q_index];

		VMK_ASSERT(vmk_UplinkQueueIDType(qid) == VMK_UPLINK_QUEUE_TYPE_TX);
		return queue;
 }

#define  SFVMK_RSS_START_INDEX(adapter)               \
    adapter->rxq_count - adapter->max_rss_channels

#endif

