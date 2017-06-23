/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#ifndef __SFVMK_RX_H__
#define __SFVMK_RX_H__

#define SFVMK_RX_BATCH  128

/* max number of rxq */
#define SFVMK_RX_SCALE_MAX  EFX_MAXRSS

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

