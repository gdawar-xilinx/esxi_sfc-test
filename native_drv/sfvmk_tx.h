
#ifndef __SFVMK_TX_H__
#define __SFVMK_TX_H__
#include "efsys.h"
#include "efx.h"

#include "sfvmk_driver.h"


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
	unsigned int			buf_base_id;
	unsigned int			entries;
	unsigned int			ptr_mask;
	unsigned int			max_pkt_desc;

//	struct sfxge_tx_mapping		*stmp;	/* Packets in flight. */
//	bus_dma_tag_t			packet_dma_tag;
	efx_desc_t			*pend_desc;
        vmk_uint64                      pend_desc_size;
	efx_txq_t			*common;

//	efsys_mem_t			*tsoh_buffer;

	char				lock_name[SFVMK_LOCK_NAME_MAX];

	/* This field changes more often and is read regularly on both
	 * the initiation and completion paths
	 */
//	int				blocked __aligned(CACHE_LINE_SIZE);

	/* The following fields change more often, and are used mostly
	 * on the initiation path
	 */
//	struct mtx			lock __aligned(CACHE_LINE_SIZE);
//	struct sfxge_tx_dpl		dpl;	/* Deferred packet list. */
	unsigned int			n_pend_desc;
	unsigned int			added;
	unsigned int			reaped;

	/* The last VLAN TCI seen on the queue if FW-assisted tagging is
	   used */
//	uint16_t			hw_vlan_tci;

	/* Statistics */
//	unsigned long			tso_bursts;
//	unsigned long			tso_packets;
//	unsigned long			tso_long_headers;
	unsigned long			collapses;
	unsigned long			drops;
	unsigned long			get_overflow;
	unsigned long			get_non_tcp_overflow;
	unsigned long			put_overflow;
	unsigned long			netdown_drops;
//	unsigned long			tso_pdrop_too_many;
//	unsigned long			tso_pdrop_no_rsrc;

	/* The following fields change more often, and are used mostly
	 * on the completion path
	 */
	unsigned int			pending ;
	unsigned int			completed;
	struct sfvmk_txq		*next;
}sfvmk_txq;

int
sfvmk_tx_init(sfvmk_adapter *adapter);

#endif
