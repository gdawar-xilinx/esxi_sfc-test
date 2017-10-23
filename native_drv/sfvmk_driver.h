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

/* Default number of descriptors required for RXQs */
#define SFVMK_NUM_RXQ_DESC 1024

/* Default number of descriptors required for TXQs */
#define SFVMK_NUM_TXQ_DESC 1024

extern VMK_ReturnStatus sfvmk_driverRegister(void);
extern void             sfvmk_driverUnregister(void);

/* Lock rank is based on the order in which locks are typically acquired.
 * A lock with higher rank can be acquired while holding a lock with a lower rank.
 * NIC and BAR locks are taken by common code and hence are at the highest rank.
 * Rest all the locks are having lower rank than the MCDI lock as MCDI can be invoked
 * at the lowest most level of the code.
 * Uplink, RXQ, TXQ and Port are the logical order in which the locks are taken.
 */
typedef enum sfvmk_spinlockRank_e {
  SFVMK_SPINLOCK_RANK_EVQ_LOCK = VMK_SPINLOCK_RANK_LOWEST,
  SFVMK_SPINLOCK_RANK_UPLINK_LOCK,
  SFVMK_SPINLOCK_RANK_RXQ_LOCK,
  SFVMK_SPINLOCK_RANK_TXQ_LOCK,
  SFVMK_SPINLOCK_RANK_PORT_LOCK,
  SFVMK_SPINLOCK_RANK_MCDI_LOCK,
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

/* Event queue state */
typedef enum sfvmk_evqState_e {
  SFVMK_EVQ_STATE_UNINITIALIZED = 0,
  SFVMK_EVQ_STATE_INITIALIZED,
} sfvmk_evqState_t;

typedef struct sfvmk_evq_s {
  struct sfvmk_adapter_s  *pAdapter;
  /* Memory for event queue */
  efsys_mem_t             mem;
  vmk_Lock                lock;
  /* Hardware EVQ index */
  vmk_uint32              index;
  /* Number of event queue descriptors in EVQ */
  vmk_uint32              numDesc;
  /* EVQ state */
  sfvmk_evqState_t        state;
  vmk_NetPoll             netPoll;
} sfvmk_evq_t;

typedef enum sfvmk_portState_e {
  SFVMK_PORT_STATE_UNINITIALIZED = 0,
  SFVMK_PORT_STATE_INITIALIZED,
} sfvmk_portState_t;

typedef struct sfvmk_port_s {
  sfvmk_portState_t   state;
  vmk_Lock            lock;
} sfvmk_port_t;

typedef enum sfvmk_txqType_e {
  SFVMK_TXQ_TYPE_NON_CKSUM = 0,
  SFVMK_TXQ_TYPE_IP_CKSUM,
  SFVMK_TXQ_TYPE_IP_TCP_UDP_CKSUM,
  SFVMK_TXQ_NTYPES
} sfvmk_txqType_t;

typedef enum sfvmk_txqState_e {
  SFVMK_TXQ_STATE_UNINITIALIZED = 0,
  SFVMK_TXQ_STATE_INITIALIZED,
} sfvmk_txqState_t;

typedef struct sfvmk_txq_s {
  vmk_Lock            lock;
  /* HW TXQ index */
  vmk_uint32          index;
  /* Associated eventq Index */
  vmk_uint32          evqIndex;
  vmk_uint32          numDesc;
  vmk_uint32          ptrMask;
  efsys_mem_t         mem;
  sfvmk_txqState_t    state;
  sfvmk_txqType_t     type;
} sfvmk_txq_t;

typedef enum sfvmk_rxqState_e {
  SFVMK_RXQ_STATE_UNINITIALIZED = 0,
  SFVMK_RXQ_STATE_INITIALIZED,
} sfvmk_rxqState_t;

typedef struct sfvmk_rxq_s {
  vmk_Lock          lock;
  efsys_mem_t       mem;
  vmk_uint32        index;
  vmk_uint32        numDesc;
  vmk_uint32        ptrMask;
  sfvmk_rxqState_t  state;
} sfvmk_rxq_t;

typedef struct sfvmk_uplink_s {
  /* Structure advertising a mode (speed/duplex/media) that is supported by an uplink. */
  vmk_UplinkSupportedMode    supportedModes[EFX_PHY_CAP_NTYPES];
  vmk_uint32                 numSupportedModes;
  /* Uplink registration data. */
  vmk_UplinkRegData          regData;
  /* Data shared between uplink layer and NIC driver. */
  vmk_UplinkSharedData       sharedData;
  /* Serializes multiple writers. */
  vmk_Lock                   shareDataLock;
  /* Shared queue information */
  vmk_UplinkSharedQueueInfo  queueInfo;
  vmk_Device                 uplinkDevice;
  vmk_DeviceID               deviceID;
  vmk_Name                   name;
  vmk_Uplink                 handle;
} sfvmk_uplink_t;

/* Adapter states */
typedef enum sfvmk_adapterState_e {
  SFVMK_ADAPTER_STATE_UNINITIALIZED = 0,
  SFVMK_ADAPTER_STATE_REGISTERED,
  SFVMK_ADAPTER_STATE_STARTED,
  SFVMK_ADAPTER_NSTATES
} sfvmk_adapterState_t;

/* Adapter structure */
typedef struct sfvmk_adapter_s {
  /* Device handle passed by VMK */
  vmk_Device                 device;
  /* DMA engine handle */
  vmk_DMAEngine              dmaEngine;
  /* Adapter state */
  sfvmk_adapterState_t       state;
  /* PCI device handle */
  vmk_PCIDevice              pciDevice;
  /* PCI vendor/device id */
  vmk_PCIDeviceID            pciDeviceID;
  /* PCI device Address (SBDF) */
  vmk_PCIDeviceAddr          pciDeviceAddr;
  /* PCI device name */
  vmk_Name                   pciDeviceName;

  /* EFX /efsys related information */
  /* EFX family */
  efx_family_t               efxFamily;
  /* Struct to store mapped memory BAR info */
  efsys_bar_t                bar;
  /* Lock required by common code nic module */
  efsys_lock_t               nicLock;
  efx_nic_t                  *pNic;
  sfvmk_mcdi_t               mcdi;
  sfvmk_intr_t               intr;

  /* Ptr to array of numEvqsAllocated EVQs */
  sfvmk_evq_t                **ppEvq;
  vmk_uint32                 numEvqsAllocated;
  vmk_uint32                 numEvqsDesired;
  vmk_uint32                 numEvqsAllotted;
  vmk_uint32                 numTxqsAllotted;
  vmk_uint32                 numRxqsAllotted;

  vmk_uint32                 numRxqBuffDesc;
  vmk_uint32                 numTxqBuffDesc;

  /* Ptr to array of numTxqsAllocated TXQs */
  sfvmk_txq_t                **ppTxq;
  vmk_uint32                 numTxqsAllocated;

  /* Ptr to array of numRxqsAllocated RXQs */
  sfvmk_rxq_t                **ppRxq;
  vmk_uint32                 numRxqsAllocated;
  sfvmk_port_t               port;

  sfvmk_uplink_t             uplink;
  /* Dev Name ptr ( pointing to PCI device name or uplink Name).
   * Used only for debugging */
  vmk_Name                   devName;
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

vmk_uint32 sfvmk_pow2GE(vmk_uint32 value);

/* Functions for MCDI handling */
VMK_ReturnStatus sfvmk_mcdiInit(sfvmk_adapter_t *pAdapter);
void sfvmk_mcdiFini(sfvmk_adapter_t *pAdapter);

/* Functions for event queue handling */
VMK_ReturnStatus sfvmk_evInit(sfvmk_adapter_t *pAdapter);
void sfvmk_evFini(sfvmk_adapter_t *pAdapter);

/* Functions for port module handling */
VMK_ReturnStatus sfvmk_portInit(sfvmk_adapter_t *pAdapter);
void sfvmk_portFini(sfvmk_adapter_t *pAdapter);

/* Functions for TXQ module handling */
VMK_ReturnStatus sfvmk_txInit(sfvmk_adapter_t *pAdapter);
void sfvmk_txFini(sfvmk_adapter_t *pAdapter);

/* Functions for RXQ module handling */
VMK_ReturnStatus sfvmk_rxInit(sfvmk_adapter_t *pAdapter);
void sfvmk_rxFini(sfvmk_adapter_t *pAdapter);

VMK_ReturnStatus sfvmk_uplinkDataInit(sfvmk_adapter_t * pAdapter);
void sfvmk_uplinkDataFini(sfvmk_adapter_t *pAdapter);

/* Locking mechanism to serialize multiple writers's access to protected sharedData area */
static inline void sfvmk_sharedAreaBeginWrite(sfvmk_uplink_t *pUplink)
{
  vmk_SpinlockLock(pUplink->shareDataLock);
  vmk_VersionedAtomicBeginWrite(&pUplink->sharedData.lock);
}

static inline void sfvmk_sharedAreaEndWrite(sfvmk_uplink_t *pUplink)
{
  vmk_VersionedAtomicEndWrite(&pUplink->sharedData.lock);
  vmk_SpinlockUnlock(pUplink->shareDataLock);
}

#endif /* __SFVMK_DRIVER_H__ */
