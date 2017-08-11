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
#include "sfvmk_ev.h"
#include "sfvmk_rx.h"
#include "sfvmk_tx.h"
#include "sfvmk_utils.h"
#include "sfvmk_port.h"
#include "sfvmk_mcdi.h"
#include "sfvmk_uplink.h"
#include "sfvmk_intr.h"

extern VMK_ReturnStatus sfvmk_driverRegister(void);
extern void             sfvmk_driverUnregister(void);

#define SFVMK_ADAPTER_LOCK(pAdapter) {   \
  vmk_MutexLock(pAdapter->lock);         \
}

#define SFVMK_ADAPTER_UNLOCK(pAdapter) { \
  vmk_MutexUnlock(pAdapter->lock);       \
}

#define SFVMK_PRIV_STATS_BUFFER_SZ    20480
#define SFVMK_PRIV_STATS_ENTRY_LEN    128

/* Offset in 256 bytes configuration address space */
#define SFVMK_PCI_COMMAND             0x04
/* type of command */
#define SFVMK_PCI_COMMAND_BUS_MASTER  0x04

#define SFVMK_NDESCS                  1024

#define SFVMK_FATSOV2 (1 << 1)

#define SFVMK_ADAPTER_TABLE_SIZE  64

/* ring size for TX and RX */
extern int sfvmk_txRingEntries;
extern int sfvmk_rxRingEntries;

/* adapter states */
enum sfvmk_adapterState {
  SFVMK_UNINITIALIZED = 0,
  SFVMK_REGISTERED,
  SFVMK_STARTED
};

/* adapter structure */
typedef struct sfvmk_adapter_s {
  /* Device handle passed by VMK */
  vmk_Device        device;

  /* VMK kernel related information */
  vmk_DMAEngine     dmaEngine;

  enum sfvmk_adapterState  initState;
  /* PCI device related information */
  vmk_PCIDevice     pciDevice;
  vmk_PCIDeviceID   pciDeviceID;
  vmk_PCIDeviceAddr pciDeviceAddr;
  vmk_Name          pciDeviceName;

  /* Adapter lock */
  vmk_Mutex         lock;

  /* EFX /efsys related information */
  efx_family_t      efxFamily;
  efsys_bar_t       bar;
  efsys_lock_t      nicLock;
  efx_nic_t         *pNic;

  sfvmk_mcdi_t      mcdi;

  /*Interrupt */
  sfvmk_intr_t      intr;

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

  size_t        rxPrefixSize;
  size_t        rxBufferSize;
  size_t        rxBufferAlign;

  vmk_uint32    maxRssChannels;

  struct sfvmk_phyInfo_s  phy;
  struct sfvmk_port_s     port;

  /* MAC stats copy */
  uint64_t adapterStats[EFX_MAC_NSTATS];

  vmk_DeviceID   deviceID;

  /* World for periodic device stats collection */
  vmk_WorldID  worldId;

  /*uplink device inforamtion */
  vmk_Name        uplinkName;
  vmk_Uplink      uplink;
  vmk_uint32      supportedModesArraySz;
  vmk_Device      uplinkDevice;

  vmk_UplinkRegData       regData;
  vmk_UplinkSharedData    sharedData;
  vmk_UplinkSupportedMode supportedModes[EFX_PHY_CAP_NTYPES];

  /* Serializes multiple writers. */
  vmk_Lock shareDataLock;

  /* shared queue information */
  vmk_UplinkSharedQueueInfo queueInfo;
  vmk_UplinkSharedQueueData queueData[SFVMK_RX_SCALE_MAX + SFVMK_TX_SCALE_MAX];

  /* VMK helper for scheduling a Adapter Reset */
  vmk_Helper helper;
  vmk_uint32      txDmaDescMaxSize;
}sfvmk_adapter_t;

/* Structure for device instance table  */
typedef struct sfvmk_devHashTable_s {
   vmk_uint8 *pUplinkName;
   sfvmk_adapter_t *pAdapter;
}sfvmk_devHashTable_t;

extern VMK_ReturnStatus sfvmk_scheduleReset(sfvmk_adapter_t *);
#endif /* __SFVMK_DRIVER_H__ */
