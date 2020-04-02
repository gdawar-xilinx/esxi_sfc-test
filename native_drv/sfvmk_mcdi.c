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

#include "efx_mcdi.h"
#include "sfvmk_driver.h"

#define SFVMK_MCDI_POLL_INTERVAL_MIN 10   /* 10us in 1us units */
#define SFVMK_MCDI_POLL_INTERVAL_MAX 100000 /* 100ms in 1us units */

/*! \brief routine to be called on mcdi timeout.
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
**
** \return: void
*/
static void
sfvmk_mcdiTimeout(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_MCDI);

  SFVMK_ADAPTER_ERROR(pAdapter, "MC_TIMEOUT");

  pAdapter->mcdi.mode = SFVMK_MCDI_MODE_DEAD;

  if ((status = sfvmk_scheduleReset(pAdapter)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_scheduleReset failed with error %s",
                        vmk_StatusToString(status));
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MCDI);
}

/*! \brief Routine for polling mcdi response.
**
** \param[in] pAdapter  pointer to sfvmk_adapter_t
** \param[in] timeoutUS timeout period in microseconds
**
** \return: void
*/
static void
sfvmk_mcdiPoll(sfvmk_adapter_t *pAdapter, vmk_uint32 timeoutUS)
{
  vmk_uint32 delayUS;
  vmk_uint64 currentTime, startTime;
  vmk_uint64 timeOut;
  boolean_t  aborted;
  VMK_ReturnStatus status;

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    return;
  }

  if (pAdapter->pNic == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL NIC ptr");
    return;
  }

  delayUS = SFVMK_MCDI_POLL_INTERVAL_MIN;

  sfvmk_getTime(&startTime);
  currentTime = startTime;
  timeOut = startTime + timeoutUS;

  while (currentTime < timeOut) {
    if (efx_mcdi_request_poll(pAdapter->pNic)) {
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_MCDI, SFVMK_LOG_LEVEL_DBG,
                         "mcdi delay is %lu US", currentTime - startTime);
      return;
    }

    status = vmk_WorldSleep(delayUS);
    if (status == VMK_DEATH_PENDING) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_WorldSleep failed status: %s",
                          vmk_StatusToString(status));
      /* World is dying */
      return;
    }

    /* Exponentially back off the poll frequency. */
    delayUS = delayUS * 2;
    if (delayUS > SFVMK_MCDI_POLL_INTERVAL_MAX)
      delayUS = SFVMK_MCDI_POLL_INTERVAL_MAX;

    sfvmk_getTime(&currentTime);
  }

  aborted = efx_mcdi_request_abort(pAdapter->pNic);
  if (!aborted)
    SFVMK_ADAPTER_ERROR(pAdapter, "Abort failed");

  sfvmk_mcdiTimeout(pAdapter);
}

/*! \brief Local routine to wait for MCDI completion event.
**
** \param[in] pAdapter  pointer to sfvmk_adapter_t
** \param[in] timeoutUS wait time for event completion in micro seconds
**
** \return: void
*/
static void
sfvmk_mcdiWaitForCompletion(sfvmk_adapter_t *pAdapter, vmk_uint32 timeoutUS)
{
  VMK_ReturnStatus status;
  vmk_uint64 currentTime;
  vmk_uint64 startTime;
  vmk_uint64 timeOut;

  VMK_ASSERT_NOT_NULL(pAdapter);

  sfvmk_getTime(&startTime);
  currentTime = startTime;
  timeOut = startTime + timeoutUS;

  while (currentTime < timeOut) {
    status = vmk_WorldWait((vmk_WorldEventID) pAdapter->mcdi.completionEvent,
                           VMK_LOCK_INVALID,
                           EFX_DIV_ROUND_UP((timeOut - currentTime), VMK_USEC_PER_MSEC),
                           "sfvmk_mcdiWaitForCompletion");
    if (status == VMK_WAIT_INTERRUPTED) {
      sfvmk_getTime(&currentTime);
      continue;
    } else if (status == VMK_TIMEOUT) {
      /* Attempt to abort any pending request. If there is no pending request,
       * we have simultaneous completion and timeout. Do nothing. */
      if (efx_mcdi_request_abort(pAdapter->pNic)) {
        sfvmk_mcdiTimeout(pAdapter);
      }
    } else if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_WorldWait Failed status: %s",
                          vmk_StatusToString(status));
      if (status == VMK_BAD_PARAM) {
        VMK_ASSERT(0);
      }
    }
    break;
  }
}

/*! \brief Routine for setting event completion mode.
**
** \param[in] pAdapter  pointer to sfvmk_adapter_t
** \param[in] mode      event completion mode
**
** \return: void
*/
void
sfvmk_setMCDIMode(sfvmk_adapter_t *pAdapter, sfvmk_mcdiMode_t mode)
{
  VMK_ASSERT_NOT_NULL(pAdapter);

  sfvmk_MutexLock(pAdapter->mcdi.lock);

  if (pAdapter->mcdi.mode != SFVMK_MCDI_MODE_DEAD) {
    pAdapter->mcdi.mode = mode;
  }

  sfvmk_MutexUnlock(pAdapter->mcdi.lock);
}

/*! \brief Callback routine to be used by common code to signal completion
**         of MCDI event.
**
** \param[in] pArg      pointer to sfvmk_adapter_t
**
** \return: void
*/
static void
sfvmk_mcdiEventCompletion(void *pArg)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)pArg;
  VMK_ReturnStatus status;

  VMK_ASSERT_NOT_NULL(pAdapter);

  status = vmk_WorldWakeup(pAdapter->mcdi.completionEvent);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_WorldWakeup failed with error status: %s",
                        vmk_StatusToString(status));
  }
}

/*! \brief Routine for sending mcdi cmd.
**
** \param[in] arg      pointer to sfvmk_adapter_t
** \param[in] pEmrp    pointer to efx_mcdi_req_t
**
** \return: void
*/
static void
sfvmk_mcdiExecute(void *arg, efx_mcdi_req_t *pEmrp)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)arg;
  vmk_uint32 timeoutUS;
  VMK_ReturnStatus status;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_MCDI);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  sfvmk_MutexLock(pAdapter->mcdi.lock);

  if (pAdapter->mcdi.state != SFVMK_MCDI_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "MCDI is not initialized");
    sfvmk_MutexUnlock(pAdapter->mcdi.lock);
    goto done;
  }

  efx_mcdi_get_timeout(pAdapter->pNic, pEmrp, &timeoutUS);

  switch (pAdapter->mcdi.mode) {
    case SFVMK_MCDI_MODE_POLL:
      /* Issue request and poll for completion. */
      efx_mcdi_request_start(pAdapter->pNic, pEmrp, B_FALSE);
      sfvmk_mcdiPoll(pAdapter, timeoutUS);
      break;
    case SFVMK_MCDI_MODE_EVENT:
      /* Send request, block until completion event received */
      efx_mcdi_request_start(pAdapter->pNic, pEmrp, B_TRUE);
      sfvmk_mcdiWaitForCompletion(pAdapter, timeoutUS);
      break;
    default:
    /* FALLTHRU */
    case SFVMK_MCDI_MODE_DEAD:
      /* MC Timeout already seen, so fail early */
      pEmrp->emr_rc = efx_mcdi_request_errcode(MC_CMD_ERR_ETIME);
      pEmrp->emr_out_length_used = 0;
      break;
  }

  sfvmk_MutexUnlock(pAdapter->mcdi.lock);

  /* Check if driver reset required */
  if (pEmrp->emr_rc == EIO) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Reboot detected, schedule the reset helper");
    if ((status = sfvmk_scheduleReset(pAdapter)) != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_scheduleReset failed with error %s",
                          vmk_StatusToString(status));
    }
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MCDI);
}

/*! \brief Routine handling mcdi exceptions.
**
** \param[in] arg  pointer to sfvmk_adapter_t
** \param[in] eme  mcdi exception
**
** \return: void
*/
static void
sfvmk_mcdiException(void *arg, efx_mcdi_exception_t eme)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)arg;
  VMK_ReturnStatus status;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_MCDI);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  SFVMK_ADAPTER_ERROR(pAdapter, "MC_%s", (eme == EFX_MCDI_EXCEPTION_MC_REBOOT)
                      ? "REBOOT" : (eme == EFX_MCDI_EXCEPTION_MC_BADASSERT)
                      ? "BADASSERT" : "UNKNOWN");


  pAdapter->mcdi.mode = SFVMK_MCDI_MODE_DEAD;

  if ((status = sfvmk_scheduleReset(pAdapter)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_scheduleReset failed with error %s",
                        vmk_StatusToString(status));
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MCDI);
}

/*! \brief Routine to post MCDI user request
**
** \param[in] pAdapter pAdapter  pointer to sfvmk_adapter_t
** \param[in] pEmReq  mcdi command request
**
** \return: VMK_OK [success],
**       Below error codes will be returned in case of failure,
**       VMK_BAD_PARAM:      Invalid input/output buffer
**       VMK_NOT_SUPPORTED:  MCDI feature not supported
**       VMK_NOT_READY:      A communication error has happened,
**                           MCDI feature not yet initialized.
**                           Retry after MC reboot.
**       VMK_FAILURE:        Any other failure
**
*/
int
sfvmk_mcdiIOHandler(sfvmk_adapter_t *pAdapter,
                    efx_mcdi_req_t *pEmReq)
{
  const efx_nic_cfg_t *pNCfg = efx_nic_cfg_get(pAdapter->pNic);
  sfvmk_mcdi_t        *pMcdi = &pAdapter->mcdi;
  VMK_ReturnStatus    status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_MCDI);

  if (!(pNCfg->enc_features & EFX_FEATURE_MCDI)) {
    SFVMK_ADAPTER_ERROR(pAdapter,"MCDI feature is not supported");
    status = VMK_NOT_SUPPORTED;
    goto done;
  }

  if (pMcdi->state != SFVMK_MCDI_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter,"MCDI State is not Initialized");
    status = VMK_NOT_READY;
    goto done;
  }

  if (!pEmReq->emr_in_buf || !pEmReq->emr_out_buf) {
    SFVMK_ADAPTER_ERROR(pAdapter,"MCDI command buffer is NULL");
    status = VMK_BAD_PARAM;
    goto done;
  }

  sfvmk_mcdiExecute(pAdapter, pEmReq);
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MCDI);

  return status;
}

#if EFSYS_OPT_MCDI_LOGGING

#define SFVMK_MCDI_LOG_BUF_SIZE 128

/*! \brief helper routine to log mcdi request.
**
** \param[in,out] pBuffer   processed message buffer
** \param[in]     pData     input message buffer
** \param[in]     dataSize  input message buffer size
** \param[in]     pfxsize   offset at which prefix finishes
** \param[in]     position  position to start writing
**
** \return: the index till which logs have been written
*/
static size_t
sfvmk_mcdiDoLog(char *pBuffer, const void *pData, size_t dataSize,
                size_t pfxsize, size_t position)
{
  const uint32_t *pWords = pData;
  vmk_ByteCount bytesCopied;
  VMK_ReturnStatus status = VMK_OK;
  size_t i;

  for (i = 0; i < dataSize; i += sizeof(*pWords)) {
    if (position + 2 * sizeof(*pWords) + 1 >= SFVMK_MCDI_LOG_BUF_SIZE) {
      pBuffer[position] = '\0';
      vmk_LogMessage(" %s \\\n", pBuffer);
      position = pfxsize;
    }

    status = vmk_StringFormat(pBuffer + position,
                              SFVMK_MCDI_LOG_BUF_SIZE - position,
                              &bytesCopied, " %08x", *pWords);
    if (status != VMK_OK)
      return position + bytesCopied;

    pWords++;
    position += 2 * sizeof(uint32_t) + 1;
  }
  return position;
}

/*! \brief Routine handling mcdi logging request.
**
** \param[in] pAdapter   pointer to sfvmk_adapter_t
** \param[in] pHeader    message header
** \param[in] headerSize message header size
** \param[in] pData      message data
** \param[in] dataSize   message data size
**
** \return: void
*/
static void
sfvmk_mcdiLogger(void *pPriv, efx_log_msg_t type,
                 void *pHeader, size_t headerSize,
                 void *pData, size_t dataSize)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)pPriv;
  sfvmk_mcdi_t *pMcdi;
  char buffer[SFVMK_MCDI_LOG_BUF_SIZE];
  vmk_ByteCount pfxsize;
  VMK_ReturnStatus status = VMK_OK;
  size_t start;

  pMcdi = &pAdapter->mcdi;

  if (pMcdi->mcLogging == VMK_FALSE)
    return;

  status = vmk_StringFormat(buffer, sizeof(buffer), &pfxsize,
                            "sfc %s %s MCDI RPC %s:",
                            pAdapter->pciDeviceName.string,
                            pAdapter->uplink.name.string,
                            type == EFX_LOG_MCDI_REQUEST ? "REQ" :
                            type == EFX_LOG_MCDI_RESPONSE ? "RESP" : "???");
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "String format failed with error %s",
                        vmk_StatusToString(status));
    return;
  }

  start = sfvmk_mcdiDoLog(buffer, pHeader,
                          headerSize, pfxsize, pfxsize);
  start = sfvmk_mcdiDoLog(buffer, pData, dataSize, pfxsize, start);
  if (start != pfxsize) {
    buffer[start] = '\0';
    vmk_LogMessage(" %s\n", buffer);
  }

}

/*! \brief Routine to get MC Logging variable state
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
**
** \return: MC Log control variable state
*/
vmk_Bool
sfvmk_getMCLogging(sfvmk_adapter_t *pAdapter)
{
  sfvmk_mcdi_t    *pMcdi;

  pMcdi = &pAdapter->mcdi;

  return pMcdi->mcLogging;

}

/*! \brief Routine for setting MC Log variable state
**         to true/false
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
** \param[in] state    MC Log control variable state
**
** \return: void
*/
void
sfvmk_setMCLogging(sfvmk_adapter_t *pAdapter, vmk_Bool state)
{
  sfvmk_mcdi_t    *pMcdi;

  pMcdi = &pAdapter->mcdi;

  sfvmk_MutexLock(pMcdi->lock);
  pMcdi->mcLogging = state;
  sfvmk_MutexUnlock(pMcdi->lock);
}
#endif

/*! \brief Routine to reset the MCDI completion mode and starting new epoch
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_mcdiReset(sfvmk_adapter_t *pAdapter)
{
  VMK_ASSERT_NOT_NULL(pAdapter);

  /* Start a new epoch (allow fresh MCDI requests to succeed) */
  efx_mcdi_new_epoch(pAdapter->pNic);

  /* Set MCDI mode to polling */
  sfvmk_MutexLock(pAdapter->mcdi.lock);
  pAdapter->mcdi.mode = SFVMK_MCDI_MODE_POLL;
  sfvmk_MutexUnlock(pAdapter->mcdi.lock);
}


#if EFSYS_OPT_MCDI_PROXY_AUTH_SERVER && defined(SFVMK_SUPPORT_SRIOV)
/*! \brief Routine handling mcdi proxy event request.
**
** \param[in] pAdapter   pointer to sfvmk_adapter_t
** \param[in] index      request index from Firmware
**
** \return: None
*/
static void
sfvmk_mcdiProxyRequest(void *pPriv, vmk_uint32 index)
{
  VMK_ReturnStatus        status = VMK_FAILURE;
  sfvmk_proxyEvent_t      *pProxyEvent = NULL;
  sfvmk_adapter_t         *pAdapter = (sfvmk_adapter_t *)pPriv;
  sfvmk_proxyAdminState_t *pProxyState = pAdapter->pProxyState;
  sfvmk_proxyReqState_t   *pReqState = NULL;
  vmk_uint64               authState;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "Proxy request index %u", index);

  if (pProxyState == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Null proxy state");
    goto done;
  }

  authState = vmk_AtomicRead64(&pProxyState->authState);
  if ((authState != SFVMK_PROXY_AUTH_STATE_READY) &&
      (authState != SFVMK_PROXY_AUTH_STATE_RUNNING)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid proxy state %lu", authState);
    goto done;
  }

  pReqState = &pProxyState->pReqState[index];
  if (vmk_AtomicReadIfEqualWrite64(&pReqState->reqState,
                                   SFVMK_PROXY_REQ_STATE_IDLE,
                                   SFVMK_PROXY_REQ_STATE_INCOMING) !=
                                   SFVMK_PROXY_REQ_STATE_IDLE) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid req state");
    goto done;
  }

  pProxyEvent = sfvmk_MemAlloc(sizeof(sfvmk_proxyEvent_t));
  if (pProxyEvent == NULL) {
    SFVMK_ERROR("sfvmk_MemAlloc proxy event %lu bytes failed",
                sizeof(sfvmk_proxyEvent_t));
    goto done;
  }

  pProxyEvent->index = index;
  pProxyEvent->pAdapter = pAdapter;
  pProxyEvent->pReqState = pReqState;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "pProxyEvent: %p, pAdapter: %p, index: %u",
                      pProxyEvent, pAdapter, index);

  status = sfvmk_submitProxyRequest(pProxyEvent);
  if(status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_submitProxyRequest failed: %s",
                        vmk_StatusToString(status));
    goto fail;
  }

  goto done;

fail:
  vmk_AtomicWrite64(&pReqState->reqState, SFVMK_PROXY_REQ_STATE_IDLE);
  sfvmk_MemFree(pProxyEvent);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
}
#endif

/*! \brief Routine allocating resource for mcdi cmd handling and initializing
**        mcdi module
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_mcdiInit(sfvmk_adapter_t *pAdapter)
{
  sfvmk_mcdi_t *pMcdi;
  vmk_uint32 maxMsgSize;
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_MCDI);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  pMcdi = &pAdapter->mcdi;

  maxMsgSize = 2 * sizeof(vmk_uint32) + MCDI_CTL_SDU_LEN_MAX_V2;

  if (pMcdi->state == SFVMK_MCDI_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "MCDI already initialized");
    goto failed_mcdi_state_check;
  }

  status = sfvmk_mutexInit("mcdiLock", &pAdapter->mcdi.lock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_mutexInit failed status  %s",
                        vmk_StatusToString(status));
    goto failed_mutex_init;
  }

  pMcdi->state = SFVMK_MCDI_STATE_INITIALIZED;

  /* Set MCDI mode to polling */
  pMcdi->mode = SFVMK_MCDI_MODE_POLL;

  /* MCDI DMA buffer should be 256 byte aligned.
   * Using page based allocator that guarantee page aligned memory */
  pMcdi->mem.pEsmBase = sfvmk_allocDMAMappedMem(pAdapter->dmaEngine, maxMsgSize,
                                                &pMcdi->mem.ioElem.ioAddr);
  if (pMcdi->mem.pEsmBase == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter,"sfvmk_allocDMAMappedMem failed");
    status = VMK_NO_MEMORY;
    goto failed_mem_alloc;
  }

  pMcdi->mem.ioElem.length = maxMsgSize;
  pMcdi->mem.esmHandle = pAdapter->dmaEngine;

  pMcdi->transport.emt_context = pAdapter;
  pMcdi->transport.emt_dma_mem = &pMcdi->mem;
  pMcdi->transport.emt_ev_cpl = sfvmk_mcdiEventCompletion;
  pMcdi->transport.emt_execute = sfvmk_mcdiExecute;
  pMcdi->transport.emt_exception = sfvmk_mcdiException;
#if EFSYS_OPT_MCDI_LOGGING
  pMcdi->transport.emt_logger = sfvmk_mcdiLogger;
  pMcdi->mcLogging = VMK_FALSE;
#endif
#if EFSYS_OPT_MCDI_PROXY_AUTH_SERVER && defined(SFVMK_SUPPORT_SRIOV)
  pMcdi->transport.emt_ev_proxy_request = sfvmk_mcdiProxyRequest;
#endif

  pAdapter->mcdi.completionEvent =
                       (vmk_WorldEventID)&pAdapter->mcdi.completionEvent;

  if ((status = efx_mcdi_init(pAdapter->pNic, &pMcdi->transport)) != 0) {
    SFVMK_ADAPTER_ERROR(pAdapter,"efx_mcdi_init failed status %s",
                        vmk_StatusToString(status));
    goto failed_mcdi_init;
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MCDI);

  return status;

failed_mcdi_init:
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine, pMcdi->mem.pEsmBase,
                         pMcdi->mem.ioElem.ioAddr, pMcdi->mem.ioElem.length);

failed_mem_alloc:
  sfvmk_mutexDestroy(pMcdi->lock);

failed_mutex_init:
failed_mcdi_state_check:
  pMcdi->state = SFVMK_MCDI_STATE_UNINITIALIZED;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MCDI);

  return status;
}

/*! \brief Routine for destroying MCDI module.
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_mcdiFini(sfvmk_adapter_t *pAdapter)
{
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_MCDI);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if (pAdapter->pNic == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL NIC ptr");
    goto done;
  }

  sfvmk_MutexLock(pAdapter->mcdi.lock);

  if (pAdapter->mcdi.state != SFVMK_MCDI_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "MCDI is not initialized");
    sfvmk_MutexUnlock(pAdapter->mcdi.lock);
    goto done;
  }

  efx_mcdi_fini(pAdapter->pNic);

  vmk_Memset(&pAdapter->mcdi.transport, 0, sizeof(efx_mcdi_transport_t));

  sfvmk_MutexUnlock(pAdapter->mcdi.lock);

  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine, pAdapter->mcdi.mem.pEsmBase,
                         pAdapter->mcdi.mem.ioElem.ioAddr,
                         pAdapter->mcdi.mem.ioElem.length);

  sfvmk_mutexDestroy(pAdapter->mcdi.lock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MCDI);
}
