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
 ** SFVMK_CB_MC_LOGGING_REQUEST:       For controlling MC Logging dynamically
 ** SFVMK_CB_PCI_INFO_GET:             Get PCI BDF and device information
 ** SFVMK_CB_VPD_REQUEST:              Get or Set VPD information
 ** SFVMK_CB_LINK_STATUS_GET:          Get the link state
 ** SFVMK_CB_LINK_SPEED_REQUEST:       Get/Set the link speed and autoneg
 ** SFVMK_CB_VERINFO_GET:              Get various version info
 ** SFVMK_CB_INTR_MODERATION_REQUEST:  Change Interrupt Moderation settings
 ** SFVMK_CB_NVRAM_REQUEST:            NVRAM operations callback
 ** SFVMK_CB_IMG_UPDATE:               Perform Image Update
 ** SFVMK_CB_HW_QUEUE_STATS_GET:       Get Rx/Tx hardware queue stats
 ** SFVMK_CB_MAC_ADDRESS_GET:          Get MAC address of an interface
 ** SFVMK_CB_IFACE_LIST_GET:           Get list of all SFVMK interface
 ** SFVMK_CB_FEC_MODE_REQUEST:         Get/Set FEC mode settings
 ** SFVMK_CB_IMG_UPDATE_V2:            Perform Image Update version 2
 ** SFVMK_CB_SENSOR_INFO_GET:          Get hardware sensor information
 ** SFVMK_CB_PRIVILEGE_REQUEST:        Get/Set PCI function privileges
 ** SFVMK_CB_NVRAM_REQUEST_V2:         NVRAM operations callback version 2
 **
 */
typedef enum sfvmk_mgmtCbTypes_e {
  SFVMK_CB_MCDI_REQUEST = (VMK_MGMT_RESERVED_CALLBACKS + 1),
  SFVMK_CB_MC_LOGGING_REQUEST,
  SFVMK_CB_PCI_INFO_GET,
  SFVMK_CB_VPD_REQUEST,
  SFVMK_CB_LINK_STATUS_GET,
  SFVMK_CB_LINK_SPEED_REQUEST,
  SFVMK_CB_VERINFO_GET,
  SFVMK_CB_INTR_MODERATION_REQUEST,
  SFVMK_CB_NVRAM_REQUEST,
  SFVMK_CB_IMG_UPDATE,
  SFVMK_CB_HW_QUEUE_STATS_GET,
  SFVMK_CB_MAC_ADDRESS_GET,
  SFVMK_CB_IFACE_LIST_GET,
  SFVMK_CB_FEC_MODE_REQUEST,
  SFVMK_CB_IMG_UPDATE_V2,
  SFVMK_CB_SENSOR_INFO_GET,
  SFVMK_CB_PRIVILEGE_REQUEST,
  SFVMK_CB_NVRAM_REQUEST_V2,
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

/* Max limit of Solarflare NIC */
#define SFVMK_MAX_INTERFACE    16

/* Max width of sensor information in a single line */
#define SFVMK_SENSOR_INFO_MAX_WIDTH 80
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
#define SFVMK_GET_DRV_VERSION     0x00000001
#define SFVMK_GET_FW_VERSION      0x00000002
#define SFVMK_GET_ROM_VERSION     0x00000004
#define SFVMK_GET_UEFI_VERSION    0x00000008
#define SFVMK_GET_SUC_VERSION     0x00000010
#define SFVMK_GET_BUNDLE_VERSION  0x00000020

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

/*! \brief Get size of NVRAM partition
 **
 **  [in]
 **
 **         op:   SFVMK_NVRAM_OP_SIZE
 **
 **         type: NVRAM partition type
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
 **         op:     SFVMK_NVRAM_OP_READ
 **
 **         type:   NVRAM partition type
 **
 **         size:   Size of data requested
 **
 **         offset: Offset to start reading NVRAM data
 **
 **  [out]
 **
 **         size: Size of data actually read
 **
 **         data: NVRAM data after read from H/W
 **
 */
#define	SFVMK_NVRAM_OP_READ		0x00000002

/*! \brief Write data into NVRAM partition
 **
 **  [in]
 **
 **         op:        SFVMK_NVRAM_OP_WRITEALL
 **
 **         type:      NVRAM partition type
 **
 **         size:      Size of data
 **
 **         data:      NVRAM data to write into H/W
 **
 **         erasePart: True if erase a partition, false otherwise
 **
 **  [out]
 **
 **         size: Actual data written into NVRAM
 **
 */
#define	SFVMK_NVRAM_OP_WRITEALL		0x00000003

/*! \brief Erase NVRAM partition
 **
 **  [in]
 **
 **         op:   SFVMK_NVRAM_OP_ERASE
 **
 **         type: NVRAM partition type
 **
 */
#define	SFVMK_NVRAM_OP_ERASE		0x00000004

/*! \brief Get NVRAM partition version
 **
 **  [in]
 **
 **         op:   SFVMK_NVRAM_OP_GET
 **
 **         type: NVRAM partition type
 **
 **  [out]
 **
 **         version: Version of partition type requested
 **
 **         subtype: Subtype version (Optional)
 **
 */
#define	SFVMK_NVRAM_OP_GET_VER		0x00000005

/*! \brief Set NVRAM partition version
 **
 **  [in]
 **
 **         op:   SFVMK_NVRAM_OP_SET
 **
 **         type: NVRAM partition type
 **
 **         version: Version of partition type
 **                  to update
 **
 */
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
  SFVMK_NVRAM_MUM,
  SFVMK_NVRAM_BUNDLE,
  SFVMK_NVRAM_BUNDLE_METADATA,
  SFVMK_NVRAM_INVALID,
  SFVMK_NVRAM_NTYPE
} sfvmk_nvramType_t;

/*! \brief struct sfvmk_nvramCmd_s for performing
 **        NVRAM operations
 **
 ** op[in]          Operation type (Size/Read/write/flash/ver_get/ver_set)
 **
 ** type[in]        Type of NVRAM
 **
 ** offset[in]      Location of NVRAM where to start read
 **
 ** size[in,out]    Size of buffer
 **
 ** subtype[out]    NVRAM subtype, part of get NVRAM version
 **
 ** version[in,out] Version info
 **
 ** data[in,out]    NVRAM data for read/write
 **
 ** erasePart[in]   Flag to tell if erase a partition or not
 **
 */
typedef	struct sfvmk_nvramCmd_s {
  vmk_uint32        op;
  sfvmk_nvramType_t type;
  vmk_uint32        offset;
  vmk_uint32        size;
  vmk_uint32        subtype;
  vmk_uint16        version[4];
  vmk_uint64        data;
  vmk_Bool          erasePart;
} __attribute__((__packed__)) sfvmk_nvramCmd_t;

/* This is an OUT flag, tells whether a partition type
 * is read-only or not. This flag is set for all NV operations */
#define SFVMK_NVRAM_PART_READONLY_FLAG  (1 << 0)

/* This flag is an IN flag for SFVMK_NVRAM_OP_WRITEALL operation.
 * If set, instructs the driver to erase a partition before
 * writing contents */
#define SFVMK_NVRAM_PART_ERASE_FLAG     (1 << 1)

/*! \brief struct sfvmk_nvramCmdV2_s for performing NVRAM operations.
 **         Added Version 2 to maintain compatibility with applications
 **         which use sfvmk_nvramCmd_t. The Version 2 has extra flags
 **         and reserved field for future use.
 **
 ** op[in]          Operation type, same as in sfvmk_nvramCmd_t
 **
 ** type[in]        Type of NVRAM, should be one of sfvmk_nvramType_t enum.
 **
 ** offset[in]      Location of NVRAM where to start read
 **
 ** size[in,out]    Size of buffer
 **
 ** subtype[out]    NVRAM subtype, part of get NVRAM version
 **
 ** version[in,out] Version info
 **
 ** data[in,out]    NVRAM data for read/write
 **
 ** flags[in,out]   This is used in both IN or out direction and
 **                 tells extra information about a partition type.
 **
 ** reserved        Reserved for future use
 **
 */
typedef	struct sfvmk_nvramCmdV2_s {
  vmk_uint32        op;
  sfvmk_nvramType_t type;
  vmk_uint32        offset;
  vmk_uint32        size;
  vmk_uint32        subtype;
  vmk_uint16        version[4];
  vmk_uint64        data;
  vmk_Bool          flags;
  vmk_uint64        reserved;
} __attribute__((__packed__)) sfvmk_nvramCmdV2_t;

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

/*! \brief struct sfvmk_imgUpdate_s to update
 **       firmware image
 **
 ** pFileBuffer[in]    Pointer to Buffer Containing
 **                    Update File's contents
 **
 ** size[in]           Size of the update file
 **
 ** type[in]           Type of NVRAM
 **
 */
typedef struct sfvmk_imgUpdateV2_s{
  vmk_uint64          pFileBuffer;
  vmk_uint32          size;
  sfvmk_nvramType_t   type;
} __attribute__((__packed__)) sfvmk_imgUpdateV2_t;

/*! \brief Sub command type for HW queue stats
 **
 **  SFVMK_MGMT_STATS_GET_SIZE: Sub command to request size of the HW queue stats
 **
 **  SFVMK_MGMT_STATS_GET_SIZE: Sub command to request HW queue stats
 **
 */
typedef enum sfvmk_mgmtStatsOps_e {
  SFVMK_MGMT_STATS_GET_SIZE = 1,
  SFVMK_MGMT_STATS_GET,
  SFVMK_MGMT_STATS_INVALID
} sfvmk_mgmtStatsOps_t;

/*! \brief struct sfvmk_hwQueueStats_s to get
 **        hardware queues stats
 **
 ** statsBuffer[out]   Pointer to Buffer Containing
 **                    stats
 **
 ** size[in,out]       size of the buffer allocated
 **                    from user
 **
 */
typedef struct sfvmk_hwQueueStats_s{
  sfvmk_mgmtStatsOps_t subCmd;
  vmk_uint64           statsBuffer;
  vmk_uint32           size;
} __attribute__((__packed__)) sfvmk_hwQueueStats_t;

/*! \brief struct sfvmk_macAddress_s to get
 **        MAC address of a port
 **
 ** macAddress[out]  MAC address
 **
 */
typedef struct sfvmk_macAddress_s {
  vmk_uint8            macAddress[6];
} __attribute__((__packed__)) sfvmk_macAddress_t;

/*! \brief struct sfvmk_hwQueueStats_s to get
 **        list of Solarflare NIC
 **
 ** ifaceCount[out]  Total count of interface name
 **                  copied in ifaceArray
 **
 ** ifaceArray[out]  List of interface name
 **
 */
typedef struct sfvmk_ifaceList_s {
  vmk_uint32  ifaceCount;
  vmk_Name    ifaceArray[SFVMK_MAX_INTERFACE];
} __attribute__((__packed__)) sfvmk_ifaceList_t;

/*! \brief struct sfvmk_fecMode_s to get/ set
 ** the FEC mode settings
 **
 ** type[in]        Command type (Get/Set)
 **
 ** activeFec[out]  Applied settings of current
 **                 FEC mode
 **
 ** fec[in]         Bitmap of the new FEC mode
 **                 settings to be applied
 **
 ** reserved        Reserved for future use
 **
 */
typedef struct sfvmk_fecMode_s {
  sfvmk_mgmtDevOps_t type;
  vmk_uint32         activeFec;
  vmk_uint32         fec;
  vmk_uint32         reserved;
} __attribute__((__packed__)) sfvmk_fecMode_t;

/*! \brief Bit number for different FEC mode settings,
 **        these bits are used to form bit map for filling
 **        the fec field
 **
 ** SFVMK_MGMT_FEC_NONE_BIT:  FEC mode configuration is not supported
 **
 ** SFVMK_MGMT_FEC_AUTO_BIT:  Default FEC mode provided by driver
 **
 ** SFVMK_MGMT_FEC_OFF_BIT:   No FEC Mode
 **
 ** SFVMK_MGMT_FEC_RS_BIT:    Reed-Solomon Forward Error Detection mode
 **
 ** SFVMK_MGMT_FEC_BASER_BIT: Base-R/Reed-Solomon Forward Error Detection mode
 **
 */
typedef enum sfvmk_mgmtFecConfigBits_e {
  SFVMK_MGMT_FEC_NONE_BIT,
  SFVMK_MGMT_FEC_AUTO_BIT,
  SFVMK_MGMT_FEC_OFF_BIT,
  SFVMK_MGMT_FEC_RS_BIT,
  SFVMK_MGMT_FEC_BASER_BIT
} sfvmk_mgmtFecConfigBits_t;

/* Define bitmask for FEC mode settings */
#define SFVMK_MGMT_FEC_NONE_MASK  (1 << SFVMK_MGMT_FEC_NONE_BIT)
#define SFVMK_MGMT_FEC_AUTO_MASK  (1 << SFVMK_MGMT_FEC_AUTO_BIT)
#define SFVMK_MGMT_FEC_OFF_MASK   (1 << SFVMK_MGMT_FEC_OFF_BIT)
#define SFVMK_MGMT_FEC_RS_MASK    (1 << SFVMK_MGMT_FEC_RS_BIT)
#define SFVMK_MGMT_FEC_BASER_MASK (1 << SFVMK_MGMT_FEC_BASER_BIT)

/*! \brief Sub command type for HW sensor info
 **
 **  SFVMK_MGMT_STATS_GET_SIZE: Sub command to request size of the HW sensor info
 **
 **  SFVMK_MGMT_STATS_GET_SIZE: Sub command to request HW sensor info
 **
 */
typedef enum sfvmk_mgmtSensorOps_e {
  SFVMK_MGMT_SENSOR_GET_SIZE = 1,
  SFVMK_MGMT_SENSOR_GET,
  SFVMK_MGMT_SENSOR_INVALID
} sfvmk_mgmtSensorOps_t;

/*! \brief struct sfvmk_hwSensor_s to get
 **        hardware sensors information
 **
 ** sensorBuffer[out]  Pointer to Buffer Containing
 **                    sensors info
 **
 ** size[in,out]       size of the buffer allocated
 **                    from user
 **
 */
typedef struct sfvmk_hwSensor_s{
  sfvmk_mgmtSensorOps_t subCmd;
  vmk_uint64            sensorBuffer;
  vmk_uint32            size;
} __attribute__((__packed__)) sfvmk_hwSensor_t;

/*! \brief struct sfvmk_privilege_s to get/ set
 ** function privilege information
 **
 ** type[in]            Command type (Get/Set)
 **
 ** pciSBDF[in]         Input PCI address string
 **
 ** privMask[inout]     OUT in Get operation and
 **                     IN (add privileges) for Set operation
 **
 ** privRemoveMask[in]  privileges to be revoked in Set operation
 **
 */
typedef struct sfvmk_privilege_s {
  sfvmk_mgmtDevOps_t   type;
  vmk_Name             pciSBDF;
  vmk_uint32           privMask;
  vmk_uint32           privRemoveMask;
} __attribute__((__packed__))  sfvmk_privilege_t;

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

VMK_ReturnStatus sfvmk_mgmtLinkStatusCallback(vmk_MgmtCookies *pCookies,
                                              vmk_MgmtEnvelope *pEnvelope,
                                              sfvmk_mgmtDevInfo_t *pDevIface,
                                              vmk_Bool *pLinkState);

VMK_ReturnStatus sfvmk_mgmtLinkSpeedCallback(vmk_MgmtCookies *pCookies,
                                             vmk_MgmtEnvelope *pEnvelope,
                                             sfvmk_mgmtDevInfo_t *pDevIface,
                                             sfvmk_linkSpeed_t *pLinkSpeed);

VMK_ReturnStatus sfvmk_mgmtVerInfoCallback(vmk_MgmtCookies *pCookies,
                                           vmk_MgmtEnvelope *pEnvelope,
                                           sfvmk_mgmtDevInfo_t *pDevIface,
                                           sfvmk_versionInfo_t *pVerInfo);

VMK_ReturnStatus sfvmk_mgmtIntrModerationCallback(vmk_MgmtCookies *pCookies,
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

VMK_ReturnStatus sfvmk_mgmtImgUpdateV2Callback(vmk_MgmtCookies *pCookies,
                                               vmk_MgmtEnvelope *pEnvelope,
                                               sfvmk_mgmtDevInfo_t *pDevIface,
                                               sfvmk_imgUpdateV2_t *pImgUpdateV2);

VMK_ReturnStatus sfvmk_mgmtHWQStatsCallback(vmk_MgmtCookies *pCookies,
                                            vmk_MgmtEnvelope *pEnvelope,
                                            sfvmk_mgmtDevInfo_t *pDevIface,
                                            sfvmk_hwQueueStats_t *pUserStatsBuffer);

VMK_ReturnStatus sfvmk_mgmtMACAddressCallback(vmk_MgmtCookies *pCookies,
                                              vmk_MgmtEnvelope *pEnvelope,
                                              sfvmk_mgmtDevInfo_t *pDevIface,
                                              sfvmk_macAddress_t *pMacBuffer);

VMK_ReturnStatus sfvmk_mgmtInterfaceListCallback(vmk_MgmtCookies *pCookies,
                                                 vmk_MgmtEnvelope *pEnvelope,
                                                 sfvmk_mgmtDevInfo_t *pDevIface,
                                                 sfvmk_ifaceList_t *pIfaceList);

VMK_ReturnStatus sfvmk_mgmtFecModeCallback(vmk_MgmtCookies *pCookies,
                                           vmk_MgmtEnvelope *pEnvelope,
                                           sfvmk_mgmtDevInfo_t *pDevIface,
                                           sfvmk_fecMode_t *pFecMode);

VMK_ReturnStatus sfvmk_mgmtHWSensorInfoCallback(vmk_MgmtCookies *pCookies,
                                                vmk_MgmtEnvelope *pEnvelope,
                                                sfvmk_mgmtDevInfo_t *pDevIface,
                                                sfvmk_hwSensor_t *pUserSensorBuffer);

VMK_ReturnStatus sfvmk_mgmtFnPrivilegeCallback(vmk_MgmtCookies     *pCookies,
                                               vmk_MgmtEnvelope    *pEnvelope,
                                               sfvmk_mgmtDevInfo_t *pDevIface,
                                               sfvmk_privilege_t   *pPrivilegeInfo);

VMK_ReturnStatus sfvmk_mgmtNVRAMV2Callback(vmk_MgmtCookies *pCookies,
                                           vmk_MgmtEnvelope *pEnvelope,
                                           sfvmk_mgmtDevInfo_t *pDevIface,
                                           sfvmk_nvramCmdV2_t *pNvrCmdV2);
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
#define sfvmk_mgmtLinkStatusCallback NULL
#define sfvmk_mgmtLinkSpeedCallback NULL
#define sfvmk_mgmtVerInfoCallback NULL
#define sfvmk_mgmtIntrModerationCallback NULL
#define sfvmk_mgmtNVRAMCallback NULL
#define sfvmk_mgmtImgUpdateCallback NULL
#define sfvmk_mgmtImgUpdateV2Callback NULL
#define sfvmk_mgmtHWQStatsCallback NULL
#define sfvmk_mgmtMACAddressCallback NULL
#define sfvmk_mgmtInterfaceListCallback NULL
#define sfvmk_mgmtFecModeCallback NULL
#define sfvmk_mgmtHWSensorInfoCallback NULL
#define sfvmk_mgmtFnPrivilegeCallback NULL
#define sfvmk_mgmtNVRAMV2Callback NULL
#endif

#endif
