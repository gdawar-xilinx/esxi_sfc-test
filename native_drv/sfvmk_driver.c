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

#define SFVMK_MIN_EVQ_COUNT 1

/* Value of SFVMK_DMA_ADDR_MASK_BITS chosen based on the field
 * TX_KER_BUF_ADDR of TX_KER_DESC */
#define SFVMK_DMA_ADDR_MASK_BITS 48
#define SFVMK_DMA_BIT_MASK(n) (((n) == 64) ? VMK_ADDRESS_MASK_64BIT : \
  ((VMK_CONST64U(1) << n) - VMK_CONST64U(1)))

/* Driver callback functions */
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
                  pAdapter->pciDeviceID.deviceID,
                  &pAdapter->efxFamily, &pAdapter->bar.index);
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

  VMK_ASSERT_NOT_NULL(pAdapter);

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  status = vmk_PCIMapIOResourceWithAttr(vmk_ModuleCurrentID, pAdapter->pciDevice,
                                        pAdapter->bar.index, VMK_MAPATTRS_READWRITE |
                                        VMK_MAPATTRS_UNCACHED,
                                        &pAdapter->bar.esbBase);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "vmk_PCIMapIOResourceWithAttr for BAR%d failed status: %s",
                        pAdapter->bar.index, vmk_StatusToString(status));
    goto failed_map_io_resource;
  }

  status = sfvmk_createLock(pAdapter, "memBarLock",
                            SFVMK_SPINLOCK_RANK_BAR_LOCK,
                            &pAdapter->bar.esbLock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock for BAR%d failed status  %s",
                        pAdapter->bar.index, vmk_StatusToString(status));
    goto failed_create_lock;
  }

  goto done;

failed_create_lock:
  vmk_PCIUnmapIOResource(vmk_ModuleCurrentID, pAdapter->pciDevice,
                         pAdapter->bar.index);

failed_map_io_resource:
  pAdapter->bar.index = VMK_PCI_NUM_BARS;
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

/*! \brief  Routine to estimate resource (eg number of RXQs,TXQs and EVQs)
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK or VMK_FAILURE
*/
VMK_ReturnStatus
sfvmk_setResourceLimits(sfvmk_adapter_t *pAdapter)
{
  efx_drv_limits_t limits;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  const efx_nic_cfg_t *pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_cfg_get failed");
    goto done;
  }

  vmk_Memset(&limits, 0, sizeof(limits));

  /* Get number of queues supported (this is depend on number of cpus) */
  status = vmk_UplinkQueueGetNumQueuesSupported(pNicCfg->enc_txq_limit,
                                                pNicCfg->enc_rxq_limit,
                                                &limits.edl_max_txq_count,
                                                &limits.edl_max_rxq_count);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "vmk_UplinkQueueGetNumQueuesSupported failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }
  /* Using following scheme for EVQs, RXQs and TXQs
   * One EVQ for each RXQ and TXQ_TYPE_IP_TCP_UDP_CKSUM TXQ
   * EVQ-0 also handles:
   * Events for one (shared) TXQ_TYPE_NON_CKSUM TXQ
   * Events for one (shared) TXQ_TYPE_IP_CKSUM TXQ
   * MCDI events
   * Error events from firmware/hardware
   * TODO: calculation of EVQs when NETQ and RSS features are added */

  limits.edl_min_evq_count = SFVMK_MIN_EVQ_COUNT;
  limits.edl_max_evq_count = MIN(limits.edl_max_rxq_count,
                                 limits.edl_max_txq_count);
  pAdapter->numEvqsDesired = limits.edl_max_evq_count;

  limits.edl_min_rxq_count = limits.edl_min_evq_count;
  limits.edl_max_rxq_count = limits.edl_max_evq_count;

  limits.edl_min_txq_count = limits.edl_min_evq_count;
  limits.edl_max_txq_count = limits.edl_max_evq_count;

  status = efx_nic_set_drv_limits(pAdapter->pNic, &limits);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_set_drv_limits failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);

  return status;
}

/*! \brief  Routine to update driver information
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: none
*/
static void
sfvmk_updateDrvInfo(sfvmk_adapter_t *pAdapter)
{
  vmk_UplinkDriverInfo *pDrvInfo;
  VMK_ReturnStatus status = VMK_BAD_PARAM;
  efx_nic_fw_info_t nicFwVer;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  vmk_Memset(&nicFwVer, 0, sizeof(efx_nic_fw_info_t));

  status  = efx_nic_get_fw_version(pAdapter->pNic, &nicFwVer);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_get_fw_version failed status: %s",
                        vmk_StatusToString(status));
  }

  pDrvInfo = &pAdapter->uplink.sharedData.driverInfo;

  sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);
  vmk_NameInitialize(&pDrvInfo->driver, sfvmk_modInfo.driverName.string);
  vmk_NameInitialize(&pDrvInfo->moduleInterface, "native");
  vmk_NameInitialize(&pDrvInfo->version, SFVMK_DRIVER_VERSION_STRING);

  status = vmk_NameFormat(&pDrvInfo->firmwareVersion, "%u.%u.%u.%u rx%u tx%u",
                          nicFwVer.enfi_mc_fw_version[0],
                          nicFwVer.enfi_mc_fw_version[1],
                          nicFwVer.enfi_mc_fw_version[2],
                          nicFwVer.enfi_mc_fw_version[3],
                          nicFwVer.enfi_rx_dpcpu_fw_id,
                          nicFwVer.enfi_tx_dpcpu_fw_id);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_NameFormat failed status: %s",
                        vmk_StatusToString(status));
  }
  sfvmk_sharedAreaEndWrite(&pAdapter->uplink);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);
}


/*! \brief  Routine to create a VMK Helper queue
**
** \param  pAdapter Pointer to sfvmk_adapter_t
**
** \return: VMK_OK <Success> or VMK_FAILURE <Failure>
**
*/
static VMK_ReturnStatus
sfvmk_createHelper(sfvmk_adapter_t *pAdapter)
{
  vmk_HelperProps props;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  vmk_Memset(&props, 0, sizeof(vmk_HelperProps));
  vmk_NameFormat(&props.name, "%s-helper",
                 vmk_NameToString(&sfvmk_modInfo.driverName));
  props.heap = sfvmk_modInfo.heapID;
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
  props.constructorArg = (vmk_AddrCookie)NULL;

  status = vmk_HelperCreate(&props, &pAdapter->helper);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HelperCreate failed status: %s",
                        vmk_StatusToString(status));
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);
  return status;
}

/* \brief  Routine to Destroy a VMK Helper queue
**
** \param  pAadapter Pointer to sfvmk_adapter_t
**
** \return: None
**
*/
static void
sfvmk_destroyHelper(sfvmk_adapter_t *pAdapter)
{
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);
  vmk_HelperDestroy(pAdapter->helper);
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);
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
  efx_rc_t error;

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

  status = sfvmk_createLock(pAdapter, "nicLock",
                            SFVMK_SPINLOCK_RANK_NIC_LOCK,
                            &pAdapter->nicLock.lock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_lock;
  }

  error = efx_nic_create(pAdapter->efxFamily,
                         (efsys_identifier_t *)pAdapter,
                         &pAdapter->bar,
                         &pAdapter->nicLock,
                         &pAdapter->pNic);
  if (error != 0) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_create failed status: %s",
                        vmk_StatusToString(error));
    goto failed_nic_create;
  }

  /* Initialize MCDI to talk to the management controller. */
  if ((error = sfvmk_mcdiInit(pAdapter)) != 0) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_mcdiInit failed status: %s",
		        vmk_StatusToString(status));
    goto failed_mcdi_init;
  }

  /* Probe  NIC and build the configuration data area. */
  if ((error = efx_nic_probe(pAdapter->pNic)) != 0) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_probe failed status: %s",
                        vmk_StatusToString(error));
    goto failed_nic_probe;
  }

  /* Reset NIC. */
  if ((error = efx_nic_reset(pAdapter->pNic)) != 0) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_reset failed status: %s",
                        vmk_StatusToString(error));
    goto failed_nic_reset;
  }

  status = sfvmk_setResourceLimits(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_setResourceLimits failed status: %s",
                        vmk_StatusToString(status));
    goto failed_set_resource_limit;
  }

  status = efx_nic_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_init failed status: %s",
                        vmk_StatusToString(status));
    goto failed_nic_init;
  }

  /* Get size of resource pool from NIC */
  status = efx_nic_get_vi_pool(pAdapter->pNic,
                               &pAdapter->numEvqsAllotted,
                               &pAdapter->numRxqsAllotted,
                               &pAdapter->numTxqsAllotted);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_get_vi_pool failed status: %s",
                        vmk_StatusToString(status));
    goto failed_get_vi_pool;
  }

  status = sfvmk_intrInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_intrInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_intr_init;
  }

  status = sfvmk_evInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_evInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_ev_init;
  }

  status = sfvmk_portInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_portInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_port_init;
  }

  status = sfvmk_txInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_txInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_tx_init;
  }

  status = sfvmk_rxInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_rxInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_rx_init;
  }

  status = sfvmk_mutexInit("adapterLock", &pAdapter->lock);
  if(status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_mutexInit failed status %s",
                        vmk_StatusToString(status));
    goto failed_mutex_init;
  }

  status = sfvmk_uplinkDataInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_uplinkDataInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_uplinkData_init;
  }

  sfvmk_updateDrvInfo(pAdapter);

  status = vmk_DeviceSetAttachedDriverData(dev, pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "vmk_DeviceSetAttachedDriverData failed status: %s",
                        vmk_StatusToString(status));
    goto failed_set_drvdata;
  }

  status = sfvmk_createHelper(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createHelper failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_helper;
  }

  efx_nic_fini(pAdapter->pNic);
  pAdapter->state = SFVMK_ADAPTER_STATE_REGISTERED;

  goto done;

failed_create_helper:
failed_set_drvdata:
  sfvmk_uplinkDataFini(pAdapter);

failed_uplinkData_init:
  sfvmk_mutexDestroy(pAdapter->lock);
  pAdapter->lock = NULL;

failed_mutex_init:
  sfvmk_rxFini(pAdapter);

failed_rx_init:
  sfvmk_txFini(pAdapter);

failed_tx_init:
  sfvmk_portFini(pAdapter);

failed_port_init:
  sfvmk_evFini(pAdapter);

failed_ev_init:
  sfvmk_intrFini(pAdapter);

failed_intr_init:
failed_get_vi_pool:
  efx_nic_fini(pAdapter->pNic);

failed_nic_init:
failed_set_resource_limit:
failed_nic_reset:
  efx_nic_unprobe(pAdapter->pNic);

failed_nic_probe:
  sfvmk_mcdiFini(pAdapter);

failed_mcdi_init:
  if (pAdapter->pNic != NULL) {
    efx_nic_destroy(pAdapter->pNic);
    pAdapter->pNic = NULL;
  }

failed_nic_create:
  sfvmk_destroyLock(pAdapter->nicLock.lock);

failed_create_lock:
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

/*! \brief  Routine to unregister the device.
**
** \param[in]  device handle.
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_removeUplinkDevice(vmk_Device device)
{
  return vmk_DeviceUnregister(device);
}

static vmk_DeviceOps sfvmk_uplinkDeviceOps = {
  .removeDevice = sfvmk_removeUplinkDevice
};

/*! \brief: Callback routine for the device layer to notify the driver to
**          scan for new device
**
** \param[in]  device  pointer to vmkDevice
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_scanDevice(vmk_Device device)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_AddrCookie data;
  sfvmk_adapter_t *pAdapter = NULL;
  vmk_Name busName;
  vmk_DeviceProps deviceProps = {0};

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER, "VMK device:%p", device);

  status = vmk_DeviceGetAttachedDriverData(device, &data);
  if (status != VMK_OK) {
    SFVMK_ERROR("vmk_DeviceGetAttachedDriverData failed status: %s",
                vmk_StatusToString(status));
    goto done;
  }

  pAdapter = (sfvmk_adapter_t *)data.ptr;
  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_FAILURE;
    goto done;
  }

  status = vmk_NameInitialize(&busName, VMK_LOGICAL_BUS_NAME);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_NameInitialize failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  status = vmk_BusTypeFind(&busName, &pAdapter->uplink.deviceID.busType);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_BusTypeFind failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  status = vmk_LogicalCreateBusAddress(sfvmk_modInfo.driverID,
                                       pAdapter->device,
                                       0,
                                       &pAdapter->uplink.deviceID.busAddress,
                                       &pAdapter->uplink.deviceID.busAddressLen);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_LogicalCreateBusAddress failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  pAdapter->uplink.deviceID.busIdentifier = VMK_UPLINK_DEVICE_IDENTIFIER;
  pAdapter->uplink.deviceID.busIdentifierLen = sizeof(VMK_UPLINK_DEVICE_IDENTIFIER) - 1;

  deviceProps.deviceID = &pAdapter->uplink.deviceID;
  deviceProps.deviceOps = &sfvmk_uplinkDeviceOps;
  deviceProps.registeringDriver = sfvmk_modInfo.driverID;
  deviceProps.registrationData.ptr = &pAdapter->uplink.regData;
  deviceProps.registeringDriverData.ptr = pAdapter;

  status = vmk_DeviceRegister(&deviceProps, device, &pAdapter->uplink.uplinkDevice);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_DeviceRegister failed status: %s",
                        vmk_StatusToString(status));
  }

  vmk_LogicalFreeBusAddress(sfvmk_modInfo.driverID, pAdapter->uplink.deviceID.busAddress);
  vmk_BusTypeRelease(pAdapter->uplink.deviceID.busType);

done:
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_DRIVER, "VMK device:%p", device);

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
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_AddrCookie data;
  sfvmk_adapter_t *pAdapter = NULL;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);

  status = vmk_DeviceGetAttachedDriverData(dev, &data);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "vmk_DeviceGetAttachedDriverData failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }
  pAdapter = (sfvmk_adapter_t *)data.ptr;
  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  sfvmk_destroyHelper(pAdapter);
  sfvmk_uplinkDataFini(pAdapter);
  sfvmk_mutexDestroy(pAdapter->lock);
  pAdapter->lock = NULL;

  sfvmk_rxFini(pAdapter);
  sfvmk_txFini(pAdapter);
  /* Deinit port */
  sfvmk_portFini(pAdapter);
  /* Deinit EVQs */
  sfvmk_evFini(pAdapter);

  /* Deinit interrupts */
  status = sfvmk_intrFini(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_intrFini failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Tear down common code subsystems. */
  if (pAdapter->pNic != NULL) {
    efx_nic_reset(pAdapter->pNic);
    efx_nic_unprobe(pAdapter->pNic);
  }

  /* Tear down MCDI. */
  sfvmk_mcdiFini(pAdapter);

  /* Destroy common code context. */
  if (pAdapter->pNic != NULL) {
    efx_nic_destroy(pAdapter->pNic);
    pAdapter->pNic = NULL;
  }

  sfvmk_destroyLock(pAdapter->nicLock.lock);
  /* vmk related resource deallocation */
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
  /* TODO: functinality will be added along with startDevice implementation */
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
  /* No functionality expected by VMK kernel */
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);
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

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER);

  /* Populate driverProps */
  driverProps.moduleID = vmk_ModuleCurrentID;
  vmk_NameCopy(&driverProps.name, &sfvmk_modInfo.driverName);
  driverProps.ops = &sfvmk_DriverOps;
  driverProps.privateData = (vmk_AddrCookie)NULL;

  /* Register Driver with the device layer */
  status = vmk_DriverRegister(&driverProps, &sfvmk_modInfo.driverID);
  if (status == VMK_OK) {
    SFVMK_DEBUG(SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                "Initialization of SFC  driver successful");
  } else {
    SFVMK_ERROR("Initialization of SFC driver failed status: %s",
                vmk_StatusToString(status));
  }

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_DRIVER);

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

