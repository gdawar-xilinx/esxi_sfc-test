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
** \return: VMK_OK  <success>  error code <failure>
*/
int
sfvmk_mgmtMcdiCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_mcdiRequest2_t *pMgmtMcdi)
{
  sfvmk_adapter_t *pAdapter = NULL;
  efx_mcdi_req_t emr;
  vmk_int8 *pMcdiBuf;
  int rc;

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Pointer to pAdapter is NULL");
    rc = ENOENT;
    goto sfvmk_dev_null;
  }

  if (pMgmtMcdi->inlen > SFVMK_MCDI_MAX_PAYLOAD ||
      pMgmtMcdi->outlen > SFVMK_MCDI_MAX_PAYLOAD) {
    SFVMK_ERR(pAdapter, "Invalid Length");
    rc = EINVAL;
    goto sfvmk_invalid_len;
  }

  pMcdiBuf = sfvmk_MemAlloc(SFVMK_MCDI_MAX_PAYLOAD);
  if (pMcdiBuf == NULL) {
    SFVMK_ERR(pAdapter, "Alloc failed for pMcdiBuf");
    rc = ENOMEM;
    goto sfvmk_alloc_failed;
  }

  memcpy(pMcdiBuf, pMgmtMcdi->payload, pMgmtMcdi->inlen);

  emr.emr_cmd = pMgmtMcdi->cmd;
  emr.emr_in_buf = pMcdiBuf;
  emr.emr_in_length = pMgmtMcdi->inlen;

  emr.emr_out_buf = pMcdiBuf;
  emr.emr_out_length = pMgmtMcdi->outlen;

  rc = sfvmk_mcdiIOHandler(pAdapter, &emr);
  if (rc != VMK_OK) {
    goto sfvmk_mcdi_fail;
  }

  pMgmtMcdi->host_errno = emr.emr_rc;
  pMgmtMcdi->cmd = emr.emr_cmd;
  pMgmtMcdi->outlen = emr.emr_out_length_used;
  memcpy(pMgmtMcdi->payload, pMcdiBuf, pMgmtMcdi->outlen);

  vmk_HeapFree(sfvmk_ModInfo.heapID, pMcdiBuf);

  return VMK_OK;

sfvmk_mcdi_fail:
  sfvmk_MemFree(pMcdiBuf);

sfvmk_alloc_failed:
sfvmk_dev_null:
sfvmk_invalid_len:
  return rc;
}

/*! \brief  A Mgmt callback routine to post NVRAM req
**
** \param[in]  pCookies    pointer to cookie
** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in/out]  pDevIface   pointer to mgmt param
** \param[in/out]  pCmd        pointer to NVRAM cmd struct
**
** \return: VMK_OK  <success>  error code <failure>
*/
int sfvmk_mgmtNVRAMCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_nvramCmd_t *pCmd)
{
  sfvmk_adapter_t *pAdapter = NULL;
  vmk_int8 *pNvramBuf = NULL;
  efx_nic_t *pNic;
  efx_nvram_type_t type;
  int rc;

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Pointer to pAdapter is NULL");
    rc = ENOENT;
    goto sfvmk_fail;
  }

  pNic = pAdapter->pNic;

  switch (pCmd->type) {
    case SFVMK_NVRAM_TYPE_BOOTROM:
      type = EFX_NVRAM_BOOTROM;
      break;
    case SFVMK_NVRAM_TYPE_BOOTROM_CFG:
      type = EFX_NVRAM_BOOTROM_CFG;
      break;
    case SFVMK_NVRAM_TYPE_MC:
      type = EFX_NVRAM_MC_FIRMWARE;
      break;
    case SFVMK_NVRAM_TYPE_MC_GOLDEN:
      type = EFX_NVRAM_MC_GOLDEN;
      if (pCmd->op == SFVMK_NVRAM_OP_WRITE ||
          pCmd->op == SFVMK_NVRAM_OP_ERASE ||
          pCmd->op == SFVMK_NVRAM_OP_SET_VER) {
        rc = ENOTSUP;
        goto sfvmk_fail;
      }
      break;
    case SFVMK_NVRAM_TYPE_PHY:
      type = EFX_NVRAM_PHY;
      break;
    case SFVMK_NVRAM_TYPE_NULL_PHY:
      type = EFX_NVRAM_NULLPHY;
      break;
    case SFVMK_NVRAM_TYPE_FPGA: /* PTP timestamping FPGA */
      type = EFX_NVRAM_FPGA;
      break;
    case SFVMK_NVRAM_TYPE_FCFW:
      type = EFX_NVRAM_FCFW;
      break;
    case SFVMK_NVRAM_TYPE_CPLD:
      type = EFX_NVRAM_CPLD;
      break;
    case SFVMK_NVRAM_TYPE_FPGA_BACKUP:
      type = EFX_NVRAM_FPGA_BACKUP;
      break;
    case SFVMK_NVRAM_TYPE_UEFIROM:
      type = EFX_NVRAM_UEFIROM;
      break;
    case SFVMK_NVRAM_TYPE_DYNAMIC_CFG:
      type = EFX_NVRAM_DYNAMIC_CFG;
      break;
    default:
      rc = EINVAL;
      goto sfvmk_fail;
  }

  if (pCmd->size > sizeof (pCmd->data)) {
    rc = ENOSPC;
    goto sfvmk_fail;
  }

  switch (pCmd->op) {
    case SFVMK_NVRAM_OP_SIZE:
    {
      size_t size;
      if ((rc = efx_nvram_size(pNic, type, &size)) != 0)
        goto sfvmk_nvram_op_failed;
      pCmd->size = (uint32_t)size;
      break;
    }
    case SFVMK_NVRAM_OP_READ:
    case SFVMK_NVRAM_OP_WRITE:
    {
       boolean_t write = B_FALSE;

       pNvramBuf = sfvmk_MemAlloc(SFVMK_NVRAM_MAX_PAYLOAD);
       if (pNvramBuf == NULL) {
         rc = ENOMEM;
         goto sfvmk_alloc_failed;
       }

       if (pCmd->op == SFVMK_NVRAM_OP_WRITE) {
         write = B_TRUE;
         memcpy(pNvramBuf, pCmd->data, pCmd->size);
       }

       if ((rc = sfvmk_nvram_rw(pAdapter, pNvramBuf, pCmd->size,
                                pCmd->offset, type, write)) != 0)
         goto sfvmk_nvram_rw_failed;

       if (pCmd->op == SFVMK_NVRAM_OP_READ)
         memcpy(pCmd->data, pNvramBuf, pCmd->size);

       sfvmk_MemFree(pNvramBuf);
       break;
    }
    case SFVMK_NVRAM_OP_ERASE:
      if ((rc = sfvmk_nvram_erase(pAdapter, type)) != 0)
        goto sfvmk_nvram_op_failed;
      break;
    case SFVMK_NVRAM_OP_GET_VER:
      if ((rc = efx_nvram_get_version(pNic, type, &pCmd->subtype,
          &pCmd->version[0])) != 0)
        goto sfvmk_nvram_op_failed;
      break;
    case SFVMK_NVRAM_OP_SET_VER:
      if ((rc = efx_nvram_set_version(pNic, type,
          &pCmd->version[0])) != 0)
        goto sfvmk_nvram_op_failed;
      break;
    default:
      rc = ENOTSUP;
      goto sfvmk_fail;
  }

  return 0;

sfvmk_nvram_rw_failed:
  sfvmk_MemFree(pNvramBuf);
sfvmk_nvram_op_failed:
sfvmk_alloc_failed:
sfvmk_fail:
  return rc;
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

/*! \brief  A Mgmt callback for Get/Set Link state
**
** \param[in]  pCookies    pointer to cookie
** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in/out]  pDevIface   pointer to mgmt param
** \param[in/out]  pLinkOps   pointer to link ops structure
**
** \return VMK_OK
**
*/
int sfvmk_mgmtLinkStatusUpdate(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_linkStatus_t *pLinkOps)
{
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
**
*/
int sfvmk_mgmtLinkSpeedUpdate(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_linkSpeed_t *pLinkSpeed)
{
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

