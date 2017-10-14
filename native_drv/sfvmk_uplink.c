/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

/* Default mtu size*/
#define SFVMK_DEFAULT_MTU 1500

/* Callback for uplink operations. */
static vmk_UplinkOps sfvmkUplinkOps = {NULL};

/*! \brief  Update supported link modes for reporting to VMK interface
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_updateSupportedCap(sfvmk_adapter_t *pAdapter)
{
  efx_phy_cap_type_t cap;
  vmk_uint32 supportedCaps;
  vmk_uint32 index = 0;
  vmk_UplinkSupportedMode *pSupportedModes = NULL;
  vmk_LinkMediaType defaultMedia;
  efx_phy_media_type_t mediumType;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  efx_phy_media_type_get(pAdapter->pNic, &mediumType);

  switch (mediumType) {
    case EFX_PHY_MEDIA_KX4:
      defaultMedia = VMK_LINK_MEDIA_BASE_KX4;
      break;
    case EFX_PHY_MEDIA_QSFP_PLUS:
    case EFX_PHY_MEDIA_SFP_PLUS:
      defaultMedia = VMK_LINK_MEDIA_UNKNOWN;
      break;
    default:
      defaultMedia = VMK_LINK_MEDIA_UNKNOWN;
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                          "Unknown media = %d", mediumType);
      break;
  }

  efx_phy_adv_cap_get(pAdapter->pNic, EFX_PHY_CAP_DEFAULT, &supportedCaps);

  pSupportedModes = pAdapter->uplink.supportedModes;

  for (cap = EFX_PHY_CAP_10HDX; cap < EFX_PHY_CAP_NTYPES; cap++) {
    if ((supportedCaps & (1U << cap)) == 0)
      continue;

    pSupportedModes[index].media = defaultMedia;

    switch (cap) {
      case EFX_PHY_CAP_10HDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_10_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_HALF;
        break;

      case EFX_PHY_CAP_10FDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_10_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_FULL;
        break;

      case EFX_PHY_CAP_100HDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_100_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_HALF;
        break;

      case EFX_PHY_CAP_100FDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_100_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_FULL;
        break;

      case EFX_PHY_CAP_1000HDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_1000_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_HALF;
        break;

      case EFX_PHY_CAP_1000FDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_1000_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_FULL;
        break;

      case EFX_PHY_CAP_10000FDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_10000_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_FULL;
        break;

      case EFX_PHY_CAP_40000FDX:
        pSupportedModes[index].speed = VMK_LINK_SPEED_40000_MBPS;
        pSupportedModes[index].duplex = VMK_LINK_DUPLEX_FULL;
        break;

      default:
        SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                            "Unsupported cap = %d", cap);
        continue;
    }
    index++;
  }
  pAdapter->uplink.numSupportedModes = index;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "No of supported modes = %u", index);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

/*! \brief  allocate the resource required for uplink device and initalize all
**          required information.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
**
*/
VMK_ReturnStatus
sfvmk_uplinkDataInit(sfvmk_adapter_t * pAdapter)
{
  vmk_UplinkSharedData *pSharedData = NULL;
  vmk_UplinkRegData *pRegData = NULL;
  VMK_ReturnStatus status = VMK_BAD_PARAM;
  const efx_nic_cfg_t *pNicCfg;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pAdapter->pNic == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL NIC ptr");
    status = VMK_FAILURE;
    goto done;
  }

  /* Create lock to serialize multiple writers's access to protected sharedData area */
  status = sfvmk_createLock(pAdapter, "uplinkLock", SFVMK_SPINLOCK_RANK_UPLINK_LOCK,
                            &pAdapter->uplink.shareDataLock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_lock;
  }

  sfvmk_updateSupportedCap(pAdapter);

  /* Initialize shared data area */
  pSharedData = &pAdapter->uplink.sharedData;
  vmk_VersionedAtomicInit(&pSharedData->lock);

  pSharedData->supportedModes = pAdapter->uplink.supportedModes;
  pSharedData->supportedModesArraySz = pAdapter->uplink.numSupportedModes;

  pSharedData->link.state = VMK_LINK_STATE_DOWN;
  pSharedData->link.speed = 0;
  pSharedData->link.duplex = VMK_LINK_DUPLEX_FULL;

  pSharedData->flags = 0;
  pSharedData->state = VMK_UPLINK_STATE_ENABLED;

  pSharedData->mtu = SFVMK_DEFAULT_MTU;

  pSharedData->queueInfo = &pAdapter->uplink.queueInfo;

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_cfg_get failed");
    status = VMK_FAILURE;
    goto failed_nic_cfg_get;
  }

  vmk_Memcpy(pSharedData->macAddr, pNicCfg->enc_mac_addr, VMK_ETH_ADDR_LENGTH);
  vmk_Memcpy(pSharedData->hwMacAddr, pNicCfg->enc_mac_addr,VMK_ETH_ADDR_LENGTH);

  pRegData = &pAdapter->uplink.regData;
  pRegData->ops = sfvmkUplinkOps;
  pRegData->moduleID = vmk_ModuleCurrentID;
  pRegData->apiRevision = VMKAPI_REVISION;
  pRegData->sharedData = pSharedData;
  pRegData->driverData.ptr = pAdapter;

  status = VMK_OK;
  goto done;

failed_nic_cfg_get:
  sfvmk_destroyLock(pAdapter->uplink.shareDataLock);

failed_create_lock:
done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Dealloocate resources acquired by uplink device
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
**
*/
void
sfvmk_uplinkDataFini(sfvmk_adapter_t * pAdapter)
{

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  sfvmk_destroyLock(pAdapter->uplink.shareDataLock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

