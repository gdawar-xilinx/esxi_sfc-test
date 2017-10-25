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

#ifndef __SFVMK_RX_H__
#define __SFVMK_RX_H__

#define SFVMK_RX_BATCH  128

/* Number of entries in the indirection table */
#define SFVMK_RX_SCALE_MAX  EFX_RSS_TBL_SIZE

/* flush state of rxq*/
enum sfvmk_flushState {
  SFVMK_FLUSH_DONE = 0,
  SFVMK_FLUSH_REQUIRED,
  SFVMK_FLUSH_PENDING,
  SFVMK_FLUSH_FAILED
};

/* buff desc for each buffer */
typedef struct sfvmk_rxSwDesc_s {
  vmk_int32      flags;
  vmk_int32      size;
  vmk_PktHandle  *pPkt;
  vmk_IOA        ioAddr;
}sfvmk_rxSwDesc_t;

/* rxq state */
enum sfvmk_rxqState {
  SFVMK_RXQ_UNINITIALIZED = 0,
  SFVMK_RXQ_INITIALIZED,
  SFVMK_RXQ_STARTED
};

typedef struct sfvmk_rxq_s {
  struct sfvmk_adapter_s *pAdapter;
  efsys_mem_t   mem;
  /* rxq Index */
  vmk_uint32    index;
  /* number of entries in each rxq*/
  vmk_uint32    entries;
  vmk_uint32    ptrMask;
  vmk_uint32    qdescSize;
  /* variable for managing queue */
  vmk_uint32    added;
  vmk_uint32    pushed;
  vmk_uint32    pending;
  vmk_uint32    completed;
  vmk_uint32    refillThreshold;
  vmk_uint32    loopback;

  sfvmk_rxSwDesc_t  *pQueue ;
  /* common rxq module handle*/
  efx_rxq_t   *pCommonRxq ;
  /* rxq state*/
  enum sfvmk_rxqState   initState;
  volatile enum sfvmk_flushState  flushState;
}sfvmk_rxq_t;

static inline uint16_t
sfvmk_swEvRxqMagic(enum sfvmk_sw_ev sw_ev, struct sfvmk_rxq_s *rxq)
{
  return sfxge_swEvMkMagic(sw_ev, 0);
}

/* functions */
VMK_ReturnStatus sfvmk_rxInit(struct sfvmk_adapter_s *adapter);
void  sfvmk_rxFini(struct sfvmk_adapter_s *adapter);

int sfvmk_rxStart(struct sfvmk_adapter_s *adapter);
void sfvmk_rxStop(struct sfvmk_adapter_s *adapter);

void sfvmk_rxqFlushFailed(struct sfvmk_rxq_s *pRxq);
void sfvmk_rxqFlushDone(struct sfvmk_rxq_s *pRxq);
void sfvmk_rxqComplete(sfvmk_rxq_t *pRxq, boolean_t eop);

#endif /* __SFVMK_RX_H__ */

