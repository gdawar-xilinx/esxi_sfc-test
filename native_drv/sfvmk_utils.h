/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/


#ifndef __SFVMK_UTILS_H__
#define __SFVMK_UTILS_H__

#include "sfvmk.h"

extern VMK_ReturnStatus
sfvmk_CreateLock(const char *lockName,
                       vmk_LockRank rank,
                       vmk_Lock *lock);

extern void
sfvmk_DestroyLock(vmk_Lock lock);


VMK_ReturnStatus sfvmk_MutexInit(const char *lckName,vmk_LockRank rank,vmk_Mutex *mutex);           // OUT: created lock

void sfvmk_MutexDestroy(vmk_Mutex mutex) ;
void *
sfvmk_MemPoolAlloc(vmk_uint64 size);

void
sfvmk_MemPoolFree(void *va, vmk_uint64 size);


void *
sfvmk_AllocCoherentDMAMapping(sfvmk_adapter *adapter, vmk_uint32 size,          
                              vmk_IOA *ioAddr) ;         
void
sfvmk_FreeCoherentDMAMapping(sfvmk_adapter *adapter,                    // IN: adapter
                                            void *va,                   // IN: virtual address
                                            vmk_IOA ioAddr,             // IN: IO address
                                            vmk_uint32 size);            // IN: size
VMK_ReturnStatus
sfvmk_SetupInterrupts(sfvmk_adapter *adapter, vmk_IntrHandler handler , vmk_IntrAcknowledge ack);
VMK_ReturnStatus
sfvmk_IntrStop(sfvmk_adapter * devData);
#endif /*  __SFVMK_UTILS_H__ */
