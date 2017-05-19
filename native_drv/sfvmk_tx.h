


/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#ifndef __SFVMK_TX_H__
#define __SFVMK_TX_H__

#include "sfvmk.h"
// Number of descriptors per transmit queue
#define SFVMK_TX_NDESCS             1024

// Number of transmit descriptors processed per batch
#define SFVMK_TX_BATCH              128

// Maximum number of packets held in the deferred packet list
#define SFVMK_TX_MAX_DEFERRED       256



/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


enum sfvmk_txq_flush_state
{
    SFVMK_TXQ_FLUSH_INACTIVE = 0,
    SFVMK_TXQ_FLUSH_DONE,
    SFVMK_TXQ_FLUSH_PENDING,
    SFVMK_TXQ_FLUSH_FAILED,
};




enum sfvmk_txq_state {
	SFVMK_TXQ_UNINITIALIZED = 0,
	SFVMK_TXQ_INITIALIZED,
	SFVMK_TXQ_STARTED
};

typedef struct sfvmk_txq {
	/* The following fields should be written very rarely */
	sfvmk_adapter		*adapter;
        vmk_Mutex               lock;
	enum sfvmk_flush_state		flush_state;
	enum sfvmk_txq_state		init_state;
	unsigned int			tso_fw_assisted;
	enum sfvmk_txq_type		type;
	unsigned int			txq_index;
	unsigned int			evq_index;
	efsys_mem_t			mem;
	unsigned int			entries;
	unsigned int			ptr_mask;
	unsigned int			max_pkt_desc;

	efx_desc_t			*pend_desc;
        vmk_uint64                      pend_desc_size;
	efx_txq_t			*common;


	char				lock_name[SFVMK_LOCK_NAME_MAX];


	int				blocked VMK_ATTRIBUTE_L1_ALIGNED;
	/* The following fields change more often, and are used mostly
	 * on the initiation path
	 */
	unsigned int			n_pend_desc;
	unsigned int			added;
	unsigned int			reaped;

	/* The last VLAN TCI seen on the queue if FW-assisted tagging is
	   used */
//	uint16_t			hw_vlan_tci;

	/* Statistics */
	unsigned long			collapses;
	unsigned long			drops;
	unsigned long			get_overflow;
	unsigned long			get_non_tcp_overflow;
	unsigned long			put_overflow;
	unsigned long			netdown_drops;

	/* The following fields change more often, and are used mostly
	 * on the completion path
	 */
	unsigned int			pending ;
	unsigned int			completed;
	struct sfvmk_txq		*next;
}sfvmk_txq;

int sfvmk_TxInit(sfvmk_adapter *adapter);

void sfvmk_TxFini(sfvmk_adapter *adapter);

int sfvmk_TXStart(sfvmk_adapter *adapter);
void sfvmk_TXStop(sfvmk_adapter *adapter);

#endif
