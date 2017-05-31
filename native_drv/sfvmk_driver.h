/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#ifndef __SFVMK_DRIVER_H__
#define __SFVMK_DRIVER_H__

#include "sfvmk.h"
#include "efx.h"
#include "sfvmk_rx.h"
#include "sfvmk_tx.h"
#include "sfvmk_ev.h"
#include "sfvmk_utils.h"
#include "sfvmk_port.h"
#include "sfvmk_mcdi.h"

extern VMK_ReturnStatus sfvmk_DriverRegister(void);
extern void             sfvmk_DriverUnregister(void);

#define SFVMK_PCI_COMMAND             0x04
#define SFVMK_PCI_COMMAND_BUS_MASTER  0x04

#define SFVMK_MAX_MSIX_VECTORS        EFX_MAXRSS
#define SFVMK_NDESCS                  1024


//praveen needs to define
#define SFVMK_ADAPTER_LOCK_ASSERT_OWNED(pAdater)

#define SFVMK_FATSOV1 (1 << 0)
#define SFVMK_FATSOV2 (1 << 1)

/* interrupt related DS */

enum sfvmk_intrState {
  SFVMK_INTR_UNINITIALIZED = 0,
  SFVMK_INTR_INITIALIZED,
  SFVMK_INTR_TESTING,
  SFVMK_INTR_STARTED
};


typedef struct sfvmk_intr_s {
  enum sfvmk_intrState  state;
  vmk_IntrCookie        intrCookies[SFVMK_MAX_MSIX_VECTORS] ;
  int                   numIntrAlloc;
  efx_intr_type_t       type;
}sfvmk_intr_t;


/* adapter structure */
typedef struct sfvmk_adapter_s{
  /* Device handle passed by VMK */
  vmk_Device        device;

  /* VMK kernel related information */
  vmk_DMAEngine     dmaEngine;

  /* PCI device related information */
  vmk_PCIDevice     pciDevice;
  vmk_PCIDeviceID   pciDeviceID;
  vmk_PCIDeviceAddr pciDeviceAddr;
  vmk_Name          pciDeviceName;

  /* EFX /efsys related information */
  efx_family_t      efxFamily;
  efsys_bar_t       bar;
  efsys_lock_t      NicLock;
  efx_nic_t         *pNic;

  sfvmk_mcdi_t      mcdi;

  /*Interrupt */
  sfvmk_intr_t        intr;

  /*queues relater DS */
  struct sfvmk_evq_s    *pEvq[SFVMK_RX_SCALE_MAX];
  struct sfvmk_rxq_s    *pRxq[SFVMK_RX_SCALE_MAX];
  struct sfvmk_txq_s    *pTxq[SFVMK_TXQ_NTYPES + SFVMK_TX_SCALE_MAX];

  vmk_uint32      rxIndirTable[SFVMK_RX_SCALE_MAX];

  vmk_uint32      evqMax;
  vmk_uint32      evqCount;
  vmk_uint32      rxqCount;
  vmk_uint32      txqCount;
  vmk_uint32      rxqEntries;
  vmk_uint32      txqEntries;
  vmk_uint32      evModeration;
  vmk_uint32      tsoFwAssisted;
  vmk_uint32      mtu;

  size_t        rxPrefixSize;
  size_t        rxBufferSize;
  size_t        rxBufferAlign;


  vmk_uint32    maxRssChannels;
  vmk_uint32    debugMask;


  struct sfvmk_phyInfo_s  phy;
  struct sfvmk_port_s     port;


  /*uplink device inforamtion */
  vmk_Name        uplinkName;

}sfvmk_adapter_t;

#endif /* __SFVMK_DRIVER_H__ */
