

/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#ifndef __SFVMK_RX_H__
#define __SFVMK_RX_H__

#include "sfvmk.h"



typedef struct sfvmk_rx_sw_desc {
//        vmk_PktHandle pkt;
	int		flags;
	int		size;
}sfvmk_rx_sw_desc;


enum sfvmk_rxq_state {
	SFVMK_RXQ_UNINITIALIZED = 0,
	SFVMK_RXQ_INITIALIZED,
	SFVMK_RXQ_STARTED
};


typedef struct sfvmk_rxq {
	sfvmk_adapter *adapter;
	unsigned int			index;
	efsys_mem_t			mem;
	enum sfvmk_rxq_state		init_state;
	unsigned int			entries;
	unsigned int			ptr_mask;

	struct sfvmk_rx_sw_desc		*queue ;
        unsigned int                    qdescSize;
	unsigned int			added;
	unsigned int			pushed;
	unsigned int			pending;
	unsigned int			completed;
	unsigned int			loopback;

	efx_rxq_t			*common ;
	volatile enum sfvmk_flush_state	flush_state;
        unsigned int                    size;
}sfvmk_rxq;

int sfvmk_RxInit(sfvmk_adapter *adapter);
void  sfvmk_RxFini(sfvmk_adapter *adapter);
int sfvmk_RXStart( sfvmk_adapter *adapter);
void sfvmk_RXStop(sfvmk_adapter *adapter);

#endif 
