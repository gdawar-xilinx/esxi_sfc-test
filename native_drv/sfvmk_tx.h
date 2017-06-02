
#ifndef __SFVMK_TX_H__
#define __SFVMK_TX_H__
#include "efsys.h"
#include "efx.h"

#include "sfvmk_driver.h"
// Number of descriptors per transmit queue
#define SFVMK_TX_NDESCS             1024

// Number of transmit descriptors processed per batch
#define SFVMK_TX_BATCH              64

// Maximum number of packets held in the deferred packet list
#define SFVMK_TX_MAX_DEFERRED       256

#define	SFVMK_TXQ_UNBLOCK_LEVEL(_entries)	(EFX_TXQ_LIMIT(_entries) / 4)

#define SFVMK_TXQ_LOCK(txq)   \
{                         \
	vmk_MutexLock(txq->lock);\
}


#define SFVMK_TXQ_UNLOCK(txq)   \
{                         \
	vmk_MutexUnlock(txq->lock);\
}
 
//TODO
#define SFVMK_TXQ_LOCK_ASSERT_OWNED(txq)

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

/*
 * Buffer mapping information for descriptors in flight.
 */
struct sfvmk_tx_mapping {
	vmk_PktHandle *pkt;
	vmk_SgElem sgelem;
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

	struct sfvmk_tx_mapping		*stmp;	/* Packets in flight. */
//	bus_dma_tag_t			packet_dma_tag;
	efx_desc_t			*pend_desc;
        vmk_uint64                      pend_desc_size;
	efx_txq_t			*common;

//	efsys_mem_t			*tsoh_buffer;

	char				lock_name[SFVMK_LOCK_NAME_MAX];

	/* This field changes more often and is read regularly on both
	 * the initiation and completion paths
	 */
	int				blocked;//TODO __aligned(CACHE_LINE_SIZE);

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
	uint16_t			hw_vlan_tci;

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

int sfvmk_tx_init(sfvmk_adapter *adapter);

int sfvmk_tx_start(sfvmk_adapter *adapter);
void sfvmk_tx_qlist_post(struct sfvmk_txq *txq);
void sfvmk_tx_qcomplete(struct sfvmk_txq *txq, struct sfvmk_evq *evq);
void sfvmk_queuedPkt(sfvmk_adapter *adapter,  sfvmk_txq *txq, vmk_PktHandle *pkt, vmk_ByteCountSmall pktLen);
VMK_ReturnStatus sfvmk_insert_vlan_hdr(vmk_PktHandle **ppPkt, vmk_uint16 vlanTag);
#endif
