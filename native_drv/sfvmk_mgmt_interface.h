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
 ** SFVMK_CB_MCDI_REQUEST:     For invoking MCDI callback
 ** SFVMK_CB_MC_LOGGING:       For controlling MC Logging dynamically
 ** SFVMK_CB_PCI_INFO_GET:     Get PCI BDF and device information
 ** SFVMK_CB_VPD_REQUEST:      Get or Set VPD information
 ** SFVMK_CB_LINK_STATUS_GET:  Get the link state
 **
 */
typedef enum sfvmk_mgmtCbTypes_e {
  SFVMK_CB_MCDI_REQUEST = (VMK_MGMT_RESERVED_CALLBACKS + 1),
  SFVMK_CB_MC_LOGGING,
  SFVMK_CB_PCI_INFO_GET,
  SFVMK_CB_VPD_REQUEST,
  SFVMK_CB_LINK_STATUS_GET,
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
 **  state[in/out]    MC Log state (True/False)
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
 ** vpdLen[in/out]      Length of VPD Data
 **
 ** vpdPayload[in/out]  VPD data buffer
 **
 */
typedef struct sfvmk_vpdInfo_s {
  sfvmk_mgmtDevOps_t  vpdOp;
  vmk_uint8           vpdTag;
  vmk_uint16          vpdKeyword;
  vmk_uint8           vpdLen;
  vmk_uint8           vpdPayload[SFVMK_VPD_MAX_PAYLOAD];
} __attribute__((__packed__)) sfvmk_vpdInfo_t;

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
#endif

#endif
