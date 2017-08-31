/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "efx_mcdi.h"
#include "efx_regs_mcdi.h"
#include "sfvmk_driver.h"
#include "sfvmk_nvram.h"
#include "sfvmk_vpd.h"
#include "sfvmk_mgmtInterface.h"

/*! \brief  Get adapter pointer based on hash.
**
** \param[in] pMgmtParm pointer to managment param
**
** \return: pointer to sfvmk_adapter_t <success>  NULL <failure>
*/
static sfvmk_adapter_t *
sfvmk_mgmtFindAdapter(sfvmk_mgmtDevInfo_t *pMgmtParm)
{
  sfvmk_devHashTable_t *pHashTblEntry = NULL;
  int rc;

  rc = vmk_HashKeyFind(sfvmk_ModInfo.vmkdevHashTable,
                       pMgmtParm->deviceName,
                       (vmk_HashValue *)&pHashTblEntry);
  if (rc != VMK_OK) {
    SFVMK_ERROR("%s: Failed to find node in vmkDevice "
                 "table status: %s", pMgmtParm->deviceName, vmk_StatusToString(rc));
    goto end;
  }

  if ((pHashTblEntry == NULL) || (pHashTblEntry->pAdapter == NULL)) {
    SFVMK_ERROR("%s: No vmkDevice (node: %p)",
                  pMgmtParm->deviceName, pHashTblEntry);
    pMgmtParm->status = VMK_NOT_FOUND;
    goto end;
  }

  return pHashTblEntry->pAdapter;

end:
  return NULL;
}

/*! \brief  A Mgmt callback routine to post MCDI commands
 **
 ** \param[in]  pCookies    pointer to cookie
 ** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
 ** \param[in/out]  pDevIface   pointer to mgmt param
 ** \param[in/out]  pMgmtMcdi   pointer to MCDI cmd struct
 **
 ** \return: VMK_OK  <success>
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:      In case of dev not found
 **     VMK_NO_MEMORY:      Alloc failed for local buffer
 **     VMK_NOT_READY:      MCDI is not initialized
 **     VMK_NOT_SUPPORTED:  MCDI feature not supported
 **     VMK_BAD_PARAM:      Invalid payload size or input param
 **     VMK_RETRY:          A communication error has happened,
 **                         Retry after MC reboot
 **
 */
int
sfvmk_mgmtMcdiCallback(vmk_MgmtCookies       *pCookies,
                        vmk_MgmtEnvelope     *pEnvelope,
                        sfvmk_mgmtDevInfo_t  *pDevIface,
                        sfvmk_mcdiRequest2_t *pMgmtMcdi)
{
  sfvmk_adapter_t *pAdapter = NULL;
  efx_mcdi_req_t   emr;
  int              rc;

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  if (!pMgmtMcdi) {
    SFVMK_ERROR("pMgmtMcdi: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pDevIface->status = VMK_OK;

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Pointer to pAdapter is NULL");
    pDevIface->status = VMK_NOT_FOUND;
    goto end;
  }

  if (pMgmtMcdi->inlen > SFVMK_MCDI_MAX_PAYLOAD ||
      pMgmtMcdi->outlen > SFVMK_MCDI_MAX_PAYLOAD) {
    SFVMK_ERR(pAdapter, "Invalid Length");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  emr.emr_cmd = pMgmtMcdi->cmd;
  emr.emr_in_buf = (vmk_uint8 *)pMgmtMcdi->payload;
  emr.emr_in_length = pMgmtMcdi->inlen;

  emr.emr_out_buf = (vmk_uint8 *)pMgmtMcdi->payload;
  emr.emr_out_length = pMgmtMcdi->outlen;

  rc = sfvmk_mcdiIOHandler(pAdapter, &emr);
  if (rc != VMK_OK) {
    SFVMK_ERR(pAdapter, "MCDI command failed %s",
              vmk_StatusToString(rc));
    pDevIface->status = rc;
    goto end;
  }

  if (emr.emr_rc) {
    if (emr.emr_out_length_used)
      pMgmtMcdi->flags |= EFX_MCDI_REQUEST_ERROR;
    else {
      pDevIface->status = VMK_RETRY;
      goto end;
    }
  }

  pMgmtMcdi->host_errno = emr.emr_rc;
  pMgmtMcdi->cmd = emr.emr_cmd;
  pMgmtMcdi->outlen = emr.emr_out_length_used;

end:
  return VMK_OK;
}

/*! \brief  A Mgmt callback routine to post NVRAM req
**
** \param[in]  pCookies    pointer to cookie
** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in/out]  pDevIface   pointer to mgmt param
** \param[in/out]  pCmd        pointer to NVRAM cmd struct
**
** \return: VMK_OK  <success>
**     Below error values are filled in the status field of
**     sfvmk_mgmtDevInfo_t.
**     VMK_NOT_FOUND:      In case of dev not found
**     VMK_NOT_SUPPORTED:  Operation not supported
**     VMK_NO_SPACE:       Insufficient space
**     VMK_BAD_PARAM:      Unknown option or NULL input param
**
*/
int sfvmk_mgmtNVRAMCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope    *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_nvramCmd_t    *pCmd)
{
  sfvmk_adapter_t               *pAdapter = NULL;
  efx_nic_t                     *pNic;
  efx_nvram_type_t               type;
  int                            rc;
  static const efx_nvram_type_t  nvramTypes[] = {
    [SFVMK_NVRAM_TYPE_BOOTROM]  = EFX_NVRAM_BOOTROM,
    [SFVMK_NVRAM_TYPE_BOOTROM_CFG]  = EFX_NVRAM_BOOTROM_CFG,
    [SFVMK_NVRAM_TYPE_MC]  = EFX_NVRAM_MC_FIRMWARE,
    [SFVMK_NVRAM_TYPE_MC_GOLDEN]  = EFX_NVRAM_MC_GOLDEN,
    [SFVMK_NVRAM_TYPE_PHY]  = EFX_NVRAM_PHY,
    [SFVMK_NVRAM_TYPE_NULL_PHY]  = EFX_NVRAM_NULLPHY,
    [SFVMK_NVRAM_TYPE_FPGA]  = EFX_NVRAM_FPGA,
    [SFVMK_NVRAM_TYPE_FCFW]  = EFX_NVRAM_FCFW,
    [SFVMK_NVRAM_TYPE_CPLD]  = EFX_NVRAM_CPLD,
    [SFVMK_NVRAM_TYPE_FPGA_BACKUP]  = EFX_NVRAM_FPGA_BACKUP,
    [SFVMK_NVRAM_TYPE_UEFIROM]  = EFX_NVRAM_UEFIROM,
    [SFVMK_NVRAM_TYPE_DYNAMIC_CFG]  = EFX_NVRAM_DYNAMIC_CFG,
  };

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  if (!pCmd) {
    SFVMK_ERROR("pCmd :NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  rc = pDevIface->status = VMK_OK;

  if (pCmd->type > SFVMK_NVRAM_TYPE_DYNAMIC_CFG) {
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Pointer to pAdapter is NULL");
    pDevIface->status = VMK_NOT_FOUND;
    goto end;
  }

  pNic = pAdapter->pNic;
  type = nvramTypes[pCmd->type];

  if (type == EFX_NVRAM_MC_GOLDEN &&
      (pCmd->op == SFVMK_NVRAM_OP_WRITE ||
       pCmd->op == SFVMK_NVRAM_OP_ERASE ||
       pCmd->op == SFVMK_NVRAM_OP_SET_VER)) {
    pDevIface->status = VMK_NOT_SUPPORTED;
    goto end;
  }

  switch (pCmd->op) {
    case SFVMK_NVRAM_OP_SIZE:
      rc = efx_nvram_size(pNic, type, (size_t *)&pCmd->size);
      break;
    case SFVMK_NVRAM_OP_READ:
      if (pCmd->size > sizeof (pCmd->data)) {
        SFVMK_ERR(pAdapter, "Invalid data chunk size, OP %d", pCmd->op);
        pDevIface->status = VMK_NO_SPACE;
        goto end;
      }

      rc = sfvmk_nvram_rw(pAdapter, pCmd->data, &pCmd->size,
                               pCmd->offset, type, VMK_FALSE);
      break;
    case SFVMK_NVRAM_OP_WRITE:
      if (pCmd->size > sizeof (pCmd->data)) {
        SFVMK_ERR(pAdapter, "Invalid data chunk size, OP %d", pCmd->op);
        pDevIface->status = VMK_NO_SPACE;
        goto end;
      }

      rc = sfvmk_nvram_rw(pAdapter, pCmd->data, &pCmd->size,
                               pCmd->offset, type, VMK_TRUE);
      break;
    case SFVMK_NVRAM_OP_ERASE:
      rc = sfvmk_nvram_erase(pAdapter, type);
      break;
    case SFVMK_NVRAM_OP_GET_VER:
      rc = efx_nvram_get_version(pNic, type,
                       &pCmd->subtype, &pCmd->version[0]);
      break;
    case SFVMK_NVRAM_OP_SET_VER:
      rc = efx_nvram_set_version(pNic, type, &pCmd->version[0]);
      break;
    default:
      pDevIface->status = VMK_NOT_SUPPORTED;
      goto end;
  }

  if (rc != VMK_OK) {
    SFVMK_ERR(pAdapter, "Operation = %d failed with error %s",
              pCmd->op, vmk_StatusToString(rc));
    pDevIface->status = rc;
  }

end:
  return VMK_OK;
}

/*! \brief  A Mgmt callback to various Version info
**
** \param[in]  pCookies    pointer to cookie
** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in/out]  pDevIface   pointer to mgmt param
** \param[in/out]  pVerInfo    pointer to version info struct
**
** \return: VMK_OK  <success>
**     Below error values are filled in the status field of
**     sfvmk_mgmtDevInfo_t.
**     VMK_NOT_FOUND:   In case of dev not found
**     VMK_NOT_READY:   If get NVRAM version failed
**     VMK_FAILURE:     If get MC Fw version failed
**     VMK_BAD_PARAM:   Unknown version option
**
*/
int sfvmk_mgmtVerInfoCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_versionInfo_t *pVerInfo)
{
  sfvmk_adapter_t *pAdapter = NULL;
  efx_nic_t *pNic = NULL;
  efx_nic_fw_info_t nicFwVer;
  efx_nvram_type_t nvramType;
  vmk_ByteCount bytesCopied;
  vmk_uint32 subtype;
  vmk_uint16 nvramVer[4];
  int rc;

  pDevIface->status = VMK_OK;

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Pointer to pAdapter is NULL");
    pDevIface->status = VMK_NOT_FOUND;
    goto end;
  }

  pNic = pAdapter->pNic;

  switch (pVerInfo->type) {
    case SFVMK_GET_DRV_VERSION:
      memcpy(pVerInfo->version, SFVMK_DRIVER_VERSION_STRING,
              sizeof(SFVMK_DRIVER_VERSION_STRING));
      break;

    case SFVMK_GET_FW_VERSION:
      rc = efx_nic_get_fw_version(pNic, &nicFwVer);
      if (rc != 0) {
        SFVMK_ERR(pAdapter, "Get MC Firmware version failed with error %s",
                  vmk_StatusToString(rc));
        pDevIface->status = VMK_FAILURE;
        goto end;
      }

      rc = vmk_StringFormat(pVerInfo->version, SFVMK_VER_MAX_CHAR_LEN,
                            &bytesCopied, "%u.%u.%u.%u",
                            nicFwVer.enfi_mc_fw_version[0],
                            nicFwVer.enfi_mc_fw_version[1],
                            nicFwVer.enfi_mc_fw_version[2],
                            nicFwVer.enfi_mc_fw_version[3]);
      if (rc != VMK_OK) {
        SFVMK_ERR(pAdapter, "String format failed with error %s",
                  vmk_StatusToString(rc));
        pDevIface->status = VMK_FAILURE;
        goto end;
      }

      break;

    case SFVMK_GET_ROM_VERSION:
    case SFVMK_GET_UEFI_VERSION:
      nvramType = (pVerInfo->type == SFVMK_GET_UEFI_VERSION) ?
                    EFX_NVRAM_UEFIROM : EFX_NVRAM_BOOTROM;

      if ((rc = efx_nvram_get_version(pNic, nvramType, &subtype,
                                      &nvramVer[0])) != 0) {
        SFVMK_ERR(pAdapter, "Get NVRAM Firmware version failed with error %s",
                  vmk_StatusToString(rc));
        pDevIface->status = VMK_NOT_READY;
        goto end;
      }

      rc = vmk_StringFormat(pVerInfo->version, SFVMK_VER_MAX_CHAR_LEN,
                            &bytesCopied, "%u.%u.%u.%u",
                            nvramVer[0], nvramVer[1],
                            nvramVer[2], nvramVer[3]);

      if (rc != VMK_OK) {
        SFVMK_ERR(pAdapter, "String format failed with error %s",
                  vmk_StatusToString(rc));
        pDevIface->status = VMK_FAILURE;
        goto end;
     }

      break;

    default:
      pDevIface->status = VMK_BAD_PARAM;
      goto end;
  }

end:
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
int sfvmk_mgmtLinkStatusUpdate(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_linkStatus_t *pLinkOps)
{
  sfvmk_adapter_t *pAdapter = NULL;
  VMK_ReturnStatus status = VMK_OK;

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  if (!pLinkOps) {
    SFVMK_ERROR("pLinkOps: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pDevIface->status = VMK_OK;

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Pointer to pAdapter is NULL");
    pDevIface->status = VMK_NOT_FOUND;
    goto end;
  }

  if (pLinkOps->type != SFVMK_MGMT_DEV_OPS_GET) {
    SFVMK_ERR(pAdapter, "Invalid operation 0x%x", pLinkOps->type);
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pLinkOps->state = VMK_FALSE;
  if ((status = sfvmk_linkStateGet(pAdapter,
                    &pLinkOps->state)) != VMK_OK) {
    SFVMK_ERR(pAdapter, "Get current link state failed with error %s",
                  vmk_StatusToString(status));
    pDevIface->status = status;
  }

end:
  return VMK_OK;
}

/*! \brief  A Mgmt callback for Get/Set Link speed and autoneg
 **
 ** \param[in]  pCookies    pointer to cookie
 ** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
 ** \param[in/out]  pDevIface    pointer to mgmt param
 ** \param[in/out]  pLinkSpeed   pointer to speed and autoneg
 **                              param structure
 **
 ** \return VMK_OK
 **     Below error values are filled in the status field of
 **     VMK_NOT_FOUND:   In case of dev not found
 **     VMK_BAD_PARAM:   Unknown command option or
 **                      Null Pointer passed in parameter
 **
 */
 int sfvmk_mgmtLinkSpeedUpdate(vmk_MgmtCookies *pCookies,
                         vmk_MgmtEnvelope *pEnvelope,
                         sfvmk_mgmtDevInfo_t *pDevIface,
                         sfvmk_linkSpeed_t *pLinkSpeed)
{
  sfvmk_adapter_t      *pAdapter = NULL;
  VMK_ReturnStatus      status = VMK_OK;
  vmk_uint32            speed;

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  if (!pLinkSpeed) {
    SFVMK_ERROR("pLinkSpeed: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pDevIface->status = VMK_OK;

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Pointer to pAdapter is NULL");
    pDevIface->status = VMK_NOT_FOUND;
    goto end;
  }

  switch (pLinkSpeed->type) {
    case SFVMK_MGMT_DEV_OPS_SET:
      speed = pLinkSpeed->autoNeg ? VMK_LINK_SPEED_AUTO : pLinkSpeed->speed;

      /* Set PHY speed */
      status = sfvmk_phyLinkSpeedSet(pAdapter, speed);
      if (status != VMK_OK) {
        SFVMK_ERR(pAdapter, "Link speed change failed with error %s",
                  vmk_StatusToString(status));
        pDevIface->status = status;
        goto end;
      }

      break;
    case SFVMK_MGMT_DEV_OPS_GET:
      status = sfvmk_phyLinkSpeedGet(pAdapter, &pLinkSpeed->speed,
                                     &pLinkSpeed->autoNeg);
      if (status != VMK_OK) {
        SFVMK_ERR(pAdapter, "Link speed get failed with error %s",
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
   return VMK_OK;
}

/*! \brief  A Mgmt callback to Get/Set intr moderation settings
**
** \param[in]  pCookies    pointer to cookie
** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in/out]  pDevIface  pointer to mgmt param
** \param[in/out]  pIntrMod   pointer to intr moderation structure
**
** \return VMK_OK
**
*/
int sfvmk_mgmtIntrModeration(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_intrCoalsParam_t *pIntrMod)
{
  return VMK_OK;
}

/*! \brief  A Mgmt callback to Get PCI BDF and PCI device information
**
** \param[in]  pCookies    pointer to cookie
** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in/out]  pDevIface  pointer to mgmt param
** \param[in/out]  pPciInfo   pointer to sfvmk_pciInfo_s structure
**
** \return VMK_OK
**     Below error values are filled in the status field of
**     sfvmk_mgmtDevInfo_t.
**     VMK_BAD_PARAM:      Null Pointer passed in parameter
**     VMK_NOT_FOUND:      In case of dev not found
**     VMK_NOT_FAILURE:    String copy failed
**
*/
int sfvmk_mgmtPCIInfoCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope      *pEnvelope,
                        sfvmk_mgmtDevInfo_t   *pDevIface,
                        sfvmk_pciInfo_t       *pPciInfo)
{
  sfvmk_adapter_t *pAdapter = NULL;
  int              rc;

  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  if (!pPciInfo) {
    SFVMK_ERROR("pPciInfo: NULL pointer passed as input param");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pDevIface->status = VMK_OK;
  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Pointer to pAdapter is NULL");
    pDevIface->status = VMK_NOT_FOUND;
    goto end;
  }

  rc = vmk_StringCopy(pPciInfo->pciBDF, pAdapter->pciDeviceName.string,
                 SFVMK_PCI_BDF_LEN);
  if (rc != VMK_OK) {
    SFVMK_ERR(pAdapter, "String copy failed with error %s",
                  vmk_StatusToString(rc));
    pDevIface->status = VMK_FAILURE;
    goto end;
  }

  pPciInfo->vendorId = pAdapter->pciDeviceID.vendorID;
  pPciInfo->deviceId = pAdapter->pciDeviceID.deviceID;
  pPciInfo->subVendorId = pAdapter->pciDeviceID.subVendorID;
  pPciInfo->subDeviceId = pAdapter->pciDeviceID.subDeviceID;

end:
  return VMK_OK;
}

/*! \brief  A Mgmt callback to Get and set VPD information
**
** \param[in]  pCookies    pointer to cookie
** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in/out]  pDevIface  pointer to mgmt param
** \param[in/out]  pVpdInfo   pointer to sfvmk_vpdInfo_s structure
**
** \return VMK_OK
**     Below error values are filled in the status field of
**     sfvmk_mgmtDevInfo_t.
**     VMK_NOT_FOUND:      In case of dev not found
**     VMK_BAD_PARAM:      Invalid Ioctl option or wrong
**                         input param
**
*/
int sfvmk_mgmtVPDInfoCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_vpdInfo_t *pVpdInfo)
{
  sfvmk_adapter_t *pAdapter = NULL;
  int              rc;

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
    SFVMK_ERROR("Pointer to pAdapter is NULL");
    pDevIface->status = VMK_NOT_FOUND;
    goto end;
  }

  switch (pVpdInfo->vpdOp) {
    case SFVMK_MGMT_DEV_OPS_GET:
      if ((rc = sfvmk_vpdGetInfo(pAdapter, &pVpdInfo->vpdPayload[0],
                     SFVMK_VPD_MAX_PAYLOAD, pVpdInfo->vpdTag,
                     pVpdInfo->vpdKeyword, &pVpdInfo->vpdLen)) != VMK_OK) {
        SFVMK_ERR(pAdapter, "Get VPD data failed with error %s",
                  vmk_StatusToString(rc));
        pDevIface->status = rc;
        goto end;
      }

      break;
    case SFVMK_MGMT_DEV_OPS_SET:
      if ((rc = sfvmk_vpdSetInfo(pAdapter, &pVpdInfo->vpdPayload[0],
                               pVpdInfo->vpdTag, pVpdInfo->vpdKeyword,
                               pVpdInfo->vpdLen)) != VMK_OK) {
        SFVMK_ERR(pAdapter, "Set VPD data failed with error %s",
                  vmk_StatusToString(rc));
        pDevIface->status = rc;
        goto end;
      }

      break;
    default:
      pDevIface->status = VMK_BAD_PARAM;
      goto end;
  }

end:
  return VMK_OK;
}

