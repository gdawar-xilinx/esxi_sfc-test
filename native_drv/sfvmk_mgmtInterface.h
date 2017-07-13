/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#ifndef __SFVMK_MGMTINTERFACE_H__
#define __SFVMK_MGMTINTERFACE_H__

#include <vmkapi.h> /* Required for VMKAPI type definitions */

/*
 * Each callback must have a unique 64-bit integer identifier.
 * The identifiers 0 through VMK_MGMT_RESERVED_CALLBACKS
 * are reserved and must not be used by consumers of the
 * management APIs.  Here, we declare just one callback
 * identifier.  These identifiers are used by consumers of
 * the API at runtime to invoke the associated callback.
 *
 * Technically this identifier could start at
 * (VMK_MGMT_RESERVED_CALLBACKS + 1), but for clarity and
 * consistency with other examples, we'll choose a new
 * value here.
 */
enum SFVMK_MGMT_CB_TYPES {
  SFVMK_CB_MCDI_REQUEST_2 = (VMK_MGMT_RESERVED_CALLBACKS + 1),
  SFVMK_CB_NVRAM_REQUEST,
  SFVMK_CB_VERINFO_GET,
 // SFVMK_CB_VPD_REQUEST,
  SFVMK_CB_MAX
};

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

typedef struct sfvmk_mgmtDevInfo_s {
  vmk_uint8  deviceName[SFVMK_DEV_NAME_LEN];
  vmk_uint32  op;
  vmk_uint32  length;
  vmk_uint32  status;
} __attribute__((__packed__)) sfvmk_mgmtDevInfo_t;

/**
 * struct efx_mcdi_request2_s - Parameters for %EFX_MCDI_REQUEST2 sub-command
 * @cmd: MCDI command type number.
 * @inlen: The length of command parameters, in bytes.
 * @outlen: On entry, the length available for the response, in bytes.
 *	On return, the length used for the response, in bytes.
 * @flags: Flags for the command or response.  The only flag defined
 *	at present is %EFX_MCDI_REQUEST_ERROR.  If this is set on return,
 *	the MC reported an error.
 * @host_errno: On return, if %EFX_MCDI_REQUEST_ERROR is included in @flags,
 *	the suggested Linux error code for the error.
 * @payload: On entry, the MCDI command parameters.  On return, the response.
 *
 * If the driver detects invalid parameters or a communication failure
 * with the MC, the MGMT calback interface will return -1, errno will be set
 * accordingly, and none of the fields will be valid.  If the MC reports
 * an error, the MGMT calback interface call will return 0 but @flags will
 * include the %EFX_MCDI_REQUEST_ERROR flag.  The MC error code can then
 * be found in @payload (if @outlen was sufficiently large) and a suggested
 * VMK error code can be found in @host_errno.
 *
 * %EFX_MCDI_REQUEST2 fully supports both MCDIv1 and v2.
 */
typedef struct sfvmk_mcdiRequest2_s {
  vmk_uint16 cmd;
  vmk_uint16 inlen;
  vmk_uint16 outlen;
  vmk_uint16 flags;
  vmk_uint32 host_errno;
  /*
   * The maximum payload length is 0x400 (MCDI_CTL_SDU_LEN_MAX_V2) - 4 bytes
   * = 255 x 32 bit words as MCDI_CTL_SDU_LEN_MAX_V2 doesn't take account of
   * the space required by the V1 header, which still exists in a V2 command.
   */
  vmk_uint32 payload[255];
} __attribute__((__packed__)) sfvmk_mcdiRequest2_t;
#define EFX_MCDI_REQUEST_ERROR	0x0001

/* NVRAM Command */
#define SFVMK_NVRAM_MAX_PAYLOAD 32*1024

typedef	struct sfvmk_nvram_cmd_s {
  vmk_uint32 op;
  vmk_uint32 type;
  vmk_uint32 offset;
  vmk_uint32 size;
  vmk_uint32 subtype;
  vmk_uint16 version[4];		/* get/set_ver */
  vmk_uint8 data[SFVMK_NVRAM_MAX_PAYLOAD];	/* read/write */
} sfvmk_nvram_cmd_t;


#define	SFVMK_NVRAM_OP_SIZE		0x00000001
#define	SFVMK_NVRAM_OP_READ		0x00000002
#define	SFVMK_NVRAM_OP_WRITE		0x00000003
#define	SFVMK_NVRAM_OP_ERASE		0x00000004
#define	SFVMK_NVRAM_OP_GET_VER		0x00000005
#define	SFVMK_NVRAM_OP_SET_VER		0x00000006

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

/* Driver version info */
typedef struct sfvmk_versionInfo_s {
  vmk_uint32 type;
  char version[SFVMK_VER_MAX_CHAR_LEN];
}sfvmk_versionInfo_t;

#define SFVMK_GET_DRV_VERSION  0x00000001
#define SFVMK_GET_FW_VERSION   0x00000002
#define SFVMK_GET_ROM_VERSION  0x00000004
#define SFVMK_GET_UEFI_VERSION 0x00000008

#ifdef VMKERNEL
/*
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
                        sfvmk_nvram_cmd_t *pNvrCmd);

int sfvmk_mgmtVerInfoCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_versionInfo_t *pVerInfo);
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
#endif /* VMKERNEL */

#endif
