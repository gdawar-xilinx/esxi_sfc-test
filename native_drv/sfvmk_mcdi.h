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

#ifndef __SFVMK_MCDI_H__
#define __SFVMK_MCDI_H__

#define SFVMK_MCDI_MAX_PAYLOAD 0x400

enum sfvmk_mcdiState {
  SFVMK_MCDI_UNINITIALIZED = 0,
  SFVMK_MCDI_INITIALIZED,
  SFVMK_MCDI_BUSY,
  SFVMK_MCDI_COMPLETED
};

enum sfvmk_mcdiMode {
  SFVMK_MCDI_MODE_POLL,
  SFVMK_MCDI_MODE_EVENT,
};

typedef struct sfvmk_mcdi_s {
  efsys_mem_t           mem;
  enum sfvmk_mcdiState  state;
  enum sfvmk_mcdiMode   mode;
  efx_mcdi_transport_t  transport;
  vmk_Mutex             lock;
}sfvmk_mcdi_t;

/* functions */
void sfvmk_mcdiFini(struct sfvmk_adapter_s *pAdapter);
int sfvmk_mcdiInit(struct sfvmk_adapter_s *pAdapter);
int sfvmk_mcdiIOHandler(struct sfvmk_adapter_s *pAdapter,
                        efx_mcdi_req_t *pEmReq);

#endif

