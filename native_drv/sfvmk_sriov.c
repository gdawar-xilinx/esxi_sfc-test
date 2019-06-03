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
    status = VMK_BAD_PARAM;
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

  pAdapter->evbState = SFVMK_EVB_STATE_STARTED;
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
    status = VMK_BAD_PARAM;
    goto done;
  }

  status = efx_evb_vswitch_destroy(pAdapter->pNic, pAdapter->pVswitch);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "efx_evb_vswitch_destroy failed status: %s",
                        vmk_StatusToString(status));
  }

  efx_evb_fini(pAdapter->pNic);
  pAdapter->evbState = SFVMK_EVB_STATE_STOPPED;

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

  status = sfvmk_mutexInit("secondaryListLock", &pAdapter->secondaryListLock);
  if(status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_mutexInit failed status %s",
                        vmk_StatusToString(status));
    goto done;
  }

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
    goto pci_find_ext_cap_failed;
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
    goto pci_read_config_failed;
  }

  numVfs = MIN(totalVFs, maxVfs);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "TotalVFs value: %u, numVFs: %u", totalVFs, numVfs);

  if (numVfs) {
    status = vmk_PCIEnableVFs(pAdapter->pciDevice, &numVfs);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIEnableVFs [%u] failed status: %s",
                          numVfs, vmk_StatusToString(status));
      goto pci_enable_vfs_failed;
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
    pVportConfig->evc_vid = EFX_VF_VID_DEFAULT;

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

pci_enable_vfs_failed:
pci_read_config_failed:
pci_find_ext_cap_failed:
  sfvmk_mutexDestroy(pAdapter->secondaryListLock);
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

  sfvmk_mutexDestroy(pAdapter->secondaryListLock);
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
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

/*! \brief  VF_SET_DEFAULT_VLAN net passthrough operation handler
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  vfIdx    VF index
** \param[in]  vid      default vlan id to set
** \param[in]  qos      Vlan priority value
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_sriovSetVfVlan(sfvmk_adapter_t *pAdapter, vmk_uint32 vfIdx,
                     vmk_uint16 vid, vmk_uint8 qos)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  const efx_nic_cfg_t *pNicCfg = NULL;
  vmk_uint16 newVlan;
  efx_vport_config_t *pConfig = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

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

  newVlan = (vid == 0) ? EFX_FILTER_VID_UNSPEC : vid;
  if ((vid & ~SFVMK_VLAN_VID_MASK) ||
      (qos & ~(SFVMK_VLAN_PRIO_MASK >> SFVMK_VLAN_PRIO_SHIFT))) {
      SFVMK_ADAPTER_ERROR(pAdapter, "Invalid vlanid: %u or qos: %u", vid, qos);
      status = VMK_BAD_PARAM;
      goto done;
  }

  pConfig = pAdapter->pVportConfig + vfIdx + 1;
  if (pConfig->evc_vid == newVlan) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                        "VF %u VLAN id %u unchanged", vfIdx, newVlan);
    status = VMK_OK;
    goto done;
  }

  status = efx_evb_vport_vlan_set(pAdapter->pNic, pAdapter->pVswitch,
                                  SFVMK_VF_VPORT_ID(pAdapter, vfIdx),
                                  newVlan);
  if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_evb_vport_vlan_set failed: %s",
                          vmk_StatusToString(status));
      goto done;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "VF %u has been reset to reconfigure VLAN", vfIdx);
done:
  /* Successfully reconfigured */
  if (status == VMK_OK)
    pConfig->evc_vid = newVlan;

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  VF_QUIESCE net passthrough operation handler
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  vfIdx    VF index
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus
sfvmk_sriovQuiesceVf(sfvmk_adapter_t *pAdapter, vmk_uint32 vfIdx)
{
  sfvmk_vfInfo_t *pVf = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  if (vfIdx >= pAdapter->numVfsEnabled) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid vfIdx: %u", vfIdx);
    status = VMK_BAD_PARAM;
    goto done;
  }

  pVf = pAdapter->pVfInfo + vfIdx;
  if (pVf == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid VF info, vfIdx: %u", vfIdx);
    goto done;
  }

  sfvmk_vfInfoReset(pAdapter, pVf);

  status = VMK_OK;
  SFVMK_DEBUG(SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
              "VF %u quiesced", vfIdx);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  Function to get the VLAN id set in pending proxy request
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  pPpr     pointer to pending proxy request
**
** \return: VLAN id set in pending proxy request or EFX_FILTER_VID_UNSPEC
*/
static vmk_uint16
sfvmk_pendingProxyReqGetVid(sfvmk_adapter_t *pAdapter,
                            sfvmk_pendingProxyReq_t *pPpr)
{
  vmk_uint16 vid = EFX_FILTER_VID_UNSPEC;
  vmk_uint32 i;
  vmk_uint32 bitInByte;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  if ((pPpr->cfg.cfgChanged & VMK_CFG_GUEST_VLAN_ADD) == 0) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                        "cfgChanged doesn't include VLAN_ADD");
    goto done;
  }

  /* Find requested VLAN ID in guest VLANs bitmap */
  for (i = 0; i < SFVMK_ARRAY_SIZE(pPpr->cfg.vlan.guestVlans); ++i) {
    if ((bitInByte = sfvmk_firstBitSet(pPpr->cfg.vlan.guestVlans[i])) > 0) {
      vid = (i * sizeof(pPpr->cfg.vlan.guestVlans[0]) * 8) + bitInByte - 1;
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                          "Bit %u found set, vid: %u", bitInByte, vid);
      break;
    }
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return vid;
}

/*! \brief  Function to handle the VLAN range response from VMKernel
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  vfIdx    VF index
**
** \return: none
*/
static void
sfvmk_proxyHandleResponseVlanRange(sfvmk_adapter_t *pAdapter,
                                   vmk_uint32 vfIdx)
{
  sfvmk_vfInfo_t *pVf = pAdapter->pVfInfo + vfIdx;
  sfvmk_pendingProxyReq_t *pPpr = NULL;
  vmk_uint32 privilegeMask = 0;
  vmk_uint16 vid;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  VMK_ASSERT_NOT_NULL(pVf);
  pPpr = &pVf->pendingProxyReq;

  vid = sfvmk_pendingProxyReqGetVid(pAdapter, pPpr);
  if (vid == EFX_FILTER_VID_UNSPEC) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                        "VF %u pprVid: %u", vfIdx, vid);
    goto done;
  }

  if (!vmk_BitVectorTest(pVf->pAllowedVlans, vid)) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                        "vid %u bit not in allowed set", vid);
    goto done;
  }

  pPpr->cfg.cfgChanged &= ~VMK_CFG_GUEST_VLAN_ADD;
  if (pPpr->cfg.cfgChanged != 0) {
    /* Only Rx mode change is valid with VLAN add */
    if ((pPpr->cfg.cfgChanged & ~VMK_CFG_RXMODE_CHANGED) != 0)
      SFVMK_ADAPTER_ERROR(pAdapter, "Invalid cfg: %x for VLAN_ADD operation",
                          pPpr->cfg.cfgChanged);
    goto done;
  }

  privilegeMask |= sfvmk_rxModeToPrivMask(pVf->rxMode) |
                   MC_CMD_PRIVILEGE_MASK_IN_GRP_UNRESTRICTED_VLAN;

  /* Authorize request with current privilege mask.
   * It will be declined by FW if privileges are insufficient.
   */
  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "VF %u authorize filter_op VLAN %u privilegeMask 0x%x"
                      " vs required 0x%x",
                       vfIdx, vid, privilegeMask, pPpr->reqdPrivileges);

  sfvmk_proxyAuthHandleResponse(pAdapter,
                                pAdapter->pPrimary->pProxyState,
                                pPpr->uhandle,
                                MC_CMD_PROXY_COMPLETE_IN_AUTHORIZED,
                                privilegeMask);

done:
  pPpr->cfg.cfgChanged = 0;
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
}

/*! \brief  Function to perform VF datapath reset
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  vfIdx    VF index
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus
sfvmk_sriovResetVf(sfvmk_adapter_t *pAdapter, vmk_uint32 vfIdx)
{
  sfvmk_vfInfo_t *pVf = pAdapter->pVfInfo + vfIdx;
  efx_vport_config_t *pConfig = NULL;
  vmk_Bool reset;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  if (!pVf) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid VF %u info", vfIdx);
    status = VMK_FAILURE;
    goto done;
  }

  pConfig = pAdapter->pVportConfig + vfIdx + 1;
  status = efx_evb_vport_reset(pAdapter->pNic, pAdapter->pVswitch,
                               SFVMK_VF_VPORT_ID(pAdapter, vfIdx),
                               pConfig->evc_mac_addr, pConfig->evc_vid,
                               &reset);
  if ((status != VMK_OK) || (reset == VMK_FALSE)) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "Could not reset VF %u using VPORT_RECONFIGURE: %s",
                        vfIdx, vmk_StatusToString(status));
  } else {
    vmk_BitVectorZap(pVf->pActiveVlans);
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  ADD/DEL VLAN_RANGE net passthrough operation handler
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  vfIdx    VF index
** \param[in]  add      VMK_TRUE if this is an add vlan range request
** \param[in]  first    first vlan id in the requested range
** \param[in]  last     last vlan id in the requested range
**
** \return: none
*/
void
sfvmk_sriovSetVfVlanRange(sfvmk_adapter_t *pAdapter, vmk_uint32 vfIdx,
                          vmk_Bool add, vmk_uint16 first, vmk_uint16 last)
{
  sfvmk_vfInfo_t *pVf = pAdapter->pVfInfo + vfIdx;
  vmk_uint16 i = 0;
  vmk_uint16 pprVid = 0;
  vmk_Bool changeActive = VMK_FALSE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  pprVid = sfvmk_pendingProxyReqGetVid(pAdapter, &pVf->pendingProxyReq);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "VF %u pprVid: %u", vfIdx, pprVid);

  if (add) {
    for (i = first; i <= last; ++i) {
      if (!vmk_BitVectorTest(pVf->pAllowedVlans, i)) {
        vmk_BitVectorSet(pVf->pAllowedVlans, i);

        /* If current pending proxy request made the VLAN active,
         * do not consider it as asynchronous permission granting
         * which requires VF datapath reset
         */
        if ((i != pprVid) && vmk_BitVectorTest(pVf->pActiveVlans, i)) {
          SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                              "Active vlan: %u", i);
          changeActive = VMK_TRUE;
        }
      }
    }
  } else {
    for (i = first; i <= last; ++i) {
       if (vmk_BitVectorTest(pVf->pAllowedVlans, i)) {
         vmk_BitVectorClear(pVf->pAllowedVlans, i);
         if (vmk_BitVectorTest(pVf->pActiveVlans, i)) {
           SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                               "Active vlan: %u", i);
           changeActive = VMK_TRUE;
        }
      }
    }
  }

  sfvmk_proxyHandleResponseVlanRange(pAdapter, vfIdx);

  if (changeActive) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                        "VF %u allowed VLAN range changes active VLAN,"
                        "reset VF to enforce", vfIdx);
    sfvmk_sriovResetVf(pAdapter, vfIdx);
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
}

/*! \brief  Function to handle the Rx mode response from VMKernel
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  vfIdx    VF index
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus
sfvmk_proxyHandleResponseRxMode(sfvmk_adapter_t *pAdapter, vmk_uint32 vfIdx)
{
  sfvmk_vfInfo_t *pVf = pAdapter->pVfInfo + vfIdx;
  sfvmk_pendingProxyReq_t *pPpr = NULL;
  vmk_uint32 privilegeMask;
  vmk_Bool authorize;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 result;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);
  if (!pVf) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid VF %u info", vfIdx);
    status = VMK_FAILURE;
    goto done;
  }

  pPpr = &pVf->pendingProxyReq;
  if ((pPpr->cfg.cfgChanged & VMK_CFG_RXMODE_CHANGED) == 0) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Rx mode change not requested [0x%x]",
                        pPpr->cfg.cfgChanged);
    status = VMK_OK;
    goto done;
  }
  pPpr->cfg.cfgChanged &= ~VMK_CFG_RXMODE_CHANGED;

  /* Only guest VLAN addition is valid with Rx mode change */
  if ((pPpr->cfg.cfgChanged & ~VMK_CFG_GUEST_VLAN_ADD) != 0) {
    SFVMK_ADAPTER_ERROR(pAdapter, "cfgChanged 0x%x invalid for Rx mode change",
                        pPpr->cfg.cfgChanged);
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Check if all required Rx mode privileges are granted */
  privilegeMask = sfvmk_rxModeToPrivMask(pVf->rxMode);
  authorize = !(~privilegeMask & pPpr->reqdPrivileges &
                SFVMK_EF10_RX_MODE_PRIVILEGE_MASK);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "reqPriv 0x%x privMask 0x%x authorize %u",
                      pPpr->reqdPrivileges, privilegeMask, authorize);

  /* If Rx mode is authorized, but other config change is pending
   * (e.g. VLAN membership), wait for it. Otherwise, reply now.
   */
  if (authorize) {
    if (pPpr->cfg.cfgChanged != 0) {
      SFVMK_ADAPTER_ERROR(pAdapter, "other config change pending[0x%x]",
                          pPpr->cfg.cfgChanged);
      status = VMK_OK;
      goto done;
    }

    /* The request is authorized by all handlers, so we
     * should add all non Rx mode required privileges.
     */
    privilegeMask |= pPpr->reqdPrivileges & ~SFVMK_EF10_RX_MODE_PRIVILEGE_MASK;
  }

  /* Authorize request with a new privilege mask.
   * It will be declined by FW if a new mask is insufficient.
   */
  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "VF %u authorize rx_mode 0x%x privilegeMask 0x%x "
                      "vs required 0x%x",
                      vfIdx, pVf->rxMode, privilegeMask, pPpr->reqdPrivileges);

  result = authorize ? MC_CMD_PROXY_COMPLETE_IN_AUTHORIZED :
                       MC_CMD_PROXY_COMPLETE_IN_DECLINED;
  status = sfvmk_proxyAuthHandleResponse(pAdapter->pPrimary,
                                         pAdapter->pPrimary->pProxyState,
                                         pPpr->uhandle, result,
                                         privilegeMask);
  if (status) {
    /* May fail because of MC reboot or the request already
     * declined because of timeout.
     */
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "VF %u uhandle=0x%lx Rx mode handle response failed: %s",
                        vfIdx, pPpr->uhandle, vmk_StatusToString(status));
    goto done;
  }

  pPpr->cfg.cfgChanged = 0;
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  SET_RX_MODE net passthrough operation handler
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t
** \param[in]  vfIdx      VF index
** \param[in]  rxMode     rxMode to be set
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_sriovSetVfRxMode(sfvmk_adapter_t *pAdapter, vmk_uint32 vfIdx,
                       vmk_VFRXMode rxMode)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_VFRXMode revokeRxMode;
  sfvmk_vfInfo_t *pVf = pAdapter->pVfInfo + vfIdx;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  revokeRxMode  = pVf->rxMode & ~rxMode;
  pVf->rxMode = rxMode;

  status = sfvmk_proxyHandleResponseRxMode(pAdapter, vfIdx);
  if (status == VMK_ESHUTDOWN) {
    SFVMK_ADAPTER_ERROR(pAdapter, "MC reboot, VF driver will reapply");
    status = VMK_OK;
  } else if (status) {
    SFVMK_ADAPTER_ERROR(pAdapter, "VF %u set Rx mode failed: %s, reset VF",
                        vfIdx, vmk_StatusToString(status));
    status = sfvmk_sriovResetVf(pAdapter, vfIdx);
  } else if (revokeRxMode) {
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                          "Revoke VF %u Rx mode %x, reset VF to apply",
                          vfIdx, revokeRxMode);
      status = sfvmk_sriovResetVf(pAdapter, vfIdx);
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  SET_ANTISPOOF net passthrough operation handler
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  vfIdx       VF index
** \param[in]  spoofChk    Passed VMK_TRUE to enable anti spoof check
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_sriovSetVfSpoofChk(sfvmk_adapter_t *pAdapter, vmk_uint32 vfIdx,
                         vmk_Bool spoofChk)
{
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  status = efx_proxy_auth_set_privilege_mask(pAdapter->pPrimary->pNic, vfIdx,
                MC_CMD_PRIVILEGE_MASK_IN_GRP_MAC_SPOOFING_TX,
                spoofChk ? 0 : MC_CMD_PRIVILEGE_MASK_IN_GRP_MAC_SPOOFING_TX);

 if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "efx_proxy_auth_set_privilege_mask failed status: %s",
                        vmk_StatusToString(status));
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  Function to handle the set MTU response from VMKernel
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  vfIdx    VF index
** \param[in]  mtu      MTU value to be applied
**
** \return: VMK_TRUE if mtu was set, VMK_FALSE otherwise
*/
vmk_Bool
sfvmk_proxyHandleResponseSetMtu(sfvmk_adapter_t *pAdapter,
                                vmk_uint32 vfIdx,
                                vmk_uint32 mtu)
{
  sfvmk_vfInfo_t *pVf = pAdapter->pVfInfo + vfIdx;
  sfvmk_pendingProxyReq_t *pPpr = &pVf->pendingProxyReq;
  vmk_Bool mtuSet = VMK_FALSE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  if ((pPpr->cfg.cfgChanged & VMK_CFG_MTU_CHANGED) == 0) {
    SFVMK_ADAPTER_ERROR(pAdapter, "MTU mode change not requested [0x%x]",
                        pPpr->cfg.cfgChanged);
    goto done;
  }

  pPpr->cfg.cfgChanged = 0;
  if (mtu == pPpr->cfg.mtu) {
    /* Complete request from PF driver since it may require
     * tweaking to set maximum MTU across PF and VFs
     */
    sfvmk_proxyCompleteSetMtu(pAdapter, pPpr->uhandle, &mtuSet);
  } else {
    sfvmk_proxyAuthHandleResponse(pAdapter, pAdapter->pPrimary->pProxyState,
                                  pPpr->uhandle,
                                  MC_CMD_PROXY_COMPLETE_IN_DECLINED, 0);
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return mtuSet;
}

/*! \brief  SET_MTU net passthrough operation handler
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  vfIdx    VF index
** \param[in]  mtu      MTU value to be applied
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_sriovSetVfMtu(sfvmk_adapter_t *pAdapter, vmk_uint32 vfIdx,
                     vmk_uint32 mtu)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_vfInfo_t *pVf = pAdapter->pVfInfo + vfIdx;
  vmk_uint32 oldMtu = pVf->macMtu;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);
  pVf->macMtu = mtu;

  if (sfvmk_proxyHandleResponseSetMtu(pAdapter, vfIdx, mtu)) {
    status = VMK_OK;
    goto done;
  }

  /* If handle response has not done the change, hypervisor
   * settings should be applied regardless any errors in handle
   * response.
   */
  status = efx_mac_pdu_set(pAdapter->pNic, sfvmk_calcMacMtuPf(pAdapter));
  if (status) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_pdu_set failed status: %s",
                        vmk_StatusToString(status));
    pVf->macMtu = oldMtu;
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_SRIOV);
  return status;
}

/*! \brief  VF_GET_QUEUE_STATS net passthrough operation handler
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
** \param[in]  vfIdx        VF index
** \param[out] pTxqStats    pointer to Txq stats structure
** \param[out] pRxqStats    pointer to Txq stats structure
**
** \return: VMK_OK [success] error code [failure]
*/
VMK_ReturnStatus
sfvmk_sriovGetVfStats(sfvmk_adapter_t *pAdapter, vmk_uint32 vfIdx,
                      vmk_NetVFTXQueueStats *pTxqStats,
                      vmk_NetVFRXQueueStats *pRxqStats)
{
  VMK_ReturnStatus status = VMK_OK;
  vmk_uint32 count;
  size_t macStatsSize;
  const efx_nic_cfg_t *pNicCfg = NULL;
  efsys_mem_t  macStatsDmaBuf;
  efsys_mem_t  *pStats = &macStatsDmaBuf;
  efsys_stat_t  vfStats[EFX_MAC_NSTATS] = {0};

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_SRIOV);

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_cfg_get failed");
    goto done;
  }

  macStatsSize = P2ROUNDUP(pNicCfg->enc_mac_stats_nstats * sizeof(uint64_t),
                           EFX_BUF_SIZE);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                      "EFX_MAC_NSTATS: %u, numMacStats: %u",
                      EFX_MAC_NSTATS, pNicCfg->enc_mac_stats_nstats);

  pStats->esmHandle = pAdapter->dmaEngine;
  pStats->ioElem.length = macStatsSize;

  /* Allocate DMA space. */
  pStats->pEsmBase = sfvmk_allocDMAMappedMem(pAdapter->dmaEngine,
                                             pStats->ioElem.length,
                                             &pStats->ioElem.ioAddr);
  if (pStats->pEsmBase == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Failed to allocate mac stats buffer: %s",
                        vmk_StatusToString(status));
    status = VMK_NO_MEMORY;
    goto done;
  }

  status = efx_evb_vport_stats(pAdapter->pNic, pAdapter->pVswitch,
                               SFVMK_VF_VPORT_ID(pAdapter, vfIdx), pStats);

  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_evb_vport_stats failed status: %s",
                        vmk_StatusToString(status));
    goto vport_stats_failed;
  }

  vmk_CPUMemFenceRead();

  /* If we're unlucky enough to read statistics during the DMA, wait
   * up to 10ms for it to finish (typically takes <500us)
   */
  for (count = 0; count < 10; ++count) {
    sfvmk_MutexLock(pAdapter->lock);
    if (pAdapter->port.state != SFVMK_PORT_STATE_STARTED) {
      /* Exit gracefully if port state is down */
      sfvmk_MutexUnlock(pAdapter->lock);
      status = VMK_NOT_READY;
      goto free_stats_memory;
    }

    /* Try to update the cached counters */
    status = efx_mac_stats_update(pAdapter->pNic, pStats,
                                  vfStats, NULL);
    sfvmk_MutexUnlock(pAdapter->lock);

    vmk_CPUMemFenceRead();

    if (status == VMK_OK)
      break;

    if (status != VMK_RETRY) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_stats_update failed status: %s",
                          vmk_StatusToString(status));
      goto free_stats_memory;
    }

    status = sfvmk_worldSleep(SFVMK_STATS_UPDATE_WAIT_USEC);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_worldSleep failed status: %s",
                          vmk_StatusToString(status));
      /* World is dying */
      goto free_stats_memory;
    }
  }

  if (pTxqStats) {
    pTxqStats->unicastPkts = vfStats[EFX_MAC_VADAPTER_TX_UNICAST_PACKETS];
    pTxqStats->unicastBytes = vfStats[EFX_MAC_VADAPTER_TX_UNICAST_BYTES];
    pTxqStats->multicastPkts = vfStats[EFX_MAC_VADAPTER_TX_MULTICAST_PACKETS];
    pTxqStats->multicastBytes = vfStats[EFX_MAC_VADAPTER_TX_MULTICAST_BYTES];
    pTxqStats->broadcastPkts = vfStats[EFX_MAC_VADAPTER_TX_BROADCAST_PACKETS];
    pTxqStats->broadcastBytes = vfStats[EFX_MAC_VADAPTER_TX_BROADCAST_BYTES];
    pTxqStats->errors = vfStats[EFX_MAC_VADAPTER_TX_BAD_PACKETS];
    pTxqStats->discards = vfStats[EFX_MAC_VADAPTER_TX_OVERFLOW];
    /* FW-assisted TSO statistics not available */
  }

  if (pRxqStats) {
    pRxqStats->unicastPkts = vfStats[EFX_MAC_VADAPTER_RX_UNICAST_PACKETS];
    pRxqStats->unicastBytes = vfStats[EFX_MAC_VADAPTER_RX_UNICAST_BYTES];
    pRxqStats->multicastPkts = vfStats[EFX_MAC_VADAPTER_RX_MULTICAST_PACKETS];
    pRxqStats->multicastBytes = vfStats[EFX_MAC_VADAPTER_RX_MULTICAST_BYTES];
    pRxqStats->broadcastPkts = vfStats[EFX_MAC_VADAPTER_RX_BROADCAST_PACKETS];
    pRxqStats->broadcastBytes = vfStats[EFX_MAC_VADAPTER_RX_BROADCAST_BYTES];
    pRxqStats->outOfBufferDrops = vfStats[EFX_MAC_VADAPTER_RX_OVERFLOW];
    pRxqStats->errorDrops = vfStats[EFX_MAC_VADAPTER_RX_BAD_PACKETS];
    /* LRO is done in SW */
  }

free_stats_memory:
vport_stats_failed:
  sfvmk_freeDMAMappedMem(pAdapter->dmaEngine, pStats->pEsmBase,
                         pStats->ioElem.ioAddr, pStats->ioElem.length);
done:
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
    case VMK_NETPTOP_VF_QUIESCE:
    {
      vmk_NetPTOPVFSimpleArgs *pVfArgs = pArgs;

      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                          "(PT) VF %u quiesce", pVfArgs->vf);
      status = sfvmk_sriovQuiesceVf(pAdapter, pVfArgs->vf);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_vfConfigOps PTOP: 0x%x status: %s",
                            op, vmk_StatusToString(status));
      }
      break;
    }

    case VMK_NETPTOP_VF_SET_MAC:
    {
      vmk_NetPTOPVFSetMacArgs *pVfArgs = pArgs;
      SFVMK_DECLARE_MAC_BUF(mac);

      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                          "(PT) VF %u set mac %s", pVfArgs->vf,
                          sfvmk_printMac(pVfArgs->mac, mac));

      status = sfvmk_sriovSetVfMac(pAdapter, pVfArgs->vf, pVfArgs->mac);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_vfConfigOps PTOP: 0x%x status: %s",
                            op, vmk_StatusToString(status));
      }
      break;
    }

    case VMK_NETPTOP_VF_SET_DEFAULT_VLAN:
    {
      vmk_NetPTOPVFSetDefaultVlanArgs *pVfArgs = pArgs;

      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                          "(PT) VF %u %s default VLAN %u prio %u",
                          pVfArgs->vf, pVfArgs->enable ? "enable" : "disable",
                          pVfArgs->vid, pVfArgs->prio);

      if (pVfArgs->enable)
        status = sfvmk_sriovSetVfVlan(pAdapter, pVfArgs->vf,
                                      pVfArgs->vid, pVfArgs->prio);
      else
        status = sfvmk_sriovSetVfVlan(pAdapter, pVfArgs->vf, 0, 0);

      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_vfConfigOps PTOP: 0x%x status: %s",
                            op, vmk_StatusToString(status));
      }

      break;
    }

    case VMK_NETPTOP_VF_ADD_VLAN_RANGE:
    case VMK_NETPTOP_VF_DEL_VLAN_RANGE:
    {
      vmk_NetPTOPVFVlanRangeArgs *pVfArgs = pArgs;
      vmk_Bool isAddVlanRange = (op == VMK_NETPTOP_VF_ADD_VLAN_RANGE);

      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                          "(PT) VF %u %s VLAN range %u-%u",
                          pVfArgs->vf,
                          isAddVlanRange ? "add" : "delete",
                          pVfArgs->first, pVfArgs->last);

      sfvmk_sriovSetVfVlanRange(pAdapter, pVfArgs->vf, isAddVlanRange,
                                pVfArgs->first, pVfArgs->last);
      break;
    }

    case VMK_NETPTOP_VF_SET_RX_MODE:
    {
      vmk_NetPTOPVFSetRxModeArgs *pVfArgs = pArgs;
      vmk_uint32 rxMode;

      rxMode = ((pVfArgs->unicast ? VMK_VF_RXMODE_UNICAST : 0) |
                (pVfArgs->multicast ? VMK_VF_RXMODE_MULTICAST : 0) |
                (pVfArgs->broadcast ? VMK_VF_RXMODE_BROADCAST : 0) |
                (pVfArgs->allmulti ? VMK_VF_RXMODE_ALLMULTI : 0) |
                (pVfArgs->promiscuous ? VMK_VF_RXMODE_PROMISC : 0));

      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                          "(PT) VF %u set Rx mode 0x%x "
                          "(unicast=%u,multicast=%u,broadcast=%u,"
                          "allmulti=%u,promiscuous=%u)",
                          pVfArgs->vf, rxMode, pVfArgs->unicast,
                          pVfArgs->multicast, pVfArgs->broadcast,
                          pVfArgs->allmulti, pVfArgs->promiscuous);

      status = sfvmk_sriovSetVfRxMode(pAdapter, pVfArgs->vf, rxMode);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_vfConfigOps PTOP: 0x%x status: %s",
                            op, vmk_StatusToString(status));
      }
      break;
    }

    case VMK_NETPTOP_VF_SET_ANTISPOOF:
    {
      vmk_NetPTOPVFSetAntispoofArgs *pVfArgs = pArgs;

      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                          "(PT) VF %u %s anti-spoof",
                          pVfArgs->vf, pVfArgs->enable ? "enable" : "disable");

      status = sfvmk_sriovSetVfSpoofChk(pAdapter, pVfArgs->vf, pVfArgs->enable);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_vfConfigOps PTOP: 0x%x status: %s",
                            op, vmk_StatusToString(status));
      }
      break;
    }

    case VMK_NETPTOP_VF_SET_MTU:
    {
      vmk_NetPTOPVFSetMtuArgs *pVfArgs = pArgs;

      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                          "(PT) VF %u set MTU %u",
                          pVfArgs->vf, pVfArgs->mtu);

      status = sfvmk_sriovSetVfMtu(pAdapter, pVfArgs->vf, pVfArgs->mtu);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_vfConfigOps PTOP: 0x%x status: %s",
                            op, vmk_StatusToString(status));
      }
      break;
    }

    case VMK_NETPTOP_VF_GET_QUEUE_STATS:
    {
      vmk_NetPTOPVFGetQueueStatsArgs *pVfArgs = pArgs;

      /* Make sure that all stats are filled in with zeros */
      vmk_Memset(pVfArgs->tqStats, 0,
                 pVfArgs->numTxQueues * sizeof(*pVfArgs->tqStats));
      vmk_Memset(pVfArgs->rqStats, 0,
                 pVfArgs->numRxQueues * sizeof(*pVfArgs->rqStats));

      status = sfvmk_sriovGetVfStats(pAdapter, pVfArgs->vf,
                                     pVfArgs->numTxQueues ?
                                     pVfArgs->tqStats : NULL,
                                     pVfArgs->numRxQueues ?
                                     pVfArgs->rqStats : NULL);

      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_vfConfigOps PTOP: 0x%x status: %s",
                            op, vmk_StatusToString(status));
      }
      else
        SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_SRIOV, SFVMK_LOG_LEVEL_DBG,
                            "(PT) VF %u get queue stats n_txq=%u n_rxq=%u",
                            pVfArgs->vf, pVfArgs->numTxQueues,
                            pVfArgs->numRxQueues);
      break;
    }

    default:
      SFVMK_ADAPTER_ERROR(pAdapter, "PTOP %d unhandled", op);
  }

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
    /* Don't treat it as an error */
    status = VMK_OK;
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
