/*
 * Copyright (c) 2017 - 2018, Solarflare Communications Inc.
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

#ifndef __SFVMK_API_H__
#define __SFVMK_API_H__

#include <stdint.h>

/*
 * The maximum payload length is 0x400 (MCDI_CTL_SDU_LEN_MAX_V2) - uint32
 * = 255 x 32 bit as MCDI_CTL_SDU_LEN_MAX_V2 doesn't take account of
 * the space required by the V1 header, which still exists in a V2 command.
 */
#define SFVMK_MCDI_MAX_PAYLOAD_ARRAY 255

#define SFVMK_MCDI_REQUEST_ERROR     0x0001

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
  uint32_t payload[SFVMK_MCDI_MAX_PAYLOAD_ARRAY];
} __attribute__((__packed__)) sfvmk_mcdiRequest2_t;

/* Setup and return a handle to send MCDI request to SFVMK kernel module */
extern void *setup_mcdiHandle(void);

/* Release the handle returned by setup_mcdiHandle() API */
extern void release_mcdiHandle(void *handle);

/* Prepare and send a MCDI request to SFVMK kernel module with help of this API */
extern int post_mcdiCommand(void *handle, char *nic_name, sfvmk_mcdiRequest2_t *mcdiReq);

#endif
