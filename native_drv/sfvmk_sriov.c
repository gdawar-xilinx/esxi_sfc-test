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

static vmk_uint32 sfvmk_sriovPfCount = 0;

/* VMware requires max_vfs module parameter for SR-IOV configuration.
 * It's name cannot be changed and VMKernel ensures that no more than
 * max_vfs array size entries are written to it at module load time */
vmk_int32 max_vfs[SFVMK_MAX_PFS];

/*! \brief  Function to increment number of PFs detected during driver load
**
** \return: none
*/
inline void
sfvmk_sriovIncrementPfCount(void)
{
  sfvmk_sriovPfCount ++;
}

/*! \brief  Function to decrement number of PFs on driver unload
**
** \return: none
*/
inline void
sfvmk_sriovDecrementPfCount(void)
{
  sfvmk_sriovPfCount --;
}

/*! \brief  Function to read max_vfs array and initialize
**          maxVfs for current adapter.
**
** \param[in]   pAdapter pointer to sfvmk_adapter_t
** \param[out]  pMaxVfs  maximum allowed vfs on this adapter
**
** \return: none
*/
static void
sfvmk_sriovInitMaxVfs(sfvmk_adapter_t *pAdapter, vmk_uint32 *pMaxVfs)
{
  vmk_uint32 idx = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pMaxVfs);

  if (modParams.maxVfsCount == 1)
    /* If there is only one entry in max_vfs array,
     * use it for all NICs for backward compatibility.
     */
    idx = 0;
  else
    idx = sfvmk_sriovPfCount - 1;

  if ((idx >= modParams.maxVfsCount) ||
      (max_vfs[idx] < 0) ||
      (max_vfs[idx] > SFVMK_MAX_VFS_PER_PF))
     *pMaxVfs = SFVMK_MAX_VFS_DEFAULT;
  else
     *pMaxVfs = max_vfs[idx];

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "Index: %u, modParams.maxVfsCount: %u maxVfs: %u",
                      idx, modParams.maxVfsCount, *pMaxVfs);
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
}

/*! \brief  Generate a MAC address for given Virtual Function
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  vfIndex  VF index
** \param[in]  pPfMac   pointer to PF MAC address
** \param[out] pVfMac   pointer to VF MAC address
**
** \return: none
*/
static void
sfvmk_generateVfEthAddress(sfvmk_adapter_t *pAdapter,
                           vmk_uint8 vfIndex,
                           vmk_uint8 *pPfMac,
                           vmk_uint8 *pVfMac)
{
   pVfMac[5] = vfIndex;
   pVfMac[4] = (vmk_uint8)((pAdapter->pciDeviceAddr.dev << 3) |
                            pAdapter->pciDeviceAddr.fn);
   pVfMac[3] = (vmk_uint8)(pAdapter->pciDeviceAddr.bus);

   /* Use the OUI from the PF MAC address */
   vmk_Memcpy(pVfMac, pPfMac, 3);
}

/*! \brief  Allocate the port configuration and setup the EVB switch
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_evbSwitchInit(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  const efx_nic_cfg_t *pNicCfg = NULL;
  vmk_uint32 i = 0;
  SFVMK_DECLARE_MAC_BUF(mac);
  efx_vport_config_t *pConfig = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);
  if (!pAdapter->numVfsEnabled) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                        "No VFs enabled");
    status = VMK_OK;
    goto done;
  }

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_cfg_get failed");
    goto done;
  }

  status = efx_evb_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_evb_init failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Populate the MAC address in vport configuration as it gets cleared
   * during quiesceIO */
  vmk_Memcpy(pAdapter->pVportConfig->evc_mac_addr, pNicCfg->enc_mac_addr,
             EFX_MAC_ADDR_LEN);

  /* Fill in the VF details starting at index 1 */
  for (i = 1; i <= pAdapter->numVfsEnabled; i++) {
    pConfig = pAdapter->pVportConfig + i;
    sfvmk_generateVfEthAddress(pAdapter, pConfig->evc_function,
                               pAdapter->pVportConfig->evc_mac_addr,
                               pConfig->evc_mac_addr);
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                        "VF[%u] MAC: %s", pConfig->evc_function,
                        sfvmk_printMac(pConfig->evc_mac_addr, mac));
  }


  status = efx_evb_vswitch_create(pAdapter->pNic,
                                  pAdapter->numVfsEnabled + 1,
                                  pAdapter->pVportConfig, &pAdapter->pVswitch);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_evb_vswitch_create failed status: %s",
                        vmk_StatusToString(status));
    goto vswitch_create_failed;
  }

  goto done;

vswitch_create_failed:
  efx_evb_fini(pAdapter->pNic);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  Free the port configuration and destroy the EVB switch
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_evbSwitchFini(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  if (!pAdapter->numVfsEnabled) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                        "No VFs enabled");
    status = VMK_OK;
    goto done;
  }

  status = efx_evb_vswitch_destroy(pAdapter->pNic, pAdapter->pVswitch);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "efx_evb_vswitch_destroy failed status: %s",
                        vmk_StatusToString(status));
  }

  efx_evb_fini(pAdapter->pNic);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  Determine the number of VFs to be enabled and
**          call vmk_PCIEnableVF to enable them.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_sriovInit(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 maxVfs = 0;
  vmk_uint32 totalVFs = 0;
  vmk_uint16 numVfs = 0;
  vmk_uint16 capOffset = 0;
  sfvmk_vfInfo_t *pVfInfo = NULL;
  efx_vport_config_t *pVportConfig = NULL;
  efx_vport_config_t *pConfig = NULL;
  vmk_uint32 i = 0;
  vmk_uint32 activeCnt = 0;
  vmk_uint32 allowedCnt = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  vmk_ListInit(&pAdapter->secondaryList);
  vmk_ListInitElement(&pAdapter->adapterLink);

  /* Description of max_vfs module parameter must be updated
   * if following static assert fails */
  EFX_STATIC_ASSERT(SFVMK_MAX_VFS_PER_PF == 63);
  VMK_ASSERT_NOT_NULL(pAdapter);

  sfvmk_sriovInitMaxVfs(pAdapter, &maxVfs);
  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "maxVfs: %u", maxVfs);

  status = vmk_PCIFindExtCapability(pAdapter->pciDevice,
                                    SFVMK_SRIOV_EXT_CAP_ID, &capOffset);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIFindExtCapability failed status %s",
                        vmk_StatusToString(status));
    goto done;
  }

  capOffset += SFVMK_TOTAL_VFS_OFFSET;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "TotalVFs offset: 0x%x", capOffset);

  status = vmk_PCIReadConfig(vmk_ModuleCurrentID,
                             pAdapter->pciDevice,
                             VMK_PCI_CONFIG_ACCESS_16,
                             capOffset, &totalVFs);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIReadConfig failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  numVfs = MIN(totalVFs, maxVfs);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "TotalVFs value: %u, numVFs: %u", totalVFs, numVfs);

  if (numVfs) {
    status = vmk_PCIEnableVFs(pAdapter->pciDevice, &numVfs);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIEnableVFs [%u] failed status: %s",
                          numVfs, vmk_StatusToString(status));
      goto done;
    }
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                        "Enabled vfs: %u", numVfs);
  }

  pAdapter->numVfsEnabled = numVfs;

  if (pAdapter->numVfsEnabled) {
    /* Allocate VF info structure */
    pVfInfo = (sfvmk_vfInfo_t *)sfvmk_memPoolAlloc(pAdapter->numVfsEnabled *
                                                   sizeof(sfvmk_vfInfo_t));
    if (!pVfInfo) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "sfvmk_memPoolAlloc vfInfo failed status: %s",
                          vmk_StatusToString(status));
      status = VMK_NO_MEMORY;
      goto mem_alloc_failed;
    }

    pAdapter->pVfInfo = pVfInfo;

    for (i = 0; i < pAdapter->numVfsEnabled; i++) {
      pVfInfo = pAdapter->pVfInfo + i;
      pVfInfo->pAllowedVlans = vmk_BitVectorAlloc(sfvmk_modInfo.heapID,
                                                  SFVMK_MAX_VLANS);
      if (pVfInfo->pAllowedVlans == NULL) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Allowed Vlan bitvector alloc failed");
        goto allowed_bitvector_alloc_failed;
      }
      ++allowedCnt;

      pVfInfo->pActiveVlans = vmk_BitVectorAlloc(sfvmk_modInfo.heapID,
                                                 SFVMK_MAX_VLANS);
      if (pVfInfo->pActiveVlans == NULL) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Active Vlan bitvector alloc failed");
        goto active_bitvector_alloc_failed;
      }
      ++activeCnt;
    }

    /* Allocate vport config structure */
    pVportConfig = (efx_vport_config_t *)
                   sfvmk_memPoolAlloc((pAdapter->numVfsEnabled + 1) *
                                       sizeof(efx_vport_config_t));
    if (!pVportConfig) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "sfvmk_memPoolAlloc pVportConfig failed status: %s",
                          vmk_StatusToString(status));
      status = VMK_NO_MEMORY;
      goto config_mem_alloc_failed;
    }

    /* Fill in the PF details at index 0 */
    pVportConfig->evc_function = 0xffff;

    /* Fill in the VF details starting at index 1 */
    for (i = 1; i <= pAdapter->numVfsEnabled; i++) {
      pConfig = pVportConfig + i;
      pConfig->evc_function = i - 1;
      pConfig->evc_vlan_restrict = VMK_TRUE;
      pConfig->evc_vid = EFX_VF_VID_DEFAULT;
    }

    pAdapter->pVportConfig = pVportConfig;
  }

  goto done;

config_mem_alloc_failed:
active_bitvector_alloc_failed:
allowed_bitvector_alloc_failed:

  for (i = 0; i < activeCnt; i++) {
    pVfInfo = pAdapter->pVfInfo + i;
    vmk_BitVectorFree(sfvmk_modInfo.heapID, pVfInfo->pActiveVlans);
  }

  for (i = 0; i < allowedCnt; i++) {
    pVfInfo = pAdapter->pVfInfo + i;
    vmk_BitVectorFree(sfvmk_modInfo.heapID, pVfInfo->pAllowedVlans);
  }

  sfvmk_memPoolFree((vmk_VA)pAdapter->pVfInfo,
                    pAdapter->numVfsEnabled * sizeof(sfvmk_vfInfo_t));
  pAdapter->pVfInfo = NULL;

mem_alloc_failed:
  if (pAdapter->numVfsEnabled) {
    status = vmk_PCIDisableVFs(pAdapter->pciDevice);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIDisableVFs failed status: %s",
                          vmk_StatusToString(status));
    }
    pAdapter->numVfsEnabled = 0;
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  SR-IOV clean-up routine: De-initialize
**          everything initialized by sriovInit.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_sriovFini(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_vfInfo_t *pVfInfo = NULL;
  vmk_uint32 i = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  VMK_ASSERT_NOT_NULL(pAdapter);

  if (pAdapter->numVfsEnabled) {
    /* Free vport config structure */
    if (pAdapter->pVportConfig) {
      sfvmk_memPoolFree((vmk_VA)pAdapter->pVportConfig,
                        (pAdapter->numVfsEnabled + 1) *
                         sizeof(efx_vport_config_t));
      pAdapter->pVportConfig = NULL;
    }

    if (pAdapter->pVfInfo) {
      for (i = 0; i < pAdapter->numVfsEnabled; i++) {
        pVfInfo = pAdapter->pVfInfo + i;
        vmk_BitVectorFree(sfvmk_modInfo.heapID,
                          pVfInfo->pActiveVlans);
        vmk_BitVectorFree(sfvmk_modInfo.heapID,
                          pVfInfo->pAllowedVlans);
      }

      sfvmk_memPoolFree((vmk_VA)pAdapter->pVfInfo,
                        pAdapter->numVfsEnabled * sizeof(sfvmk_vfInfo_t));
      pAdapter->pVfInfo = NULL;
    }

    status = vmk_PCIDisableVFs(pAdapter->pciDevice);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIDisableVFs failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }

    pAdapter->numVfsEnabled = 0;
  }

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief Function used as callback to set VF MAC address
**
** \param[in]  pAdapter       pointer to sfvmk_adapter_t
** \param[in]  pfIdx          PF index
** \param[in]  vfIdx          VF index
** \param[in]  pRequestBuff   pointer to buffer carrying request
** \param[in]  requestSize    size of proxy request
** \param[in]  pResponseBuff  pointer to buffer for posting response
** \param[in]  responseSize   size of proxy response
** \param[in]  pContext       pointer to MAC address set context
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_proxyDoSetMac(sfvmk_adapter_t *pAdapter, vmk_uint32 pfIdx,
                    vmk_uint32 vfIdx, void* pRequestBuff,
                    size_t requestSize, void* pResponseBuff,
                    size_t responseSize, void *pContext)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  efx_dword_t *pHdr;
  vmk_Bool *pDone = (vmk_Bool *)pContext;
  size_t responseSizeActual = 0;
  efx_proxy_cmd_params_t cmdParams;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  cmdParams.pf_index = pfIdx;
  cmdParams.vf_index = vfIdx;
  cmdParams.request_bufferp = pRequestBuff;
  cmdParams.request_size = requestSize;
  cmdParams.response_bufferp = pResponseBuff;
  cmdParams.response_size = responseSize;
  cmdParams.response_size_actualp = &responseSizeActual;

  status = efx_proxy_auth_exec_cmd(pAdapter->pNic, &cmdParams);
  if (status) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_proxy_auth_exec_cmd failed, status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  VMK_ASSERT(responseSizeActual >= sizeof(*pHdr));

  pHdr = (efx_dword_t *)pResponseBuff;
  *pDone = !EFX_DWORD_FIELD(*pHdr, MCDI_HEADER_ERROR);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  Function to handle ESXi response to set VF MAC address request
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  vfIdx    VF index
** \param[in]  pMac     VF MAC address to be set
**
** \return: VMK_TRUE if mac address was set, VMK_FALSE otherwise
*/
vmk_Bool
sfvmk_proxyHandleResponseSetMac(sfvmk_adapter_t *pAdapter,
                                vmk_uint32 vfIdx,
                                const vmk_uint8 *pMac)
{
  sfvmk_vfInfo_t *pVf = pAdapter->pVfInfo + vfIdx;
  sfvmk_pendingProxyReq_t *pPpr = &pVf->pendingProxyReq;
  vmk_Bool macSet = VMK_FALSE;
  vmk_Bool match = VMK_FALSE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  if ((pPpr->cfg.cfgChanged & VMK_CFG_MAC_CHANGED) == 0) {
    SFVMK_ADAPTER_ERROR(pAdapter, "MAC change not requested [0x%x]",
                        pPpr->cfg.cfgChanged);
    goto done;
  }

  pPpr->cfg.cfgChanged = 0;
  match = sfvmk_macAddrSame(pMac, pPpr->cfg.macAddr);
  if (match) {
    /* VF has requested MAC change, so driver is ready to do
     * it, complete request using PROXY_CMD to know status.
     */
    sfvmk_proxyAuthExecuteRequest(pAdapter->pPrimary, pPpr->uhandle,
                                   sfvmk_proxyDoSetMac, (void *)&macSet);
  } else {
    sfvmk_proxyAuthHandleResponse(pAdapter, pAdapter->pPrimary->pProxyState,
                                  pPpr->uhandle,
                                  MC_CMD_PROXY_COMPLETE_IN_DECLINED, 0);
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return macSet;
}

/*! \brief  VF_SET_MAC net passthrough operation handler
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  vfIdx    VF index
** \param[in]  pMac     VF MAC address to be set
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_sriovSetVfMac(sfvmk_adapter_t *pAdapter, vmk_uint32 vfIdx,
                    vmk_uint8 *pMac)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  const efx_nic_cfg_t *pNicCfg = NULL;
  efx_vport_config_t *pConfig = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  pConfig = pAdapter->pVportConfig + vfIdx + 1;

  if (sfvmk_macAddrSame(pMac, pConfig->evc_mac_addr)) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                        "MAC address unchanged");
    status = VMK_OK;
    goto done;
  }

  if (sfvmk_proxyHandleResponseSetMac(pAdapter, vfIdx, pMac) == VMK_TRUE) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                        "sfvmk_proxyHandleResponseSetMac returned true");
    status = VMK_OK;
    goto done;
  }

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_cfg_get failed");
    goto done;
  }

  if (pNicCfg->enc_vport_reconfigure_supported == VMK_FALSE) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Firmware doesn't support vport reconfigure");
    status = VMK_NOT_SUPPORTED;
    goto done;
  }

  status = efx_evb_vport_mac_set(pAdapter->pNic, pAdapter->pVswitch,
                                 SFVMK_VF_VPORT_ID(pAdapter, vfIdx), pMac);
  if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_evb_vport_mac_set failed: %s",
                          vmk_StatusToString(status));
      goto done;
    }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "VF %u has been reset to reconfigure MAC", vfIdx);
done:
  /* Successfully reconfigured */
  if (status == VMK_OK)
    vmk_Memcpy(pConfig->evc_mac_addr, pMac, VMK_ETH_ADDR_LENGTH);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  VMKernel invoked callback function to configure VF properties.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  op       operation to be performed
** \param[in]  pArgs    operation specific arguments
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus
sfvmk_vfConfigOps(sfvmk_adapter_t *pAdapter, vmk_NetPTOP op, void *pArgs)
{
  VMK_ReturnStatus status = VMK_OK;
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "sfvmk_vfConfigOps PTOP: 0x%x called", op);

  switch (op) {
    case VMK_NETPTOP_VF_SET_MAC:
    {
      vmk_NetPTOPVFSetMacArgs *pVfArgs = pArgs;
      SFVMK_DECLARE_MAC_BUF(mac);

      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                          "(PT) VF %u set mac %s", pVfArgs->vf,
                          sfvmk_printMac(pVfArgs->mac, mac));

      status = sfvmk_sriovSetVfMac(pAdapter, pVfArgs->vf, pVfArgs->mac);
      break;
    }

    default:
      SFVMK_ADAPTER_ERROR(pAdapter, "PTOP %d unhandled", op);
  }

  SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_vfConfigOps PTOP: 0x%x status: %s", op,
                      vmk_StatusToString(status));

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  VMKernel invoked callback function to unregister VF.
**
** \param[in]  vfPciDev VF VMK PCI device handle
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus
sfvmk_removeVfDevice(vmk_PCIDevice vfPciDev)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_Name pciDeviceName;
  vmk_PCIDeviceAddr vfPciDevAddr;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_SRIOV);

  status = vmk_PCIQueryDeviceAddr(vfPciDev, &vfPciDevAddr);
  if (status != VMK_OK) {
    SFVMK_ERROR("vmk_PCIQueryDeviceAddr failed status: %s",
                 vmk_StatusToString(status));
  }

  status = vmk_StringFormat(pciDeviceName.string,
		  sizeof(pciDeviceName.string),
		  NULL, "%04x:%02x:%02x.%x",
		  vfPciDevAddr.seg,
		  vfPciDevAddr.bus,
		  vfPciDevAddr.dev,
		  vfPciDevAddr.fn);

  SFVMK_DEBUG(SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
		  "vmk_PCIUnregisterVF vfPciDev: %s", pciDeviceName.string);


  status = vmk_PCIUnregisterVF(vfPciDev);
  if (status != VMK_OK) {
     SFVMK_ERROR("VF device un-register failed: %s",
                 vmk_StatusToString(status));
  }

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  Reset the VF info structure members to default values.
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
** \param[in]  pVf       pointer to VF info structure
**
** \return: none
*/
static void
sfvmk_vfInfoReset(sfvmk_adapter_t *pAdapter, sfvmk_vfInfo_t *pVf)
{
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  pVf->rxMode = 0;
  pVf->macMtu = EFX_MAC_PDU(pAdapter->uplink.sharedData.mtu);
  vmk_Memset(&pVf->pendingProxyReq, 0, sizeof(sfvmk_pendingProxyReq_t));
  vmk_BitVectorZap(pVf->pActiveVlans);
  vmk_BitVectorZap(pVf->pAllowedVlans);
  vmk_BitVectorSet(pVf->pAllowedVlans, 0);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
}

/********vmk_PCIVFDeviceOps Handler********/
static vmk_PCIVFDeviceOps sfvmk_VfDevOps = {
   .removeVF = sfvmk_removeVfDevice
};

/*! \brief  Function invoked from scanDevice callback to
**          register VFs of Network device.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
*/

VMK_ReturnStatus
sfvmk_registerVFs(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_vfInfo_t *pVfInfo = NULL;
  vmk_AddrCookie vfConfigOps;
  vmk_uint32 i = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  VMK_ASSERT_NOT_NULL(pAdapter);

  if (!pAdapter->numVfsEnabled) {
    SFVMK_ADAPTER_ERROR(pAdapter, "VFs not enabled on this adapter");
    status = VMK_BAD_PARAM;
    goto done;
  }

  vfConfigOps.ptr = &sfvmk_vfConfigOps;

  for (i = 0; i < pAdapter->numVfsEnabled; i++) {
    pVfInfo = pAdapter->pVfInfo + i;

    status = vmk_PCIGetVFPCIDevice(pAdapter->pciDevice, i,
                                   &pVfInfo->vfPciDev);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "vmk_PCIGetVFPCIDevice[%u] failed status: %s",
                          i, vmk_StatusToString(status));
      goto done;
    }

    status = vmk_PCIRegisterVF(pVfInfo->vfPciDev,
                               pAdapter->pciDevice,
                               sfvmk_modInfo.driverID,
                               &sfvmk_VfDevOps);

    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIRegisterVF[%u] failed status: %s",
                          i, vmk_StatusToString(status));
      goto done;
    }

    status = vmk_PCIQueryDeviceAddr(pVfInfo->vfPciDev,
                                    &pVfInfo->vfPciDevAddr);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "vmk_PCIQueryDeviceAddr[%u] failed status: %s",
                          i, vmk_StatusToString(status));
      goto query_addr_failed;
    }

    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                        "vmk_PCIRegisterVF vfPciDev%u: %04x:%02x:%02x.%x", i,
                        pVfInfo->vfPciDevAddr.seg, pVfInfo->vfPciDevAddr.bus,
                        pVfInfo->vfPciDevAddr.dev, pVfInfo->vfPciDevAddr.fn);

    vmk_PCISetVFPrivateData(pVfInfo->vfPciDev, vfConfigOps);
    sfvmk_vfInfoReset(pAdapter, pVfInfo);
  }

  goto done;

query_addr_failed:
  /* We can't call vmk_PCIUnregisterVF here as VMware restricts
   * calling it from any context other than directly from the
   * deviceOps->removeDevice callback */

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}


#endif
