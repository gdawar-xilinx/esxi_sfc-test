/*
 * Copyright (c) 2017, Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __SFVMK_DRIVER_H__
#define __SFVMK_DRIVER_H__
#include "sfvmk.h"
#include "efx.h"

/* Default number of descriptors required for RXQs */
#define SFVMK_NUM_RXQ_DESC 1024

/* Default number of descriptors required for TXQs */
#define SFVMK_NUM_TXQ_DESC 1024

/* Offset in 256 bytes configuration address space */
#define SFVMK_PCI_COMMAND             0x04
/* Type of command */
#define SFVMK_PCI_COMMAND_BUS_MASTER  0x04
/* Uplink Rxq start index */
#define SFVMK_UPLINK_RXQ_START_INDEX    0

#define	SFVMK_MAGIC_RESERVED	      0x8000
#define	SFVMK_SW_EV_MAGIC(_sw_ev)     (SFVMK_MAGIC_RESERVED | (_sw_ev))
#define SFVMK_SW_EV_RX_QREFILL        1

/* Device name hash table size */
#define SFVMK_ADAPTER_TABLE_SIZE  64

/* Max number of filter supported by default RX queue.
 * HW supports total 8192 filters.
 * TODO: Using a smaller number of filters in driver as
 * supporting only single queue right now.
 * Needs to revisit during multiQ implementation.
 */
#define SFVMK_MAX_FILTER 2048

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
  SFVMK_SPINLOCK_RANK_TXQ_LOCK,
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
  vmk_Mutex             lock;
  sfvmk_mcdiState_t     state;
  sfvmk_mcdiMode_t      mode;
  efx_mcdi_transport_t  transport;
#if EFSYS_OPT_MCDI_LOGGING
  /* Flag for enable/disable MCDI logging at run time */
  vmk_Bool                   mcLogging;
#endif
} sfvmk_mcdi_t;

/* Intrrupt module state */
typedef enum sfvmk_intrState_e {
  SFVMK_INTR_STATE_UNINITIALIZED = 0,
  SFVMK_INTR_STATE_INITIALIZED,
  SFVMK_INTR_STATE_STARTED
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
  SFVMK_EVQ_STATE_STARTING,
  SFVMK_EVQ_STATE_STARTED
} sfvmk_evqState_t;

typedef struct sfvmk_evq_s {
  struct sfvmk_adapter_s  *pAdapter;
  /* Memory for event queue */
  efsys_mem_t             mem;
  vmk_Lock                lock;
  /* Hardware EVQ index */
  vmk_uint32              index;
  vmk_NetPoll             netPoll;
  efx_evq_t               *pCommonEvq;
  /* Number of event queue descriptors in EVQ */
  vmk_uint32              numDesc;
  /* Following fields are protected by spinlock across multiple threads */
  /* EVQ state */
  sfvmk_evqState_t        state;
  vmk_Bool                exception;
  vmk_uint32              txDone;
  vmk_uint32              readPtr;
  vmk_uint32              rxDone;
} sfvmk_evq_t;

typedef enum sfvmk_flushState_e {
  SFVMK_FLUSH_STATE_DONE = 0,
  SFVMK_FLUSH_STATE_REQUIRED,
  SFVMK_FLUSH_STATE_PENDING,
  SFVMK_FLUSH_STATE_FAILED
} sfvmk_flushState_t;

typedef enum sfvmk_portState_e {
  SFVMK_PORT_STATE_UNINITIALIZED = 0,
  SFVMK_PORT_STATE_INITIALIZED,
  SFVMK_PORT_STATE_STARTED
} sfvmk_portState_t;

typedef struct sfvmk_port_s {
  sfvmk_portState_t   state;
  efx_link_mode_t     linkMode;
  vmk_uint32          fcRequested;
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
  SFVMK_TXQ_STATE_STARTED
} sfvmk_txqState_t;

/* Buffer mapping information for descriptors in flight */
typedef struct sfvmk_txMapping_s {
  /*TODO: TSO related fields */
  vmk_PktHandle *pXmitPkt;
  vmk_SgElem    sgElem;
} sfvmk_txMapping_t;

typedef enum {
   /*TODO: TSO offload type */
   SFVMK_TX_VLAN   = 1 << 0,
} sfvmk_offloadType_t;

typedef struct sfvmk_xmitInfo_s {
   sfvmk_offloadType_t offloadFlag;
   vmk_PktHandle       *pXmitPkt;
} sfvmk_xmitInfo_t;

typedef struct sfvmk_txq_s {
  struct sfvmk_adapter_s  *pAdapter;
  /* Lock to synchronize transmit flow with tx completion context */
  vmk_Lock                lock;
  /* HW TXQ index */
  vmk_uint32              index;
  /* Associated eventq Index */
  vmk_uint32              evqIndex;
  /* Number of buffer desc in the TXQ */
  vmk_uint32              numDesc;
  vmk_uint32              ptrMask;
  efsys_mem_t             mem;
  sfvmk_txqState_t        state;
  sfvmk_txqType_t         type;
  sfvmk_flushState_t      flushState;
  efx_txq_t               *pCommonTxq;
  efx_desc_t              *pPendDesc;

  /* Lock also protects following fields in txqStop and txqStart */
  sfvmk_txMapping_t       *pTxMap;  /* Packets in flight. */
  vmk_uint32              nPendDesc;
  vmk_uint32              added;
  vmk_uint32              reaped;
  vmk_uint32              completed;

  /* The last VLAN TCI seen on the queue if FW-assisted tagging is used */
  vmk_uint16              hwVlanTci;

  /* The following fields change more often and are read regularly
   * on the transmit and transmit completion path */
  vmk_uint32              pending VMK_ATTRIBUTE_L1_ALIGNED;
  vmk_Bool                blocked VMK_ATTRIBUTE_L1_ALIGNED;
} sfvmk_txq_t;

/* Descriptor for each buffer */
typedef struct sfvmk_rxSwDesc_s {
  vmk_int32      flags;
  vmk_int32      size;
  vmk_PktHandle  *pPkt;
  vmk_IOA        ioAddr;
} sfvmk_rxSwDesc_t;

typedef enum sfvmk_rxqState_e {
  SFVMK_RXQ_STATE_UNINITIALIZED = 0,
  SFVMK_RXQ_STATE_INITIALIZED,
  SFVMK_RXQ_STATE_STARTED
} sfvmk_rxqState_t;

typedef struct sfvmk_rxq_s {
  struct sfvmk_adapter_s  *pAdapter;
  efsys_mem_t             mem;
  vmk_uint32              index;
  vmk_uint32              numDesc;
  vmk_uint32              ptrMask;
  efx_rxq_t               *pCommonRxq;
  /* Following fields are protected by associated EVQ's spinlock */
  sfvmk_rxqState_t        state;
  sfvmk_flushState_t      flushState;
  /* Variables for managing queue */
  vmk_uint32              added;
  vmk_uint32              pushed;
  vmk_uint32              pending;
  vmk_uint32              completed;
  vmk_uint32              refillThreshold;
  vmk_uint32              refillDelay;
  sfvmk_rxSwDesc_t        *pQueue;
} sfvmk_rxq_t;

/* Data structure for filter database entry */
typedef struct sfvmk_filterDBEntry_s {
  vmk_UplinkQueueFilterClass class;
  vmk_uint32                 key;
  vmk_uint32                 qID;
  efx_filter_spec_t          spec;
} sfvmk_filterDBEntry_t;

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

typedef enum sfvmk_pktCompCtxType_e {
  SFVMK_PKT_COMPLETION_NETPOLL,
  SFVMK_PKT_COMPLETION_OTHERS,
  SFVMK_PKT_COMPLETION_MAX,
} sfvmk_pktCompCtxType_t;

typedef struct sfvmk_pktCompCtx_s {
  sfvmk_pktCompCtxType_t type;
  vmk_NetPoll            netPoll;
} sfvmk_pktCompCtx_t;

typedef struct sfvmk_pktOps_s {
  void (*pktRelease)(sfvmk_pktCompCtx_t *pCompCtx, vmk_PktHandle *pPkt);
} sfvmk_pktOps_t;

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

  /* Adapter lock */
  vmk_Mutex                  lock;

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
  /* Interrupt moderation in micro seconds */
  vmk_uint32                 intrModeration;

  vmk_uint32                 numRxqBuffDesc;
  vmk_uint32                 numTxqBuffDesc;

  /* Ptr to array of numTxqsAllocated TXQs */
  sfvmk_txq_t                **ppTxq;
  vmk_uint32                 numTxqsAllocated;

  /* Ptr to array of numRxqsAllocated RXQs */
  sfvmk_rxq_t                **ppRxq;
  vmk_uint32                 numRxqsAllocated;
  vmk_uint32                 defRxqIndex;
  size_t                     rxPrefixSize;
  size_t                     rxBufferSize;
  size_t                     rxBufferStartAlignment;
  /* Max frame size that should be accepted */
  size_t                     rxMaxFrameSize;
  vmk_Bool                   enableRSS;

  sfvmk_port_t               port;

  sfvmk_uplink_t             uplink;
  /* Dev Name ptr ( pointing to PCI device name or uplink Name).
   * Used only for debugging */
  vmk_Name                   devName;
  /* Handle for helper world queue */
  vmk_Helper                 helper;

  sfvmk_pktOps_t             pktOps[SFVMK_PKT_COMPLETION_MAX];
  vmk_uint32                 txDmaDescMaxSize;

  /* Filter Database hash table and key generator */
  vmk_HashTable              filterDBHashTable;
  vmk_uint32                 filterKey;
} sfvmk_adapter_t;

/* Release pkt in different context by using different release functions */
static inline void sfvmk_pktRelease(sfvmk_adapter_t *pAdapter,
                                    sfvmk_pktCompCtx_t *pCompCtx,
                                    vmk_PktHandle *pPkt)
{
  pAdapter->pktOps[(pCompCtx)->type].pktRelease(pCompCtx, pPkt);
}

/* Functions for interrupt handling */
VMK_ReturnStatus sfvmk_intrInit(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_intrFini(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_intrStart(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_intrStop(sfvmk_adapter_t *pAdapter);

/* Spinlock  handlers */
VMK_ReturnStatus
sfvmk_createLock(sfvmk_adapter_t *pAdapter,
                 const char *pLockName,
                 vmk_LockRank rank,
                 vmk_Lock *pLock);

void sfvmk_destroyLock(vmk_Lock lock);

/* mutex handler */
VMK_ReturnStatus
sfvmk_mutexInit(const char *pLockName,vmk_Mutex *pMutex);
void sfvmk_mutexDestroy(vmk_Mutex mutex);

/* Mempool handlers */
vmk_VA sfvmk_memPoolAlloc(size_t size);
void sfvmk_memPoolFree(vmk_VA vAddr, size_t size);

/* DMA memory handler */
void sfvmk_freeDMAMappedMem(vmk_DMAEngine engine, void *pVA,
                            vmk_IOA ioAddr, size_t size);

void *sfvmk_allocDMAMappedMem(vmk_DMAEngine dmaEngine, size_t size,
                              vmk_IOA *ioAddr);

vmk_uint32 sfvmk_pow2GE(vmk_uint32 value);

/* Function to handle world sleep */
VMK_ReturnStatus sfvmk_worldSleep(vmk_uint64 sleepTime);

/* Get time in micro seconds */
static inline void sfvmk_getTime(vmk_uint64 *pTime)
{
  vmk_TimeVal time;

  vmk_GetTimeOfDay(&time);
  *pTime = (time.sec * VMK_USEC_PER_SEC) + time.usec;
}

/* Functions for MCDI handling */
VMK_ReturnStatus sfvmk_mcdiInit(sfvmk_adapter_t *pAdapter);
void sfvmk_mcdiFini(sfvmk_adapter_t *pAdapter);
int sfvmk_mcdiIOHandler(struct sfvmk_adapter_s *pAdapter,
                        efx_mcdi_req_t *pEmReq);
#if EFSYS_OPT_MCDI_LOGGING
vmk_Bool sfvmk_getMCLogging(sfvmk_adapter_t *pAdapter);
void sfvmk_setMCLogging(sfvmk_adapter_t *pAdapter, vmk_Bool state);
#endif

/* Functions for event queue handling */
VMK_ReturnStatus sfvmk_evInit(sfvmk_adapter_t *pAdapter);
void sfvmk_evFini(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_evStart(sfvmk_adapter_t *pAdapter);
void sfvmk_evStop(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_evqPoll(sfvmk_evq_t *pEvq);

/* Functions for port module handling */
VMK_ReturnStatus sfvmk_portInit(sfvmk_adapter_t *pAdapter);
void sfvmk_portFini(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_portStart(sfvmk_adapter_t *pAdapter);
void sfvmk_portStop(sfvmk_adapter_t *pAdapter);
void sfvmk_macLinkUpdate(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_scheduleLinkUpdate(sfvmk_adapter_t *pAdapter);

/* Functions for TXQ module handling */
VMK_ReturnStatus sfvmk_txInit(sfvmk_adapter_t *pAdapter);
void sfvmk_txFini(sfvmk_adapter_t *pAdapter);
void sfvmk_txStop(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_txStart(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_txqFlushDone(sfvmk_txq_t *pTxq);
vmk_Bool sfvmk_isTxqStopped(sfvmk_adapter_t *pAdapter, vmk_uint32 txqIndex);
VMK_ReturnStatus sfvmk_transmitPkt(sfvmk_txq_t *pTxq, vmk_PktHandle *pkt);
void sfvmk_txqReap(sfvmk_txq_t *pTxq);
void sfvmk_txqComplete(sfvmk_txq_t *pTxq, sfvmk_evq_t *pEvq,
                       sfvmk_pktCompCtx_t *pCompCtx);

/* Functions for RXQ module handling */
VMK_ReturnStatus sfvmk_rxInit(sfvmk_adapter_t *pAdapter);
void sfvmk_rxFini(sfvmk_adapter_t *pAdapter);
void sfvmk_rxStop(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_rxStart(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_setRxqFlushState(sfvmk_rxq_t *pRxq, sfvmk_flushState_t flushState);
void sfvmk_rxqFill(sfvmk_rxq_t *pRxq, sfvmk_pktCompCtx_t *pCompCtx);
void sfvmk_rxqComplete(sfvmk_rxq_t *pRxq, sfvmk_pktCompCtx_t *pCompCtx);

/* Functions for Uplink filter handling */
VMK_ReturnStatus sfvmk_allocFilterDBHash(sfvmk_adapter_t *pAdapter);
void sfvmk_freeFilterDBHash(sfvmk_adapter_t *pAdapter);
vmk_uint32 sfvmk_generateFilterKey(sfvmk_adapter_t *pAdapter);
sfvmk_filterDBEntry_t * sfvmk_allocFilterRule(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_prepareFilterRule(sfvmk_adapter_t *pAdapter,
                                         vmk_UplinkQueueFilter *pFilter,
                                         sfvmk_filterDBEntry_t *pFdbEntry,
                                         vmk_uint32 filterKey, vmk_uint32 qidVal);
VMK_ReturnStatus sfvmk_insertFilterRule(sfvmk_adapter_t *pAdapter, sfvmk_filterDBEntry_t *pFdbEntry);
sfvmk_filterDBEntry_t * sfvmk_removeFilterRule(sfvmk_adapter_t *pAdapter, vmk_uint32 filterKey);
void sfvmk_freeFilterRule(sfvmk_adapter_t *pAdapter, sfvmk_filterDBEntry_t *pFdbEntry);

VMK_ReturnStatus sfvmk_uplinkDataInit(sfvmk_adapter_t * pAdapter);
void sfvmk_uplinkDataFini(sfvmk_adapter_t *pAdapter);
void sfvmk_removeUplinkFilter(sfvmk_adapter_t *pAdapter, vmk_uint32 qidVal);

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

/*! \brief Get the start index of uplink TXQs in vmk_UplinkSharedQueueData array
**
** \param[in]  pUplink  pointer to uplink structure
**
** \return: index from where TXQs is starting in vmk_UplinkSharedQueueData array
*/
static inline vmk_uint32 sfvmk_getUplinkTxqStartIndex(sfvmk_uplink_t *pUplink)
{
  /* queueData is an array of vmk_UplinkSharedQueueData for all queues.
   * It is structured to record information about maxRxQueues receive
   * queues first followed by maxTxQueuestransmit queues.
   */
  return (SFVMK_UPLINK_RXQ_START_INDEX + pUplink->queueInfo.maxRxQueues);
}

/*! \brief Get the pointer to tx shared queue data
**
** \param[in]  pAdapter  pointer to adapter structure
**
** \return: pointer to queue data from where TXQs is starting
*/
static inline vmk_UplinkSharedQueueData* sfvmk_getUplinkTxSharedQueueData(sfvmk_uplink_t *pUplink)
{
  /* queueData is an array of vmk_UplinkSharedQueueData for all queues.
   * It is structured to record information about maxRxQueues receive
   * queues first followed by maxTxQueuestransmit queues.
   */
  return &pUplink->queueInfo.queueData[sfvmk_getUplinkTxqStartIndex(pUplink)];
}

VMK_ReturnStatus sfvmk_scheduleReset(sfvmk_adapter_t *pAdapter);

vmk_UplinkCableType sfvmk_decodeQsfpCableType(sfvmk_adapter_t *pAdapter);
vmk_UplinkCableType sfvmk_decodeSfpCableType(sfvmk_adapter_t *pAdapter);

void sfvmk_updateQueueStatus(sfvmk_adapter_t *pAdapter,
                             vmk_UplinkQueueState qState,
                             vmk_uint32 qIndex);

#endif /* __SFVMK_DRIVER_H__ */
