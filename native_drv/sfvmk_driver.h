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
#include "sfvmk_mgmt_interface.h"
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

/* Max number of filter supported by default RX queue.
 * HW supports total 8192 filters.
 * TODO: Using a smaller number of filters in driver as
 * supporting only single queue right now.
 * Needs to revisit during multiQ implementation.
 */
#define SFVMK_MAX_FILTER              2048
#define SFVMK_MAX_ADAPTER             16
#define SFVMK_MAX_FW_IMAGE_SIZE       1843200 /* 1.8 MB */

/* Max number of NetQ supported */
#define SFVMK_MAX_NETQ_COUNT          16

/* Max number of RSSQ supported */
#define SFVMK_MAX_RSSQ_COUNT          4

/* 20 EVQs, 16 for NetQ and 4 for RSS */
#define SFVMK_MAX_EVQ                 SFVMK_MAX_NETQ_COUNT + SFVMK_MAX_RSSQ_COUNT
#define SFVMK_MAX_INTR                SFVMK_MAX_EVQ
#define SFVMK_MAX_TXQ                 SFVMK_MAX_EVQ
#define SFVMK_MAX_RXQ                 SFVMK_MAX_EVQ

#define SFVMK_RSS_HASH_KEY_SIZE       40

extern VMK_ReturnStatus sfvmk_driverRegister(void);
extern void             sfvmk_driverUnregister(void);

/* Lock rank is based on the order in which locks are typically acquired.
 * A lock with higher rank can be acquired while holding a lock with a lower rank.
 * NIC and BAR locks are taken by common code and are at the highest rank.
 * Uplink lock is primarily for control path and is at the next lower rank
 * than NIC and BAR.TXQ and EVQ are the logical order in which the locks are taken.
 */
typedef enum sfvmk_spinlockRank_e {
  SFVMK_SPINLOCK_RANK_EVQ_LOCK = VMK_SPINLOCK_RANK_LOWEST,
  SFVMK_SPINLOCK_RANK_TXQ_LOCK,
  SFVMK_SPINLOCK_RANK_UPLINK_LOCK,
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
  /* Maximum number of packets to be processed in each netPoll invocation */
  vmk_uint32              rxBudget;
  /* Used for storing pktList passed in sfvmk_panicPoll */
  vmk_PktList             panicPktList;
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
  sfvmk_portState_t     state;
  efx_link_mode_t       linkMode;
  vmk_uint32            fcRequested;
  vmk_uint32            advertisedCapabilities;
  efsys_mem_t	        macStatsDmaBuf;
  efx_phy_media_type_t  mediumType;
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

/* Tx Queue statistics */
typedef enum sfvmk_txqStats_e {
  SFVMK_TXQ_PKTS = 0,
  SFVMK_TXQ_BYTES,
  SFVMK_TXQ_INVALID_QUEUE_STATE,
  SFVMK_TXQ_QUEUE_BUSY,
  SFVMK_TXQ_DMA_MAP_ERROR,
  SFVMK_TXQ_DESC_POST_FAILED,
  SFVMK_TXQ_TSO_PARSING_FAILED,
  SFVMK_TXQ_TSO_LONG_HEADER_ERROR,
  SFVMK_TXQ_QUEUE_BLOCKED,
  SFVMK_TXQ_QUEUE_UNBLOCKED,
  SFVMK_TXQ_SG_ELEM_GET_FAILED,
  SFVMK_TXQ_SG_ELEM_TOO_LONG,
  SFVMK_TXQ_PARTIAL_COPY_FAILED,
  SFVMK_TXQ_DISCARD,
  SFVMK_TXQ_MAX_STATS
} sfvmk_txqStats_t;

static const char * const pSfvmkTxqStatsName[] = {
  "tx_pkts",
  "tx_bytes",
  "tx_invalid_queue_state",
  "tx_queue_busy",
  "tx_dma_map_error",
  "tx_desc_post_failed",
  "tx_tso_parsing_failed",
  "tx_tso_long_header_error",
  "tx_queue_blocked",
  "tx_queue_unblocked",
  "tx_sg_elem_get_failed",
  "tx_sg_elem_too_long",
  "tx_partial_copy_failed",
  "tx_discard",
  "tx_max_stats"
};

/* Buffer mapping information for descriptors in flight */
typedef struct sfvmk_txMapping_s {
  vmk_PktHandle *pOrigPkt;
  vmk_PktHandle *pXmitPkt;
  vmk_SgElem    sgElem;
} sfvmk_txMapping_t;

typedef enum {
   SFVMK_TX_TSO       = 1 << 0,
   SFVMK_TX_VLAN      = 1 << 1,
   SFVMK_TX_ENCAP_TSO = 1 << 2,
} sfvmk_offloadType_t;

typedef enum {
   SFVMK_TSO_DEFRAG_HEADER = 1 << 0,
   SFVMK_TSO_DEFRAG_SGES   = 1 << 1,
} sfvmk_fixType_t;

typedef struct sfvmk_xmitInfo_s {
   sfvmk_offloadType_t offloadFlag;
   sfvmk_fixType_t     fixFlag;
   vmk_uint32          seqNumNbo;
   vmk_uint32          headerLen;
   vmk_uint32          firstSgLen;
   vmk_uint32          mss;
   /* IPv4 packet ID from the original/inner packet */
   vmk_uint16          packetId;
   vmk_uint16          outerPacketId;
   vmk_uint32          dmaDescsEst;

   /* pXmitPkt is the one we need to transmit. pOrigPkt is the one forwarded by
    * kernel .uplinkTx routine. They two maybe the same if the origPkt didn't
    * violate any hardware constraints.
    */
   vmk_PktHandle       *pOrigPkt;
   vmk_PktHandle       *pXmitPkt;
} sfvmk_xmitInfo_t;

typedef enum sfvmk_hdrInfoType_e {
  SFVMK_HDR_INFO_TYPE_UNUSED = -1,
  SFVMK_HDR_INFO_TYPE_IP,
  SFVMK_HDR_INFO_TYPE_TCP,
  SFVMK_HDR_INFO_TYPE_ENCAP_IP,
  SFVMK_HDR_INFO_TYPE_MAX
} sfvmk_hdrInfoType_t;

typedef struct sfvmk_hdrInfo_s {
  vmk_PktHeaderEntry  *pHdrEntry;
  void                *pMappedPtr;
} sfvmk_hdrInfo_t;

typedef struct sfvmk_hdrParseCtrl_s {
  vmk_uint16           expHdrType;
  sfvmk_hdrInfoType_t  hdrInfoType;
} sfvmk_hdrParseCtrl_t;

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

  vmk_atomic64            stats[SFVMK_TXQ_MAX_STATS];

  /* The last VLAN TCI seen on the queue if FW-assisted tagging is used */
  vmk_uint16              hwVlanTci;
  vmk_Bool                isCso;
  vmk_Bool                isEncapCso;

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

/* Rx Queue statistics */
typedef enum sfvmk_rxqStats_e {
  SFVMK_RXQ_PKTS = 0,
  SFVMK_RXQ_BYTES,
  SFVMK_RXQ_INVALID_DESC,
  SFVMK_RXQ_INVALID_PKT_BUFFER,
  SFVMK_RXQ_DMA_UNMAP_FAILED,
  SFVMK_RXQ_PSEUDO_HDR_PKT_LEN_FAILED,
  SFVMK_RXQ_PKT_HEAD_ROOM_FAILED,
  SFVMK_RXQ_PKT_FRAME_MAPPED_PTR_FAILED,
  SFVMK_RXQ_INVALID_BUFFER_DESC,
  SFVMK_RXQ_INVALID_FRAME_SZ,
  SFVMK_RXQ_INVALID_PROTO,
  SFVMK_RXQ_DISCARD,
  SFVMK_RXQ_MAX_STATS
} sfvmk_rxqStats_t;

static const char * const pSfvmkRxqStatsName[] = {
  "rx_pkts",
  "rx_bytes",
  "rx_invalid_desc",
  "rx_invalid_pkt_buffer",
  "rx_dma_unmap_failed",
  "rx_pseudo_hdr_pkt_len_failed",
  "rx_pkt_head_room_failed",
  "rx_pkt_frame_mapped_ptr_failed",
  "rx_invalid_buffer_desc",
  "rx_invalid_frame_sz",
  "rx_invalid_proto",
  "rx_discard",
  "rx_max_stats"
};

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
  vmk_atomic64            stats[SFVMK_RXQ_MAX_STATS];
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
  /* Currently advertised modes (speed/duplex/media) */
  vmk_UplinkAdvertisedMode   advertisedModes[EFX_PHY_CAP_NTYPES];
  vmk_uint32                 numAdvertisedModes;
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
  SFVMK_PKT_COMPLETION_PANIC,
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
  vmk_uint32                 numRSSQs;
  vmk_uint32                 numNetQs;
  vmk_uint32                 rssHashKeySize;
  vmk_Bool                   rssInit;
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
  vmk_uint8                  rssHashKey[SFVMK_RSS_HASH_KEY_SIZE];
  vmk_uint32                 rssIndTable[EFX_RSS_TBL_SIZE];

  sfvmk_port_t               port;

  sfvmk_uplink_t             uplink;
  /* Dev Name ptr ( pointing to PCI device name or uplink Name).
   * Used only for debugging */
  vmk_Name                   devName;
  /* Handle for helper world queue */
  vmk_Helper                 helper;

  sfvmk_pktOps_t             pktOps[SFVMK_PKT_COMPLETION_MAX];
  vmk_uint32                 txDmaDescMaxSize;

  /* The number of tx packets dropped */
  vmk_atomic64               txDrops;

  /* Filter Database hash table and key generator */
  vmk_HashTable              filterDBHashTable;
  vmk_uint32                 filterKey;

  /* MAC stats copy */
  efsys_stat_t               adapterStats[EFX_MAC_NSTATS];

  /* isRxCsumLock synchronizes access to isRxCsumEnabled in
   * netpoll and uplink contexts */
  vmk_VersionedAtomic        isRxCsumLock;
  vmk_Bool                   isRxCsumEnabled;
  vmk_Bool                   isTsoFwAssisted;

  /* VxLAN UDP port number */
  vmk_uint16                 vxlanUdpPort;
  vmk_Bool                   startIOTunnelReCfgReqd;
} sfvmk_adapter_t;

extern const sfvmk_pktOps_t sfvmk_packetOps[];
/* Release pkt in different context by using different release functions */
static inline void sfvmk_pktRelease(sfvmk_adapter_t *pAdapter,
                                    sfvmk_pktCompCtx_t *pCompCtx,
                                    vmk_PktHandle *pPkt)
{
  if(VMK_UNLIKELY(vmk_SystemCheckState(VMK_SYSTEM_STATE_PANIC)) == VMK_TRUE) {
    pCompCtx->type = SFVMK_PKT_COMPLETION_PANIC;
  }

  if (pCompCtx->type < SFVMK_PKT_COMPLETION_MAX)
    sfvmk_packetOps[pCompCtx->type].pktRelease(pCompCtx, pPkt);
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
VMK_ReturnStatus sfvmk_evqModerate(sfvmk_adapter_t *pAdapter,
                                   unsigned int qIndex,
                                   unsigned int uSec);

/* Functions for port module handling */
VMK_ReturnStatus sfvmk_portInit(sfvmk_adapter_t *pAdapter);
void sfvmk_portFini(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_portStart(sfvmk_adapter_t *pAdapter);
void sfvmk_portStop(sfvmk_adapter_t *pAdapter);
void sfvmk_macLinkUpdate(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_scheduleLinkUpdate(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_phyLinkSpeedSet(sfvmk_adapter_t *pAdapter, vmk_LinkSpeed speed);
VMK_ReturnStatus sfvmk_macStatsUpdate(sfvmk_adapter_t *pAdapter);
void sfvmk_linkStateGet(sfvmk_adapter_t *pAdapter, vmk_LinkState *pLinkState);
void sfvmk_phyLinkSpeedGet(sfvmk_adapter_t *pAdapter, vmk_LinkSpeed *pSpeed,
                           vmk_Bool *pAutoNeg);
void sfvmk_getPhyAdvCaps(sfvmk_adapter_t *pAdapter, vmk_uint8 efxPhyCap,
                         vmk_UplinkSupportedMode *pSupportedModes,
                         vmk_uint32 *pCount);

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
VMK_ReturnStatus sfvmk_configRSS(sfvmk_adapter_t *pAdapter,
                                 vmk_uint8 *pKey,
                                 vmk_uint32 keySize,
                                 vmk_uint32 *pIndTable,
                                 vmk_uint32 indTableSize);

/*! \brief disable RSS by making numRSSQs as 0  in adapter data structure
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t structure
**
** \return: none
*/
static inline void sfvmk_disableRSS(sfvmk_adapter_t *pAdapter)
{
  VMK_ASSERT_NOT_NULL(pAdapter);
  pAdapter->numRSSQs = 0;
}

/*! \brief Check if RSS enable by checking numRSSQs
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t structure
**
** \return: VMK_TRUE if RSS is enable VMK_FALSE otherwise
*/
static inline vmk_Bool sfvmk_isRSSEnable(sfvmk_adapter_t *pAdapter)
{
  VMK_ASSERT_NOT_NULL(pAdapter);
  return (pAdapter->numRSSQs > 0) ;
}

/*! \brief Get RSS start queue index
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t structure
**
** \return:  RSS start queue index
*/
static inline vmk_uint32 sfvmk_getRSSQStartIndex(sfvmk_adapter_t *pAdapter)
{
  VMK_ASSERT_NOT_NULL(pAdapter);
  return pAdapter->numNetQs;
}

/*! \brief Get maximum Rx hardware queue number
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t structure
**
** \return: number of max HW Rx queues
*/
static inline vmk_uint32
sfvmk_getMaxRxHardwareQueues(sfvmk_adapter_t *pAdapter)
{
  VMK_ASSERT_NOT_NULL(pAdapter);
  return pAdapter->numRxqsAllocated;
}

/*! \brief Get maximum Tx hardware queue number
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t structure
**
** \return: number of max HW Tx queues
*/
static inline vmk_uint32
sfvmk_getMaxTxHardwareQueues(sfvmk_adapter_t *pAdapter)
{
  VMK_ASSERT_NOT_NULL(pAdapter);
  return pAdapter->numTxqsAllocated;
}

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

/* Functions for VPD read/write request handling */
VMK_ReturnStatus sfvmk_vpdGetInfo(sfvmk_adapter_t *pAdapter, vmk_uint8 *pVpdData,
                                  vmk_uint32 maxPayloadSize, vmk_uint8 vpdTag,
                                  vmk_uint16 vpdKeyword, vmk_uint8 *pVpdLen);
VMK_ReturnStatus sfvmk_vpdSetInfo(sfvmk_adapter_t *pAdapter, vmk_uint8 *pVpdData,
                                  vmk_uint8 vpdTag, vmk_uint16 vpdKeyword,
                                  vmk_uint8 vpdLen);

/* Locking mechanism to serialize multiple writers's access to protected sharedData area */
static inline void __attribute__((always_inline))
sfvmk_sharedAreaBeginWrite(sfvmk_uplink_t *pUplink)
{
  vmk_SpinlockLock(pUplink->shareDataLock);
  vmk_VersionedAtomicBeginWrite(&pUplink->sharedData.lock);
}

static inline void __attribute__((always_inline))
sfvmk_sharedAreaEndWrite(sfvmk_uplink_t *pUplink)
{
  vmk_VersionedAtomicEndWrite(&pUplink->sharedData.lock);
  vmk_SpinlockUnlock(pUplink->shareDataLock);
}

/* Locking mechanism to serialize multiple readers to access sharedData area */
#define SFVMK_SHARED_AREA_BEGIN_READ(adapter)                           \
  do {                                                                  \
    vmk_uint32 sharedReadLockVer;                                       \
    do {                                                                \
      sharedReadLockVer = vmk_VersionedAtomicBeginTryRead               \
                              (&adapter->uplink.sharedData.lock);

#define SFVMK_SHARED_AREA_END_READ(adapter)                             \
    } while (!vmk_VersionedAtomicEndTryRead                             \
                 (&adapter->uplink.sharedData.lock, sharedReadLockVer));\
  } while (VMK_FALSE)

/*! \brief Get the start index of uplink TXQs in vmk_UplinkSharedQueueData array
**
** \param[in]  pUplink  pointer to uplink structure
**
** \return: index from where TXQs is starting in vmk_UplinkSharedQueueData array
*/
static inline vmk_uint32
sfvmk_getUplinkTxqStartIndex(sfvmk_uplink_t *pUplink)
{
  /* queueData is an array of vmk_UplinkSharedQueueData for all queues.
   * It is structured to record information about maxRxQueues receive
   * queues first followed by maxTxQueuestransmit queues.
   */
  return (SFVMK_UPLINK_RXQ_START_INDEX + pUplink->queueInfo.maxRxQueues);
}

/*! \brief Get the pointer to tx shared queue data
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t structure
**
** \return: pointer to queue data from where TXQs is starting
*/
static inline vmk_UplinkSharedQueueData *
sfvmk_getUplinkTxSharedQueueData(sfvmk_uplink_t *pUplink)
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

VMK_ReturnStatus
sfvmk_configIntrModeration(sfvmk_adapter_t *pAdapter,
                           vmk_uint32 moderation);

void
sfvmk_configQueueDataCoalescParams(sfvmk_adapter_t *pAdapter,
                                   vmk_UplinkCoalesceParams *pParams);

/* Enum/Functions for performing Image update*/
/* Firmware types same as defined in v5/ci/mgmt/firmware_ids.h */
typedef enum sfvmk_imageReflash_e {
  REFLASH_TARGET_PHY = 0,
  REFLASH_TARGET_PHY_LOADER = 1,
  REFLASH_TARGET_BOOTROM = 2,
  REFLASH_TARGET_MCFW = 3,
  REFLASH_TARGET_MCFW_BACKUP = 4,
  REFLASH_TARGET_DISABLED_CALLISTO = 5,
  REFLASH_TARGET_FPGA = 6,
  REFLASH_TARGET_FPGA_BACKUP = 7,
  REFLASH_TARGET_FCFW = 8,
  REFLASH_TARGET_FCFW_BACKUP = 9,
  REFLASH_TARGET_CPLD = 10,
  REFLASH_TARGET_MUMFW = 11,
  REFLASH_TARGET_UEFIROM = 12,
} sfvmk_imageReflash_t;

VMK_ReturnStatus
sfvmk_performUpdate(sfvmk_imgUpdate_t *pImgUpdate,
                                     sfvmk_adapter_t  *pAdapter);


VMK_ReturnStatus
sfvmk_setMacFilter(sfvmk_adapter_t *pAdapter, vmk_UplinkState state);
#endif /* __SFVMK_DRIVER_H__ */
