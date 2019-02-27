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

  /* We need a block for every function, both PF and VF. There is
   * currently no way to determine this at runtime, since it can be
   * reconfigure quite arbitrarily. However, the index in to the various
   * buffers is only 8 bits, so we have an upper bound of 256 entries.
   */
  blockCount = SFVMK_PROXY_AUTH_NUM_BLOCKS;

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

  SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_PROXY, SFVMK_LOG_LEVEL_DBG,
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

  pAdapter->pProxyState = pProxyState;

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

  vmk_HeapFree(sfvmk_modInfo.heapID, pProxyState);
  pPrimary->pProxyState = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PROXY);
}

#endif
