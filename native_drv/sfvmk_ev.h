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

#ifndef __SFVMK_EV_H__
#define __SFVMK_EV_H__

/* Number of events processed per batch */
#define SFVMK_EV_BATCH      16384

#define SFVMK_MIN_EVQ_COUNT     1

#define SFVMK_MAGIC_RESERVED    0x8000

#define SFVMK_MAGIC_DMAQ_LABEL_WIDTH  6
#define SFVMK_MAGIC_DMAQ_LABEL_MASK                \
  ((1 << SFVMK_MAGIC_DMAQ_LABEL_WIDTH) - 1)

/* default interrupt moderation value */
#define SFVMK_MODERATION_USEC           30
/* Max interrupt moderation value */
#define SFVMK_MAX_MODERATION_USEC       23920

enum sfvmk_sw_ev {
  SFVMK_SW_EV_RX_QFLUSH_DONE = 1,
  SFVMK_SW_EV_RX_QFLUSH_FAILED,
  SFVMK_SW_EV_RX_QREFILL,
  SFVMK_SW_EV_TX_QFLUSH_DONE,
};

#define SFVMK_SW_EV_MAGIC(_sw_ev)                  \
  (SFVMK_MAGIC_RESERVED |                          \
  ((_sw_ev) << SFVMK_MAGIC_DMAQ_LABEL_WIDTH))

static inline uint16_t
sfxge_swEvMkMagic(enum sfvmk_sw_ev sw_ev, unsigned int label)
{
  VMK_ASSERT_BUG((label & SFVMK_MAGIC_DMAQ_LABEL_MASK) == label,
          "label & SFVMK_MAGIC_DMAQ_LABEL_MASK != label");
  return SFVMK_SW_EV_MAGIC(sw_ev) | label;
}

/* lock apis */
#define SFVMK_EVQ_LOCK(pEvq) {                     \
  vmk_MutexLock(pEvq->lock);                       \
}

#define SFVMK_EVQ_UNLOCK(pEvq) {                   \
  vmk_MutexUnlock(pEvq->lock);                     \
}

/* Number of events processed per batch */
#define SFVMK_EV_BATCH       16384

/* event queue state */
enum sfvmk_evq_state {
  SFVMK_EVQ_UNINITIALIZED = 0,
  SFVMK_EVQ_INITIALIZED,
  SFVMK_EVQ_STARTING,
  SFVMK_EVQ_STARTED
};

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

  /* used for storing pktList passed in sfvmk_panicPoll */
  vmk_PktList           pktList;

  enum sfvmk_evq_state  initState;

} sfvmk_evq_t VMK_ATTRIBUTE_L1_ALIGNED;

/* functions */
VMK_ReturnStatus sfvmk_evInit(struct sfvmk_adapter_s *adapter);
VMK_ReturnStatus sfvmk_evStart(struct sfvmk_adapter_s *adapter);
void sfvmk_evStop(struct sfvmk_adapter_s *adapter);
void sfvmk_evFini(struct sfvmk_adapter_s *adapter);
int sfvmk_evqPoll(sfvmk_evq_t *pEvq);
VMK_ReturnStatus sfvmk_evqModerate(struct sfvmk_adapter_s *adapter,
	unsigned int index, unsigned int us);

#endif /* __SFVMK_EV_H__ */

