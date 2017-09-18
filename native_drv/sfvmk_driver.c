/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

/* Initialize module params with default values */
sfvmk_modParams_t modParams = {
  .debugMask = SFVMK_DEBUG_DEFAULT,
};

/* List of module parameters */
VMK_MODPARAM_NAMED(debugMask, modParams.debugMask, uint, "Debug Logging Bit Masks");

/* Value of SFVMK_DMA_ADDR_MASK_BITS chosen based on the field
 * TX_KER_BUF_ADDR of TX_KER_DESC */
#define SFVMK_DMA_ADDR_MASK_BITS 48
#define SFVMK_DMA_BIT_MASK(n) (((n) == 64) ? VMK_ADDRESS_MASK_64BIT : \
  ((VMK_CONST64U(1) << n) - VMK_CONST64U(1)))

/* driver callback functions */
static VMK_ReturnStatus sfvmk_attachDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_detachDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_scanDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_quiesceDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_startDevice(vmk_Device device);
static void sfvmk_forgetDevice(vmk_Device device);

/*! \brief  Routine to check adapter's family, raise error if proper family
** is not found.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_TRUE <success> VMK_FALSE <failure>
*/
static vmk_Bool
sfvmk_isDeviceSupported(sfvmk_adapter_t *pAdapter)
{
  int rc;
  vmk_Bool supported = VMK_FALSE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  /* Check adapter's family */
  rc = efx_family(pAdapter->pciDeviceID.vendorID,
                  pAdapter->pciDeviceID.deviceID, &pAdapter->efxFamily);
  if (rc != 0) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_family failed status: %d", rc);
    goto done;
  }

  /* Driver support only Medford Family */
  switch (pAdapter->efxFamily) {
  case EFX_FAMILY_MEDFORD:
    supported = VMK_TRUE;
    break;
  case EFX_FAMILY_SIENA:
  case EFX_FAMILY_HUNTINGTON:
    SFVMK_ADAPTER_ERROR(pAdapter, "Controller family %d not supported",
                        pAdapter->efxFamily);
    break;
  default:
    SFVMK_ADAPTER_ERROR(pAdapter, "Unknown controller type");
    break;
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);

  return supported;
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
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  /* Get PCI device */
  status = vmk_DeviceGetRegistrationData(pAdapter->device, &pciDeviceCookie);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_DeviceGetRegistrationData failed status: %s",
                        vmk_StatusToString(status));
    goto failed_get_reg_data;
  }

  pAdapter->pciDevice = pciDeviceCookie.ptr;

  /* Query PCI device information */
  status = vmk_PCIQueryDeviceID(pAdapter->pciDevice, &pAdapter->pciDeviceID);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIQueryDeviceID failed status: %s",
                        vmk_StatusToString(status));
    goto failed_query_device_id;
  }

  if (sfvmk_isDeviceSupported(pAdapter) == VMK_FALSE) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Device is not supported");
    goto failed_device_support_check;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter,SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_INFO,
                      "Found PCI device with following\n"
                      "Dev Id: %x\n"
                      "Vend ID: %x\n"
                      "Sub Dev ID: %x\n"
                      "Sub Vend ID: %x\n",
                      pAdapter->pciDeviceID.deviceID,
                      pAdapter->pciDeviceID.vendorID,
                      pAdapter->pciDeviceID.subDeviceID,
                      pAdapter->pciDeviceID.subVendorID);

  status = vmk_PCIQueryDeviceAddr(pAdapter->pciDevice, &pAdapter->pciDeviceAddr);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIQueryDeviceAddr failed status: %s",
                        vmk_StatusToString(status));
    goto failed_query_device_addr;
  }

  status = vmk_StringFormat(pAdapter->pciDeviceName.string,
                            sizeof(pAdapter->pciDeviceName.string),
                            NULL, "%04x:%02x:%02x.%x",
                            pAdapter->pciDeviceAddr.seg,
                            pAdapter->pciDeviceAddr.bus,
                            pAdapter->pciDeviceAddr.dev,
                            pAdapter->pciDeviceAddr.fn);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_StringFormat failed status: %s",
                        vmk_StatusToString(status));
    goto failed_set_pci_dev_name;
  }

  /* Used for debugging purpose */
  vmk_NameCopy(&pAdapter->devName, &pAdapter->pciDeviceName);

  goto done;

failed_set_pci_dev_name:
failed_get_reg_data:
failed_query_device_id:
failed_device_support_check:
failed_query_device_addr:
done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);

  return status;
}

/*! \brief  Routine to create DMA engine.
**
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_createDMAEngine(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_BAD_PARAM;
  vmk_DMAConstraints dmaConstraints;
  vmk_DMAEngineProps dmaProps;
  vmk_uint64 dmaMask = SFVMK_DMA_BIT_MASK(SFVMK_DMA_ADDR_MASK_BITS);

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  vmk_Memset(&dmaConstraints, 0, sizeof(dmaConstraints));
  vmk_Memset(&dmaProps, 0, sizeof(dmaProps));
  vmk_NameCopy(&dmaProps.name, &sfvmk_modInfo.driverName);

  dmaProps.module = vmk_ModuleCurrentID;
  dmaProps.device = pAdapter->device;
  dmaProps.constraints = &dmaConstraints;
  dmaProps.flags = VMK_DMA_ENGINE_FLAGS_COHERENT;

  /* Set the PCI DMA mask.  Try all possibilities from our
   * genuine mask down to 32 bits */
  while (dmaMask > VMK_ADDRESS_MASK_32BIT)
  {
    dmaConstraints.addressMask = dmaMask;
    status = vmk_DMAEngineCreate(&dmaProps, &pAdapter->dmaEngine);
    if (status == VMK_OK) {
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_INFO,
                          "DMA engine created with mask %"VMK_FMT64"x", dmaMask);
      break;
    }
    dmaMask >>= 1;
  }
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Failed to create DMA engine status: %s",
                        vmk_StatusToString(status));
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);

  return status;
}

/*! \brief  Routine to destroy DMA engine.
**
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_destroyDMAEngine(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if (pAdapter->dmaEngine != VMK_DMA_ENGINE_INVALID) {
    status = vmk_DMAEngineDestroy(pAdapter->dmaEngine);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "Failed to destroy DMA engine status: %s",
                          vmk_StatusToString(status));
    }
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);

  return status;
}

/*! \brief  Routine to map memory BAR. A lock has been created which will be
**          used by common code to synchronize access to BAR region.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK ion success and failure status on failure.
*/
static VMK_ReturnStatus
sfvmk_mapBAR(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_BAD_PARAM;
  vmk_uint32 barIndex;
  vmk_PCIResource pciResource[VMK_PCI_NUM_BARS];

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  status = vmk_PCIQueryIOResources(pAdapter->pciDevice,
                                   VMK_PCI_NUM_BARS,
                                   pciResource);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIQueryIOResources failed status: %s",
                        vmk_StatusToString(status));
    goto failed_query_io_resource;
  }

  /* Look for first 64bit memory BAR containing VI aperture */
  for(barIndex = 0; barIndex < VMK_PCI_NUM_BARS; barIndex++) {
    if ((pciResource[barIndex].flags & VMK_PCI_BAR_FLAGS_IO_MASK) ==
        VMK_PCI_BAR_FLAGS_IO)
      continue;

    if (pciResource[barIndex].flags & VMK_PCI_BAR_FLAGS_MEM_64_BITS)
      break;
  }

  /* Check if BAR has been found */
  if (barIndex == VMK_PCI_NUM_BARS) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Failed to get BAR for VI register");
    goto failed_fetch_mem_bar_info;
  }

  status = vmk_PCIMapIOResourceWithAttr(vmk_ModuleCurrentID, pAdapter->pciDevice,
                                        barIndex, VMK_MAPATTRS_READWRITE |
                                        VMK_MAPATTRS_UNCACHED,
                                        &pAdapter->bar.esbBase);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "vmk_PCIMapIOResourceWithAttr for BAR%d failed status: %s",
                        barIndex, vmk_StatusToString(status));
    goto failed_map_io_resource;
  }

  status = sfvmk_createLock(pAdapter, "memBarLock",
                            SFVMK_SPINLOCK_RANK_BAR_LOCK,
                            &pAdapter->bar.esbLock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock for BAR%d failed status  %s",
                        barIndex, vmk_StatusToString(status));
    goto failed_create_lock;
  }

  /* Storing BAR index to be used for unmapping resource */
  pAdapter->bar.index = barIndex;

  goto done;

failed_create_lock:
  vmk_PCIUnmapIOResource(vmk_ModuleCurrentID, pAdapter->pciDevice, barIndex);

failed_map_io_resource:
failed_fetch_mem_bar_info:
failed_query_io_resource:
done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);

  return status;
}

/*! \brief  Routine to unmap memory BAR.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_unmapBAR(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  if (pAdapter->bar.index == VMK_PCI_NUM_BARS) {
    SFVMK_ADAPTER_ERROR(pAdapter, "BAR is not yet mapped");
    goto done;
  }
  sfvmk_destroyLock(pAdapter->bar.esbLock);

  status = vmk_PCIUnmapIOResource(vmk_ModuleCurrentID, pAdapter->pciDevice,
                                  pAdapter->bar.index);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIUnmapIOResource failed status: %s",
                        vmk_StatusToString(status));
  } else {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_INFO,
                        "Unmapped BAR %d", pAdapter->bar.index);
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);

  return status;
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
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_adapter_t *pAdapter = NULL;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);

  /* Allocate memory for adapter */
  pAdapter = vmk_HeapAlloc(sfvmk_modInfo.heapID, sizeof(sfvmk_adapter_t));
  if (pAdapter == NULL) {
    SFVMK_ERROR("Failed to alloc memory for adapter");
    goto failed_adapter_alloc;
  }

  vmk_Memset(pAdapter, 0, sizeof(sfvmk_adapter_t));

  pAdapter->device = dev;
  pAdapter->bar.index = VMK_PCI_NUM_BARS;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                      "allocated adapter =%p", (void *)pAdapter);

  status = sfvmk_getPciDevice(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_getPciDevice failed status %s",
                        vmk_StatusToString(status));
    goto failed_get_pci_dev;
  }

  status = sfvmk_createDMAEngine(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createDMAEngine failed status: %s",
                        vmk_StatusToString(status));
    goto failed_dma_eng_create;
  }

  status = sfvmk_mapBAR(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_mapBAR failed status: %s",
                        vmk_StatusToString(status));
    goto failed_map_bar;
  }

  status = vmk_DeviceSetAttachedDriverData(dev, pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "vmk_DeviceSetAttachedDriverData failed status: %s",
                        vmk_StatusToString(status));
    goto failed_set_drvdata;
  }

  pAdapter->state = SFVMK_ADAPTER_STATE_REGISTERED;

  goto done;

failed_set_drvdata:
  sfvmk_unmapBAR(pAdapter);

failed_map_bar:
  sfvmk_destroyDMAEngine(pAdapter);

failed_dma_eng_create:
failed_get_pci_dev:
  vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter);

failed_adapter_alloc:
done:
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);

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
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);
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
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER, "VMK device:%p", device);
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_DRIVER, "VMK device:%p", device);
  return VMK_OK;
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
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_AddrCookie data;
  sfvmk_adapter_t *pAdapter = NULL;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);

  status = vmk_DeviceGetAttachedDriverData(dev, &data);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Failed in vmk_DeviceGetAttachedDriverData: %s",
                        vmk_StatusToString(status));
    goto done;
  }
  pAdapter = (sfvmk_adapter_t *)data.ptr;
  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  sfvmk_unmapBAR(pAdapter);
  sfvmk_destroyDMAEngine(pAdapter);
  vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter);

done:
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);

  return status;
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
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);
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
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);
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

  /* Populate driverProps */
  driverProps.moduleID = vmk_ModuleCurrentID;
  vmk_NameCopy(&driverProps.name, &sfvmk_modInfo.driverName);
  driverProps.ops = &sfvmk_DriverOps;
  driverProps.privateData = (vmk_AddrCookie)NULL;


  /* Register Driver with the device layer */
  status = vmk_DriverRegister(&driverProps, &sfvmk_modInfo.driverID);

  if (status == VMK_OK) {
    vmk_LogMessage("Initialization of SFC  driver successful");
  } else {
    vmk_LogMessage("Initialization of SFC driver failed with status: %d", status);
  }

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
  vmk_DriverUnregister(sfvmk_modInfo.driverID);
}

