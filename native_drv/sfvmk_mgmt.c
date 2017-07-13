#include "efx_mcdi.h"
#include "efx_regs_mcdi.h"
#include "sfvmk_driver.h"
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
  sfvmk_adapter_t *pAdapter = NULL;
  sfvmk_devHashTable_t *pDevTblEntry = NULL;
  int rc;

  rc = vmk_HashKeyFind(sfvmk_vmkdevHashTable,
                       pMgmtParm->deviceName,
                       (vmk_HashValue *)&pDevTblEntry);
  if (rc != VMK_OK) {
     SFVMK_ERROR("%s: Failed to find node in vmkDevice "
                  "table status: 0x%x", pMgmtParm->deviceName, rc);
     pMgmtParm->status = VMK_NOT_FOUND;
     goto sfvmk_hash_lookup_fail;
  }

  if ((pDevTblEntry == NULL) || (pDevTblEntry->vmkDevice == NULL)) {
     SFVMK_ERROR("%s: No vmkDevice (node: %p)",
                  pMgmtParm->deviceName, pDevTblEntry);
     pMgmtParm->status = VMK_NOT_FOUND;
    goto sfvmk_invalid_dev_table;
  }

  rc = vmk_DeviceGetAttachedDriverData(pDevTblEntry->vmkDevice,
                                       (vmk_AddrCookie *)&pAdapter);
  if (rc != VMK_OK) {
     SFVMK_ERROR("%s: vmk_DeviceGetAttachedDriverData failed with "
                  "status: 0x%x", pMgmtParm->deviceName, rc);
     pMgmtParm->status = VMK_NOT_FOUND;
     goto sfvmk_get_drv_data_fail;
  }

  return pAdapter;

sfvmk_get_drv_data_fail:
sfvmk_invalid_dev_table:
sfvmk_hash_lookup_fail:
  return NULL;
}


/*! \brief  A Mgmt callback to routine to post MCDI commands
**
** \param[in]  pCookies    pointer to cookie
** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in]  pDevIface   pointer to magm param
** \param[in]  pMgmtMcdi   pointer to MCDI cmd struct
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

/*! \brief  A Mgmt callback to routine to post NVRAM req
**
** \param[in]  pCookies    pointer to cookie
** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in]  pDevIface   pointer to magm param
** \param[in]  pCmd        pointer to NVRAM cmd struct
**
** \return: VMK_OK  <success>  error code <failure>
*/
int sfvmk_mgmtNVRAMCallback(vmk_MgmtCookies *pCookies,
                        vmk_MgmtEnvelope *pEnvelope,
                        sfvmk_mgmtDevInfo_t *pDevIface,
                        sfvmk_nvram_cmd_t *pCmd)
{
  return VMK_OK;
}

/*! \brief  A Mgmt callback to various Version info
**
** \param[in]  pCookies    pointer to cookie
** \param[in]  pEnvelope   pointer to vmk_MgmtEnvelope
** \param[in]  pDevIface   pointer to magm param
** \param[in]  pVerInfo    pointer to version info struct
**
** \return: VMK_OK  <success>  error code <failure>
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
  vmk_uint32 subtype;
  vmk_uint16 nvramVer[4];
  int rc;

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Pointer to pAdapter is NULL");
    return ENOENT;
  }

  pNic = pAdapter->pNic;

  switch (pVerInfo->type) {
    case SFVMK_GET_DRV_VERSION:
      memcpy(pVerInfo->version, SFVMK_DRIVER_VERSION_STRING,
              sizeof(SFVMK_DRIVER_VERSION_STRING));
      break;

    case SFVMK_GET_FW_VERSION:
      efx_nic_get_fw_version(pNic, &nicFwVer);
      vmk_Sprintf(pVerInfo->version, "%u.%u.%u.%u",
                   nicFwVer.enfi_mc_fw_version[0],
                   nicFwVer.enfi_mc_fw_version[1],
                   nicFwVer.enfi_mc_fw_version[2],
                   nicFwVer.enfi_mc_fw_version[3]);

      break;

    case SFVMK_GET_ROM_VERSION:
    case SFVMK_GET_UEFI_VERSION:
      nvramType = (pVerInfo->type == SFVMK_GET_UEFI_VERSION) ?
                    EFX_NVRAM_UEFIROM : EFX_NVRAM_BOOTROM;

      if ((rc = efx_nvram_get_version(pNic, nvramType, &subtype,
                                       &nvramVer[0])) != 0)
        return VMK_FAILURE;

      vmk_Sprintf(pVerInfo->version, "%u.%u.%u.%u",
                   nvramVer[0], nvramVer[1],
                   nvramVer[2], nvramVer[3]);

      break;

    default:
      return VMK_BAD_PARAM;
  }

  return VMK_OK;
}

