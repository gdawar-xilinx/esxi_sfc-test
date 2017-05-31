/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/


#ifndef __SFVMK_RX_H__
#define __SFVMK_RX_H__


#define SFVMK_RX_SCALE_MAX  EFX_MAXRSS

enum sfvmk_flushState {
  SFVMK_FLUSH_DONE = 0,
  SFVMK_FLUSH_REQUIRED,
  SFVMK_FLUSH_PENDING,
  SFVMK_FLUSH_FAILED
};


typedef struct sfvmk_rxSwDesc_s {
  vmk_PktHandle     *pPkt;
  vmk_int32   flags;
  vmk_int32   size;
}sfvmk_rxSwDesc_t;


enum sfvmk_rxqState {
  SFVMK_RXQ_UNINITIALIZED = 0,
  SFVMK_RXQ_INITIALIZED,
  SFVMK_RXQ_STARTED
};


typedef struct sfvmk_rxq_s {

  struct sfvmk_adapter_s *pAdapter;

  efsys_mem_t   mem;
  vmk_uint32    index;
  vmk_uint32    entries;
  vmk_uint32    ptrMask;
  vmk_uint32    qdescSize;
  vmk_uint32    added;
  vmk_uint32    pushed;
  vmk_uint32    pending;
  vmk_uint32    completed;
  vmk_uint32    loopback;

  sfvmk_rxSwDesc_t  *pQueue ;

  efx_rxq_t   *pCommonRxq ;

  enum sfvmk_rxqState   initState;
  volatile enum sfvmk_flushState  flushState;
}sfvmk_rxq_t;

/* functions */
int sfvmk_rxInit(struct sfvmk_adapter_s *adapter);
void  sfvmk_rxFini(struct sfvmk_adapter_s *adapter);
int sfvmk_rxStart(struct sfvmk_adapter_s *adapter);
void sfvmk_rxStop(struct sfvmk_adapter_s *adapter);

#endif /* __SFVMK_RX_H__ */

