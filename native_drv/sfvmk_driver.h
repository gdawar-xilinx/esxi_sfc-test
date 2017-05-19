/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#ifndef __SFVMK_DRIVER_H__
#define __SFVMK_DRIVER_H__

#include "sfvmk.h"

extern VMK_ReturnStatus sfvmk_DriverRegister(void);
extern void             sfvmk_DriverUnregister(void);

#define SFVMK_PCI_COMMAND             0x04
#define SFVMK_PCI_COMMAND_BUS_MASTER  0x04  

enum sfvmk_port_state {
  SFVMK_PORT_UNINITIALIZED = 0,
  SFVMK_PORT_INITIALIZED,
  SFVMK_PORT_STARTED
};


typedef struct sfvmk_port {
  struct sfvmk_adapter  *sc;
 // struct mtx    lock;
  enum sfvmk_port_state init_state;
#ifndef SFXGE_HAVE_PAUSE_MEDIAOPTS
  unsigned int    wanted_fc;
#endif
  //struct sfvmk_hw_stats phy_stats;
  //struct sfvmk_hw_stats mac_stats;
  efx_link_mode_t   link_mode;
  //uint8_t     mcast_addrs[EFX_MAC_MULTICAST_LIST_MAX *
  //            EFX_MAC_ADDR_LEN];
  unsigned int    mcast_count;

  /* Only used in debugging output */
//  char      lock_name[SFVMK_LOCK_NAME_MAX];
}sfvmk_port;

/*mcdi related DS and functions*/ 

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
typedef struct sfvmk_PhyInfo {
   vmk_uint8 link_status;
   vmk_uint16 link_speed;
   vmk_uint16 phy_type;
   efx_phy_media_type_t interface_type;
   vmk_uint32 misc_params;
   vmk_uint32 advertising;
   vmk_uint32 supported;
   vmk_UplinkCableType cable_type;
   vmk_UplinkTransceiverType transceiver;
   vmk_Bool async_evt;

}sfvmk_PhyInfo;




enum sfvmk_txq_type {
	SFVMK_TXQ_NON_CKSUM = 0,
	SFVMK_TXQ_IP_CKSUM,
	SFVMK_TXQ_IP_TCP_UDP_CKSUM,
	SFVMK_TXQ_NTYPES
};
enum sfvmk_intrState {
	SFVMK_INTR_UNINITIALIZED = 0,
	SFVMK_INTR_INITIALIZED,
	SFVMK_INTR_TESTING,
	SFVMK_INTR_STARTED
};

typedef struct sfvmk_intr {
	enum sfvmk_intrState		state; 
	vmk_IntrCookie			intrCookies[64] ; //[SFVMK_MAX_MSIX_VECTORS];
	int				numIntrAlloc;
	efx_intr_type_t			type;
}sfvmk_intr;





typedef struct {
  /* Device handle passed by VMK */
  vmk_Device    vmkDevice;

  /* VMK kernel related information */
  vmk_DMAEngine     vmkDmaEngine;

  /* PCI device related information */
  vmk_PCIDevice     pciDevice;
  vmk_PCIDeviceID   pciDeviceID;
  vmk_PCIDeviceAddr pciDeviceAddr;
  vmk_Name          pciDeviceName;

  /* EFX /efsys related information */
  efx_family_t      efxFamily;
  efsys_bar_t       bar;
  efsys_lock_t	  enp_lock;
  efx_nic_t	  *enp;

  /*uplink device inforamtion */
  vmk_Name 					uplinkName;
#define	SFVMK_FATSOV1	(1 << 0)
#define	SFVMK_FATSOV2	(1 << 1)


        unsigned int			tso_fw_assisted;


  /* ESXi SFC Driver related information */
  struct sfvmk_mcdi	mcdi;
  struct sfvmk_intr intr;

  /*event queue relater DS */
  struct sfvmk_evq		*evq[SFVMK_RX_SCALE_MAX];
  unsigned int			ev_moderation;

  struct sfvmk_rxq		*rxq[SFVMK_RX_SCALE_MAX];

  struct sfvmk_txq		*txq[SFVMK_TXQ_NTYPES + SFVMK_RX_SCALE_MAX];

	unsigned int			rx_indir_table[SFVMK_RX_SCALE_MAX];


  unsigned int			evq_max;


  vmk_DeviceID deviceID;
  unsigned int 			max_rss_channels;
  unsigned int			debugMask;
  unsigned int 			evq_count;
  unsigned int 			rxq_count;
  unsigned int 			txq_count;
  unsigned int 			test;

  unsigned int			rxq_entries;
  unsigned int			txq_entries;

       size_t				rx_prefix_size;
	size_t				rx_buffer_size;
	size_t				rx_buffer_align;




/* Uplink Info */
	vmk_Device 				uplinkDevice;
	vmk_UplinkRegData regData;
	vmk_UplinkSharedData	sharedData;

 /* Serializes multiple writers. */
   vmk_Lock sharedDataWriterLock;
   vmk_UplinkSharedQueueInfo queueInfo;
   vmk_UplinkSharedQueueData queueData[SFVMK_RX_SCALE_MAX + SFVMK_RX_SCALE_MAX];

vmk_UplinkSupportedMode supportedModes[16];
   vmk_uint32 supportedModesArraySz;
vmk_Uplink uplink;

	unsigned int			mtu;
sfvmk_PhyInfo phy;
       struct sfvmk_port		port;
}sfvmk_adapter;



/*mcdi public function */
int sfvmk_mcdi_init(sfvmk_adapter *adapter);
void sfvmk_mcdi_fini(sfvmk_adapter *adapter);

void sfvmk_PortStop(sfvmk_adapter *adapter);
int sfvmk_PortStart(sfvmk_adapter *adapter);
#endif /* __SFVMK_DRIVER_H__ */

