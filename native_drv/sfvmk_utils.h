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

#ifndef __SFVMK_UTILS_H__
#define __SFVMK_UTILS_H__

#include "sfvmk.h"

/* spinklock  handlers */
extern VMK_ReturnStatus
sfvmk_createLock(const char *lockName, vmk_LockRank rank, vmk_Lock *lock);
extern void sfvmk_destroyLock(vmk_Lock lock);

/* mutex handler */
VMK_ReturnStatus
sfvmk_mutexInit(const char *pLockName,vmk_Mutex *pMutex);
void sfvmk_mutexDestroy(vmk_Mutex mutex);

/* mempool handlers */
void sfvmk_memPoolFree(void *va, vmk_uint64 size);
void *sfvmk_memPoolAlloc(vmk_uint64 size);

/* dma memory handler */
void
sfvmk_freeCoherentDMAMapping(vmk_DMAEngine engine, void *pVA,
                                            vmk_IOA ioAddr, vmk_uint32 size);
void *
sfvmk_allocCoherentDMAMapping(vmk_DMAEngine dmaEngine, vmk_uint32 size,
                                            vmk_IOA *ioAddr);
uint16_t sfvmk_swapBytes(uint16_t int16);

#endif /*  __SFVMK_UTILS_H__ */
