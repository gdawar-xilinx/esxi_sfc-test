#ifndef __SFVMK_API_H__
#define __SFVMK_API_H__

#define SFVMK_MCDI_MAX_PAYLOAD_ARRAY 255
#define EFX_MCDI_REQUEST_ERROR	0x0001

/*! \brief struct efx_mcdi_request2_s - Parameters for %EFX_MCDI_REQUEST2 sub-command
 **
 ** cmd[in] MCDI command type number.
 **
 ** inlen[in] The length of command parameters, in bytes.
 **
 ** outlen[in/out] On entry, the length available for the response, in bytes.
 **	On return, the length used for the response, in bytes.
 **
 ** flags[out] Flags for the command or response.  The only flag defined
 **	at present is %EFX_MCDI_REQUEST_ERROR.  If this is set on return,
 **	the MC reported an error.
 **
 ** host_errno[out] On return, if %EFX_MCDI_REQUEST_ERROR is included in @flags,
 **	the suggested VMK error code for the error.
 **
 ** payload[in/out] On entry, the MCDI command parameters.  On return, the response.
 **
 */
typedef struct sfvmk_mcdiRequest2_s {
  uint16_t cmd;
  uint16_t inlen;
  uint16_t outlen;
  uint16_t flags;
  uint32_t host_errno;
  /*
   * The maximum payload length is 0x400 (MCDI_CTL_SDU_LEN_MAX_V2) - 4 bytes
   * = 255 x 32 bit words as MCDI_CTL_SDU_LEN_MAX_V2 doesn't take account of
   * the space required by the V1 header, which still exists in a V2 command.
   */
  uint32_t payload[SFVMK_MCDI_MAX_PAYLOAD_ARRAY];
} __attribute__((__packed__)) sfvmk_mcdiRequest2_t;

extern void *setup_mcdiHandle(void);
extern void release_mcdiHandle(void *handle);
extern int post_mcdiCommand(void *handle, char *nic_name, sfvmk_mcdiRequest2_t *mcdiReq);

#endif
