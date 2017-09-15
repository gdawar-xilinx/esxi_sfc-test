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

