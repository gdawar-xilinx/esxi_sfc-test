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
#ifdef SFVMK_SUPPORT_SRIOV
#include "efx_mcdi.h"
#include "efx_regs_mcdi.h"

#define SFVMK_PROXY_CLEANUP_WAIT_USEC  10000

typedef struct sfvmk_functionInfo_s {
  vmk_uint32        pf;
  vmk_uint32        vf;
  sfvmk_adapter_t   *pAdapter;
  vmk_PCIDeviceAddr deviceAddr;
} sfvmk_functionInfo_t;

/*! \brief  Configure the proxy auth module
**
** \param[in]  pAdapter            pointer to sfvmk_adapter_t
** \param[in]  requestSize         size of proxy request
** \param[in]  responseSize        size of proxy response
** \param[in]  pOpList             list of operations to be proxied
** \param[in]  opCount             number of entries in pOpList
** \param[in]  handledPrivileges   bitmask of privileges to be revoked
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus
sfvmk_proxyAuthConfigureList(sfvmk_adapter_t *pAdapter,
                             size_t requestSize,
                             size_t responseSize,
                             vmk_uint32 *pOpList,
                             vmk_uint32 opCount,
                             vmk_uint32 handledPrivileges)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_proxyAdminState_t *pProxyState = NULL;
  const efx_nic_cfg_t *pNicCfg = NULL;
  efx_proxy_auth_config_t proxyConfig;
  vmk_uint32 blockCount;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  VMK_ASSERT(pAdapter->pProxyState == NULL);

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_cfg_get failed");
    goto done;
  }

  /* We're potentially going to allocate a contiguous block of memory
   * that's substantially larger than a 4k page. Before we do that we
   * check our permissions to see if we have any chance of successfully
   * configuring this.
   */
  if (!(pNicCfg->enc_privilege_mask & MC_CMD_PRIVILEGE_MASK_IN_GRP_ADMIN)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "In-sufficient privileges: 0x%x",
                        pNicCfg->enc_privilege_mask);
    status = VMK_NO_ACCESS;
    goto done;
  }

  pProxyState = (sfvmk_proxyAdminState_t *)
                vmk_HeapAlloc(sfvmk_modInfo.heapID,
                              sizeof(sfvmk_proxyAdminState_t));
  if (!pProxyState) {
    status = VMK_NO_MEMORY;
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HeapAlloc proxyState failed: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  vmk_Memset(pProxyState, 0, sizeof(sfvmk_proxyAdminState_t));

  pAdapter->pProxyState = pProxyState;

  /* We need a block for every function, both PF and VF. There is
   * currently no way to determine this at runtime, since it can be
   * reconfigure quite arbitrarily. However, the index in to the various
   * buffers is only 8 bits, so we have an upper bound of 256 entries.
   */
  blockCount = SFVMK_PROXY_AUTH_NUM_BLOCKS;

  /* Allocate per-request storage. */
  pProxyState->pReqState = (sfvmk_proxyReqState_t *)
                           sfvmk_memPoolAlloc(blockCount *
                                              sizeof(sfvmk_proxyReqState_t));
  if (!pProxyState->pReqState) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_memPoolAlloc reqState failed: %s",
                        vmk_StatusToString(status));
    status = VMK_NO_MEMORY;
    goto alloc_req_state_failed;
  }

  /* Allocate three contiguous buffers for receiving requests, returning
   * responses and bookkeeping. These must all be power of 2 sizes.
   */
  requestSize = sfvmk_pow2GE(requestSize);
  responseSize = sfvmk_pow2GE(responseSize);

  status = sfvmk_allocDmaBuffer(pAdapter, &pProxyState->requestBuffer,
                                requestSize * blockCount);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "sfvmk_allocDmaBuffer requestBuffer failed: %s",
                        vmk_StatusToString(status));
    goto request_buffer_alloc_failed;
  }

  status = sfvmk_allocDmaBuffer(pAdapter, &pProxyState->responseBuffer,
                                responseSize * blockCount);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "sfvmk_allocDmaBuffer responseBuffer failed: %s",
                        vmk_StatusToString(status));
    goto response_buffer_alloc_failed;
  }

  status = sfvmk_allocDmaBuffer(pAdapter, &pProxyState->statusBuffer,
                                MC_PROXY_STATUS_BUFFER_LEN * blockCount);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "sfvmk_allocDmaBuffer statusBuffer failed: %s",
                        vmk_StatusToString(status));
    goto status_buffer_alloc_failed;
  }

  /* Create helper world for processing:
   * proxy request events
   * timed out requests
   */
  status = sfvmk_createHelper(pAdapter, "proxy", &pProxyState->proxyHelper);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Proxy helper creation failed");
    goto proxy_helper_creation_failed;
  }

  pProxyState->requestSize = requestSize;
  pProxyState->responseSize = responseSize;
  pProxyState->blockCount = blockCount;
  pProxyState->handledPrivileges = handledPrivileges;
  pProxyState->defaultResult = MC_CMD_PROXY_COMPLETE_IN_DECLINED;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "reqSize: %lu, respSize: %lu, handPriv: 0x%x",
                      requestSize, responseSize, handledPrivileges);

  vmk_Memset(&proxyConfig, 0, sizeof(efx_proxy_auth_config_t));
  proxyConfig.request_bufferp = &pProxyState->requestBuffer;
  proxyConfig.response_bufferp = &pProxyState->responseBuffer;
  proxyConfig.status_bufferp = &pProxyState->statusBuffer;
  proxyConfig.block_cnt = pProxyState->blockCount;
  proxyConfig.op_listp = pOpList;
  proxyConfig.op_count = opCount;
  proxyConfig.handled_privileges = handledPrivileges;

  status = efx_proxy_auth_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_proxy_auth_init failed: %s",
                        vmk_StatusToString(status));
    goto proxy_auth_init_failed;
  }

  status = efx_proxy_auth_configure(pAdapter->pNic, &proxyConfig);

  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_proxy_auth_configure failed: %s",
                        vmk_StatusToString(status));
    goto proxy_auth_configure_failed;
  }

  pProxyState->authState = SFVMK_PROXY_AUTH_STATE_READY;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "Configured proxy auth successfully");

  goto done;

proxy_auth_configure_failed:
  efx_proxy_auth_fini(pAdapter->pNic);

proxy_auth_init_failed:
  sfvmk_destroyHelper(pAdapter, pProxyState->proxyHelper);

proxy_helper_creation_failed:
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                         pProxyState->statusBuffer.pEsmBase,
                         pProxyState->statusBuffer.ioElem.ioAddr,
                         pProxyState->statusBuffer.ioElem.length);

status_buffer_alloc_failed:
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                         pProxyState->responseBuffer.pEsmBase,
                         pProxyState->responseBuffer.ioElem.ioAddr,
                         pProxyState->responseBuffer.ioElem.length);

response_buffer_alloc_failed:
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine,
                         pProxyState->requestBuffer.pEsmBase,
                         pProxyState->requestBuffer.ioElem.ioAddr,
                         pProxyState->requestBuffer.ioElem.length);

request_buffer_alloc_failed:
  sfvmk_memPoolFree((vmk_VA)pProxyState->pReqState,
                     blockCount * sizeof(sfvmk_proxyReqState_t));
  pProxyState->pReqState = NULL;

alloc_req_state_failed:
  vmk_HeapFree(sfvmk_modInfo.heapID, pProxyState);
  pAdapter->pProxyState = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief  Initialize the proxy auth module
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_proxyAuthInit(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_adapter_t *pPrimary = NULL;
  vmk_uint32 reqSize;
  vmk_uint32 respSize;
  vmk_uint32 handledPrivileges;

  vmk_uint32 opList[] = {
    MC_CMD_VADAPTOR_SET_MAC,
    MC_CMD_FILTER_OP,
    MC_CMD_SET_MAC,
  };

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  VMK_ASSERT_NOT_NULL(pAdapter);

  pPrimary = pAdapter->pPrimary;

  if (pPrimary == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Primary adapter NULL");
    goto done;
  }

  sfvmk_MutexLock(pPrimary->secondaryListLock);
  if ((pPrimary->pProxyState == NULL) && pAdapter->numVfsEnabled) {
    reqSize = MAX(MC_CMD_VADAPTOR_SET_MAC_IN_LEN,
                  MAX(MC_CMD_FILTER_OP_EXT_IN_LEN,
                      MC_CMD_SET_MAC_EXT_IN_LEN));

    respSize = MAX(MC_CMD_VADAPTOR_SET_MAC_OUT_LEN,
                   MAX(MC_CMD_FILTER_OP_EXT_OUT_LEN,
                       MC_CMD_SET_MAC_OUT_LEN));

    handledPrivileges = (MC_CMD_PRIVILEGE_MASK_IN_GRP_LINK |
                         MC_CMD_PRIVILEGE_MASK_IN_GRP_CHANGE_MAC |
                         MC_CMD_PRIVILEGE_MASK_IN_GRP_UNICAST |
                         MC_CMD_PRIVILEGE_MASK_IN_GRP_MULTICAST |
                         MC_CMD_PRIVILEGE_MASK_IN_GRP_BROADCAST |
                         MC_CMD_PRIVILEGE_MASK_IN_GRP_ALL_MULTICAST |
                         MC_CMD_PRIVILEGE_MASK_IN_GRP_PROMISCUOUS |
                         MC_CMD_PRIVILEGE_MASK_IN_GRP_UNRESTRICTED_VLAN);

    /*
     * In libefx the payload buffer needs to be big enough to hold a maximum
     * sized request, a maxmimum sized response or an (extended) error.
     * Therefore (sizeof (efx_dword_t)*2) is added to each buffer size.
     */
    status = sfvmk_proxyAuthConfigureList(pPrimary,
                                          (sizeof(efx_dword_t) * 2) + reqSize,
                                          (sizeof(efx_dword_t) * 2) + respSize,
                                          opList, SFVMK_ARRAY_SIZE(opList),
                                          handledPrivileges);
    if ((status != VMK_OK) && (status != VMK_BUSY)) {
      SFVMK_ADAPTER_ERROR(pAdapter, "Proxy auth configurtion failed: %s",
                          vmk_StatusToString(status));
      goto done;
    }
  }

  pAdapter->proxiedVfs = pAdapter->numVfsEnabled;
  status = VMK_OK;

done:
  if (pPrimary)
    sfvmk_MutexUnlock(pPrimary->secondaryListLock);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief  De-initialize the proxy auth module
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: None
*/
void
sfvmk_proxyAuthFini(sfvmk_adapter_t *pAdapter)
{
  sfvmk_proxyAdminState_t *pProxyState = NULL;
  sfvmk_adapter_t *pPrimary = NULL;
  sfvmk_proxyReqState_t *pReqState = NULL;
  vmk_uint32 i = 0;
  vmk_uint32 numCancelled = 0;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  VMK_ASSERT_NOT_NULL(pAdapter);

  pAdapter->proxiedVfs = 0;

  /* Fetch the number of VFs enabled on primary port */
  pPrimary = pAdapter->pPrimary;
  if (pPrimary == NULL) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "Primary adapter not found");
    goto done;
  }

  sfvmk_MutexLock(pPrimary->secondaryListLock);
  pProxyState = pPrimary->pProxyState;

  if (pProxyState == NULL) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "Proxy auth not initialized for this card");
    goto done;
  }

  pProxyState->authState = SFVMK_PROXY_AUTH_STATE_STOPPING;

  /* Cancel all proxy requests (tagged 0) */
  status = vmk_HelperCancelRequest(pProxyState->proxyHelper, 0, &numCancelled);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "vmk_HelperCancelRequest proxy requests failed: %s",
                        vmk_StatusToString(status));
  }

  SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "%u proxy requests cancelled", numCancelled);

  /* Cancel the scheduled timeout requests */
  for (i = 0; i < pProxyState->blockCount; i++) {
    pReqState = &pProxyState->pReqState[i];
    if (pReqState->pProxyEvent != NULL) {
      status = vmk_HelperCancelRequest(pProxyState->proxyHelper,
                                       pReqState->pProxyEvent->requestTag,
                                       &numCancelled);

      if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "vmk_HelperCancelRequest timeout request %u failed: %s",
                          i, vmk_StatusToString(status));
      }

      SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                          "vmk_HelperCancelRequest %lu numCancelled: %u",
                          pReqState->pProxyEvent->requestTag.addr,
                          numCancelled);

    }
  }

  /* Now wait for the worlds processing proxy request to gracefully exit */
  while (vmk_AtomicRead64(&pProxyState->numWorlds)) {
    /* Sleep for 10 milliseconds */
    status = sfvmk_worldSleep(SFVMK_PROXY_CLEANUP_WAIT_USEC);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_WorldSleep failed status: %s",
                          vmk_StatusToString(status));
      /* World is dying */
      break;
    }
  }

  /* Clear the helper queue */
  sfvmk_destroyHelper(pPrimary, pProxyState->proxyHelper);

  efx_proxy_auth_destroy(pPrimary->pNic, pProxyState->handledPrivileges);

  efx_proxy_auth_fini(pPrimary->pNic);

  sfvmk_freeDMAMappedMem(pPrimary->dmaEngine,
                         pProxyState->statusBuffer.pEsmBase,
                         pProxyState->statusBuffer.ioElem.ioAddr,
                         pProxyState->statusBuffer.ioElem.length);

  sfvmk_freeDMAMappedMem(pPrimary->dmaEngine,
                         pProxyState->responseBuffer.pEsmBase,
                         pProxyState->responseBuffer.ioElem.ioAddr,
                         pProxyState->responseBuffer.ioElem.length);

  sfvmk_freeDMAMappedMem(pPrimary->dmaEngine,
                         pProxyState->requestBuffer.pEsmBase,
                         pProxyState->requestBuffer.ioElem.ioAddr,
                         pProxyState->requestBuffer.ioElem.length);

  sfvmk_memPoolFree((vmk_VA)pProxyState->pReqState,
                     pProxyState->blockCount * sizeof(sfvmk_proxyReqState_t));
  pProxyState->pReqState = NULL;

  vmk_HeapFree(sfvmk_modInfo.heapID, pProxyState);
  pPrimary->pProxyState = NULL;

done:
  if (pPrimary)
    sfvmk_MutexUnlock(pPrimary->secondaryListLock);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
}

/*! \brief Fuction to indicate completion of proxy request and
**         convey result to Firmware.
**
** \param[in]  pAdapter            pointer to sfvmk_adapter_t
** \param[in]  pProxyState         pointer to sfvmk_proxyAdminState_t
** \param[in]  index               proxy request index
** \param[in]  pReqState           pointer to sfvmk_proxyReqState_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_proxyAuthSendResponse(sfvmk_adapter_t *pAdapter,
                            sfvmk_proxyAdminState_t *pProxyState,
                            vmk_uint32 index,
                            sfvmk_proxyReqState_t *pReqState)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_proxyMcState_t *pMcState = NULL;
  vmk_uint32 handle;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  /* Free the proxy event for this request as it will be completed here */
  if (pReqState->pProxyEvent) {
    sfvmk_MemFree(pReqState->pProxyEvent);
    pReqState->pProxyEvent = NULL;
  }

  if (vmk_AtomicReadIfEqualWrite64(&pReqState->reqState,
                                   SFVMK_PROXY_REQ_STATE_COMPLETED,
                                   SFVMK_PROXY_REQ_STATE_RESPONDING) !=
                                   SFVMK_PROXY_REQ_STATE_COMPLETED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Response already sent %lu on index %u",
                        vmk_AtomicRead64(&pReqState->reqState), index);

    status = VMK_EALREADY;
    /* Ensure that we catch this situation on debug builds */
    VMK_ASSERT(0);
  } else {
    if (pReqState->result > MC_CMD_PROXY_COMPLETE_IN_TIMEDOUT)
      pReqState->result = MC_CMD_PROXY_COMPLETE_IN_DECLINED;

    pMcState = (sfvmk_proxyMcState_t *)pProxyState->statusBuffer.pEsmBase;
    pMcState += index;
    handle = pMcState->handle;

    SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "Sending result %u for handle %u on index %d",
                        pReqState->result, handle, index);

    pMcState->status = pReqState->result;
    if (pReqState->result == MC_CMD_PROXY_COMPLETE_IN_AUTHORIZED)
      pMcState->grantedPrivileges = pReqState->grantedPrivileges;

    /* Before we tell the MC we've finished we need to stop
     * using the request context, since we may receive another
     * request immediately.
     */
    vmk_AtomicWrite64(&pReqState->reqState, SFVMK_PROXY_REQ_STATE_IDLE);

    status = efx_proxy_auth_complete_request(pAdapter->pPrimary->pNic, index,
                                             pMcState->status, handle);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "efx_proxy_auth_complete_request failed status: %s",
                          vmk_StatusToString(status));
    }
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief Fuction to cancel outstanding delayed requests and
**         send response to Firmware.
**
** \param[in]  pAdapter            pointer to sfvmk_adapter_t
** \param[in]  pProxyState         pointer to sfvmk_proxyAdminState_t
** \param[in]  index               proxy request index
** \param[in]  pReqState           pointer to sfvmk_proxyReqState_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_proxyCompleteRequest(sfvmk_adapter_t *pAdapter,
                           sfvmk_proxyAdminState_t *pProxyState,
                           vmk_uint32 index,
                           sfvmk_proxyReqState_t *pReqState)

{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 numCancelled = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  /* Check and cancel delayed requests for this proxy request */
  status = vmk_HelperCancelRequest(pProxyState->proxyHelper,
                                   pReqState->pProxyEvent->requestTag,
                                   &numCancelled);

  VMK_ASSERT(numCancelled == 1);

  SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "vmk_HelperCancelRequest %lu status %s num: %u",
                      pReqState->pProxyEvent->requestTag.addr,
                      vmk_StatusToString(status), numCancelled);

  status = sfvmk_proxyAuthSendResponse(pAdapter, pProxyState, index, pReqState);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "sfvmk_proxyAuthSendResponse failed status: %s",
                        vmk_StatusToString(status));
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief Fuction to handle ESXi authorization request timeouts
**
** \param[in] data pointer to ProxyEvent structure
**
** \return: None
**
*/
static void
sfvmk_handleRequestTimeout(vmk_AddrCookie data)
{
  sfvmk_proxyEvent_t *pProxyEvent = (sfvmk_proxyEvent_t *)data.ptr;
  sfvmk_proxyReqState_t *pReqState = pProxyEvent->pReqState;
  sfvmk_adapter_t *pAdapter = pProxyEvent->pAdapter;
  sfvmk_proxyAdminState_t *pProxyState = pAdapter->pProxyState;
  vmk_uint32 index = pProxyEvent->index;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  vmk_AtomicInc64(&pProxyState->numWorlds);

  if (vmk_AtomicReadIfEqualWrite64(&pReqState->reqState,
                                   SFVMK_PROXY_REQ_STATE_OUTSTANDING,
                                   SFVMK_PROXY_REQ_STATE_COMPLETED) !=
                                   SFVMK_PROXY_REQ_STATE_OUTSTANDING) {
     SFVMK_ADAPTER_ERROR(pAdapter, "Invalid state %lu on index %u",
                         vmk_AtomicRead64(&pReqState->reqState), index);
  }

  pReqState->result = pProxyState->defaultResult;
  sfvmk_proxyAuthSendResponse(pAdapter, pProxyState, index, pReqState);

  vmk_AtomicDec64(&pProxyState->numWorlds);
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
}

/*! \brief Find device adapter for the given PF index
**
** \param[in]  pAdapter            pointer to sfvmk_adapter_t
** \param[in]  pf                  physical function index
**
** \return: Pointer to sfvmk_adapter_t if found, NULL if not found
**
*/
static sfvmk_adapter_t *
sfvmk_findPfAdapter(sfvmk_adapter_t *pAdapter, vmk_uint32 pf)
{
  sfvmk_adapter_t *pOther = NULL;
  vmk_ListLinks *pLink = NULL;
  vmk_Bool found = VMK_FALSE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);
  if (pAdapter->pfIndex == pf) {
    pOther = pAdapter;
    SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "Primary PF%u matched %s", pf, pAdapter->pciDeviceName.string);
    found = VMK_TRUE;
    goto done;
  }

  sfvmk_MutexLock(pAdapter->secondaryListLock);
  VMK_LIST_FORALL(&pAdapter->secondaryList, pLink) {
    pOther = VMK_LIST_ENTRY(pLink, sfvmk_adapter_t, adapterLink);
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                        "Secondary list entry %s", pOther->pciDeviceName.string);
    if (pOther->pfIndex == pf) {
      SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                          "Secondary PF%u matched %s", pf, pOther->pciDeviceName.string);
      found = VMK_TRUE;
      break;
    }
  }
  sfvmk_MutexUnlock(pAdapter->secondaryListLock);

done:
  if (found == VMK_FALSE)
    pOther = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return pOther;
}

/*! \brief Handler function for VADAPTOR_SET_MAC MC CMD
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  vf          virtual function index
** \param[in]  pCmd        pointer to proxy command received from MC
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_proxyVadaptorSetMac(sfvmk_adapter_t *pAdapter, vmk_uint32 vf,
                          const efx_dword_t *pCmd)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_vfInfo_t *pVf = pAdapter->pVfInfo + vf;
  sfvmk_pendingProxyReq_t *pPpr = &pVf->pendingProxyReq;
  vmk_uint32 portId;
  sfvmk_outbuf_t outbuf;
  sfvmk_inbuf_t inbuf;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  outbuf.emr_out_buf = (vmk_uint8 *)pCmd->ed_u8;
  inbuf.emr_in_buf = (vmk_uint8 *)pCmd->ed_u8;

  portId = MCDI_OUT_DWORD(outbuf, VADAPTOR_SET_MAC_IN_UPSTREAM_PORT_ID);

  if (portId != EVB_PORT_ID_ASSIGNED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid upstream portid 0x%x", portId);
    status = VMK_BAD_PARAM;
    goto done;
  }

  vmk_Memcpy(pPpr->cfg.macAddr,
             MCDI_IN2(inbuf, vmk_uint8, VADAPTOR_SET_MAC_IN_MACADDR),
             sizeof(pPpr->cfg.macAddr));
  pPpr->cfg.cfgChanged = VMK_CFG_MAC_CHANGED;

  pPpr->reqdPrivileges = MC_CMD_PRIVILEGE_MASK_IN_GRP_CHANGE_MAC;
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief Helper function to convert rx mode bitmask to privilege mode bitmask
**
** \param[in]  rxMode      input rx mode value
**
** \return: converted privilege mode bitmask value
**
*/
vmk_uint32
sfvmk_rxModeToPrivMask(vmk_VFRXMode rxMode)
{
  vmk_uint32 privilegeMask = 0;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_PROXY);

  if (rxMode & VMK_VF_RXMODE_UNICAST)
    privilegeMask |= MC_CMD_PRIVILEGE_MASK_IN_GRP_UNICAST;
  if (rxMode & VMK_VF_RXMODE_MULTICAST)
    privilegeMask |= MC_CMD_PRIVILEGE_MASK_IN_GRP_MULTICAST;
  if (rxMode & VMK_VF_RXMODE_BROADCAST)
    privilegeMask |= MC_CMD_PRIVILEGE_MASK_IN_GRP_BROADCAST;
  if (rxMode & VMK_VF_RXMODE_ALLMULTI)
    privilegeMask |= MC_CMD_PRIVILEGE_MASK_IN_GRP_ALL_MULTICAST;
  if (rxMode & VMK_VF_RXMODE_PROMISC)
    privilegeMask |= MC_CMD_PRIVILEGE_MASK_IN_GRP_PROMISCUOUS;

  /* ESXi 5.5 and 6.0 does not grant multicast but grant
   * all-multicast. Consider it as a bug and assume that usage of
   * exact multicast does not violate security requirements.
   */
  if (SFVMK_WORKAROUND_54586 &&
      (privilegeMask & MC_CMD_PRIVILEGE_MASK_IN_GRP_ALL_MULTICAST))
    privilegeMask |= MC_CMD_PRIVILEGE_MASK_IN_GRP_MULTICAST;

  SFVMK_DEBUG(SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
              "RxMode 0x%x privilegeMask 0x%x", rxMode, privilegeMask);

  VMK_ASSERT((privilegeMask & ~SFVMK_EF10_RX_MODE_PRIVILEGE_MASK) == 0);

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_PROXY);
  return privilegeMask;
}

/*! \brief Handler function for FILTER_OP MC CMD
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  vf          virtual function index
** \param[in]  pCmd        pointer to proxy command received from MC
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_proxyFilterOp(sfvmk_adapter_t *pAdapter, vmk_uint32 vf,
                    const efx_dword_t *pCmd)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_vfInfo_t *pVf = pAdapter->pVfInfo + vf;
  sfvmk_pendingProxyReq_t *pPpr = &pVf->pendingProxyReq;
  vmk_uint16 vid;
  vmk_uint32 op;
  vmk_uint32 portId;
  vmk_uint32 matchFields;
  vmk_VFRXMode requestedRxMode;
  const vmk_uint8 *pMac;
  sfvmk_outbuf_t outbuf;
  sfvmk_inbuf_t inbuf;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  outbuf.emr_out_buf = (vmk_uint8 *)pCmd->ed_u8;
  inbuf.emr_in_buf = (vmk_uint8 *)pCmd->ed_u8;

  op = MCDI_OUT_DWORD(outbuf, FILTER_OP_IN_OP);
  portId = MCDI_OUT_DWORD(outbuf, FILTER_OP_IN_PORT_ID);
  matchFields = MCDI_OUT_DWORD(outbuf, FILTER_OP_IN_MATCH_FIELDS);

  SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "VF %d filter_op op=%u fields=0x%x",
                      vf, op, matchFields);

  if ((op != MC_CMD_FILTER_OP_IN_OP_INSERT) &&
      (op != MC_CMD_FILTER_OP_IN_OP_SUBSCRIBE)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "VF %u unexpected filter op 0x%x",
                        vf, op);
    status = VMK_ACCESS_DENIED;
    goto done;
  }

  if (portId != EVB_PORT_ID_ASSIGNED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "VF %u unexpected port id 0x%x",
                        vf, portId);
    status = VMK_ACCESS_DENIED;
    goto done;
  }

  if (matchFields & (1 << MC_CMD_FILTER_OP_IN_MATCH_OUTER_VLAN_LBN)) {
    vid = vmk_BE16ToCPU(MCDI_OUT_DWORD(outbuf, FILTER_OP_IN_OUTER_VLAN));

    if (vid >= SFVMK_MAX_VLANS) {
      SFVMK_ADAPTER_ERROR(pAdapter, "VF %u unexpected vlanid 0x%x",
                          vf, vid);
      status = VMK_BAD_PARAM;
      goto done;
    }

    if (!vmk_BitVectorTest(pVf->pAllowedVlans, vid)) {
      /* Decline immediately if VLAN is already active but not allowed */
      if (vmk_BitVectorTest(pVf->pActiveVlans, vid)) {
        SFVMK_ADAPTER_ERROR(pAdapter, "VF %u vlan %u active but not allowed",
                            vf, vid);
        status = VMK_ACCESS_DENIED;
        goto done;
      }

      pPpr->cfg.vlan.guestVlans[vid >> 3] |= 1 << (vid & 0x7);
      pPpr->cfg.cfgChanged |= VMK_CFG_GUEST_VLAN_ADD;
      SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                          "VF %u request VLAN add %u", vf, vid);
    }
    vmk_BitVectorSet(pVf->pActiveVlans, vid);
    pPpr->reqdPrivileges |= MC_CMD_PRIVILEGE_MASK_IN_GRP_UNRESTRICTED_VLAN;
  }

  switch (matchFields &
          ((1 << MC_CMD_FILTER_OP_IN_MATCH_DST_MAC_LBN) |
           (1 << MC_CMD_FILTER_OP_IN_MATCH_UNKNOWN_MCAST_DST_LBN) |
           (1 << MC_CMD_FILTER_OP_IN_MATCH_UNKNOWN_UCAST_DST_LBN))) {
    case 0:
      /* Filter will be auto-qualified with vPort MAC address
       * (if the function does not have INSECURE_FILTERS privilege)
       */
      requestedRxMode = VMK_VF_RXMODE_UNICAST;
      break;
    case (1 << MC_CMD_FILTER_OP_IN_MATCH_DST_MAC_LBN):
      pMac = MCDI_IN2(inbuf, vmk_uint8, FILTER_OP_IN_DST_MAC);

      /* Check broadcast first since it matches multicast as well */
      if (sfvmk_isBroadcastEtherAddr(pMac))
        requestedRxMode = VMK_VF_RXMODE_BROADCAST;
      else if (EFX_MAC_ADDR_IS_MULTICAST(pMac))
        requestedRxMode = VMK_VF_RXMODE_MULTICAST;
      else
        requestedRxMode = VMK_VF_RXMODE_UNICAST;
      break;

    case (1 << MC_CMD_FILTER_OP_IN_MATCH_UNKNOWN_MCAST_DST_LBN):
      requestedRxMode = VMK_VF_RXMODE_ALLMULTI;
      break;

    case (1 << MC_CMD_FILTER_OP_IN_MATCH_UNKNOWN_UCAST_DST_LBN):
      requestedRxMode = VMK_VF_RXMODE_PROMISC;
      break;

    default:
      SFVMK_ADAPTER_ERROR(pAdapter, "VF %u unexpected filter_op match 0x%x",
                          vf, matchFields);
      status = VMK_ACCESS_DENIED;
      goto done;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "Requested rx mode:0x%x, vf rx mode: 0x%x",
                      requestedRxMode, pVf->rxMode);

  /* ESXi 5.5 and 6.0 do not grant multicast but grant
   * all-multicast. We still would like to use exact multicast
   * filters in this case to avoid the high packet replication
   * cost. So, request all-multicast as well.
   */
  if (SFVMK_WORKAROUND_54586 &&
      (requestedRxMode & VMK_VF_RXMODE_MULTICAST))
    requestedRxMode |= VMK_VF_RXMODE_ALLMULTI;

  pPpr->reqdPrivileges |=
    sfvmk_rxModeToPrivMask(requestedRxMode);

  SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "RequestedRxMode 0x%x, reqdPrivileges: 0x%x",
                      requestedRxMode, pPpr->reqdPrivileges);

  if (requestedRxMode & ~pVf->rxMode) {
    pPpr->cfg.rxMode = pVf->rxMode | requestedRxMode;
    pPpr->cfg.cfgChanged |= VMK_CFG_RXMODE_CHANGED;
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "VF %u request Rx mode 0x%x",
                        vf, pPpr->cfg.rxMode);
  }
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief Function to calculate MAC MTU
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
**
** \return: Maximum MAC MTU across PF and all VFs
**
*/
vmk_uint32
sfvmk_calcMacMtuPf(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 macMtu = EFX_MAC_PDU(pAdapter->uplink.sharedData.mtu);
  sfvmk_vfInfo_t *pVf = pAdapter->pVfInfo;
  int vfIndex;

  for (vfIndex = 0; vfIndex < pAdapter->numVfsEnabled; ++vfIndex)
    macMtu = MAX(macMtu, pVf[vfIndex].macMtu);

  return macMtu;
}

/*! \brief Function used as callback to set VF MTU
**
** \param[in]  pAdapter       pointer to sfvmk_adapter_t
** \param[in]  pf             PF index
** \param[in]  vf             VF index
** \param[in]  pRequestBuff   pointer to buffer carrying request
** \param[in]  requestSize    size of proxy request
** \param[in]  pResponseBuff  pointer to buffer for posting response
** \param[in]  responseSize   size of proxy response
** \param[in]  pContext       pointer to MTU context
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_proxyDoSetMtu(sfvmk_adapter_t *pAdapter, vmk_uint32 pf,
                    vmk_uint32 vf, void* pRequestBuff,
                    size_t requestSize, void* pResponseBuff,
                    size_t responseSize, void *pContext)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  efx_dword_t *pHdr = NULL;
  vmk_uint32 macMtu = 0;
  sfvmk_inbuf_t buf;
  vmk_Bool *pMtuSet = (vmk_Bool *)pContext;
  size_t responseSizeActual = 0;
  efx_proxy_cmd_params_t cmdParams;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  EFX_MCDI_DECLARE_BUF(inbuf,
                       sizeof(efx_dword_t) * 2 + MC_CMD_SET_MAC_EXT_IN_LEN, 0);

  vmk_Memcpy(inbuf, pRequestBuff, sizeof(inbuf));
  pHdr = (efx_dword_t *)inbuf;

  VMK_ASSERT_EQ(EFX_DWORD_FIELD(pHdr[0], MCDI_HEADER_CODE), MC_CMD_V2_EXTN);
  VMK_ASSERT_EQ(EFX_DWORD_FIELD(pHdr[1], MC_CMD_V2_EXTN_IN_EXTENDED_CMD),
                MC_CMD_SET_MAC);

  macMtu = sfvmk_calcMacMtuPf(pAdapter);
  buf.emr_in_buf = (vmk_uint8 *)pHdr[2].ed_u8;
  MCDI_IN_SET_DWORD(buf, SET_MAC_EXT_IN_MTU, macMtu);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "req_size: %lu, resp_size: %lu, mtu: %u",
                      requestSize, responseSize, macMtu);

  cmdParams.pf_index = pf;
  cmdParams.vf_index = vf;
  cmdParams.request_bufferp = inbuf;
  cmdParams.request_size = requestSize;
  cmdParams.response_bufferp = pResponseBuff;
  cmdParams.response_size = responseSize;
  cmdParams.response_size_actualp = &responseSizeActual;

  status = efx_proxy_auth_exec_cmd(pAdapter->pNic, &cmdParams);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_proxy_auth_exec_cmd failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  VMK_ASSERT(responseSizeActual >= sizeof(*pHdr));
  pHdr = (efx_dword_t *)pResponseBuff;
  *pMtuSet = !EFX_DWORD_FIELD(*pHdr, MCDI_HEADER_ERROR);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief Function to validate the proxy auth request handle
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  pProxyState pointer to proxy auth admin state
** \param[in]  uhandle     combined request handle
** \param[out] pHandle     pointer to MC request handle
** \param[out] pIndex      pointer to MC request index
**
** \return: VMK_TRUE if request handle is valid, VMK_FALSE otherwise
**
*/
static vmk_Bool
sfvmk_proxyAuthValidHandle(sfvmk_adapter_t *pAdapter,
                           sfvmk_proxyAdminState_t *pProxyState,
                           vmk_uint64 uhandle,
                           vmk_uint32 *pHandle,
                           vmk_uint32 *pIndex)
{
  sfvmk_proxyMcState_t *pMcState;
  vmk_uint32 i, h;
  vmk_Bool isValidHandle = VMK_FALSE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  /* Split user handle into index and handle. */
  i = (uhandle >> 16) & 0xffff;
  h = (uhandle >> 32) & 0xffffffff;

  if (i >= pProxyState->blockCount) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid index %u", i);
    goto done;
  }

  pMcState = (sfvmk_proxyMcState_t *)pProxyState->statusBuffer.pEsmBase;
  pMcState += i;

  /* Check handle is the one we're expecting. */
  if (pMcState->handle != h) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid handle 0x%x, expected 0x%x",
                        h, pMcState->handle);
    goto done;
  }

  *pIndex = i;
  *pHandle = h;
  isValidHandle = VMK_TRUE;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return isValidHandle;
}

/*! \brief Function to execute proxy request
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  uhandle     proxy request handle
** \param[in]  pDoCb       callback function pointer to run
** \param[in]  pCbContext  parameter for callback function
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_proxyAuthExecuteRequest(sfvmk_adapter_t *pAdapter,
                              vmk_uint64 uhandle,
                              VMK_ReturnStatus (*pDoCb)(sfvmk_adapter_t *,
                                                 vmk_uint32, vmk_uint32,
                                                 void *,
                                                 size_t, void *,
                                                 size_t, void *),
                              void *pCbContext)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_proxyAdminState_t *pProxyState = pAdapter->pProxyState;
  sfvmk_proxyMcState_t *pMcState = NULL;
  sfvmk_proxyReqState_t *pReqState = NULL;
  vmk_int8* pRequestBuff;
  vmk_int8* pResponseBuff;
  vmk_uint32 handle;
  vmk_uint32 index;
  vmk_uint32 pf;
  vmk_uint32 vf;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  VMK_ASSERT_NOT_NULL(pProxyState);
  if (pProxyState->authState != SFVMK_PROXY_AUTH_STATE_READY) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid proxy state %u",
                        pProxyState->authState);
    status = VMK_ESHUTDOWN;
    goto done;
  }

  if (sfvmk_proxyAuthValidHandle(pAdapter, pProxyState, uhandle,
                                  &handle, &index) == VMK_FALSE) {
    status = VMK_BAD_PARAM;
    goto done;
  }

  pReqState = &pProxyState->pReqState[index];

  if (vmk_AtomicReadIfEqualWrite64(&pReqState->reqState,
                                   SFVMK_PROXY_REQ_STATE_OUTSTANDING,
                                   SFVMK_PROXY_REQ_STATE_COMPLETING) !=
                                   SFVMK_PROXY_REQ_STATE_OUTSTANDING) {
     SFVMK_ADAPTER_ERROR(pAdapter, "Invalid state %lu on index %u",
                         vmk_AtomicRead64(&pReqState->reqState), index);
     status = VMK_TIMEOUT;
     goto done;
  }

  pMcState = (sfvmk_proxyMcState_t *)pProxyState->statusBuffer.pEsmBase;
  pMcState += index;

  pf = pMcState->pf;
  vf = pMcState->vf;

  pRequestBuff = pProxyState->requestBuffer.pEsmBase;
  pRequestBuff += pProxyState->requestSize * index;

  pResponseBuff = pProxyState->responseBuffer.pEsmBase;
  pResponseBuff += pProxyState->responseSize * index;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "Callback with req_size: %lu, resp_size: %lu",
                      pProxyState->requestSize, pProxyState->responseSize);

  status = pDoCb(pAdapter->pPrimary, pf, vf, pRequestBuff,
                 pProxyState->requestSize, pResponseBuff,
                 pProxyState->responseSize, pCbContext);

  if (pProxyState->authState != SFVMK_PROXY_AUTH_STATE_READY) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid proxy state %u",
                        pProxyState->authState);
    goto done;
  }

  pReqState->result = (status == VMK_OK) ? MC_CMD_PROXY_COMPLETE_IN_COMPLETE:
                                           MC_CMD_PROXY_COMPLETE_IN_DECLINED;

  vmk_AtomicWrite64(&pReqState->reqState, SFVMK_PROXY_REQ_STATE_COMPLETED);
  status = sfvmk_proxyCompleteRequest(pAdapter, pProxyState, index, pReqState);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_proxyCompleteRequest status: %s",
                        vmk_StatusToString(status));
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief Function to apply VF MTU
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  uhandle     proxy request handle
** \param[in]  pMtuSet     pointer to MTU value set
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_proxyCompleteSetMtu(sfvmk_adapter_t *pAdapter,
                          vmk_uint64 uhandle,
                          vmk_Bool *pMtuSet)
{
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  status = sfvmk_proxyAuthExecuteRequest(pAdapter->pPrimary,
                                          uhandle, sfvmk_proxyDoSetMtu,
                                          (void *)pMtuSet);

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief Handler function for SET_MAC MC CMD
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  vf          virtual function index
** \param[in]  pCmd        pointer to proxy command received from MC
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_proxySetMtu(sfvmk_adapter_t *pAdapter, vmk_uint32 vf,
                  const efx_dword_t *pCmd)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_vfInfo_t *pVf = pAdapter->pVfInfo + vf;
  sfvmk_pendingProxyReq_t *pPpr = &pVf->pendingProxyReq;
  vmk_uint32 macMtu;
  vmk_Bool mtuApplied;
  sfvmk_outbuf_t outbuf;

  outbuf.emr_out_buf = (vmk_uint8 *)pCmd->ed_u8;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  if (MCDI_OUT_DWORD(outbuf, SET_MAC_EXT_IN_CONTROL) !=
      (1 << MC_CMD_SET_MAC_EXT_IN_CFG_MTU_LBN)) {
    status = VMK_NO_ACCESS;
    SFVMK_ADAPTER_ERROR(pAdapter, "Set MTU not allowed");
    goto done;
  }

  macMtu = MCDI_OUT_DWORD(outbuf, SET_MAC_IN_MTU);
  if (macMtu > EFX_MAC_PDU(EFX_MAC_SDU_MAX)) {
    status = VMK_BAD_PARAM;
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid mtu %u passed", macMtu);
    goto done;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "MacMtu: %u  Vf Mtu: %u", macMtu, pVf->macMtu);
  if (macMtu != pVf->macMtu) {
    pPpr->cfg.mtu = macMtu;
    pPpr->cfg.cfgChanged = VMK_CFG_MTU_CHANGED;
    pPpr->reqdPrivileges = MC_CMD_PRIVILEGE_MASK_IN_GRP_LINK;
  } else {
    /* We can't simply authorize since maximum MTU should be applied */
    status = sfvmk_proxyCompleteSetMtu(pAdapter, pPpr->uhandle, &mtuApplied);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "sfvmk_proxyCompleteSetMtu failed status: %s",
                          vmk_StatusToString(status));
    } else {
      /* Either complete or declined */
      pPpr->uhandle = 0;
    }

    SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "uhandle: 0x%lx mtuApplied: %u",
                        pPpr->uhandle, mtuApplied);
  }

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief Function to handle proxy response from ESXi authorization
**
** \param[in]  pAdapter              pointer to sfvmk_adapter_t
** \param[in]  pProxyState           pointer to proxy auth admin state
** \param[in]  uhandle               combined request handle
** \param[in]  result                outcome of the proxy request
** \param[in]  grantedPrivileges     privileges granted for the proxy operation
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_proxyAuthHandleResponse(sfvmk_adapter_t *pAdapter,
                              sfvmk_proxyAdminState_t *pProxyState,
                              vmk_uint64 uhandle,
                              vmk_uint32 result,
                              vmk_uint32 grantedPrivileges)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_proxyReqState_t *pReqState = NULL;
  vmk_uint32 handle;
  vmk_uint32 index;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  VMK_ASSERT_NOT_NULL(pProxyState);

  if (pProxyState->authState != SFVMK_PROXY_AUTH_STATE_READY) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid proxy state %u",
                        pProxyState->authState);
    status = VMK_ESHUTDOWN;
    goto done;
  }

  if (sfvmk_proxyAuthValidHandle(pAdapter, pProxyState, uhandle,
                                  &handle, &index) == VMK_FALSE) {
    status = VMK_BAD_PARAM;
    goto done;
  }

  pReqState = &pProxyState->pReqState[index];

  if (vmk_AtomicReadIfEqualWrite64(&pReqState->reqState,
                                   SFVMK_PROXY_REQ_STATE_OUTSTANDING,
                                   SFVMK_PROXY_REQ_STATE_COMPLETED) !=
                                   SFVMK_PROXY_REQ_STATE_OUTSTANDING) {
     SFVMK_ADAPTER_ERROR(pAdapter, "Invalid state %lu on index %u",
                         vmk_AtomicRead64(&pReqState->reqState), index);
     status = VMK_TIMEOUT;
     goto done;
  }

  pReqState->result = result;
  pReqState->grantedPrivileges = grantedPrivileges;

  status = sfvmk_proxyCompleteRequest(pAdapter, pProxyState, index, pReqState);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "sfvmk_proxyCompleteRequest failed status: %s",
                        vmk_StatusToString(status));
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief Fuction to parse proxy request header to find command from Firmware
**
** \param[in]    pAdapter     pointer to sfvmk_adapter_t
** \param[in]    pHdr         pointer to proxy request header
** \param[out]   pOp          pointer to output operation code
**
** \return: VMK_OK [success] error code [failure]
**
*/
static inline VMK_ReturnStatus
sfvmk_proxyGetExtCommand(sfvmk_adapter_t *pAdapter, const efx_dword_t *pHdr,
                         vmk_uint32 *pOp)
{
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  VMK_ASSERT_NOT_NULL(pOp);

  if (EFX_DWORD_FIELD(pHdr[0], MCDI_HEADER_CODE) != MC_CMD_V2_EXTN) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid MCDI header code: 0x%x",
                        EFX_DWORD_FIELD(pHdr[0], MCDI_HEADER_CODE));
    status = VMK_EOPNOTSUPP;
    goto done;
  }

  *pOp = EFX_DWORD_FIELD(pHdr[1], MC_CMD_V2_EXTN_IN_EXTENDED_CMD);
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief Fuction to parse proxy request and invoke vmk_NetPTConfigureVF
**         to seek ESXi authorization if configuration change is desired.
**
** \param[in]  pAdapter            pointer to sfvmk_adapter_t
** \param[in]  uhandle             request handle
** \param[in]  pf                  physical function index
** \param[in]  vf                  virtual function index
** \param[in]  rid                 request id as received from firmware
** \param[in]  pRequestBuffer      address of request buffer
** \param[in]  requestLen          request length
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_proxyAuthorizeRequest(sfvmk_adapter_t *pAdapter,
                            vmk_uint64 uhandle,
                            vmk_uint16 pf,
                            vmk_uint16 vf,
                            vmk_uint16 rid,
                            void *pRequestBuffer,
                            size_t requestLen)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  const efx_dword_t *pHdr = pRequestBuffer;
  const efx_dword_t *pCmd = pHdr + 2;
  sfvmk_adapter_t *pPfAdapter;
  sfvmk_vfInfo_t *pVfInfo;
  sfvmk_pendingProxyReq_t *pPpr;
  vmk_uint32 op = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "uhandle: 0x%lx pf: %u vf: %u rid: %u",
                      uhandle, pf, vf, rid);

  if (vf == MC_CMD_PROXY_CMD_IN_VF_NULL) {
    /* ESX does not have any policing for unprivileged PF (NIC
     * partitioning case), so allow any traffic, but do not allow
     * link settings which may affect privileged PF.
     */
    const vmk_uint32 pfPrivileges =
      MC_CMD_PRIVILEGE_MASK_IN_GRP_CHANGE_MAC |
      MC_CMD_PRIVILEGE_MASK_IN_GRP_UNICAST |
      MC_CMD_PRIVILEGE_MASK_IN_GRP_MULTICAST |
      MC_CMD_PRIVILEGE_MASK_IN_GRP_BROADCAST |
      MC_CMD_PRIVILEGE_MASK_IN_GRP_ALL_MULTICAST |
      MC_CMD_PRIVILEGE_MASK_IN_GRP_PROMISCUOUS |
      MC_CMD_PRIVILEGE_MASK_IN_GRP_UNRESTRICTED_VLAN;

    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "PF %u immediate proxy authorize with privileges 0x%x",
                        pf, pfPrivileges);

    status = sfvmk_proxyAuthHandleResponse(pAdapter, pAdapter->pProxyState,
                                           uhandle,
                                           MC_CMD_PROXY_COMPLETE_IN_AUTHORIZED,
                                           pfPrivileges);
    goto done;
  }

  if (vf > pAdapter->numVfsEnabled) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid vf index %u", vf);
    status = VMK_FAILURE;
    goto done;
  }

  pPfAdapter = sfvmk_findPfAdapter(pAdapter, pf);
  if (pPfAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_findPfAdapter returned NULL, pf: %u",
                        pf);
    status = VMK_NOT_FOUND;
    goto done;
  }

  if (pPfAdapter->pVfInfo == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Null VF Info pointer");
    status = VMK_FAILURE;
    goto done;
  }

  pVfInfo = pPfAdapter->pVfInfo + vf;
  if (pVfInfo == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid VF info for vf: %u", vf);
    status = VMK_FAILURE;
    goto done;
  }

  /* There should be no pending request at this point as code is
   * reached here transitioning from IDLE -> INCOMING -> OUTSTANDING
   */
  pPpr = &pVfInfo->pendingProxyReq;
  vmk_Memset(pPpr, 0, sizeof(sfvmk_pendingProxyReq_t));
  pPpr->uhandle = uhandle;

  status = sfvmk_proxyGetExtCommand(pAdapter, pHdr, &op);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_proxyGetExtCommand failed: %s",
                        vmk_StatusToString(status));
    goto mcdi_processing_failed;
  }

  SFVMK_ADAPTER_DEBUG(pPfAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "proxy op 0x%x", op);
  switch (op) {
  case MC_CMD_VADAPTOR_SET_MAC:
    status = sfvmk_proxyVadaptorSetMac(pPfAdapter, vf, pCmd);
    break;
  case MC_CMD_FILTER_OP:
    status = sfvmk_proxyFilterOp(pPfAdapter, vf, pCmd);
    break;
  case MC_CMD_SET_MAC:
    status = sfvmk_proxySetMtu(pPfAdapter, vf, pCmd);
    break;
  default:
    status = VMK_EOPNOTSUPP;
    break;
  }

  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pPfAdapter, "Proxy op 0x%x failed status: %s",
                        op, vmk_StatusToString(status));
    goto mcdi_processing_failed;
  }

  if (pPpr->cfg.cfgChanged != 0) {
    SFVMK_DECLARE_MAC_BUF(mac);

    SFVMK_ADAPTER_DEBUG(pPfAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "VF %u vmk_NetPTConfigureVF(cfgChanged=0x%x, "
                        "macAddr=%s, rxMode=0x%x, mtu=%u)",
                        vf, pPpr->cfg.cfgChanged,
                        sfvmk_printMac(pPpr->cfg.macAddr, mac),
                        pPpr->cfg.rxMode, pPpr->cfg.mtu);

    status = vmk_NetPTConfigureVF(&pPpr->cfg, pVfInfo->vfPciDevAddr);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_DEBUG(pPfAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                          "VF %u vmk_NetPTConfigureVF failed: %s",
                          vf, vmk_StatusToString(status));
      goto netpt_configure_vf_failed;
    }
  } else if (pPpr->uhandle == uhandle) {
    SFVMK_ADAPTER_DEBUG(pPfAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "VF %u immediate proxy authorize with privileges 0x%x",
                        vf, pPpr->reqdPrivileges);

    status = sfvmk_proxyAuthHandleResponse(pAdapter, pAdapter->pProxyState,
                                           uhandle,
                                           MC_CMD_PROXY_COMPLETE_IN_AUTHORIZED,
                                           pPpr->reqdPrivileges);
    if (status != VMK_OK)
      goto proxy_auth_handle_response_failed;

    pPpr->uhandle = 0;
  }

  goto done;

proxy_auth_handle_response_failed:
netpt_configure_vf_failed:
mcdi_processing_failed:
  pPpr->cfg.cfgChanged = 0;
  pPpr->uhandle = 0;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief Fuction to execute proxy request
**
** \param[in] data pointer to ProxyEvent structure
**
** \return: None
**
*/

static void
sfvmk_processProxyRequest(vmk_AddrCookie data)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_proxyEvent_t *pProxyEvent = (sfvmk_proxyEvent_t *)data.ptr;
  sfvmk_proxyReqState_t *pReqState = pProxyEvent->pReqState;
  sfvmk_adapter_t *pAdapter = pProxyEvent->pAdapter;
  sfvmk_proxyAdminState_t *pProxyState = pAdapter->pProxyState;
  vmk_uint32 index = pProxyEvent->index;
  sfvmk_proxyMcState_t *pMcState = NULL;
  vmk_uint8 *pRequestBuff = NULL;
  vmk_uint32 handle = 0;
  vmk_uint64 uhandle = 0;
  vmk_uint16 pf = 0, vf = 0, rid = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  vmk_AtomicInc64(&pProxyState->numWorlds);

  VMK_ASSERT(index < pProxyState->blockCount);

  if(index != pReqState - pProxyState->pReqState) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid index %u ReqState %lu",
                        index, pReqState - pProxyState->pReqState);
    goto done;
  }

  if (vmk_AtomicReadIfEqualWrite64(&pReqState->reqState,
                                   SFVMK_PROXY_REQ_STATE_INCOMING,
                                   SFVMK_PROXY_REQ_STATE_OUTSTANDING) !=
                                   SFVMK_PROXY_REQ_STATE_INCOMING) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid req state on index %u", index);
    goto done;
  }

  pMcState = (sfvmk_proxyMcState_t *)pProxyState->statusBuffer.pEsmBase;
  pMcState += index;
  pf = pMcState->pf;
  vf = pMcState->vf;
  rid = pMcState->rid;
  handle = pMcState->handle;

  if (vf == MC_CMD_PROXY_CMD_IN_VF_NULL)
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "Handling req %u on PF %u, PCI %02x.%x\n",
                        handle, pf, (rid >> 3) & 0x1f, rid & 7);
  else
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "Handling req %d on VF %u:%u, PCI %02x.%x\n",
                        handle, pf, vf, (rid >> 3) & 0x1f, rid & 7);

  pRequestBuff = pProxyState->requestBuffer.pEsmBase;
  pRequestBuff += pProxyState->requestSize * index;

  /* Combine the index and handle to an opaque 64 bit value */
  uhandle = (vmk_uint64)handle << 32 | (index & 0xffff) << 16;

  status = sfvmk_proxyAuthorizeRequest(pAdapter, uhandle, pf, vf, rid,
                                       pRequestBuff, pProxyState->requestSize);

  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_proxyAuthorizeRequest %lu failed: %s",
                        pProxyEvent->requestTag.addr,
                        vmk_StatusToString(status));

    pReqState->result = pProxyState->defaultResult;
    if (pProxyState->authState == SFVMK_PROXY_AUTH_STATE_READY) {
      vmk_AtomicWrite64(&pReqState->reqState, SFVMK_PROXY_REQ_STATE_COMPLETED);

      status = sfvmk_proxyCompleteRequest(pAdapter, pProxyState, index, pReqState);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter,
                            "sfvmk_proxyCompleteRequest %lu failed status: %s",
                            pProxyEvent->requestTag.addr,
                            vmk_StatusToString(status));
      }
    }
  }

done:
  vmk_AtomicDec64(&pProxyState->numWorlds);
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return;
}


/*! \brief  Submit a proxy event to helper queue for processing
**
** \param[in]  pProxyEvent  pointer to proxy event to be processed
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_submitProxyRequest(sfvmk_proxyEvent_t *pProxyEvent)
{
  vmk_HelperRequestProps props;
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_adapter_t *pAdapter = pProxyEvent->pAdapter;
  sfvmk_proxyAdminState_t *pProxyState = pAdapter->pProxyState;
  vmk_uint32 numCancelled = 0;
  sfvmk_proxyReqState_t *pReqState = pProxyEvent->pReqState;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  vmk_HelperRequestPropsInit(&props);

  /* Create a request and submit */
  props.requestMayBlock = VMK_FALSE;
  props.tag = (vmk_AddrCookie)NULL;
  props.cancelFunc = NULL;
  props.worldToBill = VMK_INVALID_WORLD_ID;

  /* When timeout handling is required to be cancelled via call to
   * vmk_HelperCancelRequest, requestTag  will uniquely identify
   * request to be cancelled */
  props.tag.addr = vmk_AtomicReadInc64(&sfvmk_modInfo.proxyRequestId);

  /* Creating the timeout handler first avoids the condition when
   * request handler executes to completion and tries to cancel
   * timeout handler before it gets created */
  pProxyEvent->requestTag.addr = props.tag.addr;
  pReqState->pProxyEvent = pProxyEvent;
  status = vmk_HelperSubmitDelayedRequest(pProxyState->proxyHelper,
                                          sfvmk_handleRequestTimeout,
                                          (vmk_AddrCookie *)pProxyEvent,
                                          SFVMK_PROXY_REQ_TIMEOUT_MSEC,
                                          &props);
  if (status != VMK_OK) {
     SFVMK_ADAPTER_ERROR(pAdapter,
                         "vmk_HelperSubmitDelayedRequest proxy failed: %s",
                         vmk_StatusToString(status));
     goto done;
  }

  props.tag.addr = 0;
  status = vmk_HelperSubmitRequest(pProxyState->proxyHelper,
                                   sfvmk_processProxyRequest,
                                   (vmk_AddrCookie *)pProxyEvent,
                                   &props);
  if (status != VMK_OK) {
     SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HelperSubmitRequest proxy failed: %s",
                         vmk_StatusToString(status));
     vmk_HelperCancelRequest(pProxyState->proxyHelper,
                             pProxyEvent->requestTag,
                             &numCancelled);
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
  return status;
}

/*! \brief Iterator used to iterate through the adapter list and find the
 **        Virtual function with the given PCI address
 **
 ** \param[in]     htbl   Hash handle.
 ** \param[in]     key    Hash key.
 ** \param[in]     value  Hash entry value stored at key
 ** \param[in]     data   Pointer to sfvmk_functionInfo_t
 **
 ** \return: Key iterator commands.
 **
 */
static vmk_HashKeyIteratorCmd
sfvmk_functionLookupHashIter(vmk_HashTable htbl,
                             vmk_HashKey key, vmk_HashValue value,
                             vmk_AddrCookie data)
{
  sfvmk_functionInfo_t *pFnInfo = data.ptr;
  vmk_PCIDeviceAddr *pDeviceAddr = &pFnInfo->deviceAddr;
  sfvmk_adapter_t *pIterAdapter = (sfvmk_adapter_t *)value;
  vmk_HashKeyIteratorCmd returnCode;
  sfvmk_vfInfo_t *pVf = NULL;
  vmk_uint32 i = 0;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_PROXY);

  if (!pDeviceAddr || !pIterAdapter) {
    SFVMK_ERROR("Invalid device addr (%p) or adapter list (%p)",
                pDeviceAddr, pIterAdapter);
    returnCode = VMK_HASH_KEY_ITER_CMD_STOP;
    goto done;
  }

  SFVMK_DEBUG(SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
              "Lookup PCI address: %04x:%02x:%02x.%x",
              pDeviceAddr->seg, pDeviceAddr->bus,
              pDeviceAddr->dev, pDeviceAddr->fn);

  for (i = 0; i < pIterAdapter->numVfsEnabled; i++) {
    pVf = pIterAdapter->pVfInfo + i;
    SFVMK_DEBUG(SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                "Comparing VF address: %04x:%02x:%02x.%x",
                pVf->vfPciDevAddr.seg, pVf->vfPciDevAddr.bus,
                pVf->vfPciDevAddr.dev, pVf->vfPciDevAddr.fn);

    if (memcmp(pDeviceAddr, &pVf->vfPciDevAddr,
               sizeof(vmk_PCIDeviceAddr)) == 0) {
      pFnInfo->pf = pIterAdapter->pfIndex;
      pFnInfo->vf = i;
      pFnInfo->pAdapter = pIterAdapter;
      SFVMK_ADAPTER_DEBUG(pIterAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                          "PF: %u, VF: %u found", pFnInfo->pf, pFnInfo->vf);
      returnCode = VMK_HASH_KEY_ITER_CMD_STOP;
      goto done;
    }
  }

  SFVMK_DEBUG(SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
              "Desired VF not found under PF %04x:%02x:%02x.%x",
              pIterAdapter->pciDeviceAddr.seg, pIterAdapter->pciDeviceAddr.bus,
              pIterAdapter->pciDeviceAddr.dev, pIterAdapter->pciDeviceAddr.fn);

  returnCode = VMK_HASH_KEY_ITER_CMD_CONTINUE;

done:
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_PROXY);
  return returnCode;
}

/*! \brief  Retrieve the privilege mask for the device with provided PCI address
**
** \param[in]   pPciAddr     pointer to PCI SBDF address of the device
** \param[out]  pMask        pointer to privilege mask to be returned
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_getPrivilegeMask(vmk_PCIDeviceAddr *pPciAddr,
                       vmk_uint32 *pMask)
{
  VMK_ReturnStatus  status = VMK_FAILURE;
  sfvmk_functionInfo_t fnInfo;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER);

  vmk_Memset(&fnInfo, 0, sizeof(fnInfo));
  vmk_Memcpy(&fnInfo.deviceAddr, pPciAddr, sizeof(vmk_PCIDeviceAddr));
  status = vmk_HashKeyIterate(sfvmk_modInfo.vmkdevHashTable,
                              sfvmk_functionLookupHashIter, &fnInfo);
  if (status != VMK_OK) {
    SFVMK_ERROR("Iterator failed with error code %s",
                vmk_StatusToString(status));
    goto done;
  }

  if (fnInfo.pAdapter == NULL) {
    SFVMK_ERROR("PCI address not found");
    status = VMK_NOT_FOUND;
    goto done;
  }

  status = efx_proxy_auth_privilege_mask_get(fnInfo.pAdapter->pPrimary->pNic,
                                             fnInfo.pf, fnInfo.vf,
                                             pMask);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(fnInfo.pAdapter,
                        "efx_proxy_auth_privilege_mask_get failed: %s",
                        vmk_StatusToString(status));
    goto done;
  }

done:
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_DRIVER);
  return status;
}

/*! \brief  Retrieve the privilege mask for the device with provided PCI address
**
** \param[in]  pPciAddr                 pointer to PCI SBDF address of the device
** \param[in]  add_privileges_mask      privilege mask to be added
** \param[in]  remove_privileges_mask   privilege mask to be removed
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_modifyPrivilegeMask(vmk_PCIDeviceAddr *pPciAddr,
                          vmk_uint32 add_privileges_mask,
                          vmk_uint32 remove_privileges_mask)
{
  VMK_ReturnStatus  status = VMK_FAILURE;
  sfvmk_functionInfo_t fnInfo;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER);

  vmk_Memcpy(&fnInfo.deviceAddr, pPciAddr, sizeof(vmk_PCIDeviceAddr));
  status = vmk_HashKeyIterate(sfvmk_modInfo.vmkdevHashTable,
                              sfvmk_functionLookupHashIter, &fnInfo);
  if (status != VMK_OK) {
    SFVMK_ERROR("Iterator failed with error code %s",
                vmk_StatusToString(status));
    goto done;
  }

  if (fnInfo.pAdapter == NULL) {
    SFVMK_ERROR("PCI address not found");
    status = VMK_NOT_FOUND;
    goto done;
  }

  SFVMK_DEBUG(SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
              "PF: %u, VF: %u, add: 0x%x rem: 0x%x", fnInfo.pf, fnInfo.vf,
              add_privileges_mask, remove_privileges_mask);

  status = efx_proxy_auth_privilege_modify(fnInfo.pAdapter->pPrimary->pNic,
                                           fnInfo.pf, fnInfo.vf,
                                           add_privileges_mask,
                                           remove_privileges_mask);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(fnInfo.pAdapter,
                        "efx_proxy_auth_privilege_modify failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

done:
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_DRIVER);
  return status;
}
#endif

