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
#include "efx_regs_mcdi.h"
#include "sfvmk_driver.h"

const efx_nvram_type_t nvramTypes[] = {
  [SFVMK_NVRAM_INVALID]      = EFX_NVRAM_INVALID,
  [SFVMK_NVRAM_BOOTROM]      = EFX_NVRAM_BOOTROM,
  [SFVMK_NVRAM_BOOTROM_CFG]  = EFX_NVRAM_BOOTROM_CFG,
  [SFVMK_NVRAM_MC]           = EFX_NVRAM_MC_FIRMWARE,
  [SFVMK_NVRAM_MC_GOLDEN]    = EFX_NVRAM_MC_GOLDEN,
  [SFVMK_NVRAM_PHY]          = EFX_NVRAM_PHY,
  [SFVMK_NVRAM_NULL_PHY]     = EFX_NVRAM_NULLPHY,
  [SFVMK_NVRAM_FPGA]         = EFX_NVRAM_FPGA,
  [SFVMK_NVRAM_FCFW]         = EFX_NVRAM_FCFW,
  [SFVMK_NVRAM_CPLD]         = EFX_NVRAM_CPLD,
  [SFVMK_NVRAM_FPGA_BACKUP]  = EFX_NVRAM_FPGA_BACKUP,
  [SFVMK_NVRAM_UEFIROM]      = EFX_NVRAM_UEFIROM,
  [SFVMK_NVRAM_DYNAMIC_CFG]  = EFX_NVRAM_DYNAMIC_CFG,
  [SFVMK_NVRAM_MUM]          = EFX_NVRAM_MUM_FIRMWARE,
};

/*! \brief  Get adapter pointer based on hash.
**
** \param[in] pMgmtParm Pointer to managment param
**
** \return: Pointer to sfvmk_adapter_t [success]  NULL [failure]
*/
static sfvmk_adapter_t *
sfvmk_mgmtFindAdapter(sfvmk_mgmtDevInfo_t *pMgmtParm)
{
  sfvmk_adapter_t *pAdapter;
  VMK_ReturnStatus status;

  status = vmk_HashKeyFind(sfvmk_modInfo.vmkdevHashTable,
                           pMgmtParm->deviceName,
                           (vmk_HashValue *)&pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERROR("%s: Failed to find node in vmkDevice "
                "table status: %s", pMgmtParm->deviceName,
                vmk_StatusToString(status));
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

/*! \brief Iterator used to iterate and copy the vmnic name
 **
 ** \param[in]     htbl   Hash handle.
 ** \param[in]     key    Hash key.
 ** \param[in]     value  Hash entry value stored at key
 ** \param[in,out] data   pointer to sfvmk_ifaceList_t,
 **                       if an entry is found then
 **                       copy the vmnic name.
 **
 ** \return: Key iterator commands.
 **
 */
static vmk_HashKeyIteratorCmd
sfvmk_adapterHashIter(vmk_HashTable htbl,
                      vmk_HashKey key, vmk_HashValue value,
                      vmk_AddrCookie data)
{
  sfvmk_ifaceList_t *pIfaceList = data.ptr;
  char              *pIfaceName = (char *)key;
  VMK_ReturnStatus  status = VMK_FAILURE;

  if (!pIfaceList || !pIfaceName) {
    SFVMK_ERROR("Failed to find vmk device");
    return VMK_HASH_KEY_ITER_CMD_STOP;
  }

  if (pIfaceList->ifaceCount >= SFVMK_MAX_INTERFACE)
    return VMK_HASH_KEY_ITER_CMD_STOP;

  status = vmk_StringCopy(pIfaceList->ifaceArray[pIfaceList->ifaceCount].string,
                          pIfaceName, 32);
  if (status != VMK_OK) {
    SFVMK_ERROR("String copy failed with error %s", vmk_StatusToString(status));
    return VMK_HASH_KEY_ITER_CMD_STOP;
  }

  pIfaceList->ifaceCount++;

  return VMK_HASH_KEY_ITER_CMD_CONTINUE;
}

/*! \brief  A Mgmt callback routine to post MCDI commands
 **
 ** \param[in]      pCookies    Pointer to cookie
 ** \param[in]      pEnvelope   Pointer to vmk_MgmtEnvelope
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
sfvmk_mgmtMcdiCallback(vmk_MgmtCookies      *pCookies,
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

  if (!pMgmtMcdi) {
    SFVMK_ERROR("pMgmtMcdi: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Failed to find interface %s", pDevIface->deviceName);
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
 ** \param[in]      pCookies   Pointer to cookie
 ** \param[in]      pEnvelope  Pointer to vmk_MgmtEnvelope
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
sfvmk_mgmtMCLoggingCallback(vmk_MgmtCookies     *pCookies,
                            vmk_MgmtEnvelope    *pEnvelope,
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
  pDevIface->status = VMK_NO_ACCESS;
#endif

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback to Get PCI BDF and PCI device information
 **
 ** \param[in]      pCookies   Pointer to cookie
 ** \param[in]      pEnvelope  Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface  Pointer to device interface structure
 ** \param[in,out]  pPciInfo   Pointer to sfvmk_pciInfo_s structure
 **
 ** \return VMK_OK  [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_BAD_PARAM:      Null Pointer passed in parameter
 **     VMK_NOT_FOUND:      In case of dev not found
 **     VMK_FAILURE:        String copy failed or any other
 **                         error
 **
 */
VMK_ReturnStatus
sfvmk_mgmtPCIInfoCallback(vmk_MgmtCookies      *pCookies,
                          vmk_MgmtEnvelope     *pEnvelope,
                          sfvmk_mgmtDevInfo_t  *pDevIface,
                          sfvmk_pciInfo_t      *pPciInfo)
{
  sfvmk_adapter_t  *pAdapter = NULL;
  VMK_ReturnStatus status;

  vmk_SemaLock(&sfvmk_modInfo.lock);
  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pPciInfo) {
    SFVMK_ERROR("pPciInfo: NULL pointer passed as input param");
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

  status = vmk_StringCopy(pPciInfo->pciBDF.string, pAdapter->pciDeviceName.string,
                          SFVMK_PCI_BDF_LEN);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "String copy failed with error %s",
                        vmk_StatusToString(status));
    pDevIface->status = VMK_FAILURE;
    goto end;
  }

  pPciInfo->vendorId = pAdapter->pciDeviceID.vendorID;
  pPciInfo->deviceId = pAdapter->pciDeviceID.deviceID;
  pPciInfo->subVendorId = pAdapter->pciDeviceID.subVendorID;
  pPciInfo->subDeviceId = pAdapter->pciDeviceID.subDeviceID;

  pDevIface->status = VMK_OK;

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback to get and set VPD information
 **
 ** \param[in]      pCookies    Pointer to cookie
 ** \param[in]      pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface   Pointer to device interface structure
 ** \param[in,out]  pVpdInfo    Pointer to sfvmk_vpdInfo_s structure
 **
 ** \return VMK_OK  [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:      In case of dev not found
 **     VMK_BAD_PARAM:      Invalid Ioctl option or wrong
 **                         input param
 **     VMK_FAILURE:        Any other error
 **
 */
VMK_ReturnStatus
sfvmk_mgmtVPDInfoCallback(vmk_MgmtCookies     *pCookies,
                          vmk_MgmtEnvelope    *pEnvelope,
                          sfvmk_mgmtDevInfo_t *pDevIface,
                          sfvmk_vpdInfo_t     *pVpdInfo)
{
  sfvmk_adapter_t *pAdapter = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;

  vmk_SemaLock(&sfvmk_modInfo.lock);
  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  if (!pVpdInfo) {
    SFVMK_ERROR("pVpdInfo: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pDevIface->status = VMK_OK;

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Adapter structure corresponding to %s device not found",
                pDevIface->deviceName);
    pDevIface->status = VMK_NOT_FOUND;
    goto end;
  }

  switch (pVpdInfo->vpdOp) {
    case SFVMK_MGMT_DEV_OPS_GET:
      if ((status = sfvmk_vpdGetInfo(pAdapter, &pVpdInfo->vpdPayload[0],
                                     SFVMK_VPD_MAX_PAYLOAD, pVpdInfo->vpdTag,
                                     pVpdInfo->vpdKeyword, &pVpdInfo->vpdLen)) != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Get VPD data failed with error %s",
                            vmk_StatusToString(status));
        pDevIface->status = status;
        goto end;
      }

      break;

    case SFVMK_MGMT_DEV_OPS_SET:
      if ((status = sfvmk_vpdSetInfo(pAdapter, &pVpdInfo->vpdPayload[0],
                                     pVpdInfo->vpdTag, pVpdInfo->vpdKeyword,
                                     pVpdInfo->vpdLen)) != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Set VPD data failed with error %s",
                            vmk_StatusToString(status));
        pDevIface->status = status;
        goto end;
      }

      break;

    default:
      pDevIface->status = VMK_BAD_PARAM;
      goto end;
  }

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback for Get Link state
 **
 ** \param[in]      pCookies    Pointer to cookie
 ** \param[in]      pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface   Pointer to device interface structure
 ** \param[in,out]  pLinkState  Pointer to link state flag
 **
 ** \return VMK_OK  [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:   In case of dev not found
 **     VMK_BAD_PARAM:   Unknown command option or
 **                      Null Pointer passed in parameter
 **
 */
VMK_ReturnStatus
sfvmk_mgmtLinkStatusCallback(vmk_MgmtCookies     *pCookies,
                             vmk_MgmtEnvelope    *pEnvelope,
                             sfvmk_mgmtDevInfo_t *pDevIface,
                             vmk_Bool            *pLinkState)
{
  sfvmk_adapter_t *pAdapter = NULL;
  vmk_LinkState   linkState = VMK_LINK_STATE_DOWN;

  vmk_SemaLock(&sfvmk_modInfo.lock);

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pLinkState) {
    SFVMK_ERROR("pLinkState: NULL pointer passed as input");
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

  *pLinkState =  VMK_FALSE;

  sfvmk_linkStateGet(pAdapter, &linkState);
  *pLinkState = (linkState == VMK_LINK_STATE_UP) ?
                 VMK_TRUE : VMK_FALSE;

  pDevIface->status = VMK_OK;

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback for Get/Set Link speed and autoneg
 **
 ** \param[in]      pCookies    Pointer to cookie
 ** \param[in]      pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface   Pointer to device interface structure
 ** \param[in,out]  pLinkSpeed  Pointer to speed and autoneg
 **                             param structure
 **
 ** \return VMK_OK  [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t
 **     VMK_NOT_FOUND:   In case of dev not found
 **     VMK_BAD_PARAM:   Unknown command option or
 **                      Null Pointer passed in parameter
 **     VMK_FAILURE:     Any other error
 **
 */
VMK_ReturnStatus
sfvmk_mgmtLinkSpeedCallback(vmk_MgmtCookies     *pCookies,
                            vmk_MgmtEnvelope    *pEnvelope,
                            sfvmk_mgmtDevInfo_t *pDevIface,
                            sfvmk_linkSpeed_t   *pLinkSpeed)
{
  sfvmk_adapter_t      *pAdapter = NULL;
  vmk_uint32            speed;
  VMK_ReturnStatus      status = VMK_FAILURE;

  vmk_SemaLock(&sfvmk_modInfo.lock);

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pLinkSpeed) {
    SFVMK_ERROR("pLinkSpeed: NULL pointer passed as input");
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

  switch (pLinkSpeed->type) {
    case SFVMK_MGMT_DEV_OPS_SET:
      speed = pLinkSpeed->autoNeg ? VMK_LINK_SPEED_AUTO : pLinkSpeed->speed;

      /* Set PHY speed */
      status = sfvmk_phyLinkSpeedSet(pAdapter, (vmk_LinkSpeed)speed);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Link speed change failed with error %s",
                            vmk_StatusToString(status));
        pDevIface->status = status;
        goto end;
      }

      break;

    case SFVMK_MGMT_DEV_OPS_GET:
      sfvmk_phyLinkSpeedGet(pAdapter, (vmk_LinkSpeed *)&pLinkSpeed->speed,
                            &pLinkSpeed->autoNeg);
      break;

    default:
      pDevIface->status = VMK_BAD_PARAM;
      goto end;
  }

  pDevIface->status = VMK_OK;

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback to get Version info
 **
 ** \param[in]      pCookies    Pointer to cookie
 ** \param[in]      pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface   Pointer to device interface structure
 ** \param[in,out]  pVerInfo    Pointer to version info struct
 **
 ** \return: VMK_OK  [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_BAD_PARAM:   Unknown version option or
 **                      Null Pointer passed in parameter
 **     VMK_NOT_FOUND:   In case of dev not found
 **     VMK_NOT_READY:   If get NVRAM version failed
 **     VMK_FAILURE:     If get MC Fw version failed
 **                      or any other error
 **
 */
VMK_ReturnStatus
sfvmk_mgmtVerInfoCallback(vmk_MgmtCookies     *pCookies,
                          vmk_MgmtEnvelope    *pEnvelope,
                          sfvmk_mgmtDevInfo_t *pDevIface,
                          sfvmk_versionInfo_t *pVerInfo)
{
  sfvmk_adapter_t      *pAdapter = NULL;
  efx_nic_t            *pNic = NULL;
  vmk_UplinkDriverInfo *pDrvInfo = NULL;
  efx_nvram_type_t     nvramType = EFX_NVRAM_INVALID;
  vmk_ByteCount        bytesCopied;
  vmk_uint32           subtype;
  vmk_uint16           nvramVer[4];
  VMK_ReturnStatus     status = VMK_FAILURE;

  vmk_SemaLock(&sfvmk_modInfo.lock);

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pVerInfo) {
    SFVMK_ERROR("pLinkSpeed: NULL pointer passed as input");
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

  pDrvInfo = &pAdapter->uplink.sharedData.driverInfo;

  switch (pVerInfo->type) {
    case SFVMK_GET_DRV_VERSION:
      SFVMK_SHARED_AREA_BEGIN_READ(pAdapter);
      vmk_Memcpy(&pVerInfo->version, &pDrvInfo->version, sizeof(vmk_Name));
      SFVMK_SHARED_AREA_END_READ(pAdapter);
      break;

    case SFVMK_GET_FW_VERSION:
      SFVMK_SHARED_AREA_BEGIN_READ(pAdapter);
      vmk_Memcpy(&pVerInfo->version, &pDrvInfo->firmwareVersion, sizeof(vmk_Name));
      SFVMK_SHARED_AREA_END_READ(pAdapter);
      break;

    case SFVMK_GET_ROM_VERSION:
    case SFVMK_GET_UEFI_VERSION:
    case SFVMK_GET_SUC_VERSION:
      if (pVerInfo->type == SFVMK_GET_UEFI_VERSION)
        nvramType = EFX_NVRAM_UEFIROM;
      else if (pVerInfo->type == SFVMK_GET_ROM_VERSION)
        nvramType = EFX_NVRAM_BOOTROM;
      else if (pVerInfo->type == SFVMK_GET_SUC_VERSION)
        nvramType = EFX_NVRAM_MUM_FIRMWARE;

      pNic = pAdapter->pNic;

      if ((status = efx_nvram_get_version(pNic, nvramType, &subtype,
                                          &nvramVer[0])) != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Get NVRAM Firmware version failed with error %s",
                            vmk_StatusToString(status));
        pDevIface->status = VMK_NOT_READY;
        goto end;
      }

      status = vmk_StringFormat(pVerInfo->version.string, sizeof(vmk_Name),
                                &bytesCopied, "%u.%u.%u.%u",
                                nvramVer[0], nvramVer[1],
                                nvramVer[2], nvramVer[3]);

      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "String format failed with error %s",
                            vmk_StatusToString(status));
        pDevIface->status = VMK_FAILURE;
        goto end;
      }

      break;

    default:
      pDevIface->status = VMK_BAD_PARAM;
      goto end;
  }

  pDevIface->status = VMK_OK;

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback to Get/Set interrupt
 **         moderation settings
 **
 ** \param[in]      pCookies   Pointer to cookie
 ** \param[in]      pEnvelope  Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface  Pointer to device interface structure
 ** \param[in,out]  pIntrMod   Pointer to interrrupt
 **                            moderation structure
 **
 ** \return VMK_OK [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:   In case of dev not found
 **     VMK_BAD_PARAM:   Unknown command/param option or
 **                      Null Pointer passed in parameter
 **     VMK_FAILURE:     Any other error
 **
 */
VMK_ReturnStatus
sfvmk_mgmtIntrModerationCallback(vmk_MgmtCookies        *pCookies,
                                 vmk_MgmtEnvelope       *pEnvelope,
                                 sfvmk_mgmtDevInfo_t    *pDevIface,
                                 sfvmk_intrCoalsParam_t *pIntrMod)
{
  sfvmk_adapter_t           *pAdapter = NULL;
  vmk_UplinkCoalesceParams   params;
  VMK_ReturnStatus           status = VMK_FAILURE;

  vmk_SemaLock(&sfvmk_modInfo.lock);

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pIntrMod) {
    SFVMK_ERROR("pIntrMod: NULL pointer passed as input");
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

  switch (pIntrMod->type) {
    case SFVMK_MGMT_DEV_OPS_SET:
      if (!(pIntrMod->txUsecs)) {
        pDevIface->status = VMK_BAD_PARAM;
        goto end;
      }

      /* Change interrupt moderation settings for Rx/Tx queues */
      status = sfvmk_configIntrModeration(pAdapter, pIntrMod->txUsecs);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Failed interrupt moderation settings error %s",
                            vmk_StatusToString(status));
        pDevIface->status = status;
        goto end;
      }

      memset(&params, 0, sizeof(vmk_UplinkCoalesceParams));
      params.txUsecs = params.rxUsecs = pIntrMod->txUsecs;

      /* Copy the current interrupt coals params into shared queue data */
      sfvmk_configQueueDataCoalescParams(pAdapter, &params);
      break;

    case SFVMK_MGMT_DEV_OPS_GET:
      /* Firmware doesn't support moderation settings for
       * different rx and tx event types. Only txUsecs parameter
       * is being used for both rx & tx queue interrupt moderation
       * conifguration */
      sfvmk_MutexLock(pAdapter->lock);
      pIntrMod->txUsecs = pIntrMod->rxUsecs = pAdapter->intrModeration;
      sfvmk_MutexUnlock(pAdapter->lock);
      break;

    default:
      pDevIface->status = VMK_BAD_PARAM;
      goto end;
  }

  pDevIface->status = VMK_OK;

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback to perform Image Update
 **
 ** \param[in]      pCookies    Pointer to cookie
 ** \param[in]      pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface   Pointer to device interface structure
 ** \param[in]      pImgUpdate  Pointer to sfvmk_imgUpdate_t structure
 **
 ** \return VMK_OK [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:      In case of dev not found
 **     VMK_BAD_PARAM:      Invalid Ioctl option or wrong
 **                         input param
 **     VMK_NO_MEMORY:      Memory Allocation failed
 **     VMK_NOT_SUPPORTED:  Unsupported Firmware type
 **     VMK_FAILURE:        Copy from User or NVRAM operation failure
 **
 */
VMK_ReturnStatus sfvmk_mgmtImgUpdateCallback(vmk_MgmtCookies     *pCookies,
                                             vmk_MgmtEnvelope    *pEnvelope,
                                             sfvmk_mgmtDevInfo_t *pDevIface,
                                             sfvmk_imgUpdate_t   *pImgUpdate)
{
  sfvmk_imgUpdateV2_t  imgInfoV2;

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  if (!pImgUpdate) {
    SFVMK_ERROR("pImgUpdate: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  imgInfoV2.pFileBuffer = pImgUpdate->pFileBuffer;
  imgInfoV2.size = pImgUpdate->size;
  imgInfoV2.type = SFVMK_NVRAM_INVALID;

  sfvmk_mgmtImgUpdateV2Callback(pCookies, pEnvelope, pDevIface, &imgInfoV2);

end:
  return VMK_OK;
}

/*! \brief  A Mgmt callback to perform Image Update V2
 **         This management callback validates that the passed
 **         image is correspoding to the passed  NVRAM type
 **         before updating image
 **
 ** \param[in]      pCookies      Pointer to cookie
 ** \param[in]      pEnvelope     Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface     Pointer to device interface structure
 ** \param[in]      pImgUpdateV2  Pointer to sfvmk_imgUpdateV2_t structure
 **
 ** \return VMK_OK [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:      In case of dev not found
 **     VMK_BAD_PARAM:      Invalid Ioctl option or wrong
 **                         input param
 **     VMK_NO_MEMORY:      Memory Allocation failed
 **     VMK_NOT_SUPPORTED:  Unsupported Firmware type
 **     VMK_NO_PERMISSION:  Mismatch between firmware type and firmware image
 **     VMK_FAILURE:        Copy from User or NVRAM operation failure
 **
 */
VMK_ReturnStatus
sfvmk_mgmtImgUpdateV2Callback(vmk_MgmtCookies      *pCookies,
                              vmk_MgmtEnvelope     *pEnvelope,
                              sfvmk_mgmtDevInfo_t  *pDevIface,
                              sfvmk_imgUpdateV2_t  *pImgUpdateV2)
{
  sfvmk_adapter_t   *pAdapter = NULL;
  VMK_ReturnStatus  status = VMK_FAILURE;

  vmk_SemaLock(&sfvmk_modInfo.lock);

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pImgUpdateV2) {
    SFVMK_ERROR("pImgUpdateV2: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  if (pImgUpdateV2->size == 0) {
    SFVMK_ERROR("pImgUpdateV2: Invalid file size");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  if (pImgUpdateV2->type >= SFVMK_NVRAM_NTYPE) {
    SFVMK_ERROR("Invalid firmware image type");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  if (!pImgUpdateV2->pFileBuffer) {
    SFVMK_ERROR("pFileBuffer: NULL pointer passed as input");
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

  status = sfvmk_performUpdate(pAdapter, pImgUpdateV2);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Firmware update failed for deivce");
    pDevIface->status = status;
    goto end;
  }

  pDevIface->status = VMK_OK;

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback routine to post NVRAM req
 **
 ** \param[in]      pCookies   Pointer to cookie
 ** \param[in]      pEnvelope  Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface  Pointer to device interface structure
 ** \param[in,out]  pCmd       Pointer to NVRAM cmd struct
 **
 ** \return: VMK_OK  [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:      In case of dev not found
 **     VMK_NOT_SUPPORTED:  Operation not supported
 **     VMK_BAD_PARAM:      Unknown option or NULL input param
 **     VMK_NO_MEMORY:      Memory Allocation failed
 **     VMK_FAILURE:        Any other error
 **
 */
VMK_ReturnStatus
sfvmk_mgmtNVRAMCallback(vmk_MgmtCookies     *pCookies,
                        vmk_MgmtEnvelope    *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_nvramCmd_t    *pCmd)
{
  sfvmk_adapter_t   *pAdapter = NULL;
  vmk_uint8         *pNvramBuf = NULL;
  efx_nic_t         *pNic = NULL;
  efx_nvram_type_t  type;
  size_t            partSize = 0;
  VMK_ReturnStatus  status = VMK_FAILURE;

  vmk_SemaLock(&sfvmk_modInfo.lock);

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pCmd) {
    SFVMK_ERROR("pCmd: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  if (pCmd->type >= SFVMK_NVRAM_INVALID) {
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

  pNic = pAdapter->pNic;
  type = nvramTypes[pCmd->type];

  if (pCmd->op == SFVMK_NVRAM_OP_READ ||
      pCmd->op == SFVMK_NVRAM_OP_WRITEALL) {
    if (!pCmd->data) {
      SFVMK_ERROR("Nvram data: NULL pointer passed as input");
      pDevIface->status = VMK_BAD_PARAM;
      goto end;
    }

    status = efx_nvram_size(pNic, type, (size_t *)&partSize);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM get partition size failed with error %s",
                          vmk_StatusToString(status));
      pDevIface->status = status;
      goto end;
    }

    if (pCmd->size <= 0 || pCmd->size > partSize) {
      SFVMK_ADAPTER_ERROR(pAdapter, "User buffer size is invalid");
      pDevIface->status = VMK_NO_SPACE;
      goto end;
    }

    if ((pCmd->op == SFVMK_NVRAM_OP_WRITEALL) && (pCmd->size != partSize)) {
      SFVMK_ADAPTER_ERROR(pAdapter, "User write buffer size is invalid");
      pDevIface->status = VMK_NO_SPACE;
      goto end;
    }

    pNvramBuf = (char *)vmk_HeapAlloc(sfvmk_modInfo.heapID, pCmd->size);
    if (pNvramBuf == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "pNvramBuf memory allocation failed");
      pDevIface->status = VMK_NO_MEMORY;
      goto end;
    }

    vmk_Memset(pNvramBuf, 0, pCmd->size);
  }

  if (type == EFX_NVRAM_MC_GOLDEN &&
      (pCmd->op == SFVMK_NVRAM_OP_ERASE ||
       pCmd->op == SFVMK_NVRAM_OP_WRITEALL ||
       pCmd->op == SFVMK_NVRAM_OP_SET_VER)) {
    pDevIface->status = VMK_NOT_SUPPORTED;
    goto freemem;
  }

  switch (pCmd->op) {
    case SFVMK_NVRAM_OP_SIZE:
      status = efx_nvram_size(pNic, type, (size_t *)&pCmd->size);
      break;

    case SFVMK_NVRAM_OP_GET_VER:
      status = efx_nvram_get_version(pNic, type,
                                     &pCmd->subtype, &pCmd->version[0]);
      break;

    case SFVMK_NVRAM_OP_SET_VER:
      status = efx_nvram_set_version(pNic, type, &pCmd->version[0]);
      break;

    case SFVMK_NVRAM_OP_READ:
      if ((status = sfvmk_nvramRead(pAdapter, type, pNvramBuf,
                                    &pCmd->size, pCmd->offset)) != VMK_OK) {
        break;
      }

      status = vmk_CopyToUser((vmk_VA)pCmd->data, (vmk_VA)pNvramBuf, pCmd->size);

      break;

    case SFVMK_NVRAM_OP_WRITEALL:
      if ((status = vmk_CopyFromUser((vmk_VA)pNvramBuf,
                                     (vmk_VA)pCmd->data,
                                     pCmd->size)) != VMK_OK) {
        break;
      }

      status = sfvmk_nvramWriteAll(pAdapter, type, pNvramBuf,
                                   &pCmd->size, pCmd->erasePart);
      break;

    /* TODO: Will add support for this in future */
    case SFVMK_NVRAM_OP_ERASE:
    default:
      pDevIface->status = VMK_NOT_SUPPORTED;
      goto end;
  }

  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Operation = %u failed with error %s",
                        pCmd->op, vmk_StatusToString(status));
    pDevIface->status = status;
    goto freemem;
  }

  pDevIface->status = VMK_OK;

freemem:
  if (pNvramBuf)
    vmk_HeapFree(sfvmk_modInfo.heapID, pNvramBuf);

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback routine to get HW queue stats
 **
 ** \param[in]      pCookies       Pointer to cookie
 ** \param[in]      pEnvelope      Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface      Pointer to device interface structure
 ** \param[in,out]  pHwQueueStats  Pointer to hw queue stats buffer
 **
 ** \return: VMK_OK  [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:      In case of dev not found
 **     VMK_BAD_PARAM:      Unknown option or NULL input param
 **     VMK_NO_MEMORY:      Memory Allocation failed
 **     VMK_NO_SPACE:       Not enough space available in user buffer
 **     VMK_WRITE_ERROR:    Copy to user buffer failed
 **     VMK_FAILURE:        Any other error
 **
 */
VMK_ReturnStatus
sfvmk_mgmtHWQStatsCallback(vmk_MgmtCookies      *pCookies,
                           vmk_MgmtEnvelope     *pEnvelope,
                           sfvmk_mgmtDevInfo_t  *pDevIface,
                           sfvmk_hwQueueStats_t *pHwQueueStats)
{
  sfvmk_adapter_t   *pAdapter = NULL;
  char              *pCurr = NULL;
  char              *pStatsBuffer = NULL;
  vmk_ByteCount     bytesCopied = 0;
  vmk_ByteCount     maxBytes;
  vmk_ByteCount     totalBytes = 0;
  VMK_ReturnStatus  status = VMK_FAILURE;

  vmk_SemaLock(&sfvmk_modInfo.lock);

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pHwQueueStats) {
    SFVMK_ERROR("pHwQueueStats: NULL pointer passed as input");
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

  if (pHwQueueStats->subCmd == SFVMK_MGMT_STATS_GET_SIZE) {
    /* Set size of the hardware queue stats buffer as requested
     * by user before allocating memory and requesting the hardware
     * queue stats data */
    pHwQueueStats->size = SFVMK_STATS_BUFFER_SZ;
    pDevIface->status = VMK_OK;
    goto end;
  } else if (pHwQueueStats->subCmd != SFVMK_MGMT_STATS_GET) {
    SFVMK_ERROR("Invalid sub command");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  if (!pHwQueueStats->statsBuffer) {
    SFVMK_ERROR("statsBuffer: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  if (pHwQueueStats->size < SFVMK_STATS_BUFFER_SZ) {
    SFVMK_ADAPTER_ERROR(pAdapter, "User buffer size is not sufficient");
    pDevIface->status = VMK_NO_SPACE;
    goto freemem;
  }

  pStatsBuffer = (char *)vmk_HeapAlloc(sfvmk_modInfo.heapID, SFVMK_STATS_BUFFER_SZ);
  if (pStatsBuffer == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Queue stats memory allocation failed");
    status = VMK_NO_MEMORY;
    goto end;
  }

  vmk_Memset(pStatsBuffer, 0, SFVMK_STATS_BUFFER_SZ);
  pCurr = pStatsBuffer;
  maxBytes = SFVMK_STATS_BUFFER_SZ;

  status = sfvmk_requestMACStats(pAdapter, pCurr, maxBytes, &bytesCopied);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_requestMacStats failed error: %s",
                        vmk_StatusToString(status));
    goto freemem;
  }

  pCurr += bytesCopied;
  maxBytes -= bytesCopied;
  totalBytes += bytesCopied;

  status = sfvmk_requestQueueStats(pAdapter, pCurr, maxBytes, &bytesCopied);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_requestQueueStats failed error: %s",
                        vmk_StatusToString(status));
    goto freemem;
  }

  maxBytes -= bytesCopied;
  totalBytes += bytesCopied;

  if ((status = vmk_CopyToUser((vmk_VA)pHwQueueStats->statsBuffer, (vmk_VA)pStatsBuffer,
                                SFVMK_STATS_BUFFER_SZ)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Copy to user failed with error: %s",
                        vmk_StatusToString(status));
    pDevIface->status = VMK_WRITE_ERROR;
    goto freemem;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_MGMT, SFVMK_LOG_LEVEL_DBG,
                      "max Bytes %d, bytes copied %lu, bytes remaining %lu",
                      SFVMK_STATS_BUFFER_SZ, totalBytes, maxBytes);

  pHwQueueStats->size = totalBytes;
  pDevIface->status = VMK_OK;

freemem:
  vmk_HeapFree(sfvmk_modInfo.heapID, pStatsBuffer);
end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback to get the MAC address of an interface
 **
 ** \param[in]      pCookies    Pointer to cookie
 ** \param[in]      pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface   Pointer to device interface structure
 ** \param[out]     pMacAddr    Pointer to MAC address
 **
 ** \return VMK_OK [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:   In case of dev not found
 **     VMK_BAD_PARAM:   Unknown command option or
 **                      Null Pointer passed in parameter
 **
 */
VMK_ReturnStatus
sfvmk_mgmtMACAddressCallback(vmk_MgmtCookies     *pCookies,
                             vmk_MgmtEnvelope    *pEnvelope,
                             sfvmk_mgmtDevInfo_t *pDevIface,
                             sfvmk_macAddress_t  *pMacAddr)
{
  sfvmk_adapter_t      *pAdapter = NULL;
  vmk_UplinkSharedData *pSharedData = NULL;

  vmk_SemaLock(&sfvmk_modInfo.lock);

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pMacAddr) {
    SFVMK_ERROR("pMacAddr: NULL pointer passed as input");
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

  pSharedData = &pAdapter->uplink.sharedData;

  vmk_Memcpy(pMacAddr->macAddress, pSharedData->macAddr, VMK_ETH_ADDR_LENGTH);

  pDevIface->status = VMK_OK;

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback to get SolarFlare interface list
 **
 ** \param[in]      pCookies    Pointer to cookie
 ** \param[in]      pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[out]     pDevIface   Pointer to device interface structure
 ** \param[out]     pIfaceList  Pointer to interface list structure
 **
 ** \return VMK_OK [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:   In case of dev not found
 **     VMK_BAD_PARAM:   Unknown command option or
 **                      Null Pointer passed in parameter
 **
 */
VMK_ReturnStatus
sfvmk_mgmtInterfaceListCallback(vmk_MgmtCookies     *pCookies,
                                vmk_MgmtEnvelope    *pEnvelope,
                                sfvmk_mgmtDevInfo_t *pDevIface,
                                sfvmk_ifaceList_t   *pIfaceList)
{
  VMK_ReturnStatus  status = VMK_FAILURE;

  vmk_SemaLock(&sfvmk_modInfo.lock);

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pIfaceList) {
    SFVMK_ERROR("pIfaceList: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pIfaceList->ifaceCount = 0;
  status = vmk_HashKeyIterate(sfvmk_modInfo.vmkdevHashTable,
                              sfvmk_adapterHashIter, pIfaceList);
  if (status != VMK_OK) {
    SFVMK_ERROR("Iterator failed with error code %s",
                 vmk_StatusToString(status));
    pDevIface->status = status;
    goto end;
  }

  pDevIface->status = VMK_OK;

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback to get/ set the FEC mode settings
 **
 ** \param[in]      pCookies   Pointer to cookie
 ** \param[in]      pEnvelope  Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface  Pointer to device interface structure
 ** \param[in,out]  pFecMode   Pointer to FEC mode structure
 **
 ** \return VMK_OK [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:   In case of dev not found
 **     VMK_BAD_PARAM:   Unknown command option or
 **                      Null Pointer passed in parameter
 **     VMK_FAILURE:     Any other error
 **
 */
VMK_ReturnStatus
sfvmk_mgmtFecModeCallback(vmk_MgmtCookies     *pCookies,
                          vmk_MgmtEnvelope    *pEnvelope,
                          sfvmk_mgmtDevInfo_t *pDevIface,
                          sfvmk_fecMode_t     *pFecMode)
{
  sfvmk_adapter_t   *pAdapter = NULL;
  VMK_ReturnStatus  status = VMK_FAILURE;

  vmk_SemaLock(&sfvmk_modInfo.lock);

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pFecMode) {
    SFVMK_ERROR("pFecMode: NULL pointer passed as input");
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

  EFX_STATIC_ASSERT(SFVMK_MGMT_FEC_NONE_BIT == SFVMK_PORT_FEC_NONE_BIT);
  EFX_STATIC_ASSERT(SFVMK_MGMT_FEC_AUTO_BIT == SFVMK_PORT_FEC_AUTO_BIT);
  EFX_STATIC_ASSERT(SFVMK_MGMT_FEC_OFF_BIT == SFVMK_PORT_FEC_OFF_BIT);
  EFX_STATIC_ASSERT(SFVMK_MGMT_FEC_RS_BIT == SFVMK_PORT_FEC_RS_BIT);
  EFX_STATIC_ASSERT(SFVMK_MGMT_FEC_BASER_BIT == SFVMK_PORT_FEC_BASER_BIT);

  switch (pFecMode->type) {
    case SFVMK_MGMT_DEV_OPS_SET:
      status = sfvmk_phyFecSet(pAdapter, pFecMode->fec);
      if (status != VMK_OK) {
        SFVMK_ERROR("Set FEC mode failed with error code %s",
                    vmk_StatusToString(status));
        pDevIface->status = status;
        goto end;
      }

      break;
    case SFVMK_MGMT_DEV_OPS_GET:
      status = sfvmk_phyFecGet(pAdapter, &pFecMode->fec, &pFecMode->activeFec);
      if (status != VMK_OK) {
        SFVMK_ERROR("Get FEC mode failed with error code %s",
                    vmk_StatusToString(status));
        pDevIface->status = status;
        goto end;
      }

      break;
    default:
      pDevIface->status = VMK_BAD_PARAM;
      goto end;
  }

  pDevIface->status = VMK_OK;
end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback routine to get HW hardware sensor info
 **
 ** \param[in]      pCookies   Pointer to cookie
 ** \param[in]      pEnvelope  Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface  Pointer to device interface structure
 ** \param[in,out]  pHwSensor  Pointer to hw sensor info
 **
 ** \return: VMK_OK  [success]
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:      In case of dev not found
 **     VMK_BAD_PARAM:      Unknown option or NULL input param
 **     VMK_NO_MEMORY:      Memory Allocation failed
 **     VMK_NO_SPACE:       Not enough space available in user buffer
 **     VMK_WRITE_ERROR:    Copy to user buffer failed
 **     VMK_FAILURE:        Any other error
 **
 */
VMK_ReturnStatus
sfvmk_mgmtHWSensorInfoCallback(vmk_MgmtCookies      *pCookies,
                               vmk_MgmtEnvelope     *pEnvelope,
                               sfvmk_mgmtDevInfo_t  *pDevIface,
                               sfvmk_hwSensor_t     *pHwSensor)
{
  sfvmk_adapter_t   *pAdapter = NULL;
  char              *pSensorBuffer = NULL;
  vmk_ByteCount     bytesCopied = 0;
  VMK_ReturnStatus  status = VMK_FAILURE;

  vmk_SemaLock(&sfvmk_modInfo.lock);

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  pDevIface->status = VMK_FAILURE;

  if (!pHwSensor) {
    SFVMK_ERROR("pHwSensor: NULL pointer passed as input");
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

  if (pHwSensor->subCmd == SFVMK_MGMT_SENSOR_GET_SIZE) {
    /* Set size of the hardware sensor buffer as requested
     * by user before allocating memory and requesting the
     * hardware sensor data */
    pHwSensor->size = (SFVMK_SENSOR_INFO_MAX_WIDTH * EFX_MON_NSTATS);
    pDevIface->status = VMK_OK;
    goto end;
  } else if (pHwSensor->subCmd != SFVMK_MGMT_SENSOR_GET) {
    SFVMK_ERROR("Invalid sub command");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  if (!pHwSensor->sensorBuffer) {
    SFVMK_ERROR("sensorBuffer: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  if (pHwSensor->size < (SFVMK_SENSOR_INFO_MAX_WIDTH * EFX_MON_NSTATS)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "User buffer size is not sufficient");
    pDevIface->status = VMK_NO_SPACE;
    goto end;
  }

  pSensorBuffer = (char *)vmk_HeapAlloc(sfvmk_modInfo.heapID, pHwSensor->size);
  if (pSensorBuffer == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "HW sensor info buffer memory allocation failed");
    pDevIface->status = VMK_NO_MEMORY;
    goto end;
  }

  vmk_Memset(pSensorBuffer, 0, pHwSensor->size);

  status = sfvmk_requestSensorData(pAdapter, pSensorBuffer, pHwSensor->size, &bytesCopied);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Sensor info failed with error: %s",
                        vmk_StatusToString(status));
    pDevIface->status = status;
    goto freemem;
  }

  if ((status = vmk_CopyToUser((vmk_VA)pHwSensor->sensorBuffer, (vmk_VA)pSensorBuffer,
                                pHwSensor->size)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Copy to user failed with error: %s",
                        vmk_StatusToString(status));
    pDevIface->status = VMK_WRITE_ERROR;
    goto freemem;
  }

  pHwSensor->size = bytesCopied;
  pDevIface->status = VMK_OK;

freemem:
  vmk_HeapFree(sfvmk_modInfo.heapID, pSensorBuffer);
end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

/*! \brief  A Mgmt callback to get or set PCI function privileges
 **
 ** \param[in]      pCookies       Pointer to cookie
 ** \param[in]      pEnvelope      Pointer to vmk_MgmtEnvelope
 ** \param[out]     pDevIface      Pointer to device interface structure
 ** \param[out]     pPrivilegeInfo Pointer to privileges info structure
 **
 ** \return VMK_OK [success]
 **     Below error values are filled in the status field of
 **     sfvmk_privilege_t
 **     VMK_NOT_FOUND:       In case of dev not found
 **     VMK_BAD_PARAM:       Unknown command option, invalid PCI address or
 **                          Null Pointer passed in parameter
 **     VMK_NOT_SUPPORTED:   Driver doesn't support SR-IOV and hence this API.
 **                          Use the driver with SFVMK_SUPPORT_SRIOV defined.
 **     VMK_FAILURE:         Any other error
 **
 */
VMK_ReturnStatus
sfvmk_mgmtFnPrivilegeCallback(vmk_MgmtCookies     *pCookies,
                              vmk_MgmtEnvelope    *pEnvelope,
                              sfvmk_mgmtDevInfo_t *pDevIface,
                              sfvmk_privilege_t   *pPrivilegeInfo)
{
  vmk_PCIDeviceAddr pciSBDF = {0,};
  vmk_uint32 seg = 0;
  vmk_uint32 bus = 0;
  vmk_uint32 dev = 0;
  vmk_uint32 fn = 0;
  vmk_uint32 numAssigned = 0;

  vmk_SemaLock(&sfvmk_modInfo.lock);

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

#ifndef SFVMK_SUPPORT_SRIOV
  SFVMK_ERROR("SR-IOV not supported on this driver release");
  pDevIface->status = VMK_NOT_SUPPORTED;
  goto end;
#endif

  pDevIface->status = VMK_FAILURE;

  if (!pPrivilegeInfo) {
    SFVMK_ERROR("pPrivilegeInfo: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  numAssigned = vmk_Sscanf(pPrivilegeInfo->pciSBDF.string,
                           "%04x:%02x:%02x.%x", &seg, &bus, &dev, &fn);
  if (numAssigned != 4) {
    SFVMK_ERROR("Parsing input PCI address [%s] failed, parsed %u members",
                pPrivilegeInfo->pciSBDF.string, numAssigned);
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pciSBDF.seg = seg;
  pciSBDF.bus = bus;
  pciSBDF.dev = dev;
  pciSBDF.fn = fn;

  SFVMK_DEBUG(SFVMK_DEBUG_MGMT, SFVMK_LOG_LEVEL_DBG,
              "Look up PCI address %04x:%02x:%02x.%x", pciSBDF.seg,
              pciSBDF.bus, pciSBDF.dev, pciSBDF.fn);

#ifdef SFVMK_SUPPORT_SRIOV
  if (pPrivilegeInfo->type == SFVMK_MGMT_DEV_OPS_GET) {
    pDevIface->status = sfvmk_getPrivilegeMask(&pciSBDF,
                                               &pPrivilegeInfo->privMask);
  } else if (pPrivilegeInfo->type == SFVMK_MGMT_DEV_OPS_SET) {
    pDevIface->status = sfvmk_modifyPrivilegeMask(&pciSBDF,
                                                  pPrivilegeInfo->privMask,
                                                  pPrivilegeInfo->privRemoveMask);
  } else {
    SFVMK_ERROR("Invalid operation [%u] on function privileges",
                pPrivilegeInfo->type);
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }
#endif

  if (pDevIface->status != VMK_OK) {
    SFVMK_ERROR("Operation [%u] on function privileges failed: %s",
                pPrivilegeInfo->type,
                vmk_StatusToString(pDevIface->status));
    goto end;
  }

  pDevIface->status = VMK_OK;
end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

