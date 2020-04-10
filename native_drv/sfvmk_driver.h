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

/* Enabling workarounds */
/* This is a workaround to take care of bug 84849. Diagnostic in offline
 * mode has an issue which cause crash. This workaround disable offline test.
 */
#define SFVMK_WORKAROUND_84849

/* Default number of descriptors required for RXQs */
#define SFVMK_NUM_RXQ_DESC 512

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

/* Max number of uplink filter supported by default RX queue.
 * For encapsulation case, there are 2 HW filters per 1 uplink filter
 * HW supports total 8192 filters.
 */
#define SFVMK_MAX_FILTER              2048
#define SFMK_MAX_HWF_PER_UPF          2

#define SFVMK_MAX_ADAPTER             16
#define SFVMK_MAX_FW_SIGNED_IMAGE     1843200 /* 1.8 MB */
#define SFVMK_BUNDLE_IMAGE            2000000 /* 2 MB */
#define SFVMK_ALLOC_FW_IMAGE_SIZE     SFVMK_MAX((SFVMK_MAX_FW_SIGNED_IMAGE * 2), \
                                                 SFVMK_BUNDLE_IMAGE)

/* Max number of NetQ supported */
#define SFVMK_MAX_NETQ_COUNT          15

/* Max number of RSSQ supported */
#define SFVMK_MAX_RSSQ_COUNT          4

#define SFVMK_NETPOLL_TX_BUDGET       128

/* Wait time for StartIO on MC Reboot */
#ifdef SFVMK_SUPPORT_SRIOV
/* Allow extra time for proxy auth reinitialization */
#define SFVMK_STARTIO_ON_MC_REBOOT_TIME_OUT_MSEC    10000
#else
#define SFVMK_STARTIO_ON_MC_REBOOT_TIME_OUT_MSEC    300
#endif

/* Wait time for MC Reboot to complete. Value set higher
 * than max MCDI timeout value for worst case scenarios
 */
#define SFVMK_MC_REBOOT_TIME_OUT_MSEC               11000

/* 20 EVQs, 15 (NetQ) + 1 (RSS) for Uplink Qs and 4 for HW RSS Q */
#define SFVMK_MAX_EVQ                 (SFVMK_MAX_NETQ_COUNT + 1 + SFVMK_MAX_RSSQ_COUNT)
#define SFVMK_MAX_INTR                SFVMK_MAX_EVQ
#define SFVMK_MAX_TXQ                 SFVMK_MAX_EVQ
#define SFVMK_MAX_RXQ                 SFVMK_MAX_EVQ

#define SFVMK_RSS_HASH_KEY_SIZE       40

/* Bit Masks for Encap offload features */
#define SFVMK_VXLAN_OFFLOAD           1 << 0
#define SFVMK_GENEVE_OFFLOAD          1 << 1

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
typedef vmk_Mutex sfvmk_Lock;
#define SFVMK_SPINLOCK_RANK_LOWEST    VMK_SPINLOCK_RANK_LOWEST
#define sfvmk_MutexUnlock(lock)       vmk_MutexUnlock(lock)
#define sfvmk_MutexLock(lock)         vmk_MutexLock(lock)
#else
/* Mutex is not supported, mapping it to semaphore */
typedef vmk_Semaphore sfvmk_Lock;
#define SFVMK_SPINLOCK_RANK_LOWEST    1
#define sfvmk_MutexUnlock(lock)       vmk_SemaUnlock(&(lock))
#define sfvmk_MutexLock(lock)         vmk_SemaLock(&(lock))
#endif

/* Array for NVRAM types */
extern const efx_nvram_type_t nvramTypes[];

extern VMK_ReturnStatus sfvmk_driverRegister(void);
extern void             sfvmk_driverUnregister(void);

/* Lock rank is based on the order in which locks are typically acquired.
 * A lock with higher rank can be acquired while holding a lock with a lower rank.
 * NIC and BAR locks are taken by common code and are at the highest rank.
 * Uplink lock is primarily for control path and is at the next lower rank
 * than NIC and BAR.TXQ and EVQ are the logical order in which the locks are taken.
 */
typedef enum sfvmk_spinlockRank_e {
  SFVMK_SPINLOCK_RANK_EVQ_LOCK = SFVMK_SPINLOCK_RANK_LOWEST,
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
  SFVMK_MCDI_MODE_EVENT,
  SFVMK_MCDI_MODE_DEAD
} sfvmk_mcdiMode_t;

typedef struct sfvmk_mcdi_s {
  efsys_mem_t           mem;
  sfvmk_Lock            lock;
  sfvmk_mcdiState_t     state;
  sfvmk_mcdiMode_t      mode;
  efx_mcdi_transport_t  transport;
  vmk_WorldEventID      completionEvent;
#if EFSYS_OPT_MCDI_LOGGING
  /* Flag for enable/disable MCDI logging at run time */
  vmk_Bool              mcLogging;
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
  /* Maximum number of rx packets to be processed in each netPoll invocation */
  vmk_uint32              rxBudget;
  /* Maximum number of tx packets to be processed in each netPoll invocation */
  vmk_uint32              txBudget;
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

typedef enum sfvmk_monState_e {
  SFVMK_MON_STATE_UNINITIALIZED = 0,
  SFVMK_MON_STATE_INITIALIZED
} sfvmk_monState_t;

typedef struct sfvmk_mon_s {
  sfvmk_monState_t      state;
  efsys_mem_t	        monStatsDmaBuf;
  /* Sensor state copy */
  efx_mon_stat_state_t	lastState[EFX_MON_NSTATS];
} sfvmk_mon_t;

typedef struct sfvmk_port_s {
  sfvmk_portState_t     state;
  efx_link_mode_t       linkMode;
  vmk_uint32            fcRequested;
  vmk_uint32            advertisedCapabilities;
  efsys_mem_t	        macStatsDmaBuf;
  efx_phy_media_type_t  mediumType;
} sfvmk_port_t;

typedef enum sfvmk_txqState_e {
  SFVMK_TXQ_STATE_UNINITIALIZED = 0,
  SFVMK_TXQ_STATE_INITIALIZED,
  SFVMK_TXQ_STATE_STARTED,
  SFVMK_TXQ_STATE_STOPPING
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
  "tx_packets",
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

/* SFVMK_PORT_FEC_NONE_BIT   : FEC mode configuration is not supported
 * SFVMK_PORT_FEC_AUTO_BIT   : Default/Best FEC mode provided by driver
 * SFVMK_PORT_FEC_OFF_BIT    : NO FEC mode
 * SFVMK_PORT_FEC_RS_BIT     : Reed-Solomon Forward Error Detection mode
 * SFVMK_PORT_FEC_BASER_BIT  : Base-R/Reed-Solomon Forward Error Detection mode
 */
typedef enum sfvmk_fecConfigBits_e {
  SFVMK_PORT_FEC_NONE_BIT,
  SFVMK_PORT_FEC_AUTO_BIT,
  SFVMK_PORT_FEC_OFF_BIT,
  SFVMK_PORT_FEC_RS_BIT,
  SFVMK_PORT_FEC_BASER_BIT
}sfvmk_fecConfigBits_t;

#define SFVMK_FEC_NONE          (1 << SFVMK_PORT_FEC_NONE_BIT)
#define SFVMK_FEC_AUTO          (1 << SFVMK_PORT_FEC_AUTO_BIT)
#define SFVMK_FEC_OFF           (1 << SFVMK_PORT_FEC_OFF_BIT)
#define SFVMK_FEC_RS            (1 << SFVMK_PORT_FEC_RS_BIT)
#define SFVMK_FEC_BASER         (1 << SFVMK_PORT_FEC_BASER_BIT)

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
  /* Number of descriptors in transmit queue */
  vmk_uint32              numDesc;
  vmk_uint32              ptrMask;
  efsys_mem_t             mem;
  sfvmk_txqState_t        state;
  sfvmk_flushState_t      flushState;
  efx_txq_t               *pCommonTxq;
  efx_desc_t              *pPendDesc;

  /* Lock also protects following fields in txqStop and txqStart */
  sfvmk_txMapping_t       *pTxMap;  /* Packets in flight. */
  vmk_uint32              nPendDesc;
  vmk_uint32              added;
  vmk_uint32              reaped;
  vmk_uint32              completed;

  vmk_uint64              stats[SFVMK_TXQ_MAX_STATS];

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
  SFVMK_RXQ_RSS_HASH_FAILED,
  SFVMK_RXQ_MAX_STATS
} sfvmk_rxqStats_t;

static const char * const pSfvmkRxqStatsName[] = {
  "rx_packets",
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
  "rx_rsshash_failed",
  "rx_max_stats"
};

typedef enum sfvmk_rxqState_e {
  SFVMK_RXQ_STATE_UNINITIALIZED = 0,
  SFVMK_RXQ_STATE_INITIALIZED,
  SFVMK_RXQ_STATE_STARTED,
  SFVMK_RXQ_STATE_STOPPING
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
  vmk_uint64              stats[SFVMK_RXQ_MAX_STATS];
  sfvmk_rxSwDesc_t        *pQueue;
} sfvmk_rxq_t;

/* Estimate length of hardware queues stats buffer and MAC stats buffer */
#define SFVMK_STATS_ENTRY_LEN     60
#define SFVMK_MAC_STATS_BUF_LEN   (EFX_MAC_NSTATS * SFVMK_STATS_ENTRY_LEN)
#define SFVMK_QUEUE_STATS_BUF_LEN (((SFVMK_MAX_TXQ * SFVMK_TXQ_MAX_STATS)  + \
                                    (SFVMK_MAX_RXQ * SFVMK_RXQ_MAX_STATS)) * \
                                    SFVMK_STATS_ENTRY_LEN)
#define SFVMK_STATS_BUFFER_SZ     (SFVMK_QUEUE_STATS_BUF_LEN + SFVMK_MAC_STATS_BUF_LEN)
#define SFVMK_STATS_UPDATE_WAIT_USEC  VMK_USEC_PER_MSEC

/* Data structure for filter database entry
** Note:
**   For encapsulation case, more than 1 filter gets created per uplink filter
*/
typedef struct sfvmk_filterDBEntry_s {
  vmk_UplinkQueueFilterClass class;
  vmk_uint32                 key;
  vmk_uint32                 qID;
  vmk_uint8                  numHwFilter;
  efx_filter_spec_t          spec[SFMK_MAX_HWF_PER_UPF];
} sfvmk_filterDBEntry_t;

typedef struct sfvmk_uplink_s {
  /* Structure advertising a mode (speed/duplex/media) that is supported by an uplink. */
  vmk_UplinkSupportedMode    supportedModes[EFX_PHY_CAP_NTYPES];
  vmk_uint32                 numSupportedModes;
#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
  /* Currently advertised modes (speed/duplex/media) */
  vmk_UplinkAdvertisedMode   advertisedModes[EFX_PHY_CAP_NTYPES];
  vmk_uint32                 numAdvertisedModes;
#endif
  /* Uplink Q allocated for RSS */
  vmk_uint32                 rssUplinkQueue;
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

#define SFVMK_MAC_BUF_SIZE           18
#define SFVMK_DECLARE_MAC_BUF(var)   char var[SFVMK_MAC_BUF_SIZE]

/* utility macros for VLAN handling */
#define SFVMK_VLAN_PRIO_SHIFT                 13
#define SFVMK_VLAN_PRIO_MASK                  0xe000
#define SFVMK_VLAN_VID_MASK                   0x0fff

#ifdef SFVMK_SUPPORT_SRIOV
#define SFVMK_MAX_VLANS              4096
#define SFVMK_WORKAROUND_54586       1
#if SFVMK_WORKAROUND_54586
/*
 * Do not request multicast privilege together with all-multicast.
 * Just all-multicast is sufficient for multicast filter if
 * SFVMK_WORKAROUND_54586 is enabled.
 */
#define SFVMK_WORKAROUND_79967	    1
#endif
#define SFVMK_EF10_RX_MODE_PRIVILEGE_MASK \
                                 (MC_CMD_PRIVILEGE_MASK_IN_GRP_UNICAST |\
                                  MC_CMD_PRIVILEGE_MASK_IN_GRP_MULTICAST |\
                                  MC_CMD_PRIVILEGE_MASK_IN_GRP_BROADCAST |\
                                  MC_CMD_PRIVILEGE_MASK_IN_GRP_ALL_MULTICAST |\
                                  MC_CMD_PRIVILEGE_MASK_IN_GRP_PROMISCUOUS)

/* First index in pVportConfig holds PF configuration & VFs start at index 1 */
#define SFVMK_VF_VPORT_ID(pAdapter, vf) \
                                 (pAdapter->pVportConfig[vf + 1].evc_vport_id)
/* Structures to help parsing of MCDI request and response buffers */
typedef struct {
  vmk_uint8      *emr_out_buf;
} sfvmk_outbuf_t;

typedef struct {
  vmk_uint8      *emr_in_buf;
} sfvmk_inbuf_t;

typedef struct sfvmk_pendingProxyReq_s {
  vmk_uint32             reqdPrivileges;
  vmk_NetVFCfgInfo       cfg;
  vmk_uint64             uhandle;
} sfvmk_pendingProxyReq_t;

/* VF info structure */
typedef struct sfvmk_vfInfo_s {
  vmk_PCIDevice           vfPciDev;
  vmk_PCIDeviceAddr       vfPciDevAddr;
  vmk_VFRXMode            rxMode;
  vmk_uint32              macMtu;
  sfvmk_pendingProxyReq_t pendingProxyReq;
  vmk_BitVector           *pAllowedVlans;
  vmk_BitVector           *pActiveVlans;
} sfvmk_vfInfo_t;

typedef enum sfvmk_proxyAuthState_e {
  SFVMK_PROXY_AUTH_STATE_STARTING = 0,
  SFVMK_PROXY_AUTH_STATE_READY,
  SFVMK_PROXY_AUTH_STATE_RUNNING,
  SFVMK_PROXY_AUTH_STATE_STOPPING,
} sfvmk_proxyAuthState_t;

typedef struct sfvmk_proxyReqState_s {
  vmk_atomic64              reqState;
  vmk_int32                 result;
  vmk_uint32                grantedPrivileges;
  struct sfvmk_proxyEvent_s *pProxyEvent;
} sfvmk_proxyReqState_t;

typedef struct sfvmk_proxyEvent_s {
  struct sfvmk_adapter_s    *pAdapter;
  sfvmk_proxyReqState_t     *pReqState;
  vmk_uint32                index;
  vmk_AddrCookie            requestTag;
} sfvmk_proxyEvent_t;

typedef struct sfvmk_proxyAdminState_s {
  /* Proxy auth module state */
  vmk_atomic64              authState;
  /* Pointer to array of proxy request state for all functions */
  sfvmk_proxyReqState_t     *pReqState;
  /* Proxy result in case of authorization failure/timeout */
  vmk_uint32                defaultResult;
  /* Maximum size of proxy request */
  size_t                    requestSize;
  /* Maximum size of proxy response */
  size_t                    responseSize;
  /* Maximum number of functions */
  vmk_uint32                blockCount;
  /* Memory for proxy requests */
  efsys_mem_t               requestBuffer;
  /* Memory for proxy responses */
  efsys_mem_t               responseBuffer;
  /* Memory for book-keeping */
  efsys_mem_t               statusBuffer;
  /* Privileges handled by proxy operation */
  vmk_uint32                handledPrivileges;
  /* Helper world for processing proxy requests */
  vmk_Helper                proxyHelper;
  /* Total number of requests submitted on the helper queue */
  vmk_atomic64               numRequests;
} sfvmk_proxyAdminState_t;

typedef enum sfvmk_proxyRequestState_e {
  /* Proxy request state is set to IDLE on creation and closure */
  SFVMK_PROXY_REQ_STATE_IDLE,
  /* Proxy request state is set to INCOMING on receiving the MCDI event */
  SFVMK_PROXY_REQ_STATE_INCOMING,
  /* Proxy request state is set OUTSTANDING before seeking ESXi authorization */
  SFVMK_PROXY_REQ_STATE_OUTSTANDING,
  /* Proxy request moves to COMPLETED state on ESXi authorization or Timeout */
  SFVMK_PROXY_REQ_STATE_COMPLETED,
  /* COMPLETING state is entered when it is executed by PF on VF's behalf */
  SFVMK_PROXY_REQ_STATE_COMPLETING,
  SFVMK_PROXY_REQ_STATE_RESPONDING,
} sfvmk_proxyRequestState_t;

/* Host memory status buffer used to manage proxied requests.
 * This structure is shared with Firmware MC_PROXY_STATUS_BUFFER
 * structure definition in efx_regs_mcdi.h
 */
typedef struct sfvmk_proxyMcState_s {
  vmk_uint32 handle;
  vmk_uint16 pf;
  vmk_uint16 vf;
  vmk_uint16 rid;
  vmk_uint16 status;
  vmk_uint32 grantedPrivileges;
} __attribute__ (( packed )) sfvmk_proxyMcState_t;

typedef enum sfvmk_evbState_e {
  SFVMK_EVB_STATE_UNINITIALIZED = 0,
  SFVMK_EVB_STATE_STARTED,
  SFVMK_EVB_STATE_STOPPING,
} sfvmk_evbState_t;
#endif

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

  /* Adapter lock  is used to serialize code flow
   * and access to adapter data i.e.  startIO/quiesceIO.
   * It also protects the fields:
   * Driver state
   * Interrupt Param (intr, intrModeration, numRxqBuffDesc,
   * numRxqBuffDesc, numTxqBuffDesc)
   * Port
   * Uplink
   * RxQ Param (defRxqIndex, rxPrefixSize, rxBufferSize,
   * rxBufferAlign, enableRSS)
   */
  sfvmk_Lock                 lock;

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

  sfvmk_mon_t                mon;

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

  vmk_Bool                   isRxCsumEnabled;
  vmk_Bool                   isTsoFwAssisted;

  /* VxLAN UDP port number */
  vmk_uint16                 vxlanUdpPort;
  /* Geneve UDP port number */
  vmk_uint16                 geneveUdpPort;
  /* Bit Mask for supported Encap offloads */
  vmk_uint8                  isTunnelEncapSupported;

  vmk_Bool                   startIOTunnelReCfgReqd;

  /* Event for startIO completion */
  vmk_WorldEventID           startIO_compl_event;

#ifdef SFVMK_SUPPORT_SRIOV
#define SFVMK_SN_MAX_LEN              32

  /* Number of VFs enabled */
  vmk_uint32                 numVfsEnabled;
  /* Pointer to primary adapter (port number 0) */
  struct sfvmk_adapter_s*    pPrimary;
  /* Linked list for secondary ports */
  vmk_ListLinks              secondaryList;
  /* Lock to synchronize access to secondary NIC list */
  sfvmk_Lock                 secondaryListLock;
  /* Linked list entry reference */
  vmk_ListLinks              adapterLink;
  /* Serial number of this adapter */
  vmk_uint8                  vpdSN[SFVMK_SN_MAX_LEN];
  /* Pointer to array of numVfsEnabled VF's info */
  sfvmk_vfInfo_t             *pVfInfo;
  /* Pointer to vSwitch */
  efx_vswitch_t              *pVswitch;
  /* Pointer to config info structure*/
  efx_vport_config_t         *pVportConfig;
  /* Pointer to proxy admin structure */
  sfvmk_proxyAdminState_t    *pProxyState;
  /* PF index */
  vmk_uint32                 pfIndex;
  /* EVB state */
  sfvmk_evbState_t           evbState;
#endif
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
VMK_ReturnStatus sfvmk_mutexInit(const char *pLockName, sfvmk_Lock *pLock);
void sfvmk_mutexDestroy(sfvmk_Lock lock);

/* Mempool handlers */
vmk_VA sfvmk_memPoolAlloc(size_t size);
void sfvmk_memPoolFree(vmk_VA vAddr, size_t size);

/* DMA memory handler */
void sfvmk_freeDMAMappedMem(vmk_DMAEngine engine, void *pVA,
                            vmk_IOA ioAddr, size_t size);

void *sfvmk_allocDMAMappedMem(vmk_DMAEngine dmaEngine, size_t size,
                              vmk_IOA *ioAddr);

vmk_uint32 sfvmk_pow2GE(vmk_uint32 value);

VMK_ReturnStatus
sfvmk_createHelper(sfvmk_adapter_t *pAdapter, char *pHelperName,
                   vmk_Helper *pHelper);

void
sfvmk_destroyHelper(sfvmk_adapter_t *pAdapter, vmk_Helper helper);

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
void sfvmk_setMCDIMode(sfvmk_adapter_t *pAdapter, sfvmk_mcdiMode_t mode);
void sfvmk_mcdiReset(sfvmk_adapter_t *pAdapter);

/* Functions for event queue handling */
VMK_ReturnStatus sfvmk_evInit(sfvmk_adapter_t *pAdapter);
void sfvmk_evFini(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_evStart(sfvmk_adapter_t *pAdapter);
void sfvmk_evStop(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus sfvmk_evqPoll(sfvmk_evq_t *pEvq, vmk_Bool panic);
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

/* Functions for mon module handling */
VMK_ReturnStatus sfvmk_monInit(sfvmk_adapter_t *pAdapter);
void sfvmk_monFini(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus
sfvmk_monStatsUpdate(sfvmk_adapter_t *pAdapter,
                     efx_mon_stat_value_t *pStatVal,
                     efx_mon_stat_limits_t *pStatLimit);
VMK_ReturnStatus
sfvmk_scheduleMonitorUpdate(sfvmk_adapter_t *pAdapter);

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

/*! \brief Get RSS start queue index in the HW Queue structures
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t structure
**
** \return:  RSS start queue index
*/
static inline vmk_uint32 sfvmk_getRSSQStartIndex(sfvmk_adapter_t *pAdapter)
{
  VMK_ASSERT_NOT_NULL(pAdapter);
  /* +1 is for the uplink Q corresponding to RSS */
  return pAdapter->numNetQs + 1;
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
                                         vmk_uint32 filterKey, vmk_uint32 qidVal,
                                         efx_filter_flags_t flags);
VMK_ReturnStatus sfvmk_insertFilterRule(sfvmk_adapter_t *pAdapter, sfvmk_filterDBEntry_t *pFdbEntry);
sfvmk_filterDBEntry_t * sfvmk_removeFilterRule(sfvmk_adapter_t *pAdapter, vmk_uint32 filterKey);
void sfvmk_freeFilterRule(sfvmk_adapter_t *pAdapter, sfvmk_filterDBEntry_t *pFdbEntry);

VMK_ReturnStatus sfvmk_uplinkDataInit(sfvmk_adapter_t * pAdapter);
void sfvmk_uplinkDataFini(sfvmk_adapter_t *pAdapter);
void sfvmk_removeUplinkFilter(sfvmk_adapter_t *pAdapter, vmk_uint32 qidVal);
VMK_ReturnStatus sfvmk_requestQueueStats(sfvmk_adapter_t *pAdapter, char *pStart,
                                         vmk_ByteCount maxBytes, vmk_ByteCount *pBytesCopied);
VMK_ReturnStatus sfvmk_requestMACStats(sfvmk_adapter_t *pAdapter, char *pStart, vmk_ByteCount maxBytes,
                                       vmk_ByteCount *pBytesCopied);

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
** \param[in]  pUplink  pointer to uplink structure
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
  REFLASH_TARGET_BUNDLE = 13,
} sfvmk_imageReflash_t;

VMK_ReturnStatus
sfvmk_performUpdate(sfvmk_adapter_t  *pAdapter, sfvmk_imgUpdateV2_t *pImgUpdateV2);

VMK_ReturnStatus
sfvmk_setMacFilter(sfvmk_adapter_t *pAdapter, vmk_UplinkState state);

void
sfvmk_updateDrvInfo(sfvmk_adapter_t *pAdapter);

VMK_ReturnStatus
sfvmk_phyFecSet(sfvmk_adapter_t *pAdapter, vmk_uint32 reqFec);

VMK_ReturnStatus
sfvmk_phyFecGet(sfvmk_adapter_t *pAdapter,  vmk_uint32 *advFec, vmk_uint32 *activeFec);

VMK_ReturnStatus
sfvmk_nvramRead(sfvmk_adapter_t *pAdapter, efx_nvram_type_t type, vmk_uint8 *pNvramBuf,
                vmk_uint32 *pBufSize, vmk_uint32 start);

VMK_ReturnStatus
sfvmk_nvramWriteAll(sfvmk_adapter_t *pAdapter, efx_nvram_type_t type, vmk_uint8 *pNvramBuf,
                    vmk_uint32 *pBufSize, vmk_Bool eraseNvram);

VMK_ReturnStatus
sfvmk_requestSensorData(sfvmk_adapter_t *pAdapter, char *pSensorBuf, vmk_ByteCount size, vmk_ByteCount *pBytesCopied);

#ifdef SFVMK_SUPPORT_SRIOV
/* SR-IOV handlers */

/* Considering MAX_PFS per card as 4 for four port cards as
 * sfvmk doesn't support PF partitioning: 4 PFs + 252 VFs */
#define SFVMK_MAX_FNS_PER_CARD   256
#define SFVMK_MAX_PFS            SFVMK_MAX_ADAPTER
#define SFVMK_MAX_PFS_PER_CARD   4
#define SFVMK_MAX_VFS_DEFAULT    0
#define SFVMK_MAX_VFS_PER_PF     (SFVMK_MAX_FNS_PER_CARD - SFVMK_MAX_PFS_PER_CARD)/\
                                 SFVMK_MAX_PFS_PER_CARD

/* SR-IOV extended capabilites offsets */
/* Refer PCIE SR-IOV specification, SF-118940-PS Section 3.3.1 */
#define SFVMK_SRIOV_EXT_CAP_ID      0x10
/* Refer PCIE SR-IOV specification, SF-118940-PS Figure 3-1 */
#define SFVMK_TOTAL_VFS_OFFSET      0xE

#define SFVMK_ARRAY_SIZE(array)       (sizeof(array) / sizeof(array[0]))
#define SFVMK_PROXY_AUTH_NUM_BLOCKS   256
/* Setting the ESXi response timeout shorter than EF10_MCDI_CMD_TIMEOUT_US
 * (the MCDI timeout) to avoid timeouts in the proxy client (VF driver) */
#define SFVMK_PROXY_REQ_TIMEOUT_MSEC  5000

extern vmk_int32 max_vfs[SFVMK_MAX_PFS];

vmk_Bool
sfvmk_sameController(sfvmk_adapter_t *pAdapter, sfvmk_adapter_t *pOther);
void
sfvmk_sriovIncrementPfCount(void);
void
sfvmk_sriovDecrementPfCount(void);
VMK_ReturnStatus
sfvmk_sriovInit(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus
sfvmk_registerVFs(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus
sfvmk_sriovFini(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus
sfvmk_evbSwitchInit(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus
sfvmk_evbSwitchFini(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus
sfvmk_proxyAuthInit(sfvmk_adapter_t *pAdapter);
void
sfvmk_proxyAuthFini(sfvmk_adapter_t *pAdapter);
VMK_ReturnStatus
sfvmk_submitProxyRequest(sfvmk_proxyEvent_t *pProxyEvent);
VMK_ReturnStatus
sfvmk_proxyAuthHandleResponse(sfvmk_adapter_t *pAdapter,
                              sfvmk_proxyAdminState_t *pProxyState,
                              vmk_uint64 uhandle,
                              vmk_uint32 result,
                              vmk_uint32 grantedPrivileges);
VMK_ReturnStatus
sfvmk_proxyAuthExecuteRequest(sfvmk_adapter_t *pAdapter,
                               vmk_uint64 uhandle,
                               VMK_ReturnStatus (*pDoCb)(sfvmk_adapter_t *,
                                                  vmk_uint32, vmk_uint32,
                                                  void *,
                                                  size_t, void *,
                                                  size_t, void *),
                               void *pCbContext);
vmk_uint32
sfvmk_rxModeToPrivMask(vmk_VFRXMode rxMode);
VMK_ReturnStatus
sfvmk_proxyCompleteSetMtu(sfvmk_adapter_t *pAdapter,
                          vmk_uint64 uhandle,
                          vmk_Bool *pMtuSet);
vmk_uint32
sfvmk_calcMacMtuPf(sfvmk_adapter_t *pAdapter);

VMK_ReturnStatus
sfvmk_getPrivilegeMask(vmk_PCIDeviceAddr *pPciAddr,
                       vmk_uint32 *pMask);

VMK_ReturnStatus
sfvmk_modifyPrivilegeMask(vmk_PCIDeviceAddr *pPciAddr,
                          vmk_uint32 add_privileges_mask,
                          vmk_uint32 remove_privileges_mask);
#endif

char *
sfvmk_printMac(const vmk_uint8 *pAddr, vmk_int8 *pBuffer);

vmk_Bool
sfvmk_macAddrSame(const vmk_uint8 *pAddr1, const vmk_uint8 *pAddr2);

VMK_ReturnStatus
sfvmk_allocDmaBuffer(sfvmk_adapter_t *pAdapter,
                          efsys_mem_t *pMem,
                          size_t size);

vmk_Bool
sfvmk_isBroadcastEtherAddr(const vmk_uint8 *pAddr);
vmk_uint8
sfvmk_firstBitSet(vmk_uint8 mask);

VMK_ReturnStatus
sfvmk_changeAtomicVar(vmk_atomic64  *var,
                      vmk_uint64 old,
                      vmk_uint64 new,
                      vmk_Bool   termCheck,
                      vmk_uint64 termVal,
                      vmk_uint64 timeout);
#endif /* __SFVMK_DRIVER_H__ */
