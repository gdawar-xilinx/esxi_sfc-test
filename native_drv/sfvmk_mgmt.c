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

#include "efx_mcdi.h"
#include "efx_regs_mcdi.h"
#include "sfvmk_driver.h"
#include "sfvmk_mgmt_interface.h"

/*! \brief  Get adapter pointer based on hash.
**          This function assumes that module level
**          semaphore is already taken
**
** \param[in] pMgmtParm pointer to managment param
**
** \return: pointer to sfvmk_adapter_t [success]  NULL [failure]
*/
static sfvmk_adapter_t *
sfvmk_mgmtFindAdapter(sfvmk_mgmtDevInfo_t *pMgmtParm)
{
  sfvmk_adapter_t *pAdapter;
  VMK_ReturnStatus status;

  vmk_SemaAssertIsLocked(&sfvmk_modInfo.lock);

  status = vmk_HashKeyFind(sfvmk_modInfo.vmkDevHashTable,
                           pMgmtParm->deviceName,
                           (vmk_HashValue *)&pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERROR("%s: Find vmkDevice failed with status: %s",
                pMgmtParm->deviceName, vmk_StatusToString(status));
    goto end;
  }

  if (pAdapter == NULL) {
    SFVMK_ERROR("%s: No match found for vmkDevice", pMgmtParm->deviceName);
    pMgmtParm->status = VMK_NOT_FOUND;
    goto end;
  }

  return pAdapter;

end:
  return NULL;
}

/*! \brief  A Mgmt callback routine to post MCDI commands
 **
 ** \param[in]  pCookies    Pointer to cookie
 ** \param[in]  pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface   Pointer to device interface structure
 ** \param[in,out]  pMgmtMcdi   Pointer to MCDI cmd struct
 **
 ** \return: VMK_OK  [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:      In case of dev not found
 **     VMK_BAD_PARAM:      Invalid payload size or input param
 **     VMK_FAILURE:        Any other failure
 **
 */
VMK_ReturnStatus
sfvmk_mgmtMcdiCallback(vmk_MgmtCookies       *pCookies,
                        vmk_MgmtEnvelope     *pEnvelope,
                        sfvmk_mgmtDevInfo_t  *pDevIface,
                        sfvmk_mcdiRequest_t  *pMgmtMcdi)
{
  sfvmk_adapter_t *pAdapter = NULL;
  efx_mcdi_req_t   emr;
  VMK_ReturnStatus status = VMK_FAILURE;

  vmk_SemaLock(&sfvmk_modInfo.lock);
  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pMgmtMcdi) {
    SFVMK_ERROR("pMgmtMcdi: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Adapter structure corresponding to %s device not found",
                pDevIface->deviceName);
    pDevIface->status = VMK_NOT_FOUND;
    goto end;
  }

  if (pMgmtMcdi->inlen > MCDI_CTL_SDU_LEN_MAX_V2 ||
      pMgmtMcdi->outlen > MCDI_CTL_SDU_LEN_MAX_V2) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid Length");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  emr.emr_cmd = pMgmtMcdi->cmd;
  emr.emr_in_buf = (vmk_uint8 *)pMgmtMcdi->payload;
  emr.emr_in_length = pMgmtMcdi->inlen;

  emr.emr_out_buf = (vmk_uint8 *)pMgmtMcdi->payload;
  emr.emr_out_length = pMgmtMcdi->outlen;

  status = sfvmk_mcdiIOHandler(pAdapter, &emr);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "MCDI command failed %s",
                        vmk_StatusToString(status));
    pDevIface->status = status;
    goto end;
  }

  pMgmtMcdi->host_errno = emr.emr_rc;
  pMgmtMcdi->cmd = emr.emr_cmd;
  pMgmtMcdi->outlen = emr.emr_out_length_used;

  pDevIface->status = VMK_OK;

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback routine to control dynamic
 **         logging of MC Logs
 **
 ** \param[in]  pCookies    Pointer to cookie
 ** \param[in]  pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface  Pointer to device interface structure
 ** \param[in,out]  pMcdiLog   Pointer to MCDI log structure
 **
 ** \return: VMK_OK  [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:      In case of dev not found
 **     VMK_BAD_PARAM:      Invalid payload size or input param
 **     VMK_NO_ACCESS:      MC Log disabled. Not allowed to perform
 **                         get/set operation.
 **     VMK_FAILURE:        For any other errors.
 **
 */
VMK_ReturnStatus
sfvmk_mgmtMCLoggingCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_mcdiLogging_t *pMcdiLog)
{
  sfvmk_adapter_t *pAdapter = NULL;

  vmk_SemaLock(&sfvmk_modInfo.lock);
  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  if (!pMcdiLog) {
    SFVMK_ERROR("pMcdiLog: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Adapter structure corresponding to %s device not found",
                pDevIface->deviceName);
    pDevIface->status = VMK_NOT_FOUND;
    goto end;
  }

#if EFSYS_OPT_MCDI_LOGGING
  switch (pMcdiLog->mcLoggingOp) {
    case SFVMK_MGMT_DEV_OPS_GET:
      pMcdiLog->state = sfvmk_getMCLogging(pAdapter);
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_MGMT, SFVMK_LOG_LEVEL_DBG,
                          "%s: Get MC Log Status (%s)", pDevIface->deviceName,
                          pMcdiLog->state ? "Enabled" : "Disabled");
      break;

    case SFVMK_MGMT_DEV_OPS_SET:
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_MGMT, SFVMK_LOG_LEVEL_DBG,
                          "%s: Set MC Log Status (%s)", pDevIface->deviceName,
                          pMcdiLog->state ? "Enable" : "Disable");

      sfvmk_setMCLogging(pAdapter, pMcdiLog->state);
      break;

    default:
      pDevIface->status = VMK_BAD_PARAM;
      goto end;
  }

  pDevIface->status = VMK_OK;
#else
  pDevIface->status = VMK_NOT_SUPPORTED;
#endif

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

