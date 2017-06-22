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

VMK_ReturnStatus sfvmk_initUplinkData(struct sfvmk_adapter_s * pAdapter);
VMK_ReturnStatus sfvmk_destroyUplinkData(struct sfvmk_adapter_s *pAdapter);

#endif /* __SFVMK_RX_H__ */

