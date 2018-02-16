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

#ifndef __SFVMK_MGMTINTERFACE_H__
#define __SFVMK_MGMTINTERFACE_H__

#include <vmkapi.h>

/*! \brief Management Callback number
 **
 ** Each callback must have a unique 64-bit integer identifier.
 ** The identifiers 0 through VMK_MGMT_RESERVED_CALLBACKS
 ** are reserved and must not be used by consumers of the
 ** management APIs.  Here, we declare all callback
 ** identifiers starting at (VMK_MGMT_RESERVED_CALLBACKS + 1).
 ** These identifiers are used by consumers of
 ** the API at runtime to invoke the associated callback.
 **
 ** Note: These identifiers cannot be changed or renumbered,
 ** so new callback IDs must go at the end (before SFVMK_CB_MAX).
 **
 ** SFVMK_CB_MCDI_REQUEST:             For invoking MCDI callback
 ** SFVMK_CB_MC_LOGGING:               For controlling MC Logging dynamically
 ** SFVMK_CB_PCI_INFO_GET:             Get PCI BDF and device information
 ** SFVMK_CB_VPD_REQUEST:              Get or Set VPD information
 ** SFVMK_CB_LINK_STATUS_GET:          Get the link state
 ** SFVMK_CB_LINK_SPEED_REQUEST:       Get/Set the link speed and autoneg
 ** SFVMK_CB_VERINFO_GET:              Get various version info
 ** SFVMK_CB_INTR_MODERATION_REQUEST:  Change Interrupt Moderation settings
 ** SFVMK_CB_NVRAM_REQUEST:            NVRAM operations callback
 ** SFVMK_CB_IMG_UPDATE:               Perform Image Update
 **
 */
typedef enum sfvmk_mgmtCbTypes_e {
  SFVMK_CB_MCDI_REQUEST = (VMK_MGMT_RESERVED_CALLBACKS + 1),
  SFVMK_CB_MC_LOGGING,
  SFVMK_CB_PCI_INFO_GET,
  SFVMK_CB_VPD_REQUEST,
  SFVMK_CB_LINK_STATUS_GET,
  SFVMK_CB_LINK_SPEED_REQUEST,
  SFVMK_CB_VERINFO_GET,
  SFVMK_CB_INTR_MODERATION_REQUEST,
  SFVMK_CB_NVRAM_REQUEST,
  SFVMK_CB_IMG_UPDATE,
  SFVMK_CB_MAX
} sfvmk_mgmtCbTypes_t;

/*
 * The name used to describe management interface ABI between kernel
 * and user.
 */
#define SFVMK_INTERFACE_NAME   "sfvmkMgmtToKernel"

/* Length of uplink device name string */
#define SFVMK_DEV_NAME_LEN     10

/* Length of PCI BDF string*/
#define SFVMK_PCI_BDF_LEN      16

/* Length of VPD data */
#define SFVMK_VPD_MAX_PAYLOAD  0x100

/*
 * The vendor name for interface. The vendor name cannot be changed for
 * any internal or customers build.
 */
#define SFVMK_INTERFACE_VENDOR    "Solarflare Communications Inc"

/*! \brief Management dev interface structure
 **
 ** status[out] Error status returned
 **
 ** deviceName[in] Name of the vmk net device
 **
 */
typedef struct sfvmk_mgmtDevInfo_s {
  vmk_uint32  status;
  char  deviceName[SFVMK_DEV_NAME_LEN];
} __attribute__((__packed__)) sfvmk_mgmtDevInfo_t;

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

/*
 * The maximum payload length is 0x400 (MCDI_CTL_SDU_LEN_MAX_V2) - uint32
 * = 255 x 32 bit as MCDI_CTL_SDU_LEN_MAX_V2 doesn't take account of
 * the space required by the V1 header, which still exists in a V2 command.
 */
#define SFVMK_MCDI_MAX_PAYLOAD_ARRAY 255

/*! \brief struct efx_mcdi_request_s - Parameters for EFX_MCDI_REQUEST2 sub-command
 **
 ** cmd[in] MCDI command type number.
 **
 ** inlen[in] The length of command parameters, in bytes.
 **
 ** outlen[in,out] On entry, the length available for the response, in bytes.
 **	On return, the length used for the response, in bytes.
 **
 ** flags[out] Flags for the command or response.  The only flag defined
 **	at present is EFX_MCDI_REQUEST_ERROR.  If this is set on return,
 **	the MC reported an error.
 **
 ** host_errno[out] On return, if EFX_MCDI_REQUEST_ERROR is included in flags,
 **	the suggested VMK error code for the error.
 **
 ** payload[in,out] On entry, the MCDI command parameters.  On return, the response.
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
typedef struct sfvmk_mcdiRequest_s {
  vmk_uint16  cmd;
  vmk_uint16  inlen;
  vmk_uint16  outlen;
  vmk_uint16  flags;
  vmk_uint32  host_errno;
  vmk_uint32  payload[SFVMK_MCDI_MAX_PAYLOAD_ARRAY];
} __attribute__((__packed__)) sfvmk_mcdiRequest_t;

/*! \brief struct sfvmk_mcdiLogging_s for controlling
 **        MC Logging dynamically
 **
 **  mcLoggingOp[in]  Command types to Get/Set MC Log state
 **  state[in,out]    MC Log state (True/False)
 */
typedef struct sfvmk_mcdiLogging_s {
  sfvmk_mgmtDevOps_t  mcLoggingOp;
  vmk_Bool            state;
} __attribute__((__packed__)) sfvmk_mcdiLogging_t;

/*! \brief struct sfvmk_pciInfo_s for
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
  vmk_Name   pciBDF;
  vmk_uint16 vendorId;
  vmk_uint16 deviceId;
  vmk_uint16 subVendorId;
  vmk_uint16 subDeviceId;
} __attribute__((__packed__))  sfvmk_pciInfo_t;

/*! \brief struct sfvmk_vpdInfo_s to get
 **        and set VPD info
 **
 ** vpdOp[in]           VPD operation - Get/Set
 **
 ** vpdTag[in]          VPD tag
 **
 ** vpdKeyword[in]      VPD keyword
 **
 ** vpdLen[in,out]      Length of VPD Data
 **
 ** vpdPayload[in,out]  VPD data buffer
 **
 */
typedef struct sfvmk_vpdInfo_s {
  sfvmk_mgmtDevOps_t  vpdOp;
  vmk_uint8           vpdTag;
  vmk_uint16          vpdKeyword;
  vmk_uint8           vpdLen;
  vmk_uint8           vpdPayload[SFVMK_VPD_MAX_PAYLOAD];
} __attribute__((__packed__)) sfvmk_vpdInfo_t;

/* Define various link speed numbers supported by SFVMK driver */
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
 **  speed[in,out]   Current link speed
 **
 **  autoNeg[in,out] Get/Set autoNeg
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

/* Types of different versions */
#define SFVMK_GET_DRV_VERSION  0x00000001
#define SFVMK_GET_FW_VERSION   0x00000002
#define SFVMK_GET_ROM_VERSION  0x00000004
#define SFVMK_GET_UEFI_VERSION 0x00000008

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
  vmk_Name   version;
} __attribute__((__packed__)) sfvmk_versionInfo_t;

/*! \brief struct sfvmk_intrCoalsParam_s for
 **         Interrupt coalesce parameters
 **
 ** Note: At present only txUsecs/rxUsecs is being used.
 ** The firmware doesn't support moderation settings for
 ** different (rx/tx) event types. Only txUsecs would be
 ** considered.
 **
 ** type[in]  Command type (Get/Set)
 **
 ** rxUsecs[in,out] number of microseconds to wait
 **          for Rx, before interrupting
 **
 ** rxMaxFrames[in,out] maximum number of (Rx) frames
 **              to wait for, before interrupting
 **
 ** txUsecs[in,out] number of microseconds to wait
 **          for completed Tx, before interrupting
 **
 ** txMaxFrames[in,out] maximum number of completed (Tx)
 **              frames to wait for, before interrupting
 **
 ** useAdaptiveRx[in,out] Use adaptive Rx coalescing
 **
 ** useAdaptiveTx[in,out] Use adaptive Tx coalescing
 **
 ** rateSampleInterval[in,out] Rate sampling interval
 **
 ** pktRateLowWatermark[in,out] Low packet rate watermark
 **
 ** pktRateHighWatermark[in,out] High packet rate watermark
 **
 ** rxUsecsLow[in,out] Rx usecs low
 **
 ** rxFramesLow[in,out] Rx frames low
 **
 ** txUsecsLow[in,out] Tx usecs low
 **
 ** txFramesLow[in,out] Tx frames low
 **
 ** rxUsecsHigh[in,out] Rx usecs high
 **
 ** rxFramesHigh[in,out] Rx frames high
 **
 ** txUsecsHigh[in,out] Tx usecs high
 **
 ** txFramesHigh[in,out] Tx frames high
 **
 */
typedef struct sfvmk_intrCoalsParam_s {
  sfvmk_mgmtDevOps_t type;
  vmk_uint32         rxUsecs;
  vmk_uint32         rxMaxFrames;
  vmk_uint32         txUsecs;
  vmk_uint32         txMaxFrames;
  vmk_Bool           useAdaptiveRx;
  vmk_Bool           useAdaptiveTx;
  vmk_uint32         rateSampleInterval;
  vmk_uint32         pktRateLowWatermark;
  vmk_uint32         pktRateHighWatermark;
  vmk_uint32         rxUsecsLow;
  vmk_uint32         rxFramesLow;
  vmk_uint32         txUsecsLow;
  vmk_uint32         txFramesLow;
  vmk_uint32         rxUsecsHigh;
  vmk_uint32         rxFramesHigh;
  vmk_uint32         txUsecsHigh;
  vmk_uint32         txFramesHigh;
} __attribute__((__packed__)) sfvmk_intrCoalsParam_t;

/* NVRAM Command max payload size*/
#define SFVMK_NVRAM_MAX_PAYLOAD         32*1024

/* Get size of NVRAM partition */
#define	SFVMK_NVRAM_OP_SIZE		0x00000001

/* Read data from NVRAM partition */
#define	SFVMK_NVRAM_OP_READ		0x00000002

/* Write data into NVRAM partition */
#define	SFVMK_NVRAM_OP_WRITE		0x00000003

/* Erase NVRAM partition */
#define	SFVMK_NVRAM_OP_ERASE		0x00000004

/* Get NVRAM partition version */
#define	SFVMK_NVRAM_OP_GET_VER		0x00000005

/* Set NVRAM partition version */
#define	SFVMK_NVRAM_OP_SET_VER		0x00000006

/** NVRAM Partition Types */
typedef enum sfvmk_nvramType_e {
  SFVMK_NVRAM_BOOTROM,
  SFVMK_NVRAM_BOOTROM_CFG,
  SFVMK_NVRAM_MC,
  SFVMK_NVRAM_MC_GOLDEN,
  SFVMK_NVRAM_PHY,
  SFVMK_NVRAM_NULL_PHY,
  SFVMK_NVRAM_FPGA,
  SFVMK_NVRAM_FCFW,
  SFVMK_NVRAM_CPLD,
  SFVMK_NVRAM_FPGA_BACKUP,
  SFVMK_NVRAM_UEFIROM,
  SFVMK_NVRAM_DYNAMIC_CFG,
  SFVMK_NVRAM_TYPE_UNKNOWN
} sfvmk_nvramType_t;

/*! \brief struct sfvmk_nvramCmd_s for performing
 **        NVRAM operations
 **
 ** op[in]          Operation type (Size/Read/write/flash/ver_get/ver_set)
 **
 ** type[in]        Type of NVRAM
 **
 ** offset[in]      Location of NVRAM where to start
 **                 read/write
 **
 ** size[in,out]    Size of buffer, should be <= SFVMK_NVRAM_MAX_PAYLOAD
 **
 ** subtype[out]    NVRAM subtype, part of get NVRAM version
 **
 ** version[in,out] Version info
 **
 ** data[in,out]    NVRAM data for read/write
 **
 */
typedef	struct sfvmk_nvramCmd_s {
  vmk_uint32        op;
  sfvmk_nvramType_t type;
  vmk_uint32        offset;
  vmk_uint32        size;
  vmk_uint32        subtype;
  vmk_uint16        version[4];
  vmk_uint8         data[SFVMK_NVRAM_MAX_PAYLOAD];
} __attribute__((__packed__)) sfvmk_nvramCmd_t;

/*! \brief struct sfvmk_imgUpdate_s to update
 **       firmware image
 **
 ** pFileBuffer[in]    Pointer to Buffer Containing
 **                    Update File's contents
 **
 ** size[in]           size of the update file
 **
 */
typedef struct sfvmk_imgUpdate_s{
  vmk_uint64          pFileBuffer;
  vmk_uint32          size;
} __attribute__((__packed__)) sfvmk_imgUpdate_t;


#ifdef VMKERNEL
/*!
 ** These are the definitions of prototypes as viewed from kernel-facing code.
 ** Kernel callbacks have their prototypes defined.
 **
 ** All callbacks must return an integer, and must take two metadata parameters.
 ** Kernel callbacks take a vmk_MgmtCookies pointer as the first parameter and a
 ** vmk_MgmtEnvelope pointer as the second parameter.
 **
 ** The cookies structure contains handle-wide and session-specific cookie
 ** values. The cookie argument passed to the callback is the same value that
 ** was given as the 'cookie' parameter during initialization.  Thus kernel
 ** callbacks get a handle cookie provided to vmk_MgmtInit().
 **
 ** The envelope structure contains a session ID (indicating which session the
 ** callback request originated from) and an instance ID (indicating which
 ** specific instance, this callback is destined for). When not addressing
 ** specific instances or tracking instance-specific callback invocations, just
 ** use MGMT_NO_INSTANCE_ID for this parameter.
 **
 ** Other parameters are user defined and are defined in the
 ** structure sfvmk_mgmtCallbacks
 **
 ** The return type merely indicates that the callback ran to completion without
 ** error, it does not indicate the semantic success or failure of the operation
 **
 */
VMK_ReturnStatus sfvmk_mgmtMcdiCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_mcdiRequest_t *pMgmtMCDI);

VMK_ReturnStatus sfvmk_mgmtMCLoggingCallback(vmk_MgmtCookies *pCookies,
                                             vmk_MgmtEnvelope *pEnvelope,
                                             sfvmk_mgmtDevInfo_t *pDevIface,
                                             sfvmk_mcdiLogging_t *pMcdiLog);

VMK_ReturnStatus sfvmk_mgmtPCIInfoCallback(vmk_MgmtCookies *pCookies,
                                           vmk_MgmtEnvelope *pEnvelope,
                                           sfvmk_mgmtDevInfo_t *pDevIface,
                                           sfvmk_pciInfo_t *pPciInfo);

VMK_ReturnStatus sfvmk_mgmtVPDInfoCallback(vmk_MgmtCookies *pCookies,
                                           vmk_MgmtEnvelope *pEnvelope,
                                           sfvmk_mgmtDevInfo_t *pDevIface,
                                           sfvmk_vpdInfo_t *pVpdInfo);

VMK_ReturnStatus sfvmk_mgmtLinkStatusGet(vmk_MgmtCookies *pCookies,
                                         vmk_MgmtEnvelope *pEnvelope,
                                         sfvmk_mgmtDevInfo_t *pDevIface,
                                         vmk_Bool *pLinkState);

VMK_ReturnStatus sfvmk_mgmtLinkSpeedRequest(vmk_MgmtCookies *pCookies,
                                            vmk_MgmtEnvelope *pEnvelope,
                                            sfvmk_mgmtDevInfo_t *pDevIface,
                                            sfvmk_linkSpeed_t *pLinkSpeed);

VMK_ReturnStatus sfvmk_mgmtVerInfoCallback(vmk_MgmtCookies *pCookies,
                                           vmk_MgmtEnvelope *pEnvelope,
                                           sfvmk_mgmtDevInfo_t *pDevIface,
                                           sfvmk_versionInfo_t *pVerInfo);

VMK_ReturnStatus sfvmk_mgmtIntrModeration(vmk_MgmtCookies *pCookies,
                                          vmk_MgmtEnvelope *pEnvelope,
                                          sfvmk_mgmtDevInfo_t *pDevIface,
                                          sfvmk_intrCoalsParam_t *pIntrMod);

VMK_ReturnStatus sfvmk_mgmtNVRAMCallback(vmk_MgmtCookies *pCookies,
                                         vmk_MgmtEnvelope *pEnvelope,
                                         sfvmk_mgmtDevInfo_t *pDevIface,
                                         sfvmk_nvramCmd_t *pNvrCmd);

VMK_ReturnStatus sfvmk_mgmtImgUpdateCallback(vmk_MgmtCookies *pCookies,
                                             vmk_MgmtEnvelope *pEnvelope,
                                             sfvmk_mgmtDevInfo_t *pDevIface,
                                             sfvmk_imgUpdate_t *pImgUpdate);

#else /* VMKERNEL */
/*!
 ** This section is where callback definitions, as visible to user-space, go.
 ** All kernel callbacks defined as "NULL" here.
 **
 */
#define sfvmk_mgmtMcdiCallback NULL
#define sfvmk_mgmtMCLoggingCallback NULL
#define sfvmk_mgmtPCIInfoCallback NULL
#define sfvmk_mgmtVPDInfoCallback NULL
#define sfvmk_mgmtLinkStatusGet NULL
#define sfvmk_mgmtLinkSpeedRequest NULL
#define sfvmk_mgmtVerInfoCallback NULL
#define sfvmk_mgmtIntrModeration NULL
#define sfvmk_mgmtNVRAMCallback NULL
#define sfvmk_mgmtImgUpdateCallback NULL
#endif

#endif
