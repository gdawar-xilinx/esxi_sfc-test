/*
 * Copyright (c) 2018, Solarflare Communications Inc.
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

#include "sfvmk_driver.h"

static const char * sensorStateName[] = {
  "OK",        /* EFX_MON_STAT_STATE_OK */
  "Warn",      /* EFX_MON_STAT_STATE_WARNING */
  "Fatal",     /* EFX_MON_STAT_STATE_FATAL */
  "Broken",    /* EFX_MON_STAT_STATE_BROKEN */
  "No reading" /* EFX_MON_STAT_STATE_NO_READING */
};

/*! \brief  Allocate resource and initialize mon module.
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] Error code [failure]
*/
VMK_ReturnStatus
sfvmk_monInit(sfvmk_adapter_t *pAdapter)
{
  sfvmk_mon_t *pMon;
  efsys_mem_t *pMonStatsBuf;
  size_t monStatsSize;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_MON);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter error");
    status = VMK_BAD_PARAM;
    goto done;
  }

  pMon = &pAdapter->mon;
  if (pMon->state != SFVMK_MON_STATE_UNINITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Mon is already initialized");
    goto done;
  }

  pMonStatsBuf = &pMon->monStatsDmaBuf;

  /* Allocate memory */
  monStatsSize = P2ROUNDUP(EFX_MON_NSTATS * sizeof(uint32_t), EFX_BUF_SIZE);

  pMonStatsBuf->esmHandle = pAdapter->dmaEngine;
  pMonStatsBuf->ioElem.length = monStatsSize;

  /* Allocate DMA space. */
  pMonStatsBuf->pEsmBase = sfvmk_allocDMAMappedMem(pAdapter->dmaEngine,
                                                   pMonStatsBuf->ioElem.length,
                                                   &pMonStatsBuf->ioElem.ioAddr);
  if (pMonStatsBuf->pEsmBase == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter,"Failed to allocate DMA memory for pMonStatsBuf enteries");
    status = VMK_NO_MEMORY;
    goto failed_dma_alloc;
  }

  /* Initialize mon module */
  status = efx_mon_init( pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mon_init failed status: %s",
                        vmk_StatusToString(status));
    goto failed_mon_init;
  }

  pMon->state = SFVMK_MON_STATE_INITIALIZED;

  status = VMK_OK;

  goto done;

failed_mon_init:
  sfvmk_freeDMAMappedMem(pMonStatsBuf->esmHandle,
                         pMonStatsBuf->pEsmBase,
                         pMonStatsBuf->ioElem.ioAddr,
                         pMonStatsBuf->ioElem.length);

failed_dma_alloc:
done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MON);
  return status;
}

/*! \brief  Releases resource and deinit mon module.
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_monFini(sfvmk_adapter_t *pAdapter)
{
  sfvmk_mon_t *pMon;
  efsys_mem_t *pMonStatsBuf;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_MON);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter error");
    goto done;
  }

  pMon = &pAdapter->mon;
  if (pMon->state != SFVMK_MON_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Mon is not initialized");
    goto done;
  }

  pMonStatsBuf = &pMon->monStatsDmaBuf;

  efx_mon_fini(pAdapter->pNic);

  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine, pMonStatsBuf->pEsmBase,
                         pMonStatsBuf->ioElem.ioAddr, pMonStatsBuf->ioElem.length);

  pMon->state = SFVMK_MON_STATE_UNINITIALIZED;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MON);
}

/*! \brief  Releases resource and deinit mon module.
 **
 ** \param[in]  pAdapter     pointer to sfvmk_adapter_t
 ** \param[in]  pStatVal     pointer to efx_mon_stat_value_t
 ** \param[in]  pStatLimit   pointer to efx_mon_stat_limits_t
 **
 ** \return: VMK_OK [success] Error code [failure]
 */
VMK_ReturnStatus
sfvmk_monStatsUpdate(sfvmk_adapter_t *pAdapter,
                     efx_mon_stat_value_t *pStatVal,
                     efx_mon_stat_limits_t *pStatLimit)
{
  sfvmk_mon_t *pMon = NULL;
  efsys_mem_t *pMonStatsBuf = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_MON);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter error");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pStatVal == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL pStatVal error");
    status = VMK_BAD_PARAM;
    goto done;
  }

  pMon = &pAdapter->mon;
  if (pMon->state != SFVMK_MON_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "MON Module uninitialized");
    status = VMK_NOT_READY;
    goto done;
  }

  pMonStatsBuf = &pMon->monStatsDmaBuf;

  status = efx_mon_stats_update(pAdapter->pNic,
                                pMonStatsBuf, pStatVal);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mon_stats_update failed status: %s",
                        vmk_StatusToString(status));
    goto failed_stats_update;
  }

  if (pStatLimit != NULL) {
    status = efx_mon_limits_update(pAdapter->pNic, pStatLimit);
	if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_mon_limits_update failed status: %s",
                          vmk_StatusToString(status));
	  goto failed_limit_update;
    }
  }

failed_stats_update:
failed_limit_update:

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_MON);
  return status;
}

/*! \brief  Helper world function for sensor event
 **
 ** \param[in] data  Pointer to sfvmk_adapter_t
 **
 ** \return: void
 */
void sfvmk_monUpdateHelper(vmk_AddrCookie data)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)data.ptr;
  efx_mon_stat_value_t sensorVal[EFX_MON_NSTATS];
  efx_mon_stat_t id;
  sfvmk_mon_t *pMon = NULL;
  const efx_nic_cfg_t *pNicCfg = NULL;
  efx_mon_stat_value_t *pValue = NULL;
  efx_mon_stat_state_t *pLastState = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto exit;
  }

  sfvmk_MutexLock(pAdapter->lock);

  pMon = &pAdapter->mon;
  if (pMon->state != SFVMK_MON_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "MON Module uninitialized");
    status = VMK_NOT_READY;
    goto done;
  }

  if ((status = sfvmk_monStatsUpdate(pAdapter, sensorVal, NULL)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_monStatsUpdate failed");
    goto done;
  }

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);

  for (id = 0; id < EFX_MON_NSTATS; id++) {
    if (pNicCfg->enc_mon_stat_mask[id / EFX_MON_MASK_ELEMENT_SIZE] &
         (1 << (id % EFX_MON_MASK_ELEMENT_SIZE))) {

      pValue = &(sensorVal[id]);
      pLastState = &(pMon->lastState[id]);

      if (pValue->emsv_state != *pLastState) {
        if (pValue->emsv_state != EFX_MON_STAT_STATE_OK &&
            pValue->emsv_state != EFX_MON_STAT_STATE_NO_READING) {
          SFVMK_ADAPTER_ERROR(pAdapter, "State of Sensor: %s"
                              "[Value : %d] changed from : %s to : %s",
                              (char *)efx_mon_stat_description(pAdapter->pNic, id),
                              pValue->emsv_value,
                              sensorStateName[*pLastState],
                              sensorStateName[pValue->emsv_state]);
        }
        *pLastState = pValue->emsv_state;

      }
    }
  }

done:
  sfvmk_MutexUnlock(pAdapter->lock);
exit:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);
}

/*! \brief Fuction to schedule monitor update
 **
 ** \param[in] pAdapter  pointer to sfvmk_adapter_t
 **
 ** \return: VMK_OK [success] error code [failure]
 **
 */
VMK_ReturnStatus
sfvmk_scheduleMonitorUpdate(sfvmk_adapter_t *pAdapter)
{
  vmk_HelperRequestProps props;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  vmk_HelperRequestPropsInit(&props);

  /* Create a request and submit */
  props.requestMayBlock = VMK_FALSE;
  props.tag = (vmk_AddrCookie)NULL;
  props.cancelFunc = NULL;
  props.worldToBill = VMK_INVALID_WORLD_ID;
  status = vmk_HelperSubmitRequest(pAdapter->helper,
                                   sfvmk_monUpdateHelper,
                                   (vmk_AddrCookie *)pAdapter,
                                   &props);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HelperSubmitRequest failed status: %s",
                        vmk_StatusToString(status));
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);

  return status;
}

