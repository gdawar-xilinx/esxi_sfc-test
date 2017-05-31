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
sfvmk_createLock(const char *lockName, vmk_LockRank rank, vmk_Lock *lock);
extern void sfvmk_destroyLock(vmk_Lock lock);

VMK_ReturnStatus
sfvmk_mutexInit(const char *lckName,vmk_LockRank rank,vmk_Mutex *mutex);
void sfvmk_mutexDestroy(vmk_Mutex mutex);


void sfvmk_memPoolFree(void *va, vmk_uint64 size);
void *sfvmk_memPoolAlloc(vmk_uint64 size);

void
sfvmk_freeCoherentDMAMapping(vmk_DMAEngine engine, void *pVA,
                                            vmk_IOA ioAddr, vmk_uint32 size);
void *
sfvmk_allocCoherentDMAMapping(vmk_DMAEngine dmaEngine, vmk_uint32 size,
                                            vmk_IOA *ioAddr);

#endif /*  __SFVMK_UTILS_H__ */
