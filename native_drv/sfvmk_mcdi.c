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

/*! \brief routine to be for scheduling reset.
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_scheduleReset(sfvmk_adapter_t *pAdapter)
{
  /* TODO add a proper implementation.
   * This will be done after startIO impl */
  VMK_ASSERT_BUG(0);
}

/*! \brief routine to be called on mcdi timeout.
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
**
** \return: void
*/
static void
sfvmk_mcdiTimeout(sfvmk_adapter_t *pAdapter)
{
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_MCDI);

  SFVMK_ADAPTER_ERROR(pAdapter, "MC_TIMEOUT");

  sfvmk_scheduleReset(pAdapter);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MCDI);
}

/*! \brief Routine for polling mcdi response.
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
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

/*! \brief Routine for sending mcdi cmd.
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
** \param[in] pEmrp    pointer to efx_mcdi_req_t
**
** \return: void
*/
static void
sfvmk_mcdiExecute(void *arg, efx_mcdi_req_t *pEmrp)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)arg;
  vmk_uint32 timeoutUS;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_MCDI);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  vmk_MutexLock(pAdapter->mcdi.lock);

  if (pAdapter->mcdi.state != SFVMK_MCDI_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "MCDI is not initialized");
    vmk_MutexUnlock(pAdapter->mcdi.lock);
    goto done;
  }

  /* Issue request and poll for completion. */
  efx_mcdi_get_timeout(pAdapter->pNic, pEmrp, &timeoutUS);
  efx_mcdi_request_start(pAdapter->pNic, pEmrp, B_FALSE);
  sfvmk_mcdiPoll(pAdapter, timeoutUS);

  vmk_MutexUnlock(pAdapter->mcdi.lock);

  /* Check if driver reset required */
  if ((pEmrp->emr_rc == EIO) && (pAdapter->mcdi.mode == SFVMK_MCDI_MODE_POLL)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Reboot detected, schedule the reset helper");
    sfvmk_scheduleReset(pAdapter);
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MCDI);
}

/*! \brief Routine handling mcdi exceptions.
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
** \param[in]  mcdi exception
**
** \return: void
*/
static void
sfvmk_mcdiException(void *arg, efx_mcdi_exception_t eme)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)arg;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_MCDI);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  SFVMK_ADAPTER_ERROR(pAdapter, "MC_%s", (eme == EFX_MCDI_EXCEPTION_MC_REBOOT)
                      ? "REBOOT" : (eme == EFX_MCDI_EXCEPTION_MC_BADASSERT)
                      ? "BADASSERT" : "UNKNOWN");

  sfvmk_scheduleReset(pAdapter);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MCDI);
}

/*! \brief Routine allocating resource for mcdi cmd handling and initializing
**        mcdi module
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
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
  pMcdi->transport.emt_execute = sfvmk_mcdiExecute;
  pMcdi->transport.emt_exception = sfvmk_mcdiException;

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

  vmk_MutexLock(pAdapter->mcdi.lock);

  if (pAdapter->mcdi.state != SFVMK_MCDI_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "MCDI is not initialized");
    vmk_MutexUnlock(pAdapter->mcdi.lock);
    goto done;
  }

  efx_mcdi_fini(pAdapter->pNic);

  vmk_Memset(&pAdapter->mcdi.transport, 0, sizeof(efx_mcdi_transport_t));

  vmk_MutexUnlock(pAdapter->mcdi.lock);

  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine, pAdapter->mcdi.mem.pEsmBase,
                         pAdapter->mcdi.mem.ioElem.ioAddr,
                         pAdapter->mcdi.mem.ioElem.length);

  sfvmk_mutexDestroy(pAdapter->mcdi.lock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MCDI);
}
