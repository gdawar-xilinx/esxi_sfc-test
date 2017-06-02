/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"
/* ring size for TX and RX */
static const int sfvmk_txRingEntries = SFVMK_NDESCS;
static const int sfvmk_rxRingEntries = SFVMK_NDESCS;

static VMK_ReturnStatus sfvmk_AttachDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_DetachDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_ScanDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_ShutdownDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_StartDevice(vmk_Device device);
static void sfvmk_ForgetDevice(vmk_Device device);

/* helper functions*/
static VMK_ReturnStatus sfvmk_verifyDevice(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_createDMAEngine(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_mapBAR(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_unmapBAR(sfvmk_adapter_t *pAdapter);
static void sfvmk_destroyDMAEngine(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_setBusMaster(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_estimateRsrcLimits(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_intrInit(sfvmk_adapter_t *pAdapter);
static void sfvmk_freeInterrupts(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus getPciDevice(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_removeUplinkDevice(vmk_Device sfvmk_device);


static vmk_DeviceOps sfvmk_uplinkDeviceOps = {
  .removeDevice = sfvmk_removeUplinkDevice
};

static VMK_ReturnStatus 
sfvmk_removeUplinkDevice(vmk_Device sfvmk_device)
{
  vmk_LogMessage("sfvmk_removeUplinkDevice: device=%p", sfvmk_device);
  return vmk_DeviceUnregister(sfvmk_device);
}



#ifdef SFVMK_WITH_UNIT_TESTS
extern void sfvmk_run_ut();
#endif
/************************************************************************
 * sfvmk_verifyDevice --
 *
 * @brief  Routine to check adapter's family, raise error if proper family
 * is not found.
 *
 * @param  adapter pointer to sfvmk_adapter_t
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_verifyDevice(sfvmk_adapter_t *pAdapter)
{
  int rc;

  /*check adapter's family */
  rc = efx_family(pAdapter->pciDeviceID.vendorID,
                  pAdapter->pciDeviceID.deviceID, &pAdapter->efxFamily);
  if (rc != 0) {
    SFVMK_ERR(pAdapter, "efx_family fail %d", rc);
    return VMK_FAILURE;
  }
  /* driver support only Medford Family */
  if (pAdapter->efxFamily == EFX_FAMILY_MEDFORD) {
    return VMK_OK;
  }

  SFVMK_ERR(pAdapter, "impossible controller family %d", pAdapter->efxFamily);

  return VMK_FAILURE;
}

/************************************************************************
 * sfvmk_createDMAEngine --
 *
 * @brief  Routine to create dma engine.
 *
 *
 * @param  adapter pointer to sfvmk_adapter_t
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_createDMAEngine(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status;
  vmk_DMAConstraints dmaConstraints;
  vmk_DMAEngineProps dmaProps;

  if (NULL == pAdapter)
    return VMK_FAILURE;

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

  return (status);
}

/************************************************************************
 * sfvmk_mapBAR --
 *
 * @brief  Routine to map memory bar.
 *
 *
 * @param  adapter pointer to sfvmk_adapter_t
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_mapBAR(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status;
  efsys_bar_t *bar;

  if (NULL == pAdapter)
      return VMK_FAILURE;

  bar = &pAdapter->bar;

  status = vmk_PCIMapIOResource(vmk_ModuleCurrentID, pAdapter->pciDevice,
                                EFX_MEM_BAR, NULL,(vmk_VA *)&bar->esb_base);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to map BAR2 (%x)", status);
  }

  status = sfvmk_createLock("mem-bar", VMK_SPINLOCK_RANK_HIGHEST-2, &bar->esb_lock);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "Failed to create lock for BAR2 (%x)", status);
  }

  return (status);
}


/************************************************************************
 * sfvmk_unmapBAR --
 *
 * @brief  Routine to unmap memory bar.
 *
 *
 * @param  adapter pointer to sfvmk_adapter_t
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_unmapBAR(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_OK;

  if (NULL == pAdapter)
      return VMK_FAILURE;

  sfvmk_destroyLock(pAdapter->bar.esb_lock);

  if (pAdapter->bar.esb_base) {
    status = vmk_PCIUnmapIOResource(vmk_ModuleCurrentID, pAdapter->pciDevice,
                                    EFX_MEM_BAR);
    if (status != VMK_OK)
      SFVMK_ERR(pAdapter, "Failed to unmap memory BAR status %x", status);

    SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 2, "Freed Bar %d ", EFX_MEM_BAR);
  }
  return status;
}

/************************************************************************
 * sfvmk_destroyDMAEngine --
 *
 * @brief  Routine to destory dma engine.
 *
 *
 * @param  adapter pointer to sfvmk_adapter_t
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static void
sfvmk_destroyDMAEngine(sfvmk_adapter_t *pAdapter)
{

  if (NULL == pAdapter)
        return ;

  vmk_DMAEngineDestroy(pAdapter->dmaEngine);
}

/************************************************************************
 * sfvmk_setBusMaster --
 *
 * @brief  Routine to enable bus matering mode
 *
 *
 * @param  adapter pointer to sfvmk_adapter_t
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_setBusMaster(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 cmd;
  VMK_ReturnStatus status = VMK_OK;

  if (NULL == pAdapter)
      return VMK_FAILURE;

  status = vmk_PCIReadConfig(vmk_ModuleCurrentID, pAdapter->pciDevice,
                      VMK_PCI_CONFIG_ACCESS_16, SFVMK_PCI_COMMAND, &cmd);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed to read PCI config : %u (%x)",
                SFVMK_PCI_COMMAND, status);
    return status;
  }

  if (!(cmd & SFVMK_PCI_COMMAND_BUS_MASTER)) {

    SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 0, "enabling bus mastering");

    cmd |= SFVMK_PCI_COMMAND_BUS_MASTER;
    status = vmk_PCIWriteConfig(vmk_ModuleCurrentID, pAdapter->pciDevice,
                          VMK_PCI_CONFIG_ACCESS_16, SFVMK_PCI_COMMAND, cmd);
    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed to write PCI config. %u, data: %u (%x)",
                  SFVMK_PCI_COMMAND, cmd, status);
      return status;
    }
  }

  return VMK_OK;

}

/************************************************************************
 * sfvmk_estimateRsrcLimits --
 *
 * @brief  Routine to check adapter's family, raise the error if proper family
 *
 *
 * @param  adapter pointer to sfvmk_adapter_t
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_estimateRsrcLimits(sfvmk_adapter_t *pAdapter)
{
  efx_drv_limits_t limits;
  unsigned int evqMax;
  uint32_t evqAllocated;
  uint32_t rxqAllocated;
  uint32_t txqAllocated;
  VMK_ReturnStatus status = VMK_OK;
  int rc;

  /*
   * Limit the number of event queues to:
   *  - number of CPUs
   *  - hardwire maximum RSS channels
   *  - administratively specified maximum RSS channels
   */

  vmk_Memset(&limits, 0, sizeof(limits));

  status = vmk_UplinkQueueGetNumQueuesSupported(EFX_MAXRSS, EFX_MAXRSS,
                    &limits.edl_max_txq_count , &limits.edl_max_rxq_count);
  if (status != VMK_OK) {
    return status ;
  }

  evqMax = MIN(limits.edl_max_rxq_count ,
                (limits.edl_max_txq_count - SFVMK_TXQ_NTYPES +1));

  // tuning parameter
  if (pAdapter->maxRssChannels> 0)
    evqMax = MIN(evqMax, pAdapter->maxRssChannels);

  limits.edl_min_evq_count = 1;
  limits.edl_max_evq_count = evqMax;
  limits.edl_min_txq_count = SFVMK_TXQ_NTYPES;
  limits.edl_min_rxq_count = 1;
  limits.edl_max_rxq_count = evqMax;

  /* set the limits in fw */
  efx_nic_set_drv_limits(pAdapter->pNic, &limits);

  if ((rc = efx_nic_init(pAdapter->pNic)) != 0)
    return VMK_FAILURE;

  rc = efx_nic_get_vi_pool(pAdapter->pNic, &evqAllocated, &rxqAllocated,
                            &txqAllocated);
  if (rc != 0) {
    efx_nic_fini(pAdapter->pNic);
    return VMK_FAILURE;
  }

  if (txqAllocated < SFVMK_TXQ_NTYPES)
    return VMK_FAILURE;

  pAdapter->evqMax = MIN(evqAllocated, evqMax);
  pAdapter->evqMax = MIN(rxqAllocated, pAdapter->evqMax);
  pAdapter->evqMax = MIN(txqAllocated - (SFVMK_TXQ_NTYPES - 1),
                          pAdapter->evqMax);

  if (pAdapter->evqMax > evqMax)
    return VMK_FAILURE;

  VMK_ASSERT(pAdapter->evqMax <= evqMax);

  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 0, "max txq = %d", txqAllocated );
  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 0, "max rxq = %d", rxqAllocated );
  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 0, "max evq = %d", pAdapter->evqMax );

  return (0);
}
/************************************************************************
 * sfvmk_intrInit --
 *
 * @brief   Routine to alloc msix interrupt. if msix interrupt is not
 *          supported alloc legacy interrupt.
 *
 * @param  adapter pointer to sfvmk_adapter_t
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/

static VMK_ReturnStatus
sfvmk_intrInit(sfvmk_adapter_t *pAdapter)
{
  unsigned int numIntReq, numIntrsAlloc;
  unsigned int index =0;
  VMK_ReturnStatus status;

  numIntReq = pAdapter->evqMax;

  /* initializing interrupt cookie */
  for (index = 0; index < numIntReq; index++)
    pAdapter->intr.intrCookies[index] = VMK_INVALID_INTRCOOKIE;

  /* allocate msix interrupt */
  status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                              pAdapter->pciDevice,  VMK_PCI_INTERRUPT_TYPE_MSIX,
                              numIntReq, numIntReq, NULL,
                              pAdapter->intr.intrCookies, &numIntrsAlloc);
  if (status == VMK_OK) {
    SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 0, "Allocated %d interrupts",
              numIntrsAlloc);

    pAdapter->intr.numIntrAlloc = numIntrsAlloc;
    pAdapter->intr.type = EFX_INTR_MESSAGE;

  } else {

    for (index = 0; index < numIntReq; index++)
      pAdapter->intr.intrCookies[index] = VMK_INVALID_INTRCOOKIE;

    SFVMK_ERR(pAdapter, "PCIAllocIntrCookie failed with error %x ", status);


    /* Try single msix vector */
    status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID, pAdapter->pciDevice,
                                  VMK_PCI_INTERRUPT_TYPE_MSIX, 1, 1, NULL,
                                  pAdapter->intr.intrCookies, &numIntrsAlloc);
    if (status != VMK_OK) {
      /* try Legacy Interrupt */
      SFVMK_ERR(pAdapter, "PCIAllocIntrCookie failed for 1 MSIX ");
      status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                        pAdapter->pciDevice,
                                        VMK_PCI_INTERRUPT_TYPE_LEGACY,
                                        1, 1 , NULL,
                                        pAdapter->intr.intrCookies,
                                        &numIntrsAlloc);
      if (status == VMK_OK) {

        SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 2,"Allocated  %d INTX intr",
                  numIntrsAlloc);
        pAdapter->intr.numIntrAlloc = 1;
        pAdapter->intr.type = EFX_INTR_LINE;
      } else {

        pAdapter->intr.intrCookies[0] = VMK_INVALID_INTRCOOKIE;
        SFVMK_ERR(pAdapter, "Failed to allocate 1 INTX intr");

      }

    } else {
      SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 2,  "Allocated 1 MSIx vector for device");
      pAdapter->intr.numIntrAlloc = 1;
      pAdapter->intr.type = EFX_INTR_MESSAGE;
    }

  }

  if(status == VMK_OK)
    pAdapter->intr.state = SFVMK_INTR_INITIALIZED ;

  return (status);


}

/************************************************************************
 * sfvmk_freeInterrupts --
 *
 * @brief  Routine to free allocated interrupt
 *
 *
 * @param  adapter pointer to sfvmk_adapter_t
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static void
sfvmk_freeInterrupts(sfvmk_adapter_t *pAdapter)
{
   vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, pAdapter->pciDevice);
}
/************************************************************************
 * getPciDevice --
 *
 * @brief  Routine to get pci device information such as vendor id , device ID
 *
 *
 * @param  adapter pointer to sfvmk_adapter_t
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
getPciDevice(sfvmk_adapter_t *pAdapter)
{
  vmk_AddrCookie pciDeviceCookie;
  VMK_ReturnStatus status;

  if (NULL== pAdapter)
    return VMK_FAILURE;

  /* get pci device */
  status = vmk_DeviceGetRegistrationData(pAdapter->device, &pciDeviceCookie);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "DeviceGetRegistrationData failed status: %x", status);
    return status;
  }

  pAdapter->pciDevice = pciDeviceCookie.ptr;

  /* query PCI device information */
  status = vmk_PCIQueryDeviceID(pAdapter->pciDevice, &pAdapter->pciDeviceID);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "PCIQueryDeviceID failed status: %x",status);
    return status;
  }

  status = sfvmk_verifyDevice(pAdapter);
  if (status != VMK_OK) {
    return status;
  }

  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, 2,"Found kernel device with following "
            "Dev Id:  %x\n Vend ID:  %x\n Sub Dev ID: %x\n Sub Vend ID: %x\n ",
            pAdapter->pciDeviceID.deviceID, pAdapter->pciDeviceID.vendorID,
            pAdapter->pciDeviceID.subDeviceID, pAdapter->pciDeviceID.subVendorID);


  status = vmk_PCIQueryDeviceAddr(pAdapter->pciDevice, &pAdapter->pciDeviceAddr);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "PCIQueryDeviceAddr failed, status 0x%x",status);
    return status;
  }

  vmk_StringFormat(pAdapter->pciDeviceName.string,
                    sizeof(pAdapter->pciDeviceName.string),
                    NULL, SFVMK_SBDF_FMT, pAdapter->pciDeviceAddr.seg,
                    pAdapter->pciDeviceAddr.bus, pAdapter->pciDeviceAddr.dev,
                    pAdapter->pciDeviceAddr.fn);


  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 2, "Found kernel device");

  return VMK_OK;
}
/************************************************************************
 * getPciDevice --
 *
 * @brief  Routine to get pci device information such as vendor id , device ID
 *
 *
 * @param  adapter pointer to sfvmk_adapter_t
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
void
sfvmk_updateSupportedCap(sfvmk_adapter_t *pAdapter)
{
  efx_phy_cap_type_t capMask = EFX_PHY_CAP_10HDX;
  vmk_uint32 supportedCaps, index = 0;

  VMK_ASSERT_BUG(NULL != pAdapter, "NULL adapter ptr" );
  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 5, "sfvmk_updateSupportedCap Enetered");
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

  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 4, " no of supported modes = %d", index);
  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 5, "sfvmk_updateSupportedCap Enetered");
}


/************************************************************************
 * Device Driver Operations
 ************************************************************************/

static vmk_DriverOps sfvmk_DriverOps = {
   .attachDevice  = sfvmk_AttachDevice,
   .detachDevice  = sfvmk_DetachDevice,
   .scanDevice    = sfvmk_ScanDevice,
   .quiesceDevice = sfvmk_ShutdownDevice,
   .startDevice   = sfvmk_StartDevice,
   .forgetDevice  = sfvmk_ForgetDevice,
};

/************************************************************************
 * sfvmk_AttachDevice --
 *
 * @brief  Callback routine for the device layer to announce device to the
 * driver.
 *
 * @param  dev  pointer to device
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_AttachDevice(vmk_Device dev)
{

  efx_nic_t *pNic;
  efsys_lock_t *pNicLock;
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_adapter_t *pAdapter = NULL;
  unsigned int error;

  //SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 2, "AttachDevice is called!");

  /* allocation memory for adapter */
  pAdapter = sfvmk_memPoolAlloc(sizeof(sfvmk_adapter_t));
  if (pAdapter == NULL) {
    goto sfvmk_adapter_alloc_fail;
  }

  pAdapter->maxRssChannels = 0;
  pAdapter->device = dev;
  pAdapter->debugMask = -1;

  /*Initializing ring enteries */
  /* these are tunable parameter */

  pAdapter->rxqEntries = sfvmk_rxRingEntries;
  pAdapter->txqEntries = sfvmk_txRingEntries;

  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 2, "allocated adapter =%p",
            (void *)pAdapter);

  status = getPciDevice(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "could not get PCI device information with"
        "status %d", status);
    goto sfvmk_get_pci_dev_fail;
  }

  status = sfvmk_createDMAEngine(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "DMA Engine create failed with status %d", status);
    goto sfvmk_DMA_eng_create_fail;
  }

  status = sfvmk_mapBAR(pAdapter);
  if (status != VMK_OK) {
    goto sfvmk_map_bar_fail;
  }

  status = sfvmk_setBusMaster(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "sfvmk_setBusMaster failed status %x", status);
    goto sfvmk_bus_master_fail;
  }

  pNicLock = &pAdapter->NicLock;
  sfvmk_createLock("enp", VMK_SPINLOCK_RANK_HIGHEST-1, &pNicLock->lock);
  if ((error = efx_nic_create(pAdapter->efxFamily, (efsys_identifier_t *)pAdapter,
      &pAdapter->bar, pNicLock, &pNic)) != 0)
  {
    SFVMK_ERR(pAdapter, "failed in efx_nic_create status %x", error);
    goto sfvmk_nic_create_fail;
  }

  pAdapter->pNic = pNic;

  /* Initialize MCDI to talk to the microcontroller. */
  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, 2, "mcdi_init...");
  if ((error = sfvmk_mcdiInit(pAdapter)) != 0) {

    SFVMK_ERR(pAdapter, "failed in sfvmk_mcdiInit status %x", error);
    goto sfvmk_mcdi_init_fail;

  }
  /* Probe  NIC and build the configuration data area. */
  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, 2, "nic_probe...");
  if ((error = efx_nic_probe(pNic)) != 0) {

    SFVMK_ERR(pAdapter, "failed in efx_nic_probe status %x", error);
    goto sfvmk_nic_probe_fail;
  }
  /* Initialize NVRAM. */
  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, 2, "nvram_init...");
  if ((error = efx_nvram_init(pNic)) != 0) {

    SFVMK_ERR(pAdapter, "failed in efx_nvram_init status %x", error);
    goto sfvmk_nvram_init_fail;
  }

  /* Initialize VPD. */
  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, 2, "vpd_init...");
  if ((error = efx_vpd_init(pNic)) != 0) {

    SFVMK_ERR(pAdapter, "failed in efx_vpd_init status %x", error);
    goto sfvmk_vpd_init_fail;
  }

  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, 2, "mcdi_new_epoch...");
  efx_mcdi_new_epoch(pNic);

  /* Reset NIC. */
  SFVMK_DBG(pAdapter,SFVMK_DBG_DRIVER, 2, "nic_reset...");
  if ((error = efx_nic_reset(pNic)) != 0) {

    SFVMK_ERR(pAdapter, "failed in efx_nic_reset status %x", error);
    goto sfvmk_nic_reset_fail;
  }


  status = sfvmk_estimateRsrcLimits(pAdapter);
  if (status != VMK_OK) {

    SFVMK_ERR(pAdapter, "failed in sfvmk_estimateRsrcLimits status %x", status);
    goto sfvmk_rsrc_info_fail;
  }

  status = sfvmk_intrInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed in sfvmk_intrInit status %x", status);
    goto sfvmk_intr_init_fail;
  }


  status = sfvmk_evInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed in sfvmk_evInit status %x", status);
    goto sfvmk_ev_init_fail;
  }


  status = sfvmk_rxInit(pAdapter);
  if (status != VMK_OK)  {
    SFVMK_ERR(pAdapter, "failed in sfvmk_rxInit status %x", status);
    goto sfvmk_rx_init_fail;
  }

  status = sfvmk_txInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "failed in sfvmk_txInit status %x", status);
    goto sfvmk_tx_init_fail;
  }
  sfvmk_updateSupportedCap(pAdapter);

  status = sfvmk_initUplinkData(pAdapter);
  if (status != VMK_OK) {
    goto sfvmk_init_uplink_data_fail;
  }

  efx_nic_fini(pAdapter->pNic);
  
  status = vmk_DeviceSetAttachedDriverData(dev, pAdapter);
  if (status != VMK_OK) {
    goto sfvmk_set_drvdata_fail;
  }
  pAdapter->initState = SFVMK_REGISTERED;
  return VMK_OK;


#ifdef SFVMK_WITH_UNIT_TESTS
  sfvmk_run_ut();
#endif

  return VMK_OK;

sfvmk_set_drvdata_fail:
  sfvmk_destroyUplinkData(pAdapter);
sfvmk_init_uplink_data_fail:
  sfvmk_txFini(pAdapter);
sfvmk_tx_init_fail:
  sfvmk_rxFini(pAdapter);
sfvmk_rx_init_fail:
  sfvmk_evFini(pAdapter);
sfvmk_ev_init_fail:
  sfvmk_freeInterrupts(pAdapter);
sfvmk_rsrc_info_fail:
sfvmk_intr_init_fail:
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
  sfvmk_destroyLock(pNicLock->lock);
sfvmk_nic_create_fail:
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

/************************************************************************
 * sfvmk_StartDevice --
 *
 * @brief: Callback routine for the device layer to notify the driver to
 * bring up the specified device.
 *
 * @param  dev  pointer to device
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_StartDevice(vmk_Device dev)
{
  vmk_LogMessage("StartDevice is invoked!");
  return VMK_OK;
}

/************************************************************************
 * sfvmk_ScanDevice --
 *
 * Callback routine for the device layer to notify the driver to scan
 * for new devices.
 *
 * @param  dev  pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_ScanDevice(vmk_Device device)
{
  VMK_ReturnStatus status = 0;
  sfvmk_adapter_t *pAdapter;
  vmk_Name busName;
  vmk_DeviceID *pDeviceID;
  vmk_DeviceProps deviceProps;

  //SFVMK_DEBUG(SFVMK_DBG_DRIVER, 5, "ScanDevice is invoked!");
  vmk_LogMessage("ScanDevice is invoked!");

  status = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *) &pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "vmk_DeviceGetAttachedDriverData failed, status %s",
              vmk_StatusToString(status));
    return status;
  }

  VMK_ASSERT_BUG(pAdapter != NULL, "NULL adapter ptr");

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
    SFVMK_ERR(pAdapter, "Failed to register device: %s", 
              vmk_StatusToString(status));
    return status;
  }

  vmk_LogicalFreeBusAddress(sfvmk_ModInfo.driverID, pDeviceID->busAddress);
  vmk_BusTypeRelease(pDeviceID->busType);

  return VMK_OK;
}


/************************************************************************
 * sfvmk_DetachDevice --
 *
 * @brief : Callback routine for the device layer to notify the driver to
 * release control of a driver.
 *
 * @param  dev  pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/

static VMK_ReturnStatus
sfvmk_DetachDevice(vmk_Device dev)
{
 efx_nic_t *pNic = NULL ;
 sfvmk_adapter_t *pAdapter = NULL;
 return VMK_OK;
 vmk_DeviceGetAttachedDriverData(dev, (vmk_AddrCookie *)&pAdapter);

  if (NULL != pAdapter)
  {

    sfvmk_destroyUplinkData(pAdapter);
    sfvmk_txFini(pAdapter);
    sfvmk_rxFini(pAdapter);
    sfvmk_evFini(pAdapter);
    sfvmk_freeInterrupts(pAdapter);

    /* Tear down common code subsystems. */
    pNic = pAdapter->pNic;
    //efx_nic_fini(pNic);
    efx_vpd_fini(pNic);
    efx_nvram_fini(pNic);
    efx_nic_unprobe(pNic);

    /* Tear down MCDI. */
    sfvmk_mcdiFini(pAdapter);

    /* Destroy common code context. */
    pAdapter->pNic = NULL;
    efx_nic_destroy(pNic);
    sfvmk_destroyLock(pAdapter->NicLock.lock);

    /* vmk related resource deallocation */
    sfvmk_unmapBAR(pAdapter);
    sfvmk_destroyDMAEngine(pAdapter);
    sfvmk_memPoolFree(pAdapter, sizeof(sfvmk_adapter_t));

  }
 return VMK_OK;

}

/************************************************************************
 * sfvmk_ShutdownDevice --
 *
 * @brief : Callback routine for the device layer to notify the driver to
 * shutdown the specified device.
 *
 * @param  dev  pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_ShutdownDevice(vmk_Device dev)
{
  vmk_LogMessage("QuiesceDevice/drv_DeviceShutdown is invoked!");
  return VMK_OK;
}

/************************************************************************
 * sfvmk_ForgetDevice --
 *
 * @brief: Callback routine for the device layer to notify the driver the
 * specified device is not responsive.
 *
 * @param  dev  pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static void
sfvmk_ForgetDevice(vmk_Device dev)
{
  vmk_LogMessage("ForgetDevice is invoked!");
  return;
}

/************************************************************************
 * sfvmk_DriverRegister --
 *
 * @brief: This function registers the driver as network driver
 *
 * @param : None
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
VMK_ReturnStatus
sfvmk_DriverRegister(void)
{
  VMK_ReturnStatus status;
  vmk_DriverProps driverProps;

  /* Populate driverProps */
  driverProps.moduleID = vmk_ModuleCurrentID;
  vmk_NameCopy(&driverProps.name, &sfvmk_ModInfo.driverName);
  driverProps.ops = &sfvmk_DriverOps;
  driverProps.privateData = (vmk_AddrCookie)NULL;


  /* Register Driver with the device layer */
  status = vmk_DriverRegister(&driverProps, &sfvmk_ModInfo.driverID);

  if (status == VMK_OK) {
    vmk_LogMessage("Initialization of SFC  driver successful");
  } else {
    vmk_LogMessage("Initialization of SFC driver failed with status: %d", status);
  }

  return status;
}

/************************************************************************
 * sfvmk_DriverUnregister --
 *
 * @brief: Function to unregister the device that was previous registered.
 *
 * @param : None
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
void
sfvmk_DriverUnregister(void)
{
  vmk_DriverUnregister(sfvmk_ModInfo.driverID);
}

