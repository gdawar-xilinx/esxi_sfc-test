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

extern VMK_ReturnStatus sfvmk_driverRegister(void);
extern void             sfvmk_driverUnregister(void);

typedef enum sfvmk_spinlockRank_e {
  SFVMK_SPINLOCK_RANK_NIC_LOCK = VMK_SPINLOCK_RANK_LOWEST,
  SFVMK_SPINLOCK_RANK_BAR_LOCK
} sfvmk_spinlockRank_t;

/* Adapter states */
typedef enum sfvmk_adapterState_e {
  SFVMK_ADAPTER_STATE_UNINITIALIZED = 0,
  SFVMK_ADAPTER_STATE_REGISTERED,
  SFVMK_ADAPTER_NSTATES
} sfvmk_adapterState_t;

/* Adapter structure */
typedef struct sfvmk_adapter_s {
  /* Device handle passed by VMK */
  vmk_Device               device;
  /* DMA engine handle */
  vmk_DMAEngine            dmaEngine;
  /* Adapter state */
  sfvmk_adapterState_t     state;
  /* PCI device handle */
  vmk_PCIDevice            pciDevice;
  /* PCI vendor/device id */
  vmk_PCIDeviceID          pciDeviceID;
  /* PCI device Address (SBDF) */
  vmk_PCIDeviceAddr        pciDeviceAddr;
  /* PCI device name */
  vmk_Name                 pciDeviceName;

  /* EFX /efsys related information */
  /* EFX family */
  efx_family_t             efxFamily;
  /* Struct to store mapped memory BAR info */
  efsys_bar_t              bar;
  /* Lock required by common code nic module */
  efsys_lock_t             nicLock;
  efx_nic_t                *pNic;
  /* Dev Name ptr ( pointing to PCI device name or uplink Name).
   * Used only for debugging */
  vmk_Name                 devName;
} sfvmk_adapter_t;

/* Spinlock  handlers */
VMK_ReturnStatus
sfvmk_createLock(sfvmk_adapter_t *pAdapter,
                 const char *pLockName,
                 vmk_LockRank rank,
                 vmk_Lock *pLock);

void sfvmk_destroyLock(vmk_Lock lock);
#endif /* __SFVMK_DRIVER_H__ */
