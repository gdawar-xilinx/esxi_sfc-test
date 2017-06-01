/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#ifndef __SFVMK_DRIVER_H__
#define __SFVMK_DRIVER_H__
#include "efx.h"
#include  "sfvmk.h"
//#include  "sfvmk_rx.h"
//#include  "sfvmk_tx.h"

extern VMK_ReturnStatus sfvmk_DriverRegister(void);
extern void             sfvmk_DriverUnregister(void);
#define SFC_PCI_COMMAND             0x04
#define SFC_PCI_COMMAND_BUS_MASTER  0x04  // Bus master
#define	SFVMK_MODERATION	30
#if 0 
typedef struct efsys_bar_s
{
	vmk_VA vaddr;

}efsys_bar_t;
typedef struct efsys_mem_s
{
   vmk_SgOpsHandle SgOps_handle;
   vmk_SgArray *MachSg;
   vmk_uint32 dma_size;
   void *virtaddr;
}efsys_mem_t;
#endif 


//static int sfvmk_rx_ring_entries = SFVMK_NDESCS;
//static int sfvmk_tx_ring_entries = SFVMK_NDESCS;
typedef struct sfvmk_adptrPhyInfo {
   vmk_uint8 link_status;
   vmk_uint16 link_speed;
   vmk_uint16 phy_type;
   efx_phy_media_type_t interface_type;
   vmk_uint32 misc_params;
   vmk_uint32 advertising;
   vmk_uint32 supported;
#if ESX_VERSION_NUMBER >= ESX_VERSION(2015)
   vmk_UplinkCableType cable_type;
   vmk_UplinkTransceiverType transceiver;
   vmk_Bool async_evt;
#endif
}sfvmk_adptrPhyInfo;

enum sfvmk_txq_type {
	SFVMK_TXQ_NON_CKSUM = 0,
	SFVMK_TXQ_IP_CKSUM,
	SFVMK_TXQ_IP_TCP_UDP_CKSUM,
	SFVMK_TXQ_NTYPES
};

enum sfvmk_flush_state {
	SFVMK_FLUSH_DONE = 0,
	SFVMK_FLUSH_REQUIRED,
	SFVMK_FLUSH_PENDING,
	SFVMK_FLUSH_FAILED
};
enum sfvmk_intrState {
	SFVMK_INTR_UNINITIALIZED = 0,
	SFVMK_INTR_INITIALIZED,
	SFVMK_INTR_TESTING,
	SFVMK_INTR_STARTED
};

typedef struct sfvmk_intr {
	enum sfvmk_intrState		state; /* ?? TBD */
	vmk_IntrCookie			intrCookies[64] ; //[SFVMK_MAX_MSIX_VECTORS];
	int				numIntrAlloc;
	efx_intr_type_t			type;
	unsigned int 			zero_count;      /* ?? TBD */
}sfvmk_intr;
enum sfvmk_mcdi_state {
	SFVMK_MCDI_UNINITIALIZED = 0,
	SFVMK_MCDI_INITIALIZED,
	SFVMK_MCDI_BUSY,
	SFVMK_MCDI_COMPLETED
};

struct sfvmk_mcdi {
	//struct mtx		lock;
        vmk_Mutex               lock;
	efsys_mem_t		mem;
	enum sfvmk_mcdi_state	state;
	efx_mcdi_transport_t	transport;

	/* Only used in debugging output */
	char			lock_name[SFVMK_LOCK_NAME_MAX];
};


typedef struct sfvmk_queueInfo
{
   //struct elxnet_DMAMem dma_mem;
   vmk_uint16 len;
   vmk_uint16 entry_size;
   vmk_uint16 id;
   vmk_uint16 tail, head;
   vmk_Bool created;
   vmk_atomic64 used;
}sfvmk_queueInfo;


struct sfvmk_rxObj
{
   struct sfvmk_adapter *adapter;
   struct sfvmk_queueInfo q;
   struct sfvmk_queueInfo cq;

	 // praveen willl do later on
   //struct elxnet_rxComplInfo rxcp;
  // struct elxnet_rxFragInfo *frag_info_table;
   vmk_uint64 memSize;
   vmk_UplinkSharedQueueData *qData;
   //struct elxnet_rxStats stats;
   vmk_uint8 rss_id;
   vmk_Bool rx_post_starved;
   vmk_uint16 prev_frag_idx;
   vmk_uint32 q_index;
   vmk_uint32 if_handle;
   vmk_uint32 if_type;
   vmk_Bool if_created;
   vmk_Bool rss_init_done;
} VMK_ATTRIBUTE_L1_ALIGNED;

struct sfvmk_txObj
{
   struct sfvmk_queueInfo q;
   struct sfvmk_queueInfo cq;
//   struct elxnet_txPktInfo pktInfo[ELXNET_TX_Q_LEN];
   vmk_UplinkSharedQueueData *qData;
 //  struct elxnet_txStats stats;
   vmk_uint32 q_index;
   vmk_uint32 dbOffset;
} VMK_ATTRIBUTE_L1_ALIGNED;

typedef struct sfvmk_nic_poll {
   struct sfvmk_adapter *adapter;
   vmk_UplinkSharedQueueData *queueData;
   vmk_NetPoll netPoll;
   struct sfvmk_rxObj *rxo;
   vmk_uint8 state;
   vmk_Bool vector_set;
   vmk_IntrCookie vector;
} sfvmk_nicPoll;

struct sfvmk_hw_stats {
  //clock_t	update_time;
  efsys_mem_t	dma_buf;
  void		*decode_buf;
};

typedef struct sfvmk_port {
  struct sfvmk_adapter_s  *sc;
  efsys_lock_t	  lock;
  enum sfvmk_port_state init_state;
#ifndef SFXGE_HAVE_PAUSE_MEDIAOPTS
  unsigned int    wanted_fc;
#endif
  struct sfvmk_hw_stats mac_stats;
  //efx_link_mode_t   link_mode;
  //uint8_t     mcast_addrs[EFX_MAC_MULTICAST_LIST_MAX *
  //            EFX_MAC_ADDR_LEN];
  unsigned int    mcast_count;
}sfvmk_port;

/** Adapter data structure **/
typedef struct sfvmk_adapter_s {
	/* Device handle passed by VMK */
	vmk_Device		vmkDevice;

	/* VMK kernel related information */
	vmk_DMAEngine    	vmkDmaEngine;

	/* PCI device related information */
	vmk_PCIDevice    	pciDevice;
	vmk_PCIDeviceID   pciDeviceID;
	vmk_PCIDeviceAddr pciDeviceAddr;
	vmk_Name          pciDeviceName;

	vmk_VA            *resourceBar2MappedAddress;

	/* EFX /efsys related information */
	efx_family_t      efxFamily;
	efsys_bar_t       bar;
	efsys_lock_t	  enp_lock;
	efx_nic_t	  *enp;
  
        vmk_WorldID 	  worldId;

	/* ESXi SFC Driver related information */
	struct sfvmk_mcdi	mcdi;
	struct sfvmk_intr sfvmkIntrInfo;
        struct sfvmk_port		port;
	/*event queue  */
	unsigned int			evq_max;
	unsigned int 			evq_count;
	unsigned int 			rxq_count;
	unsigned int 			txq_count;
	unsigned int			rxq_enteries;
	unsigned int			txq_enteries;
	unsigned int			mtu;

#define	SFVMK_FATSOV1	(1 << 0)
#define	SFVMK_FATSOV2	(1 << 1)


        unsigned int			tso_fw_assisted;

	struct sfvmk_evq		*evq[SFVMK_RX_SCALE_MAX];

	struct sfvmk_rxq		*rxq[SFVMK_RX_SCALE_MAX];

	struct sfvmk_txq		*txq[SFVMK_TXQ_NTYPES + SFVMK_RX_SCALE_MAX];
	unsigned int			rx_indir_table[SFVMK_RX_SCALE_MAX];


        size_t				rx_prefix_size;
	size_t				rx_buffer_size;
	size_t				rx_buffer_align;


	/*  */
  unsigned int 			max_rss_channels;
	unsigned int			ev_moderation;

	/* Copy of MAC stats */
	uint64_t mac_stats[EFX_MAC_NSTATS];
 
	/* Uplink Info */
	vmk_Device 				uplinkDevice;
	vmk_UplinkRegData regData;
	vmk_UplinkSharedData	sharedData;
	vmk_Name 					uplinkName;

 /* Serializes multiple writers. */
   vmk_Lock sharedDataWriterLock;
   vmk_UplinkSharedQueueInfo queueInfo;
   vmk_UplinkSharedQueueData queueData[SFVMK_RX_SCALE_MAX + SFVMK_RX_SCALE_MAX];

   sfvmk_nicPoll nicPoll[SFVMK_RX_SCALE_MAX];
   vmk_Uplink uplink;
   vmk_UplinkSupportedMode supportedModes[16];
   vmk_uint32 supportedModesArraySz;

       vmk_Name idName;
       vmk_uint32 debugMask;
       vmk_uint64 memSize;

     /* TX Rings  */
   struct sfvmk_txObj tx_obj[SFVMK_RX_SCALE_MAX];

   /* Rx rings */
   struct sfvmk_rxObj rx_obj[SFVMK_RX_SCALE_MAX];
   sfvmk_adptrPhyInfo phy;
}sfvmk_adapter;

/* mcdi calls */

int sfvmk_mcdi_init(sfvmk_adapter *sfAdapter);

int sfvmk_port_start(sfvmk_adapter *adapter);
#endif /* __SFVMK_DRIVER_H__ */

