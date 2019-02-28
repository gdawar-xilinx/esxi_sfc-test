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

  pAdapter->proxiedVfs = pAdapter->numVfsEnabled;
  pPrimary = pAdapter->pPrimary;

  if (pPrimary == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Primary adapter NULL");
    goto done;
  }

  if (pPrimary->pProxyState) {
    status = VMK_OK;
    goto done;
  }

  if (pAdapter->numVfsEnabled ||
      ((pAdapter == pPrimary) && pPrimary->numVfsEnabled)) {
    status = efx_proxy_auth_init(pPrimary->pNic);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_proxy_auth_init failed: %s",
                          vmk_StatusToString(status));
      goto done;
    }

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
      goto proxy_auth_config_failed;
    }
  }

  status = VMK_OK;
  goto done;

proxy_auth_config_failed:
  efx_proxy_auth_fini(pPrimary->pNic);

done:
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
  sfvmk_adapter_t *pOther = NULL;
  vmk_ListLinks   *pLink = NULL;
  vmk_uint32      numProxiedVfs = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PROXY);

  VMK_ASSERT_NOT_NULL(pAdapter);

  /* Fetch the number of VFs enabled on primary port */
  pPrimary = pAdapter->pPrimary;
  pProxyState = pPrimary->pProxyState;
  numProxiedVfs = pPrimary->proxiedVfs;

  if (pProxyState == NULL) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "Proxy auth not initialized for this card");
    goto done;
  }

  if (pProxyState->authState != SFVMK_PROXY_AUTH_STATE_READY) {
    SFVMK_ADAPTER_ERROR(pAdapter,"Invalid proxy auth state: %u",
                        pProxyState->authState);
    goto done;
  }

  /* May be secondary port needs proxy */
  VMK_LIST_FORALL(&pPrimary->secondaryList, pLink) {
    pOther = VMK_LIST_ENTRY(pLink, sfvmk_adapter_t, adapterLink);
    numProxiedVfs += pOther->proxiedVfs;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "NumProxiedVfs total: %u, current: %u",
                      numProxiedVfs, pAdapter->proxiedVfs);

  /* See if this is last port for which proxy auth is enabled */
  if (numProxiedVfs - pAdapter->proxiedVfs != 0) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                        "Proxy auth currently being used");
    pAdapter->proxiedVfs = 0;
    goto done;
  }

  efx_proxy_auth_destroy(pPrimary->pNic, pProxyState->handledPrivileges);

  efx_proxy_auth_fini(pPrimary->pNic);

  sfvmk_destroyHelper(pPrimary, pProxyState->proxyHelper);

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

    status = efx_proxy_auth_complete_request(pAdapter->pNic, index,
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

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "pProxyEvent: %p, pAdapter: %p, index: %u, pReqState: %p",
                      pProxyEvent, pAdapter, index, pReqState);

  if (vmk_AtomicReadIfEqualWrite64(&pReqState->reqState,
                                   SFVMK_PROXY_REQ_STATE_OUTSTANDING,
                                   SFVMK_PROXY_REQ_STATE_COMPLETED) !=
                                   SFVMK_PROXY_REQ_STATE_OUTSTANDING) {
     SFVMK_ADAPTER_ERROR(pAdapter, "Invalid state %lu on index %u",
                         vmk_AtomicRead64(&pReqState->reqState), index);
  }

  pReqState->result = pProxyState->defaultResult;
  sfvmk_proxyAuthSendResponse(pAdapter, pProxyState, index, pReqState);

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
static VMK_ReturnStatus
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
    /* TODO: implementation */
    break;
  case MC_CMD_SET_MAC:
    /* TODO: implementation */
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

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
                      "pProxyEvent: %p, index: %u, reqId: %lu",
                      pProxyEvent, index, pProxyEvent->requestTag.addr);

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
#endif
