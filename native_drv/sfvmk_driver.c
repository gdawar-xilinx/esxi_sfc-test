/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

#define SFVMK_MOD_INTERFACE "native"

/* Initialize default value of module parameters */
#define PARAMS(param, defval, type1, type2, min, max, desc)     \
    .param = defval,

sfvmk_modParams_t modParams = {
  SFVMK_MOD_PARAMS_LIST
};
#undef PARAMS

/* Define module parameters with vmkernel */
#define PARAMS(param, defval, type1, type2, min, max, desc)     \
  VMK_MODPARAM_NAMED( param,                                    \
                      modParams.param,                          \
                      type2,                                    \
                      desc);

SFVMK_MOD_PARAMS_LIST
#undef PARAMS

/* ring size for TX and RX */
int sfvmk_txRingEntries = SFVMK_NDESCS;
int sfvmk_rxRingEntries = SFVMK_NDESCS;

#ifdef SFVMK_WITH_UNIT_TESTS
extern void sfvmk_run_ut();
#endif

/* driver callback functions */
static VMK_ReturnStatus sfvmk_attachDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_detachDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_scanDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_quiesceDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_startDevice(vmk_Device device);
static void sfvmk_forgetDevice(vmk_Device device);

/* helper functions*/
static VMK_ReturnStatus sfvmk_verifyDevice(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_createDMAEngine(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_mapBAR(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_unmapBAR(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_setBusMaster(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_estimateRsrcLimits(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_getPciDevice(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_removeUplinkDevice(vmk_Device sfvmk_device);
static void sfvmk_destroyDMAEngine(sfvmk_adapter_t *pAdapter);
static void sfvmk_updateDrvInfo(sfvmk_adapter_t *pAdapter);

static vmk_DeviceOps sfvmk_uplinkDeviceOps = {
  .removeDevice = sfvmk_removeUplinkDevice
};

/*! \brief  Routine to unregister the device.
**
** \param[in]  device handle.
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_removeUplinkDevice(vmk_Device device)
{
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DBG_DRIVER);

  return vmk_DeviceUnregister(device);
}

/*! \brief  Routine to check adapter's family, raise error if proper family
** is not found.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_verifyDevice(sfvmk_adapter_t *pAdapter)
{
  int rc;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_DRIVER);

  /*check adapter's family */
  rc = efx_family(pAdapter->pciDeviceID.vendorID,
                  pAdapter->pciDeviceID.deviceID, &pAdapter->efxFamily);
  if (rc != 0) {
    SFVMK_ERR(pAdapter, "efx_family fail %d", rc);
    return VMK_FAILURE;
  }
  /* driver support only Medford Family */
  if (pAdapter->efxFamily == EFX_FAMILY_MEDFORD) {
    SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_DRIVER);
    return VMK_OK;
  }

  SFVMK_ERR(pAdapter, "Controller family %d not supported", pAdapter->efxFamily);

  return VMK_FAILURE;
}

/*! \brief  Routine to create dma engine.
**
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_createDMAEngine(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status;
  vmk_DMAConstraints dmaConstraints;
  vmk_DMAEngineProps dmaProps;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_DRIVER);

  vmk_Memset(&dmaConstraints, 0, sizeof(dmaConstraints));
  dmaConstraints.addressMask = VMK_ADDRESS_MASK_64BIT;

  vmk_Memset(&dmaProps, 0, sizeof(dmaProps));
  vmk_NameCopy(&dmaProps.name, &sfvmk_ModInfo.driverName);

  dmaProps.module = vmk_ModuleCurrentID;
  dmaProps.device = pAdapter->device;
  dmaProps.constraints = &dmaConstraints;
  dmaProps.flags = VMK_DMA_ENGINE_FLAGS_COHERENT;

  status = vmk_DMAEngineCreate(&dmaProps, &pAdapter->dmaEngine);
  if (status != VMK_OK) {
    dmaConstraints.addressMask = VMK_ADDRESS_MASK_32BIT;
    status = vmk_DMAEngineCreate(&dmaProps, &pAdapter->dmaEngine);
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_DRIVER);

  return (status);
}

/*! \brief  Routine to map memory bar. A lock has been created which will be
**         used by common code to synchronize access to BAR region.
**
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK ion success and failure status on failure.
*/
static VMK_ReturnStatus
sfvmk_mapBAR(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status;
  efsys_bar_t *pBar;
  vmk_Name lockName;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_DRIVER);

  pBar = &pAdapter->bar;

  status = vmk_PCIMapIOResourceWithAttr(vmk_ModuleCurrentID, pAdapter->pciDevice,
                                  EFX_MEM_BAR, VMK_MAPATTRS_READWRITE ,
                                        &pBar->esbBase);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to map BAR%d (%s)", EFX_MEM_BAR,
              vmk_StatusToString(status));
  }
  vmk_NameFormat(&lockName, "sfvmk_memBarLock");
  status = sfvmk_createLock(lockName.string, VMK_SPINLOCK_RANK_LOWEST,
                            &pBar->esbLock);
  if (status != VMK_OK) {
    vmk_PCIUnmapIOResource(vmk_ModuleCurrentID, pAdapter->pciDevice,
                           EFX_MEM_BAR);
    SFVMK_ERR(pAdapter, "Failed to create lock for BAR%d (%s)", EFX_MEM_BAR,
              vmk_StatusToString(status));
  }
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_DRIVER);
  return (status);
}

/*! \brief  Routine to unmap memory bar.
**
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_unmapBAR(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_OK;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_DRIVER);

  sfvmk_destroyLock(pAdapter->bar.esbLock);

  status = vmk_PCIUnmapIOResource(vmk_ModuleCurrentID, pAdapter->pciDevice,
                                  EFX_MEM_BAR);
  if (status != VMK_OK)
    SFVMK_ERR(pAdapter, "Failed to unmap memory BAR status %s",
              vmk_StatusToString(status));
  else
    SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_INFO,
              "Freed Bar %d ", EFX_MEM_BAR);

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_DRIVER);
  return status;
}

/*! \brief  Routine to destory dma engine.
**
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK or VMK_FAILURE
*/
static void
sfvmk_destroyDMAEngine(sfvmk_adapter_t *pAdapter)
{
  SFVMK_NULL_PTR_CHECK(pAdapter);
  vmk_DMAEngineDestroy(pAdapter->dmaEngine);
}

/*! \brief  Routine to set bus mastering mode
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_setBusMaster(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 cmd;
  VMK_ReturnStatus status = VMK_OK;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_DRIVER);

  status = vmk_PCIReadConfig(vmk_ModuleCurrentID, pAdapter->pciDevice,
                      VMK_PCI_CONFIG_ACCESS_16, SFVMK_PCI_COMMAND, &cmd);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed to read PCI config : %u (%s)",
               SFVMK_PCI_COMMAND, vmk_StatusToString(status));
    return status;
  }

  if (!(cmd & SFVMK_PCI_COMMAND_BUS_MASTER)) {

    SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 0, "enabling bus mastering");

    cmd |= SFVMK_PCI_COMMAND_BUS_MASTER;
    status = vmk_PCIWriteConfig(vmk_ModuleCurrentID, pAdapter->pciDevice,
                          VMK_PCI_CONFIG_ACCESS_16, SFVMK_PCI_COMMAND, cmd);
    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed to write PCI config. %u, data: %u (%s)",
                  SFVMK_PCI_COMMAND, cmd, vmk_StatusToString(status));
      return status;
    }
  }
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_DRIVER);

  return VMK_OK;

}

/*! \brief  Routine to estimate resource ( eg number of rxq, txq and evq)
**
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK or VMK_FAILURE
*/
VMK_ReturnStatus
sfvmk_estimateRsrcLimits(sfvmk_adapter_t *pAdapter)
{
  efx_drv_limits_t limits;
  unsigned int evqMax;
  uint32_t evqAllocated;
  uint32_t rxqAllocated;
  uint32_t txqAllocated;
  VMK_ReturnStatus status = VMK_OK;
  int rc;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_DRIVER);

  /* Limit the number of event queues to:
  *  - number of CPUs
  *  - hardwire maximum RSS channels
  *  - administratively specified maximum RSS channelsi*/

  vmk_Memset(&limits, 0, sizeof(limits));
  /* get number of queues supported ( this is depend on number of cpus) */
  status = vmk_UplinkQueueGetNumQueuesSupported(EFX_MAXRSS, EFX_MAXRSS,
                    &limits.edl_max_txq_count , &limits.edl_max_rxq_count);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed in vmk_UplinkQueueGetNumQueuesSupported: "
              " with err %s", vmk_StatusToString(status));
    return status;
  }

  evqMax = MIN(limits.edl_max_rxq_count ,
                (limits.edl_max_txq_count - SFVMK_TXQ_NTYPES +1));

  if (pAdapter->maxRssChannels > 0)
    evqMax = MIN(evqMax, pAdapter->maxRssChannels);

  limits.edl_min_evq_count = SFVMK_MIN_EVQ_COUNT;
  limits.edl_max_evq_count = evqMax;
  limits.edl_min_txq_count = SFVMK_TXQ_NTYPES;
  limits.edl_min_rxq_count = limits.edl_min_evq_count;
  limits.edl_max_rxq_count = evqMax;
  limits.edl_max_txq_count = evqMax + SFVMK_TXQ_NTYPES - 1;

  /* set the limits in fw */
  efx_nic_set_drv_limits(pAdapter->pNic, &limits);

  if ((rc = efx_nic_init(pAdapter->pNic)) != 0) {
    SFVMK_ERR(pAdapter, "Failed to init nic with err %d", rc);
    return VMK_FAILURE;
  }

  rc = efx_nic_get_vi_pool(pAdapter->pNic, &evqAllocated, &rxqAllocated,
                            &txqAllocated);
  if (rc != 0) {
    SFVMK_ERR(pAdapter, "Failed to get vi pool with err %d", rc);
    efx_nic_fini(pAdapter->pNic);
    return VMK_FAILURE;
  }

  if (txqAllocated < SFVMK_TXQ_NTYPES) {
    efx_nic_fini(pAdapter->pNic);
    SFVMK_ERR(pAdapter, " txqAllocated should be more than type of txqs");
    return VMK_FAILURE;
  }

  pAdapter->evqMax = MIN(evqAllocated, evqMax);
  pAdapter->evqMax = MIN(rxqAllocated, pAdapter->evqMax);
  pAdapter->evqMax = MIN(txqAllocated - (SFVMK_TXQ_NTYPES - 1),
                          pAdapter->evqMax);

  VMK_ASSERT(pAdapter->evqMax <= evqMax);

  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_DBG,
              "max txq = %d\t max rxq = %d\t max evq = %d",
               txqAllocated, rxqAllocated, pAdapter->evqMax );

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_DRIVER);

  return VMK_OK;
}

/*! \brief  Routine to update driver information
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: none
*/
static void sfvmk_updateDrvInfo(sfvmk_adapter_t *pAdapter)
{
  vmk_UplinkDriverInfo *drvInfo;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  drvInfo = &pAdapter->sharedData.driverInfo;

  SFVMK_SHARED_AREA_BEGIN_WRITE(pAdapter);
  vmk_NameInitialize(&drvInfo->driver, sfvmk_ModInfo.driverName.string);
  vmk_NameInitialize(&drvInfo->moduleInterface, SFVMK_MOD_INTERFACE);
  vmk_NameInitialize(&drvInfo->version, SFVMK_DRIVER_VERSION_STRING);
  /* TODO FW version needs to be populated */
  SFVMK_SHARED_AREA_END_WRITE(pAdapter);

  return;
}

/*! \brief  Routine to get pci device information such as vendor id , device ID
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_getPciDevice(sfvmk_adapter_t *pAdapter)
{
  vmk_AddrCookie pciDeviceCookie;
  VMK_ReturnStatus status;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_DRIVER);

  /* get pci device */
  status = vmk_DeviceGetRegistrationData(pAdapter->device, &pciDeviceCookie);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "DeviceGetRegistrationData failed status: %s",
              vmk_StatusToString(status));
    return status;
  }

  pAdapter->pciDevice = pciDeviceCookie.ptr;

  /* query PCI device information */
  status = vmk_PCIQueryDeviceID(pAdapter->pciDevice, &pAdapter->pciDeviceID);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "PCIQueryDeviceID failed status: %s",
              vmk_StatusToString(status));
    return status;
  }

  status = sfvmk_verifyDevice(pAdapter);
  if (status != VMK_OK) {
    return status;
  }

  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_INFO,"Found kernel"
            "device with following Dev Id: %x\n Vend ID: %x\n Sub Dev ID: %x\n"
            "Sub Vend ID: %x\n ", pAdapter->pciDeviceID.deviceID,
            pAdapter->pciDeviceID.vendorID, pAdapter->pciDeviceID.subDeviceID,
            pAdapter->pciDeviceID.subVendorID);

  status = vmk_PCIQueryDeviceAddr(pAdapter->pciDevice, &pAdapter->pciDeviceAddr);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "PCIQueryDeviceAddr failed, status: %s",
              vmk_StatusToString(status));
    return status;
  }

  vmk_StringFormat(pAdapter->pciDeviceName.string,
                    sizeof(pAdapter->pciDeviceName.string),
                    NULL, SFVMK_SBDF_FMT, pAdapter->pciDeviceAddr.seg,
                    pAdapter->pciDeviceAddr.bus, pAdapter->pciDeviceAddr.dev,
                    pAdapter->pciDeviceAddr.fn);

  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_DBG,
            "Found kernel device");

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_DRIVER);
  return VMK_OK;
}

/*! \brief  Routine to get the supported capability (link speed etc)
**
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_updateSupportedCap(sfvmk_adapter_t *pAdapter)
{
  efx_phy_cap_type_t capMask = EFX_PHY_CAP_10HDX;
  vmk_uint32 supportedCaps, index = 0;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_DRIVER);

  efx_phy_adv_cap_get(pAdapter->pNic, EFX_PHY_CAP_PERM, &supportedCaps);

  do
  {
    while(!(supportedCaps & (1 << capMask)))
     capMask++;

    switch(capMask)
    {
      case EFX_PHY_CAP_10HDX:
        pAdapter->supportedModes[index].speed = VMK_LINK_SPEED_10_MBPS;
        pAdapter->supportedModes[index++].duplex = VMK_LINK_DUPLEX_HALF;
        break ;

      case EFX_PHY_CAP_10FDX:
        pAdapter->supportedModes[index].speed = VMK_LINK_SPEED_10_MBPS;
        pAdapter->supportedModes[index++].duplex = VMK_LINK_DUPLEX_FULL;
        break ;

      case EFX_PHY_CAP_100HDX:
        pAdapter->supportedModes[index].speed = VMK_LINK_SPEED_100_MBPS;
        pAdapter->supportedModes[index++].duplex = VMK_LINK_DUPLEX_HALF;
        break ;

      case EFX_PHY_CAP_100FDX:
        pAdapter->supportedModes[index].speed = VMK_LINK_SPEED_100_MBPS;
        pAdapter->supportedModes[index++].duplex = VMK_LINK_DUPLEX_FULL;
        break ;

      case EFX_PHY_CAP_1000HDX:
        pAdapter->supportedModes[index].speed = VMK_LINK_SPEED_1000_MBPS;
        pAdapter->supportedModes[index++].duplex = VMK_LINK_DUPLEX_HALF;
        break ;

      case EFX_PHY_CAP_1000FDX:
        pAdapter->supportedModes[index].speed = VMK_LINK_SPEED_1000_MBPS;
        pAdapter->supportedModes[index++].duplex = VMK_LINK_DUPLEX_FULL;
        break ;

      case EFX_PHY_CAP_10000FDX:
        pAdapter->supportedModes[index].speed = VMK_LINK_SPEED_10000_MBPS;
        pAdapter->supportedModes[index++].duplex = VMK_LINK_DUPLEX_FULL;
        break ;

      case EFX_PHY_CAP_40000FDX:
        pAdapter->supportedModes[index].speed = VMK_LINK_SPEED_40000_MBPS;
        pAdapter->supportedModes[index++].duplex = VMK_LINK_DUPLEX_FULL;
        break ;

      default:
        break;
    }

    capMask++;

  } while (capMask < EFX_PHY_CAP_NTYPES );

  pAdapter->supportedModesArraySz = index ;

  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_DBG,
            "no of supported modes = %d", index);
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_DRIVER);

}

/*! \brief  Routine to create a VMK Helper queue
**
** \param  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK or VMK_FAILURE
**
*/
static VMK_ReturnStatus
sfvmk_createHelper(sfvmk_adapter_t *pAdapter)
{
  vmk_HelperProps props;
  VMK_ReturnStatus status;

  SFVMK_NULL_PTR_CHECK(pAdapter);
  SFVMK_DBG_FUNC_ENTRY(pAdapter,SFVMK_DBG_DRIVER);

  vmk_NameFormat(&props.name, "%s-helper",
                 SFVMK_NAME_TO_STRING(sfvmk_ModInfo.driverName));
  props.heap = sfvmk_ModInfo.heapID;
  props.useIrqSpinlock = VMK_TRUE;
  props.preallocRequests = VMK_FALSE;
  props.blockingSubmit = VMK_FALSE;
  props.maxRequests = 8;
  props.mutables.minWorlds = 0;
  props.mutables.maxWorlds = 1;
  props.mutables.maxIdleTime = 0;
  props.mutables.maxRequestBlockTime = 0;
  props.tagCompare = NULL;
  props.constructor = NULL;
  props.constructorArg = (vmk_AddrCookie) NULL;

  status = vmk_HelperCreate(&props, &pAdapter->helper);
  if (status != VMK_OK) {
     SFVMK_ERR(pAdapter, "Failed to create helper world queue %s",
                vmk_StatusToString(status));
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_DRIVER);
  return status;
}

/* \brief  Routine to Destroy a VMK Helper queue
**
** \param  adapter pointer to sfvmk_adapter_t
**
** \return: None
**
*/
static void
sfmvk_destroyHelper(sfvmk_adapter_t *pAdapter)
{
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_DRIVER);
  vmk_HelperDestroy(pAdapter->helper);
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_DRIVER);
}

/************************************************************************
 * Device Driver Operations
 ************************************************************************/

static vmk_DriverOps sfvmk_DriverOps = {
   .attachDevice  = sfvmk_attachDevice,
   .detachDevice  = sfvmk_detachDevice,
   .scanDevice    = sfvmk_scanDevice,
   .quiesceDevice = sfvmk_quiesceDevice,
   .startDevice   = sfvmk_startDevice,
   .forgetDevice  = sfvmk_forgetDevice,
};

/*! \brief  Callback routine for the device layer to announce device to the
** driver.
**
** \param[in]  dev  pointer to device
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_attachDevice(vmk_Device dev)
{
  efx_nic_t *pNic;
  efsys_lock_t *pNicLock;
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_adapter_t *pAdapter = NULL;
  unsigned int error;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DBG_DRIVER);

  /* allocation memory for adapter */
  pAdapter = sfvmk_memPoolAlloc(sizeof(sfvmk_adapter_t));
  if (pAdapter == NULL) {
    goto sfvmk_adapter_alloc_fail;
  }

  pAdapter->maxRssChannels = 0;
  pAdapter->device = dev;

  /*Initializing ring enteries */
  pAdapter->rxqEntries = sfvmk_rxRingEntries;
  pAdapter->txqEntries = sfvmk_txRingEntries;

  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_DBG,
             "allocated adapter =%p", (void *)pAdapter);

  status = sfvmk_getPciDevice(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed to get PCI device information with"
               "error %s", vmk_StatusToString(status));
    goto sfvmk_get_pci_dev_fail;
  }

  status = sfvmk_createDMAEngine(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "DMA Engine create failed with error %s",
                vmk_StatusToString(status));
    goto sfvmk_DMA_eng_create_fail;
  }

  status = sfvmk_mapBAR(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed to map memory bar with error %s",
               vmk_StatusToString(status));
    goto sfvmk_map_bar_fail;
  }

  status = sfvmk_setBusMaster(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed to set bus master with error %s",
               vmk_StatusToString(status));
    goto sfvmk_bus_master_fail;
  }

  pNicLock = &pAdapter->nicLock;
  status = sfvmk_createLock("nicLock", VMK_SPINLOCK_RANK_LOWEST, &pNicLock->lock);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed to create mutex lock for nic with err %s",
               vmk_StatusToString(status));
    goto sfvmk_create_lock_fail;
  }
  if ((error = efx_nic_create(pAdapter->efxFamily, (efsys_identifier_t *)pAdapter,
      &pAdapter->bar, pNicLock, &pNic)) != 0)
  {
    SFVMK_ERR(pAdapter, "failed in efx_nic_create with error %x", error);
    goto sfvmk_nic_create_fail;
  }

  pAdapter->pNic = pNic;

  /* Initialize MCDI to talk to the microcontroller. */
  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_DBG, "mcdi_init...");
  if ((error = sfvmk_mcdiInit(pAdapter)) != 0) {
    SFVMK_ERR(pAdapter, "failed in sfvmk_mcdiInit with err %x", error);
    goto sfvmk_mcdi_init_fail;
  }

  /* Probe  NIC and build the configuration data area. */
  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_DBG, "nic_probe...");
  if ((error = efx_nic_probe(pNic)) != 0) {
    SFVMK_ERR(pAdapter, "failed in efx_nic_probe with err %x", error);
    goto sfvmk_nic_probe_fail;
  }

  /* Initialize NVRAM. */
  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_DBG, "nvram_init...");
  if ((error = efx_nvram_init(pNic)) != 0) {

    SFVMK_ERR(pAdapter, "failed in efx_nvram_init with err %x", error);
    goto sfvmk_nvram_init_fail;
  }

  /* Initialize VPD. */
  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_DBG, "vpd_init...");
  if ((error = efx_vpd_init(pNic)) != 0) {
    SFVMK_ERR(pAdapter, "failed in efx_vpd_init with err %x", error);
    goto sfvmk_vpd_init_fail;
  }

  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_DBG, "mcdi_new_epoch...");
  efx_mcdi_new_epoch(pNic);

  /* Reset NIC. */
  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_DBG, "nic_reset...");
  if ((error = efx_nic_reset(pNic)) != 0) {
    SFVMK_ERR(pAdapter, "failed in efx_nic_reset with err %x", error);
    goto sfvmk_nic_reset_fail;
  }

  status = sfvmk_estimateRsrcLimits(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed in sfvmk_estimateRsrcLimits status %x", status);
    goto sfvmk_rsrc_info_fail;
  }

  status = sfvmk_intrInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed in sfvmk_intrInit with err  %s",
              vmk_StatusToString(status));
    goto sfvmk_intr_init_fail;
  }

  status = sfvmk_evInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed in sfvmk_evInit with err  %s",
               vmk_StatusToString(status));
    goto sfvmk_ev_init_fail;
  }

  status = sfvmk_portInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed in sfvmk_portInit with err  %s",
              vmk_StatusToString(status));
    goto sfvmk_port_init_fail;
  }

  status = sfvmk_rxInit(pAdapter);
  if (status != VMK_OK)  {
    SFVMK_ERR(pAdapter, "failed in sfvmk_rxInit with err %s",
              vmk_StatusToString(status));
    goto sfvmk_rx_init_fail;
  }

  status = sfvmk_txInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed in sfvmk_txInit with err %s",
              vmk_StatusToString(status));
    goto sfvmk_tx_init_fail;
  }
  sfvmk_updateSupportedCap(pAdapter);

  status = sfvmk_mutexInit("adapterLock", &pAdapter->lock);
  if(status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed in init adapter Lock, with err %s",
              vmk_StatusToString(status));
    goto sfvmk_mutex_fail;
  }

  status = sfvmk_initUplinkData(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed in sfvmk_initUplinkData with err %s",
              vmk_StatusToString(status));
    goto sfvmk_init_uplink_data_fail;
  }

  /* updating driver infor */
  (void)sfvmk_updateDrvInfo(pAdapter);

  status = vmk_DeviceSetAttachedDriverData(dev, pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed in vmk_DeviceSetAttachedDriverData"
              "with err %s", vmk_StatusToString(status));
    goto sfvmk_set_drvdata_fail;
  }

  status = sfvmk_createHelper(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed while creating helper"
              "with err %s", vmk_StatusToString(status));
    goto sfvmk_create_helper_fail;
  }
  /* calling nic_fini as nic init is happening in startIO */
  efx_nic_fini(pAdapter->pNic);

  pAdapter->initState = SFVMK_REGISTERED;

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DBG_DRIVER);

  return VMK_OK;

sfvmk_create_helper_fail:
sfvmk_set_drvdata_fail:
  sfvmk_destroyUplinkData(pAdapter);
sfvmk_init_uplink_data_fail:
  sfvmk_mutexDestroy(pAdapter->lock);
sfvmk_mutex_fail:
  sfvmk_txFini(pAdapter);
sfvmk_tx_init_fail:
  sfvmk_rxFini(pAdapter);
sfvmk_rx_init_fail:
  sfvmk_portFini(pAdapter);
sfvmk_port_init_fail:
  sfvmk_evFini(pAdapter);
sfvmk_ev_init_fail:
  sfvmk_freeInterrupts(pAdapter);
sfvmk_intr_init_fail:
sfvmk_rsrc_info_fail:
  efx_nic_fini(pNic);
sfvmk_nic_reset_fail:
  efx_vpd_fini(pNic);
sfvmk_vpd_init_fail:
  efx_nvram_fini(pNic);
sfvmk_nvram_init_fail:
  efx_nic_unprobe(pNic);
sfvmk_nic_probe_fail:
  sfvmk_mcdiFini(pAdapter);
sfvmk_mcdi_init_fail:
  pAdapter->pNic = NULL;
  efx_nic_destroy(pNic);
sfvmk_nic_create_fail:
  sfvmk_destroyLock(pNicLock->lock);
sfvmk_create_lock_fail:
sfvmk_bus_master_fail:
  sfvmk_unmapBAR(pAdapter);
sfvmk_map_bar_fail:
  sfvmk_destroyDMAEngine(pAdapter);
sfvmk_DMA_eng_create_fail:
sfvmk_get_pci_dev_fail:
  sfvmk_memPoolFree(pAdapter, sizeof(sfvmk_adapter_t));
sfvmk_adapter_alloc_fail:
  return status;
}

/*! \brief: Callback routine for the device layer to notify the driver to
** bring up the specified device.
**
** \param[in]  dev  pointer to device
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_startDevice(vmk_Device dev)
{
  sfvmk_adapter_t *pAdapter = NULL;
  VMK_ReturnStatus status;

  /* although the Native Driver document states that this callback should
  *  put the driver in the IO able state.
  *  Driver is moved into IO able state as part of UplinkStartIO call. */

  status = vmk_DeviceGetAttachedDriverData(dev,
                                            (vmk_AddrCookie *)&pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "sfvmk_startDevice failed, status %s",
              vmk_StatusToString(status));
    return status;
  }

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_DRIVER);

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DBG_DRIVER);

  return VMK_OK;
}

/*! \brief: Callback routine for the device layer to notify the driver to
**          scan for new device
**
** \param[in]  dev  pointer to vmkDevice
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_scanDevice(vmk_Device device)
{
  VMK_ReturnStatus status = 0;
  sfvmk_adapter_t *pAdapter;
  vmk_Name busName;
  vmk_DeviceID *pDeviceID;
  vmk_DeviceProps deviceProps;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DBG_DRIVER);

  status = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *) &pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "vmk_DeviceGetAttachedDriverData failed, status %s",
              vmk_StatusToString(status));
    return status;
  }

  pDeviceID = &pAdapter->deviceID;
  status = vmk_NameInitialize(&busName, VMK_LOGICAL_BUS_NAME);
  VMK_ASSERT(status == VMK_OK);
  status = vmk_BusTypeFind(&busName, &pDeviceID->busType);
  VMK_ASSERT(status == VMK_OK);

  status = vmk_LogicalCreateBusAddress(sfvmk_ModInfo.driverID,
                     pAdapter->device,
                     0,
                     &pDeviceID->busAddress,
                     &pDeviceID->busAddressLen);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "vmk_LogicalCreateBusAddress failed, status %s",
                vmk_StatusToString(status));
    return status;
  }

  pDeviceID->busIdentifier = VMK_UPLINK_DEVICE_IDENTIFIER;
  pDeviceID->busIdentifierLen = sizeof(VMK_UPLINK_DEVICE_IDENTIFIER) - 1;
  deviceProps.registeringDriver = sfvmk_ModInfo.driverID;
  deviceProps.deviceID = pDeviceID;
  deviceProps.deviceOps = &sfvmk_uplinkDeviceOps;
  deviceProps.registeringDriverData.ptr = pAdapter;
  deviceProps.registrationData.ptr = &pAdapter->regData;

  status = vmk_DeviceRegister(&deviceProps, device, &pAdapter->uplinkDevice);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to register uplink device: %s",
              vmk_StatusToString(status));
  }

  vmk_LogicalFreeBusAddress(sfvmk_ModInfo.driverID, pDeviceID->busAddress);
  vmk_BusTypeRelease(pDeviceID->busType);

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DBG_DRIVER);

  return status;
}

/*! \brief : Callback routine for the device layer to notify the driver to
** release control of a driver.
**
** \param[in]  dev  pointer to vmkDevice
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_detachDevice(vmk_Device dev)
{
  efx_nic_t *pNic = NULL ;
  VMK_ReturnStatus status;
  sfvmk_adapter_t *pAdapter = NULL;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DBG_DRIVER);

  status = vmk_DeviceGetAttachedDriverData(dev, (vmk_AddrCookie *)&pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed in vmk_DeviceGetAttachedDriverData: %s",
              vmk_StatusToString(status));
    return status ;
  }

  SFVMK_NULL_PTR_CHECK(pAdapter);

  sfmvk_destroyHelper(pAdapter);

  sfvmk_destroyUplinkData(pAdapter);
  sfvmk_mutexDestroy(pAdapter->lock);
  sfvmk_txFini(pAdapter);
  sfvmk_rxFini(pAdapter);
  sfvmk_portFini(pAdapter);
  sfvmk_evFini(pAdapter);

  sfvmk_freeInterrupts(pAdapter);
  /* Tear down common code subsystems. */
  pNic = pAdapter->pNic;
  efx_vpd_fini(pNic);
  efx_nvram_fini(pNic);
  efx_nic_unprobe(pNic);

  /* Tear down MCDI. */
  sfvmk_mcdiFini(pAdapter);

  /* Destroy common code context. */
  pAdapter->pNic = NULL;
  efx_nic_destroy(pNic);
  sfvmk_destroyLock(pAdapter->nicLock.lock);

  /* vmk related resource deallocation */
  sfvmk_unmapBAR(pAdapter);
  sfvmk_destroyDMAEngine(pAdapter);
  sfvmk_memPoolFree(pAdapter, sizeof(sfvmk_adapter_t));

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DBG_DRIVER);

  return VMK_OK;
}

/*! \brief : Callback routine for the device layer to notify the driver to
** shutdown the specified device.
**
** \param[in]  dev  pointer to vmkDevice
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_quiesceDevice(vmk_Device dev)
{
  /* TODO: functinality will be added along with startDevice implementation */
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DBG_DRIVER);

  return VMK_OK;
}

/*! \brief: Callback routine for the device layer to notify the driver the
** specified device is not responsive.
**
** \param[in]  dev  pointer to vmkDevice
**
** \return: VMK_OK or VMK_FAILURE
*/
static void
sfvmk_forgetDevice(vmk_Device dev)
{
  /* no functionality expected by VMK kernel */
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DBG_DRIVER);

  return;
}

/*! \brief: This function registers the driver as network driver
**
** \param[in] : None
**
** \return: VMK_OK or VMK_FAILURE
*/
VMK_ReturnStatus
sfvmk_driverRegister(void)
{
  VMK_ReturnStatus status;
  vmk_DriverProps driverProps;
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DBG_DRIVER);

  /* Populate driverProps */
  driverProps.moduleID = vmk_ModuleCurrentID;
  vmk_NameCopy(&driverProps.name, &sfvmk_ModInfo.driverName);
  driverProps.ops = &sfvmk_DriverOps;
  driverProps.privateData = (vmk_AddrCookie)NULL;

  /* Register Driver with the device layer */
  status = vmk_DriverRegister(&driverProps, &sfvmk_ModInfo.driverID);

  if (status != VMK_OK)
    SFVMK_ERROR("Initialization of SFC driver failed with status: %s",
                vmk_StatusToString(status));

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DBG_DRIVER);

  return status;
}

/*! \brief: Function to unregister the device that was previous registered.
**
** \param[in] : None
**
** \return: VMK_OK or VMK_FAILURE
*/
void
sfvmk_driverUnregister(void)
{
  vmk_DriverUnregister(sfvmk_ModInfo.driverID);
}

