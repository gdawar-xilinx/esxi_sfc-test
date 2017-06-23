/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#ifndef __SFVMK_TX_H__
#define __SFVMK_TX_H__

#define SFVMK_TX_SCALE_MAX  EFX_MAXRSS

#define SFVMK_TXQ_LOCK_ASSERT_OWNED(pAdapter)

#define SFVMK_TXQ_LOCK(pTxq) {     \
  vmk_MutexLock(pTxq->lock);       \
}
#define SFVMK_TXQ_UNLOCK(txq) {  \
  vmk_MutexUnlock(pTxq->lock);   \
}

#define NUM_TX_QUEUES_FOR_EVQ0 3

/*txq type */
enum sfvmk_txqType {
  SFVMK_TXQ_NON_CKSUM = 0,
  SFVMK_TXQ_IP_CKSUM,
  SFVMK_TXQ_IP_TCP_UDP_CKSUM,
  SFVMK_TXQ_NTYPES
};

enum sfvmk_txqFlushState
{
  SFVMK_TXQ_FLUSH_INACTIVE = 0,
  SFVMK_TXQ_FLUSH_DONE,
  SFVMK_TXQ_FLUSH_PENDING,
  SFVMK_TXQ_FLUSH_FAILED,
};

/*txq state */
enum sfvmk_txqState {
  SFVMK_TXQ_UNINITIALIZED = 0,
  SFVMK_TXQ_INITIALIZED,
  SFVMK_TXQ_STARTED
};

typedef struct sfvmk_txq_s {

  struct sfvmk_adapter_s  *pAdapter;
  vmk_Mutex       lock;

  vmk_uint32      tsoFwAssisted;

  vmk_uint32      txqIndex;
  /* associated eventq Index */
  vmk_uint32      evqIndex;
  /* number of entries in the txq*/
  vmk_uint32      entries;
  vmk_uint32      ptrMask;
  vmk_uint32      maxPktDesc;
  efsys_mem_t     mem;

  efx_desc_t      *pPendDesc;
  vmk_uint64      pendDescSize;

  efx_txq_t       *pCommonTxq;
  vmk_Name        mtxLckName;

  enum sfvmk_flushState   flushState;
  enum sfvmk_txqState     initState;
  enum sfvmk_txqType      type;

  vmk_int32       blocked VMK_ATTRIBUTE_L1_ALIGNED;

  /* The following fields change more often, and are used mostly
  * on the initiation path*/
  vmk_uint32      nPendDesc;
  vmk_uint32      added;
  vmk_uint32      reaped;
  vmk_uint32      pending VMK_ATTRIBUTE_L1_ALIGNED;
  vmk_uint32      completed;

  struct sfvmk_txq_s    *next;
}sfvmk_txq_t;

static inline uint16_t
sfvmk_swEvTxqMagic(enum sfvmk_sw_ev sw_ev, struct sfvmk_txq_s *txq)
{
  return sfxge_swEvMkMagic(sw_ev, txq->type);
}

int sfvmk_txInit(struct sfvmk_adapter_s *pAdapter);
void sfvmk_txFini(struct sfvmk_adapter_s *pAdapter);

void sfvmk_txStop(struct sfvmk_adapter_s *pAdapter);
VMK_ReturnStatus sfvmk_txStart(struct sfvmk_adapter_s *pAdapter);

void sfvmk_txqFlushDone(struct sfvmk_txq_s *pTxq);
void sfvmk_txqComplete(sfvmk_txq_t *pTxq, sfvmk_evq_t *pEvq);

#endif /* __SFVMK_TX_H__ */

