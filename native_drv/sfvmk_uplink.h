/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#ifndef __SFVMK_UPLINK_H__
#define __SFVMK_UPLINK_H__

#define SFVMK_SHARED_AREA_BEGIN_WRITE(adapter)                \
{                                                             \
  vmk_SpinlockLock(adapter->shareDataLock);                   \
  vmk_VersionedAtomicBeginWrite(&adapter->sharedData.lock);   \
}

#define SFVMK_SHARED_AREA_END_WRITE(adapter)                  \
{                                                             \
  vmk_VersionedAtomicEndWrite(&adapter->sharedData.lock);     \
  vmk_SpinlockUnlock(adapter->shareDataLock);                 \
}

#define SFVMK_SHARED_AREA_BEGIN_READ(adapter)                      \
  do {                                                             \
    vmk_uint32 sharedReadLockVer;                                  \
    do {                                                           \
      sharedReadLockVer = vmk_VersionedAtomicBeginTryRead          \
                              (&adapter->sharedData.lock);

#define SFVMK_SHARED_AREA_END_READ(adapter)                        \
    } while (!vmk_VersionedAtomicEndTryRead                        \
                 (&adapter->sharedData.lock, sharedReadLockVer));  \
  } while (VMK_FALSE)

#define SFVMK_GET_RX_SHARED_QUEUE_DATA(pAdapter)              \
               &pAdapter->queueData[0];

#define SFVMK_GET_TX_SHARED_QUEUE_DATA(pAdapter)              \
               &pAdapter->queueData[pAdapter->queueInfo.maxRxQueues];

#define SFVMK_MAX_PKT_SIZE         65535

VMK_ReturnStatus sfvmk_initUplinkData(struct sfvmk_adapter_s * pAdapter);
VMK_ReturnStatus sfvmk_destroyUplinkData(struct sfvmk_adapter_s *pAdapter);
void sfvmk_updateQueueStatus(struct sfvmk_adapter_s *pAdapter, vmk_UplinkQueueState qState, vmk_uint32 qIndex);
VMK_ReturnStatus sfvmk_changeLinkState(struct sfvmk_adapter_s *pAdapter, vmk_LinkState state);
VMK_ReturnStatus sfvmk_changeRxTxIntrModeration(struct sfvmk_adapter_s *pAdapter, vmk_uint32 moderation);
void sfvmk_updateIntrCoalesceQueueData(struct sfvmk_adapter_s *pAdapter, vmk_UplinkCoalesceParams *params);
#endif /* __SFVMK_RX_H__ */

