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

#ifndef __SFVMK_INTR_H__
#define __SFVMK_INTR_H__

/* interrupt related DS */

/* MAX MSIX supported */
#define SFVMK_MAX_MSIX_VECTORS        EFX_MAXRSS

enum sfvmk_intrState {
  SFVMK_INTR_UNINITIALIZED = 0,
  SFVMK_INTR_INITIALIZED,
  SFVMK_INTR_TESTING,
  SFVMK_INTR_STARTED
};

typedef struct sfvmk_intr_s {
  enum sfvmk_intrState  state;
  vmk_IntrCookie        intrCookies[SFVMK_MAX_MSIX_VECTORS] ;
  int                   numIntrAlloc;
  efx_intr_type_t       type;
}sfvmk_intr_t;

/* functions */
VMK_ReturnStatus sfvmk_intrStart(struct sfvmk_adapter_s *pAdapter);
VMK_ReturnStatus sfvmk_intrStop(struct sfvmk_adapter_s * pAdapter);
VMK_ReturnStatus sfvmk_intrInit(struct sfvmk_adapter_s *pAdapter);
void sfvmk_freeInterrupts(struct sfvmk_adapter_s *pAdapter);

#endif /* __SFVMK_RX_H__ */

