/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/


#ifndef __SFVMK_EV_H__
#define __SFVMK_EV_H__


#define SFVMK_MAGIC_RESERVED    0x8000

#define SFVMK_MAGIC_DMAQ_LABEL_WIDTH  6
#define SFVMK_MAGIC_DMAQ_LABEL_MASK \
  ((1 << SFVMK_MAGIC_DMAQ_LABEL_WIDTH) - 1)

enum sfvmk_sw_ev {
  SFVMK_SW_EV_RX_QFLUSH_DONE = 1,
  SFVMK_SW_EV_RX_QFLUSH_FAILED,
  SFVMK_SW_EV_RX_QREFILL,
  SFVMK_SW_EV_TX_QFLUSH_DONE,
};

#define SFVMK_SW_EV_MAGIC(_sw_ev) \
  (SFVMK_MAGIC_RESERVED | ((_sw_ev) << SFVMK_MAGIC_DMAQ_LABEL_WIDTH))

static inline uint16_t
sfxge_swEvMkMagic(enum sfvmk_sw_ev sw_ev, unsigned int label)
{
  VMK_ASSERT_BUG((label & SFVMK_MAGIC_DMAQ_LABEL_MASK) == label,
          "label & SFVMK_MAGIC_DMAQ_LABEL_MASK != label");
  return SFVMK_SW_EV_MAGIC(sw_ev) | label;
}


/* lock apis */
#define SFVMK_EVQ_LOCK(pEvq) {   \
  vmk_MutexLock(pEvq->lock);     \
}

#define SFVMK_EVQ_UNLOCK(pEvq) { \
  vmk_MutexUnlock(pEvq->lock);   \
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
  struct sfvmk_txq_s       *pTxq;
  struct sfvmk_txq_s       **pTxqs;

  vmk_NetPoll       netPoll;
  vmk_IntrCookie    vector;

  enum sfvmk_evq_state  initState;

} sfvmk_evq_t VMK_ATTRIBUTE_L1_ALIGNED;


/* functions */
VMK_ReturnStatus sfvmk_evInit(struct sfvmk_adapter_s *adapter);
VMK_ReturnStatus sfvmk_evStart(struct sfvmk_adapter_s *adapter);
void sfvmk_evStop(struct sfvmk_adapter_s *adapter);
void sfvmk_evFini(struct sfvmk_adapter_s *adapter);
int sfvmk_evqPoll(sfvmk_evq_t *pEvq);


#endif /* __SFVMK_EV_H__ */

