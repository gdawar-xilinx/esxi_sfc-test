/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/


#ifndef __SFVMK_EV_H__
#define __SFVMK_EV_H__


/* praveen needs to define this */
#define SFVMK_EVQ_LOCK_ASSERT_OWNED(evq)


/* lock apis */
#define SFVMK_EVQ_LOCK(txq) {   \
  vmk_MutexLock(txq->lock);     \
}

#define SFVMK_EVQ_UNLOCK(txq) { \
  vmk_MutexUnlock(txq->lock);   \
}


/* event queue state */
enum sfvmk_evq_state {
  SFVMK_EVQ_UNINITIALIZED = 0,
  SFVMK_EVQ_INITIALIZED,
  SFVMK_EVQ_STARTING,
  SFVMK_EVQ_STARTED
};


// better to aliign on cache line

typedef struct sfvmk_evq_s {
  /* Structure members below are sorted by usage order */
  struct sfvmk_adapter_s  *pAdapter;
  efsys_mem_t       mem;
  efx_evq_t         *pCommonEvq;
  boolean_t         exception;
  vmk_Mutex         lock;
  vmk_uint32        index;
  vmk_uint32        entries;
  vmk_uint32        rxDone;
  vmk_uint32        txDone;
  vmk_uint32        readPtr;

  /* Linked list of TX queues with completions to process */
  sfvmk_txq_t       *pTxq;
  sfvmk_txq_t       **pTxqs;

  vmk_NetPoll       netPoll;
  vmk_IntrCookie    vector;

  char              lockName[SFVMK_LOCK_NAME_MAX];

  enum sfvmk_evq_state  initState;

} sfvmk_evq_t;//VMK_ATTRIBUTE_L1_ALIGNED;


/* functions */

VMK_ReturnStatus sfvmk_evInit(struct sfvmk_adapter_s *adapter);
void sfvmk_evFini(struct sfvmk_adapter_s *adapter);
VMK_ReturnStatus sfvmk_evStart(struct sfvmk_adapter_s *adapter);
void sfvmk_evStop(struct sfvmk_adapter_s *adapter);



#endif /* __SFVMK_EV_H__ */

