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
#include "sfvmk_mgmt_interface.h"

/*! \brief  Get adapter pointer based on hash.
**
** \param[in] pMgmtParm pointer to managment param
**
** \return: pointer to sfvmk_adapter_t <success>  NULL <failure>
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

/*! \brief  A Mgmt callback routine to post MCDI commands
 **
 ** \param[in]      pCookies    Pointer to cookie
 ** \param[in]      pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[in/out]  pDevIface   Pointer to device interface structure
 ** \param[in/out]  pMgmtMcdi   Pointer to MCDI cmd struct
 **
 ** \return: VMK_OK  <success>
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:      In case of dev not found
 **     VMK_BAD_PARAM:      Invalid payload size or input param
 **     VMK_FAILURE:        Any other failure
 **
 */
VMK_ReturnStatus
sfvmk_mgmtMcdiCallback(vmk_MgmtCookies       *pCookies,
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
 ** \param[in]  pCookies    Pointer to cookie
 ** \param[in]  pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[in/out]  pDevIface  Pointer to device interface structure
 ** \param[in/out]  pMcdiLog   Pointer to MCDI log structure
 **
 ** \return: VMK_OK  <success>
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
sfvmk_mgmtMCLoggingCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
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
** \param[in]  pCookies    pointer to cookie
** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in/out]  pDevIface  pointer to device interface structure
** \param[in/out]  pPciInfo   pointer to sfvmk_pciInfo_s structure
**
** \return VMK_OK
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
** \param[in]      pCookies    pointer to cookie
** \param[in]      pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in/out]  pDevIface   pointer to mgmt param
** \param[in/out]  pVpdInfo    pointer to sfvmk_vpdInfo_s structure
**
** \return VMK_OK
**     Below error values are filled in the status field of
**     sfvmk_mgmtDevInfo_t.
**     VMK_NOT_FOUND:      In case of dev not found
**     VMK_BAD_PARAM:      Invalid Ioctl option or wrong
**                         input param
**     VMK_FAILURE:        Any other error
**
*/
VMK_ReturnStatus
sfvmk_mgmtVPDInfoCallback(vmk_MgmtCookies *pCookies,
                          vmk_MgmtEnvelope *pEnvelope,
                          sfvmk_mgmtDevInfo_t *pDevIface,
                          sfvmk_vpdInfo_t *pVpdInfo)
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
 ** \param[in]  pCookies    pointer to cookie
 ** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
 ** \param[in/out]  pDevIface   pointer to mgmt param
 ** \param[in/out]  pLinkOps   pointer to link ops structure
 **
 ** \return VMK_OK
 **     Below error values are filled in the status field of
 **     VMK_NOT_FOUND:   In case of dev not found
 **     VMK_BAD_PARAM:   Unknown command option or
 **                      Null Pointer passed in parameter
 **
 */
VMK_ReturnStatus
sfvmk_mgmtLinkStatusGet(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        vmk_Bool *pLinkState)
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
 ** \param[in]  pCookies    pointer to cookie
 ** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
 ** \param[in/out]  pDevIface    pointer to device interface structure
 ** \param[in/out]  pLinkSpeed   pointer to speed and autoneg
 **                              param structure
 **
 ** \return VMK_OK
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t
 **     VMK_NOT_FOUND:   In case of dev not found
 **     VMK_BAD_PARAM:   Unknown command option or
 **                      Null Pointer passed in parameter
 **     VMK_FAILURE:     Any other error
 **
 */
VMK_ReturnStatus
sfvmk_mgmtLinkSpeedRequest(vmk_MgmtCookies *pCookies,
                           vmk_MgmtEnvelope *pEnvelope,
                           sfvmk_mgmtDevInfo_t *pDevIface,
                           sfvmk_linkSpeed_t *pLinkSpeed)
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
** \param[in]  pCookies    pointer to cookie
** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in/out]  pDevIface   pointer to device interface structure
** \param[in/out]  pVerInfo    pointer to version info struct
**
** \return: VMK_OK  <success>
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
sfvmk_mgmtVerInfoCallback(vmk_MgmtCookies *pCookies,
                          vmk_MgmtEnvelope *pEnvelope,
                          sfvmk_mgmtDevInfo_t *pDevIface,
                          sfvmk_versionInfo_t *pVerInfo)
{
  sfvmk_adapter_t      *pAdapter = NULL;
  efx_nic_t            *pNic = NULL;
  vmk_UplinkDriverInfo *pDrvInfo = NULL;
  efx_nvram_type_t     nvramType;
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
      nvramType = (pVerInfo->type == SFVMK_GET_UEFI_VERSION) ?
                   EFX_NVRAM_UEFIROM : EFX_NVRAM_BOOTROM;

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

