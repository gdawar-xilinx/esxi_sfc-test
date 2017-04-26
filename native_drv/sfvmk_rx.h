
#ifndef __SFVMK_RX_H__
#define __SFVMK_RX_H__
#include "efsys.h"
#include  "efx.h"

#include "sfvmk_driver.h"


typedef struct sfvmk_rx_sw_desc {
//	struct mbuf	*mbuf;
//	bus_dmamap_t	map;
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
	unsigned int			buf_base_id;
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
#if 0
#ifdef SFXGE_LRO
	struct sfxge_lro_state		lro;
#endif
#endif
	unsigned int			refill_threshold;
//	struct callout			refill_callout;
	unsigned int			refill_delay;

	efx_rxq_t			*common ;
	volatile enum sfvmk_flush_state	flush_state;
        unsigned int                    size;
}sfvmk_rxq;

int
sfvmk_rx_init(sfvmk_adapter *adapter);
#endif
