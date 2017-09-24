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
  SFVMK_SPINLOCK_RANK_MCDI_LOCK = VMK_SPINLOCK_RANK_LOWEST,
  SFVMK_SPINLOCK_RANK_NIC_LOCK,
  SFVMK_SPINLOCK_RANK_BAR_LOCK
} sfvmk_spinlockRank_t;

typedef enum sfvmk_mcdiState_e {
  SFVMK_MCDI_STATE_UNINITIALIZED = 0,
  SFVMK_MCDI_STATE_INITIALIZED,
} sfvmk_mcdiState_t;

typedef enum sfvmk_mcdiMode_e {
  SFVMK_MCDI_MODE_POLL,
  SFVMK_MCDI_MODE_EVENT
} sfvmk_mcdiMode_t;

typedef struct sfvmk_mcdi_s {
  efsys_mem_t           mem;
  vmk_Lock              lock;
  sfvmk_mcdiState_t     state;
  sfvmk_mcdiMode_t      mode;
  efx_mcdi_transport_t  transport;
} sfvmk_mcdi_t;

/* Intrrupt module state */
typedef enum sfvmk_intrState_e {
  SFVMK_INTR_STATE_UNINITIALIZED = 0,
  SFVMK_INTR_STATE_INITIALIZED,
} sfvmk_intrState_t;

/* Data structure for interrupt handling */
typedef struct sfvmk_intr_s {
  /* Number of interrupt allocated */
  vmk_uint32            numIntrAlloc;
  vmk_uint32            numIntrDesired;
  /* Interrupt Cookies */
  vmk_IntrCookie        *pIntrCookies;
  /* Interrupt module state */
  sfvmk_intrState_t     state;
  /* Interrupt type (MESSAGE, LINE) */
  efx_intr_type_t       type;
} sfvmk_intr_t;

typedef enum sfvmk_txqType_e {
  SFVMK_TXQ_TYPE_NON_CKSUM = 0,
  SFVMK_TXQ_TYPE_IP_CKSUM,
  SFVMK_TXQ_TYPE_IP_TCP_UDP_CKSUM,
  SFVMK_TXQ_NTYPES
} sfvmk_txqType_t;

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
  sfvmk_mcdi_t             mcdi;
  sfvmk_intr_t             intr;
  vmk_uint32               numEvqsDesired;
  vmk_uint32               numEvqsAllotted;
  vmk_uint32               numTxqsAllotted;
  vmk_uint32               numRxqsAllotted;

  /* Dev Name ptr ( pointing to PCI device name or uplink Name).
   * Used only for debugging */
  vmk_Name                 devName;
} sfvmk_adapter_t;

/* Functions for interrupt handling */
VMK_ReturnStatus sfvmk_intrInit(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_intrFini(sfvmk_adapter_t *pAdapter);

/* Spinlock  handlers */
VMK_ReturnStatus
sfvmk_createLock(sfvmk_adapter_t *pAdapter,
                 const char *pLockName,
                 vmk_LockRank rank,
                 vmk_Lock *pLock);

void sfvmk_destroyLock(vmk_Lock lock);

/* Mempool handlers */
vmk_VA sfvmk_memPoolAlloc(size_t size);
void sfvmk_memPoolFree(vmk_VA vAddr, size_t size);

/* DMA memory handler */
void sfvmk_freeDMAMappedMem(vmk_DMAEngine engine, void *pVA,
                            vmk_IOA ioAddr, size_t size);

void *sfvmk_allocDMAMappedMem(vmk_DMAEngine dmaEngine, size_t size,
                              vmk_IOA *ioAddr);

/* Functions for MCDI handling */
VMK_ReturnStatus sfvmk_mcdiInit(sfvmk_adapter_t *pAdapter);
void sfvmk_mcdiFini(sfvmk_adapter_t *pAdapter);

#endif /* __SFVMK_DRIVER_H__ */
