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

#define SFVMK_MCDI_MAX_PAYLOAD 0x400

#define SFVMK_MCDI_POLL_INTERVAL_MIN 10   /* 10us in 1us units */
#define SFVMK_MCDI_POLL_INTERVAL_MAX 100000 /* 100ms in 1us units */
#define SFVMK_MCDI_WATCHDOG_INTERVAL 10000000 /* 10s in 1us units */

#define SFVMK_MCDI_LOCK(mcdi)     vmk_MutexLock(mcdi->lock);
#define SFVMK_MCDI_UNLOCK(mcdi)   vmk_MutexUnlock(mcdi->lock);

/*! \brief routine to be called on mcdi timeout.
**
** \param[in] adapter pointer to sfvmk_adapter_t
**
** \return: void
*/
static void
sfvmk_mcdiTimeout(sfvmk_adapter_t *pAdapter)
{
  SFVMK_ERR(pAdapter, " MC_TIMEOUT");

  EFSYS_PROBE(mcdi_timeout);

  sfvmk_scheduleReset(pAdapter);
  return;
}

/*! \brief routine called for getting current system time
**
** \param[in] pointer to vmk_uint64
**
** \return: void
*/
static inline void sfvmk_getTime(vmk_uint64 *pTime)
{
  vmk_TimeVal time;
  vmk_GetTimeOfDay(&time);
  *pTime = (time.sec * VMK_USEC_PER_SEC) + time.usec;
}

/*! \brief Routine for polling mcdi response.
**
** \param[in] adapter pointer to sfvmk_adapter_t
**
** \return: 0 <success> error code <failure>
*/
static int
sfvmk_mcdiPoll(sfvmk_adapter_t *pAdapter, vmk_uint32 timeoutUS)
{
  efx_nic_t *pNic;
  vmk_uint32 delayUS;
  boolean_t aborted;
  vmk_uint64 timeout, currentTime;
  vmk_uint64 startTime;
  VMK_ReturnStatus status = VMK_OK;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  delayUS = SFVMK_MCDI_POLL_INTERVAL_MIN;
  pNic = pAdapter->pNic;

  SFVMK_NULL_PTR_CHECK(pNic);

  sfvmk_getTime(&currentTime);
  startTime = currentTime;
  timeout = currentTime + timeoutUS;
  while (currentTime < timeout)
  {
    if (efx_mcdi_request_poll(pNic)) {
      EFSYS_PROBE1(mcdi_delay, vmk_uint32, currentTime - startTime);
      return 0;
    }
    status = vmk_WorldSleep(delayUS);
    if ((status != VMK_OK) && (status != VMK_WAIT_INTERRUPTED)) {
      SFVMK_ERR(pAdapter, "vmk_WorldSleep failed status: %s",
                vmk_StatusToString(status));
      break;
    }

    /* Exponentially back off the poll frequency. */
    delayUS = delayUS * 2;
    if (delayUS > SFVMK_MCDI_POLL_INTERVAL_MAX)
      delayUS = SFVMK_MCDI_POLL_INTERVAL_MAX;

    sfvmk_getTime(&currentTime);
  }

  if (currentTime >= timeout) {
    aborted = efx_mcdi_request_abort(pNic);
    VMK_ASSERT_BUG(aborted, "abort failed");
    sfvmk_mcdiTimeout(pAdapter);
    return ETIMEDOUT;
  }

  return status;
}

/*! \brief Routine for sending mcdi cmd.
**
** \param[in] adapter pointer to sfvmk_adapter_t
**
** \return: void
*/
static void
sfvmk_mcdiExecute(void *arg, efx_mcdi_req_t *emrp)
{
  sfvmk_adapter_t *pAdapter;
  sfvmk_mcdi_t *pMcdi;
  int status;
  vmk_uint32 timeoutUS;

  pAdapter = (sfvmk_adapter_t *)arg;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  pMcdi = &pAdapter->mcdi;

  SFVMK_MCDI_LOCK(pMcdi);

  VMK_ASSERT_BUG((pMcdi->state == SFVMK_MCDI_INITIALIZED),
                    "MCDI not initialized");

  /* Issue request and poll for completion. */
  efx_mcdi_get_timeout(pAdapter->pNic, emrp, &timeoutUS);
  efx_mcdi_request_start(pAdapter->pNic, emrp, B_FALSE);
  status = sfvmk_mcdiPoll(pAdapter, timeoutUS);

  SFVMK_MCDI_UNLOCK(pMcdi);

  /* Check if driver reset required */
  if (!status && (emrp->emr_rc == EIO) &&
      (pMcdi->mode == SFVMK_MCDI_MODE_POLL)) {
      SFVMK_ERR(pAdapter, "Reboot detected, schedule the reset helper");
      sfvmk_scheduleReset(pAdapter);
  }
}

/*! \brief Routine for mcdi event handling.
**
** \param[in] adapter pointer to sfvmk_adapter_t
**
** \return: void
*/
static void
sfvmk_mcdiEvCpl(void *arg)
{

  sfvmk_adapter_t *pAdapter;
  sfvmk_mcdi_t *pMcdi;

  pAdapter= (sfvmk_adapter_t *)arg;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  pMcdi = &pAdapter->mcdi;

  VMK_ASSERT_BUG(pMcdi->state == SFVMK_MCDI_INITIALIZED,
                  "MCDI not initialized");
  /* We do not use MCDI completion, MCDI is simply polled */
  return;
}

/*! \brief Routine handling mcdi exceptions.
**
** \param[in] adapter pointer to sfvmk_adapter_t
** \param[in]  mcdi exception
**
** \return: void
*/
static void
sfvmk_mcdiException(void *arg, efx_mcdi_exception_t eme)
{
  sfvmk_adapter_t *pAdapter;
  vmk_Device dev;

  pAdapter= (sfvmk_adapter_t *)arg;
  SFVMK_NULL_PTR_CHECK(pAdapter);

  dev = pAdapter->device;

  SFVMK_ERR(pAdapter, "MC_%s", (eme == EFX_MCDI_EXCEPTION_MC_REBOOT)
              ? "REBOOT": (eme == EFX_MCDI_EXCEPTION_MC_BADASSERT)
              ? "BADASSERT" : "UNKNOWN");

  EFSYS_PROBE(mcdi_exception);

  sfvmk_scheduleReset(pAdapter);
  return;
}

/*! \brief Routine to post MCDI user request
**
** \param[in] pAdapter adapter pointer to sfvmk_adapter_t
** \param[in] pEmReq  mcdi command request
**
** \return: VMK_OK <success> error code <failure>
**
*/
int
sfvmk_mcdiIOHandler(struct sfvmk_adapter_s *pAdapter,
                      efx_mcdi_req_t *pEmReq)
{
  const efx_nic_cfg_t *pNCfg = efx_nic_cfg_get(pAdapter->pNic);
  sfvmk_mcdi_t        *pMcdi = &pAdapter->mcdi;
  int                  rc = VMK_OK;

  if (!(pNCfg->enc_features & EFX_FEATURE_MCDI)) {
    SFVMK_ERR(pAdapter,"MCDI feature is not supported");
    rc = VMK_NOT_SUPPORTED;
    goto fail;
  }

  if (pMcdi->state != SFVMK_MCDI_INITIALIZED) {
    SFVMK_ERR(pAdapter,"MCDI State is not Initialized");
    rc = VMK_NOT_READY;
    goto fail;
  }

  if (!pEmReq->emr_in_buf || !pEmReq->emr_out_buf) {
    SFVMK_ERR(pAdapter,"MCDI command buffer is NULL");
    rc = VMK_BAD_PARAM;
    goto fail;
  }

  sfvmk_mcdiExecute((void *)pAdapter, pEmReq);

  return VMK_OK;

fail:
  return rc;
}

/*! \brief Routine allocating resource for mcdi cmd handling and initializing
**        mcdi module
**
** \param[in] adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
int
sfvmk_mcdiInit(sfvmk_adapter_t *pAdapter)
{
  efx_nic_t *pNic;
  efx_mcdi_transport_t *pEmt;
  efsys_mem_t *pMcdiMem;
  vmk_uint32 maxMsgSize;
  vmk_int32 rc = -1;
  sfvmk_mcdi_t *pMcdi;
  VMK_ReturnStatus status = VMK_OK;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  pNic = pAdapter->pNic;
  pMcdi = &pAdapter->mcdi;
  pEmt = &pMcdi->transport;
  pMcdiMem = &pMcdi->mem;

  maxMsgSize = sizeof(vmk_uint32) + MCDI_CTL_SDU_LEN_MAX_V2;

  VMK_ASSERT_BUG(pMcdi->state != SFVMK_MCDI_INITIALIZED,
                  "MCDI already initialized");

  status = sfvmk_mutexInit("mcdi", &pMcdi->lock);
  if (status != VMK_OK)
    goto lock_create_fail;

  pMcdi->state = SFVMK_MCDI_INITIALIZED;

  /* Set MCDI mode to Polling */
  pMcdi->mode = SFVMK_MCDI_MODE_POLL;

  pMcdiMem->pEsmBase = sfvmk_allocCoherentDMAMapping(pAdapter->dmaEngine,
                                        maxMsgSize, &pMcdiMem->ioElem.ioAddr);
  if (NULL== pMcdiMem->pEsmBase) {
    SFVMK_ERR(pAdapter,"failed to allocate memory for mcdi module");
    goto sfvmk_mem_alloc_fail;
  }

  pMcdiMem->ioElem.length = maxMsgSize;
  pMcdiMem->esmHandle = pAdapter->dmaEngine;

  pEmt->emt_context = pAdapter;
  pEmt->emt_dma_mem = pMcdiMem;
  pEmt->emt_execute = sfvmk_mcdiExecute;
  pEmt->emt_ev_cpl =  sfvmk_mcdiEvCpl;
  pEmt->emt_exception = sfvmk_mcdiException;

  if ((rc = efx_mcdi_init(pNic, pEmt)) != 0) {
    SFVMK_ERR(pAdapter,"failed to init mcdi module");
    goto sfvmk_mcdi_init_fail;
  }

  return rc;

sfvmk_mcdi_init_fail:
  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pMcdiMem->pEsmBase ,
                              pMcdiMem->ioElem.ioAddr, pMcdiMem->ioElem.length);
sfvmk_mem_alloc_fail:
  sfvmk_mutexDestroy(pMcdi->lock);
lock_create_fail:
  pMcdi->state = SFVMK_MCDI_UNINITIALIZED;

  return rc;
}

/*! \brief Routine for destroying mcdi module.
**
** \param[in] adapter pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_mcdiFini(sfvmk_adapter_t *pAdapter)
{
  sfvmk_mcdi_t *pMcdi;
  efx_nic_t *pNic;
  efx_mcdi_transport_t *pEmt;
  efsys_mem_t *pMcdiMem;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  pNic = pAdapter->pNic;
  SFVMK_NULL_PTR_CHECK(pNic);

  pMcdi = &pAdapter->mcdi;
  pEmt = &pMcdi->transport;
  pMcdiMem = &pMcdi->mem;

  SFVMK_MCDI_LOCK(pMcdi);
  VMK_ASSERT_BUG(pMcdi->state == SFVMK_MCDI_INITIALIZED,
                    "MCDI not initialized");

  efx_mcdi_fini(pNic);
  vmk_Memset(pEmt, 0 , sizeof(*pEmt));

  SFVMK_MCDI_UNLOCK(pMcdi);

  sfvmk_freeCoherentDMAMapping(pAdapter->dmaEngine, pMcdiMem->pEsmBase,
                                pMcdiMem->ioElem.ioAddr,
                                pMcdiMem->ioElem.length);

  sfvmk_mutexDestroy(pMcdi->lock);

  return;
}
