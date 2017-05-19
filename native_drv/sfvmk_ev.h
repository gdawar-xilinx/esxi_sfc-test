
/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#ifndef __SFVMK_EV_H__
#define __SFVMK_EV_H__

#include "sfvmk.h"
#define SFVMK_EVQ_LOCK_ASSERT_OWNED(evq)

enum sfvmk_evq_state {
	SFVMK_EVQ_UNINITIALIZED = 0,
	SFVMK_EVQ_INITIALIZED,
	SFVMK_EVQ_STARTING,
	SFVMK_EVQ_STARTED
};


// better to aliign on cache line
typedef struct sfvmk_evq {
	/* Structure members below are sorted by usage order */
	sfvmk_adapter *adapter;
        vmk_Mutex               lock;
	unsigned int		index;
	enum sfvmk_evq_state	init_state;
	efsys_mem_t		mem;
	efx_evq_t		*common;
	unsigned int		read_ptr;
	boolean_t		exception;
	unsigned int		rx_done;
	unsigned int		tx_done;

	/* Linked list of TX queues with completions to process */
	struct sfvmk_txq	*txq;
	struct sfvmk_txq	**txqs;

	/* Structure members not used on event processing path */
	unsigned int		entries;
	char			lock_name[SFVMK_LOCK_NAME_MAX];
        unsigned int            size;
        vmk_NetPoll             netPoll;
         vmk_IntrCookie vector;
}sfvmk_evq;


int
sfvmk_EvInit(sfvmk_adapter *adapter);

void
sfvmk_EvFini(sfvmk_adapter *adapter);
int
sfvmk_ev_qpoll(sfvmk_evq *evq);
void sfvmk_EVStop(sfvmk_adapter *adapter);
int sfvmk_EVStart(sfvmk_adapter *adapter);
#endif
