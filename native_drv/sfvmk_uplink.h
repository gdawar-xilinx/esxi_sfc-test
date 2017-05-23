
#ifndef __SFVMK_UPLINK_H__
#define __SFVMK_UPLINK_H__

#include "sfvmk_driver.h"

VMK_ReturnStatus
sfvmk_createUplinkData(sfvmk_adapter * adapter);
#define SFVMK_GET_RX_SHARED_QUEUE_DATA(adapter)                      \
               &adapter->queueData[0];

#define SFVMK_GET_TX_SHARED_QUEUE_DATA(adapter)                      \
               &adapter->queueData[adapter->queueInfo.maxRxQueues];

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

#define sfvmk_for_all_pkts_in_list(iter, pktList)                    \
   for (vmk_PktListIterStart(iter, pktList);                       \
       !vmk_PktListIterIsAtEnd(iter);                             \
       )

#define SFVMK_MAX_PKT_SIZE	   65535

 /* MultiQueue Helper Routines */
 static inline struct sfvmk_rxObj *
 sfvmk_getRxQueueByQID(sfvmk_adapter *driver, vmk_UplinkQueueID qid)
 {
		vmk_uint32 qd_index = vmk_UplinkQueueIDVal(qid);
		vmk_UplinkSharedQueueData *qData = &driver->queueData[qd_index];
		int q_index = qData - SFVMK_GET_RX_SHARED_QUEUE_DATA(driver);
		struct sfvmk_rxObj *queue = &driver->rx_obj[q_index];

		VMK_ASSERT(vmk_UplinkQueueIDType(qid) == VMK_UPLINK_QUEUE_TYPE_RX);
		VMK_ASSERT(q_index < driver->queueInfo.maxRxQueues && q_index >= 0);

		return queue;
 }

 static inline struct sfvmk_txObj *
 sfvmk_getTxQueueByQID(sfvmk_adapter *driver, vmk_UplinkQueueID qid)
 {
		vmk_uint32 qd_index = vmk_UplinkQueueIDVal(qid);
		vmk_UplinkSharedQueueData *qData = &driver->queueData[qd_index];
		int q_index = qData - SFVMK_GET_TX_SHARED_QUEUE_DATA(driver);
		struct sfvmk_txObj *queue = &driver->tx_obj[q_index];

      		vmk_LogMessage("qd_index=%d,q_index=%d,queue=%p",qd_index, q_index, queue);
		VMK_ASSERT(vmk_UplinkQueueIDType(qid) == VMK_UPLINK_QUEUE_TYPE_TX);
		VMK_ASSERT(q_index < driver->queueInfo.maxTxQueues && q_index >= 0);

		return queue;
 }

 static inline vmk_Bool
 sfvmk_isDefaultRxQueue(sfvmk_adapter *driver, struct sfvmk_rxObj *queue)
 {
		return queue == driver->rx_obj;
 }

 static inline vmk_Bool
 sfvmk_isDefaultTxQueue(sfvmk_adapter *driver, struct sfvmk_txObj *queue)
 {
		return queue == driver->tx_obj;
 }

 static inline vmk_uint32
 sfvmk_getQIDValByQData(sfvmk_adapter *driver,												 vmk_UplinkSharedQueueData *qData)
 {
		return (qData - &driver->queueData[0]);
 }

void sfvmk_updateQueueStatus(sfvmk_adapter *adapter, vmk_UplinkQueueState qState);
#define  SFVMK_RSS_START_INDEX(adapter)               \
    adapter->txq_count - adapter->max_rss_channels

#endif
