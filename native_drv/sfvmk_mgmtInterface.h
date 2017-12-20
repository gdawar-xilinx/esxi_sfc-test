/*
 * Copyright (c) 2017, Solarflare Communications Inc.
 * All rights reserved.
 *  
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#ifndef __SFVMK_MGMTINTERFACE_H__
#define __SFVMK_MGMTINTERFACE_H__

#include <vmkapi.h> /* Required for VMKAPI type definitions */

/*! \brief Management Callback number
 **
 ** Each callback must have a unique 64-bit integer identifier.
 ** The identifiers 0 through VMK_MGMT_RESERVED_CALLBACKS
 ** are reserved and must not be used by consumers of the
 ** management APIs.  Here, we declare just one callback
 ** identifier.  These identifiers are used by consumers of
 ** the API at runtime to invoke the associated callback.
 **
 ** Technically this identifier could start at
 ** (VMK_MGMT_RESERVED_CALLBACKS + 1), but for clarity and
 ** consistency with other examples, we'll choose a new
 ** value here.
 **
 ** SFVMK_CB_MCDI_REQUEST_2:     For invoking MCDI callback
 **
 ** SFVMK_CB_NVRAM_REQUEST:      NVRAM operations callback
 **
 ** SFVMK_CB_VERINFO_GET:        Callback to retrieve various
 **                              version info.
 **
 ** SFVMK_CB_LINK_STATUS_UPDATE: Get or Set the Link state
 **
 ** SFVMK_CB_LINK_SPEED_UPDATE:  Get/Set the link speed and autoneg
 **
 ** SFVMK_CB_INTR_MODERATION:    Change Interrupt Moderation settings
 **
 ** SFVMK_CB_PCI_INFO_GET:       Get PCI BDF and device information
 **
 ** SFVMK_CB_VPD_REQUEST:        Get or Set VPD information
 **
 */
typedef enum sfvmk_mgmtCbTypes_e {
  SFVMK_CB_MCDI_REQUEST_2 = (VMK_MGMT_RESERVED_CALLBACKS + 1),
  SFVMK_CB_NVRAM_REQUEST,
  SFVMK_CB_VERINFO_GET,
  SFVMK_CB_LINK_STATUS_UPDATE,
  SFVMK_CB_LINK_SPEED_UPDATE,
  SFVMK_CB_INTR_MODERATION,
  SFVMK_CB_PCI_INFO_GET,
  SFVMK_CB_VPD_REQUEST,
  SFVMK_CB_MAX
}sfvmk_mgmtCbTypes_t;

/*
 * The total number of callbacks
 */
#define SFVMK_MGMT_CB_TOTAL (SFVMK_CB_MAX - SFVMK_CB_MCDI_REQUEST_2)

/*
 * The name used to describe this interface.
 */
#define SFVMK_INTERFACE_NAME   "sfvmkMgmtToKernel"
/*
 * The vendor for this interface.
 */
#define SFVMK_INTERFACE_VENDOR "sfvmk"
#define SFVMK_KV_INTERFACE_VENDOR "SFVMK"

#define SFVMK_VER_MAX_CHAR_LEN 128
#define SFVMK_MGMT_COOKIE               (0)
#define SFVMK_DEV_NAME_LEN           (10)
#define SFVMK_PCI_BDF_LEN 16
#define SFVMK_VPD_MAX_PAYLOAD 0x100

/*! \brief Management dev interface structure
 **
 ** deviceName[in] Name of the vmk net device
 **
 ** status[out] Error status returned
 **
 */
typedef struct sfvmk_mgmtDevInfo_s {
  char  deviceName[SFVMK_DEV_NAME_LEN];
  vmk_uint32  status;
} __attribute__((__packed__)) sfvmk_mgmtDevInfo_t;

#define SFVMK_MCDI_MAX_PAYLOAD_ARRAY 255

/*! \brief struct efx_mcdi_request2_s - Parameters for EFX_MCDI_REQUEST2 sub-command
 **
 ** cmd[in] MCDI command type number.
 **
 ** inlen[in] The length of command parameters, in bytes.
 **
 ** outlen[in/out] On entry, the length available for the response, in bytes.
 **	On return, the length used for the response, in bytes.
 **
 ** flags[out] Flags for the command or response.  The only flag defined
 **	at present is EFX_MCDI_REQUEST_ERROR.  If this is set on return,
 **	the MC reported an error.
 **
 ** host_errno[out] On return, if EFX_MCDI_REQUEST_ERROR is included in flags,
 **	the suggested VMK error code for the error.
 **
 ** payload[in/out] On entry, the MCDI command parameters.  On return, the response.
 **
 ** If the driver detects invalid parameters or a communication failure
 ** with the MC, the MGMT calback interface will return VMK_OK, errno will be set
 ** accordingly, and none of the fields will be valid.  If the MC reports
 ** an error, the MGMT calback interface call will return VMK_OK but flags will
 ** include the EFX_MCDI_REQUEST_ERROR flag.  The MC error code can then
 ** be found in payload (if outlen was sufficiently large) and a suggested
 ** VMK error code can be found in host_errno.
 **
 ** EFX_MCDI_REQUEST2 fully supports both MCDIv1 and v2.
 **
 */
typedef struct sfvmk_mcdiRequest2_s {
  vmk_uint16  cmd;
  vmk_uint16  inlen;
  vmk_uint16  outlen;
  vmk_uint16  flags;
  vmk_uint32  host_errno;
  /*
   * The maximum payload length is 0x400 (MCDI_CTL_SDU_LEN_MAX_V2) - 4 bytes
   * = 255 x 32 bit words as MCDI_CTL_SDU_LEN_MAX_V2 doesn't take account of
   * the space required by the V1 header, which still exists in a V2 command.
   */
  vmk_uint32  payload[SFVMK_MCDI_MAX_PAYLOAD_ARRAY];
} __attribute__((__packed__)) sfvmk_mcdiRequest2_t;
#define EFX_MCDI_REQUEST_ERROR	0x0001

/* NVRAM Command */
#define SFVMK_NVRAM_MAX_PAYLOAD 32*1024

/*! \brief struct sfvmk_nvramCmd_s for performing
 **        NVRAM operations
 **
 ** op[in]         Operation type (Size/Read/write/flash/ver_get/ver_set)
 **
 ** type[in]       Type of NVRAM
 **
 ** offset[in]     Location of NVRAM where to start
 **                read/write
 **
 ** size[in/out]    Size of buffer, should be <= SFVMK_NVRAM_MAX_PAYLOAD
 **
 ** subtype[out]    NVRAM subtype, part of get NVRAM version
 **
 ** version[in/out] Version info
 **
 ** data[in/out]    NVRAM data for read/write
 **
 */
typedef	struct sfvmk_nvramCmd_s {
  vmk_uint32 op;
  vmk_uint32 type;
  vmk_uint32 offset;
  vmk_uint32 size;
  vmk_uint32 subtype;
  vmk_uint16 version[4];		/* get/set_ver */
  vmk_uint8 data[SFVMK_NVRAM_MAX_PAYLOAD];	/* read/write */
} __attribute__((__packed__)) sfvmk_nvramCmd_t;

/*! \brief Get size of NVRAM partition
 **
 **  [in]
 **
 **         op:  SFVMK_NVRAM_OP_READ
 **
 **         type: Set NVRAM partition type
 **
 **  [out]
 **
 **         size: Return the size of NVRAM partition type
 **
 */
#define	SFVMK_NVRAM_OP_SIZE		0x00000001

/*! \brief Read data from NVRAM partition
 **
 **  [in]
 **
 **         op:  SFVMK_NVRAM_OP_READ
 **
 **         type: Set NVRAM partition type
 **
 **         size: size of data requested
 **
 **  [out]
 **
 **         size: New size
 **
 **         data: NVRAM data after read from H/W
 **
 */
#define	SFVMK_NVRAM_OP_READ		0x00000002

/*! \brief Write data into NVRAM partition
 **
 **  [in]
 **
 **         op:  SFVMK_NVRAM_OP_WRITE
 **
 **         type: Set NVRAM partition type
 **
 **         size: size of data
 **
 **         data: NVRAM data to write into H/W
 **
 **  [out]
 **
 **         size: Actaul len written into H/W
 **
 */
#define	SFVMK_NVRAM_OP_WRITE		0x00000003

/*! \brief Erase NVRAM partition
 **
 **  [in]
 **
 **         op:  SFVMK_NVRAM_OP_ERASE
 **
 **         type: Set NVRAM partition type
 **
 */
#define	SFVMK_NVRAM_OP_ERASE		0x00000004

/*! \brief Get NVRAM partition version
 **
 **  [in]
 **
 **         op:  SFVMK_NVRAM_OP_GET
 **
 **         type: Set NVRAM partition type
 **
 **  [out]
 **
 **         version: Version of partition type requested
 **
 **         subtype: Subtype veriosn (Optional)
 **
 */
#define	SFVMK_NVRAM_OP_GET_VER		0x00000005

/*! \brief Set NVRAM partition version
 **
 **  [in]
 **
 **         op:  SFVMK_NVRAM_OP_SET
 **
 **         type: Set NVRAM partition type
 **
 **         version: Version of partition type
 **                  to update
 **
 */
#define	SFVMK_NVRAM_OP_SET_VER		0x00000006

/** NVRAM Partition Types */
#define	SFVMK_NVRAM_TYPE_BOOTROM        0x00000001
#define	SFVMK_NVRAM_TYPE_BOOTROM_CFG    0x00000002
#define	SFVMK_NVRAM_TYPE_MC             0x00000003
#define	SFVMK_NVRAM_TYPE_MC_GOLDEN      0x00000004
#define	SFVMK_NVRAM_TYPE_PHY            0x00000005
#define	SFVMK_NVRAM_TYPE_NULL_PHY       0x00000006
#define	SFVMK_NVRAM_TYPE_FPGA           0x00000007
#define	SFVMK_NVRAM_TYPE_FCFW           0x00000008
#define	SFVMK_NVRAM_TYPE_CPLD           0x00000009
#define	SFVMK_NVRAM_TYPE_FPGA_BACKUP    0x0000000a
#define	SFVMK_NVRAM_TYPE_UEFIROM        0x0000000b
#define	SFVMK_NVRAM_TYPE_DYNAMIC_CFG    0x0000000c

/*! \brief struct sfvmk_versionInfo_s Retrieve various
 **        Driver/FW version info
 **
 ** type[in]     Type of SW/Fw Entity whose version
 **              information needs to be returned.
 **
 ** version[out] Version string
 **
 */
typedef struct sfvmk_versionInfo_s {
  vmk_uint32 type;
  char version[SFVMK_VER_MAX_CHAR_LEN];
} __attribute__((__packed__)) sfvmk_versionInfo_t;

#define SFVMK_GET_DRV_VERSION  0x00000001
#define SFVMK_GET_FW_VERSION   0x00000002
#define SFVMK_GET_ROM_VERSION  0x00000004
#define SFVMK_GET_UEFI_VERSION 0x00000008

/*! \brief General commands to perform Get/Set operations
 **
 **  SFVMK_MGMT_DEV_OPS_GET: Generic command to Get data
 **
 **  SFVMK_MGMT_DEV_OPS_SET: Generic command to Set device param
 **
 */
typedef enum sfvmk_mgmtDevOps_e {
  SFVMK_MGMT_DEV_OPS_GET = 1,
  SFVMK_MGMT_DEV_OPS_SET,
  SFVMK_MGMT_DEV_OPS_INVALID
}sfvmk_mgmtDevOps_t;

/*! \brief struct sfvmk_linkStatus_s for get or
 **        set link state
 **
 **  type[in]       Type of operation (Get/Set)
 **
 **  state[in/out]  Retrieve the Current link state
 **                 if operation type is Get.
 **                 If operation type is Set and
 **                 state = TRUE,  Bring Link UP
 **                 state = FALSE, Bring link down
 **
 */
typedef struct sfvmk_linkStatus_s {
  sfvmk_mgmtDevOps_t type;
  vmk_Bool state;
} __attribute__((__packed__)) sfvmk_linkStatus_t;

#define SFVMK_LINK_SPEED_10_MBPS    10
#define SFVMK_LINK_SPEED_100_MBPS   100
#define SFVMK_LINK_SPEED_1000_MBPS  1000
#define SFVMK_LINK_SPEED_10000_MBPS 10000
#define SFVMK_LINK_SPEED_25000_MBPS 25000
#define SFVMK_LINK_SPEED_40000_MBPS 40000
#define SFVMK_LINK_SPEED_50000_MBPS 50000
#define SFVMK_LINK_SPEED_100000_MBPS 100000

/*! \brief struct sfvmk_linkSpeed_s for get or
 **        set link speed and autoneg
 **
 **  type[in]        Type of operation (Get/Set)
 **
 **  speed[in/out]   Current link speed
 **
 **  autoNeg[in/out] Get/Set autoNeg
 **
 **  Please Note: in case of set
 **    1. if autoneg is true then speed is ignored.
 **
 **    2. if autoneg is false, then speed MUST be filled.
 **
 **    In case of get, if autoneg is true, speed value
 **    indicates the link speed at that instant.
 **
 */
typedef struct sfvmk_linkSpeed_s {
  sfvmk_mgmtDevOps_t   type;
  vmk_uint32           speed;
  vmk_Bool             autoNeg;
} __attribute__((__packed__)) sfvmk_linkSpeed_t;

/*! \brief struct sfvmk_intrCoalsParam_s for
 **         Interrupt coalesce parameters
 **
 ** type[in]  Command type (Get/Set)
 **
 ** rxUsecs[in/out] number of microseconds to wait
 **          for Rx, before interrupting
 **
 ** rxMaxFrames[in/out] maximum number of (Rx) frames
 **              to wait for, before interrupting
 **
 ** txUsecs[in/out] number of microseconds to wait
 **          for completed Tx, before interrupting
 **
 ** txMaxFrames[in/out] maximum number of completed (Tx)
 **              frames to wait for, before interrupting
 **
 ** useAdaptiveRx[in/out] Use adaptive Rx coalescing
 **
 ** useAdaptiveTx[in/out] Use adaptive Tx coalescing
 **
 ** rateSampleInterval[in/out] Rate sampling interval
 **
 ** pktRateLowWatermark[in/out] Low packet rate watermark
 **
 ** pktRateHighWatermark[in/out] High packet rate watermark
 **
 ** rxUsecsLow[in/out] Rx usecs low
 **
 ** rxFramesLow[in/out] Rx frames low
 **
 ** txUsecsLow[in/out] Tx usecs low
 **
 ** txFramesLow[in/out] Tx frames low
 **
 ** rxUsecsHigh[in/out] Rx usecs high
 **
 ** rxFramesHigh[in/out] Rx frames high
 **
 ** txUsecsHigh[in/out] Tx usecs high
 **
 ** txFramesHigh[in/out] Tx frames high
 **
 */
typedef struct sfvmk_intrCoalsParam_s {
  sfvmk_mgmtDevOps_t type;
  vmk_uint32 rxUsecs;
  vmk_uint32 rxMaxFrames;
  vmk_uint32 txUsecs;
  vmk_uint32 txMaxFrames;
  vmk_Bool useAdaptiveRx;
  vmk_Bool useAdaptiveTx;
  vmk_uint32 rateSampleInterval;
  vmk_uint32 pktRateLowWatermark;
  vmk_uint32 pktRateHighWatermark;
  vmk_uint32 rxUsecsLow;
  vmk_uint32 rxFramesLow;
  vmk_uint32 txUsecsLow;
  vmk_uint32 txFramesLow;
  vmk_uint32 rxUsecsHigh;
  vmk_uint32 rxFramesHigh;
  vmk_uint32 txUsecsHigh;
  vmk_uint32 txFramesHigh;
} __attribute__((__packed__)) sfvmk_intrCoalsParam_t;

/*! \brief struct sfvmk_pciInfo_s to
 **        PCI BDF and device information
 **
 ** pciBDF[out]       Buffer to retrieve the PCI BDF string
 **
 ** vendorId[out]     PCI Vendor ID
 **
 ** deviceId[out]     PCI device ID
 **
 ** subVendorId[out]  PCI sub vendor ID
 **
 ** subDeviceId[out]  PCI sub device ID
 **
 */
typedef struct sfvmk_pciInfo_s {
  char  pciBDF[SFVMK_PCI_BDF_LEN];
  vmk_uint16 vendorId;
  vmk_uint16 deviceId;
  vmk_uint16 subVendorId;
  vmk_uint16 subDeviceId;
} __attribute__((__packed__))  sfvmk_pciInfo_t;

/*! \brief struct sfvmk_vpdInfo_s to get
 **        and set VPD info
 **
 ** vpdOp[in]           VPD operation type Get/Set
 **
 ** vpdTag[in]          VPD tag type
 **
 ** vpdKeyword[in]      VPD keyword
 **
 ** vpdLen[in/out]      Length of VPD Data
 **
 ** vpdPayload[in/out]  VPD data buffer
 **
 */
typedef struct sfvmk_vpdInfo_s {
  sfvmk_mgmtDevOps_t  vpdOp;
  vmk_uint8   vpdTag;
  vmk_uint16  vpdKeyword;
  vmk_uint8   vpdLen; /* In or out */
  vmk_uint8   vpdPayload[SFVMK_VPD_MAX_PAYLOAD]; /* In or out */
} __attribute__((__packed__)) sfvmk_vpdInfo_t;

#ifdef VMKERNEL
/**
 * These are the definitions of prototypes as viewed from
 * kernel-facing code.  Kernel callbacks have their prototypes
 * defined.  User callbacks, in this section, will be
 * #define'd as NULL, since their callback pointer has no
 * meaning from the kernel perspective.
 *
 * All callbacks must return an integer, and must take two
 * metadata parameters.  User callbacks take two vmk_uint64
 * paramaters as the first two arguments.  The first argument is
 * the cookie value, and the second is an instance ID from which
 * the callback originated in the kernel.  Kernel callbacks take
 * a vmk_MgmtCookies pointer as the first parameter and a
 * vmk_MgmtEnvelope pointer as the second parameter.  The cookies
 * structure contains handle-wide and session-specific cookie
 * values.  The envelope structure contains a session ID (indicating
 * which session the callback request originated from) and an
 * instance ID (indicating which specific instance, if any, this
 * callback is destined for).  When not addressing specific instances
 * or tracking instance-specific callback invocations, simply use
 * VMK_MGMT_NO_INSTANCE_ID for this parameter.  Regarding session IDs,
 * kernel management handles support simultaneous access by user-space
 * applications, thus the callbacks convey more information about which
 * session the callback invocation is associated with.  The return type merely
 * indicates that the callback ran to completion without
 * error - it does not indicate the semantic success or failure
 * of the operation.  The cookie argument passed to the callback
 * is the same value that was given as the 'cookie' parameter
 * during initialization.  Thus kernel callbacks get a handle cookie
 * provided to vmk_MgmtInit() (in addition to the session-specific cookie
 * that a kernel module may set in its session-announcement function), and
 * provided to vmk_MgmtUserInit().  The instanceId corresponds
 * to the specific instance that this callback is targeted to
 * (if it's a kernel-located callback) or the instance from
 * which the callback originates (if it's a user-located callback).
 *
 * This callback takes two payload parameters: flagFromUser
 * and mgmtParm.  The semantics of the buffers used for
 * flagFromUser and mgmtParm are determined by the individual
 * parameter type, as specified in the vmk_MgmtCallbackInfo
 * corresponding to this callback.  In this case, the parameter
 * type for flagFromUser is VMK_MGMT_PARMTYPE_IN, which means
 * flagFromUser is an input parameter to the callback.  The
 * API automatically copies this parameter from user to kernel
 * space and makes it available to the callback when it is
 * invoked.  Any changes to this buffer will be discarded.
 * The parameter type for mgmtParm is VMK_MGMT_PARMTYPE_INOUT.
 * For that parameter, The API automatically copies the parameter
 * from user space, invokes the callback with the kernel copy
 * of the parameter, and then copies the kernel buffer
 * back to user space if the callback completes successfully.
 *
 * In both cases, the buffers passed for these parameters are
 * temporary.  After the callback has completed execution, those
 * buffers are no longer valid inside the kernel and may be reused
 * by the API.  If you need persistent copies of these buffers,
 * you must allocate memory and make copies during execution of your
 * callback.
 */
int sfvmk_mgmtMcdiCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_mcdiRequest2_t *pMgmtMCDI);

int sfvmk_mgmtNVRAMCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_nvramCmd_t *pNvrCmd);

int sfvmk_mgmtVerInfoCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_versionInfo_t *pVerInfo);

int sfvmk_mgmtLinkStatusUpdate(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_linkStatus_t *pLinkOps);

int sfvmk_mgmtLinkSpeedUpdate(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_linkSpeed_t *pLinkSpeed);

int sfvmk_mgmtIntrModeration(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_intrCoalsParam_t *pIntrMod);

int sfvmk_mgmtPCIInfoCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_pciInfo_t *pPciInfo);

int sfvmk_mgmtVPDInfoCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_vpdInfo_t *pVpdInfo);
#else
/*
 * This section is where callback definitions, as visible to
 * user-space, go.  In this example, there are no user-run callbacks,
 * but we still must define the symbol "testKernelCallback".  Since
 * the actual value of the symbol is not used on the user side,
 * we define all kernel callback names as "NULL" in the user-space
 * section here.
 */
#define sfvmk_mgmtMcdiCallback NULL
#define sfvmk_mgmtNVRAMCallback NULL
#define sfvmk_mgmtVerInfoCallback NULL
#define sfvmk_mgmtLinkStatusUpdate NULL
#define sfvmk_mgmtLinkSpeedUpdate NULL
#define sfvmk_mgmtIntrModeration NULL
#define sfvmk_mgmtPCIInfoCallback NULL
#define sfvmk_mgmtVPDInfoCallback NULL
#endif /* VMKERNEL */

#endif
