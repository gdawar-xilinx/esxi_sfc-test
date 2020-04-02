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

#include "sfvmk_driver.h"

/* Default number of NetQ supported */
#define SFVMK_NETQ_COUNT_DEFAULT 8

/* Default number of RSSQ supported */
#define SFVMK_RSSQ_COUNT_DEFAULT 0

#define SFVMK_DYN_VPD_AREA_TAG   0x10

/* Initialize module params with default values */
sfvmk_modParams_t modParams = {
  .debugMask = SFVMK_DEBUG_DEFAULT,
  .netQCount = SFVMK_NETQ_COUNT_DEFAULT,
  .vxlanOffload = VMK_TRUE,
#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
  .geneveOffload = VMK_TRUE,
#endif
  .rssQCount = SFVMK_RSSQ_COUNT_DEFAULT,
  .evqType = SFVMK_EVQ_TYPE_AUTO
};

/* List of module parameters */
VMK_MODPARAM_NAMED(debugMask, modParams.debugMask, uint, "Debug Logging Bit Masks");
VMK_MODPARAM_NAMED(netQCount, modParams.netQCount, uint,
                   "NetQ count(includes defQ) [Min:1 Max:15 Default:8]"
                   "(invalid value sets netQCount to default value(8))");
VMK_MODPARAM_NAMED(rssQCount, modParams.rssQCount, uint,
                   "RSSQ count [Min:1 (RSS disable) Max:4 Default:RSS disable]"
                   "(invalid value of rssQCount disables RSS");
VMK_MODPARAM_NAMED(vxlanOffload, modParams.vxlanOffload, bool,
                   "Enable / disable vxlan offload "
                   "[0:Disable, 1:Enable (default)]");
#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
VMK_MODPARAM_NAMED(geneveOffload, modParams.geneveOffload, bool,
                   "Enable / disable geneve offload "
                   "[0:Disable, 1:Enable (default)]");
#endif
#ifdef SFVMK_SUPPORT_SRIOV
VMK_MODPARAM_ARRAY(max_vfs, int, &modParams.maxVfsCount,
                   "Number of VFs per PF [Max:63 Min:0 Default:0]. "
                   "Invalid entry will set max_vfs value to 0 (SR-IOV disable)");
#endif
VMK_MODPARAM_NAMED(evqType, modParams.evqType, uint,
                   "EVQ type [0:Auto (default), 1:Throughput, 2:Low latency]"
                   "(invalid value sets EVQ type to default value (Auto))");

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
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
**
** \return: VMK_TRUE [success] VMK_FALSE [failure]
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

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_INFO,
                      "PCI VendorID %x DevID %x",
                      pAdapter->pciDeviceID.vendorID,
                      pAdapter->pciDeviceID.deviceID);

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
  case EFX_FAMILY_MEDFORD2:
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
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
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
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
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
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
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

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
  status = vmk_PCIMapIOResourceWithAttr(vmk_ModuleCurrentID, pAdapter->pciDevice,
                                        pAdapter->bar.index,
                                        VMK_MAPATTRS_READWRITE |
                                        VMK_MAPATTRS_UNCACHED,
                                        &pAdapter->bar.esbBase);
#else
  status = vmk_PCIMapIOResource(vmk_ModuleCurrentID, pAdapter->pciDevice,
                                pAdapter->bar.index, NULL,
                                &pAdapter->bar.esbBase);
#endif
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
  vmk_uint32 maxEvqCount;
  vmk_uint32 maxRxq, maxTxq;

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

  pAdapter->txDmaDescMaxSize = pNicCfg->enc_tx_dma_desc_size_max;
  vmk_Memset(&limits, 0, sizeof(limits));

  /* Minimum value of intr supported and queues limit,
   * this is the max number of queues can be supported */
  maxTxq = MIN(pNicCfg->enc_txq_limit, pNicCfg->enc_intr_limit);
  maxRxq = MIN(pNicCfg->enc_rxq_limit, pNicCfg->enc_intr_limit);

  /* Get number of queues supported (this is depend on number of cpus) */
  status = vmk_UplinkQueueGetNumQueuesSupported(maxTxq,
                                                maxRxq,
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
   */
  pAdapter->numNetQs = modParams.netQCount;

  if ((modParams.netQCount == 0) || (modParams.netQCount > SFVMK_MAX_NETQ_COUNT))
    pAdapter->numNetQs = SFVMK_NETQ_COUNT_DEFAULT;

  pAdapter->numRSSQs = modParams.rssQCount;
  if (pAdapter->numRSSQs > SFVMK_MAX_RSSQ_COUNT)
    pAdapter->numRSSQs = SFVMK_RSSQ_COUNT_DEFAULT;
  else if (pAdapter->numRSSQs == 1)
    pAdapter->numRSSQs = 0;

  limits.edl_min_evq_count = SFVMK_MIN_EVQ_COUNT;
  /* Max number of event q = netQCount +  (rssQCount + 1)
   * There is one to one mapping between uplink queues and hardware queues
   * a) for rss there are rssQCount hw queues per hardware queue
   * b) also all the rss hardware queues need to be contiguous
   * to allow any uplink queue to support RSSQ and still support (a) and (b)
   * rssQCount RSSQ's are created at the end of hardware queues in addition
   * to one HW queue corresponding to the uplink RSS queue
   *
   * As of now, Hw queue corresponding to the uplink RSS queue is used
   * for transmitting pkts on RSS Q.
   *
   *         ___________________                _____________________
   *         |                 |                |                   |
   *         |                 | One to One     |                   |
   *         |                 | mapping betwn  |                   |
   *         | Uplink Qs       | Uplink Q index |  Hardware Qs      |
   *         |                 | & Hw Q index   | (EvQ, TxQ, RxQ)   |
   *         |                 |                |                   |
   *         |                 |                |                   |
   *         | Uplink Q for RSS| <--------->    | HW Q for RSS (used|for Tx only)
   *         |                 |                |                   |
   *         |                 |                |                   |
   *         |                 |                |                   |
   *         -------------------                | - - - - - - - - - | <-- end of 1 to 1 correspondance
   *                                            |   RSS Q #1        |
   *                                            |   RSS Q #2        |
   *                                            |                   |
   *                                            |                   |
   *                                            |                   |
   *                                            |   RSS Q #rssQCount|
   *                                            ---------------------
   *
   *
   *
   * In the corner case of RSS queue being created on the last uplink queue
   * one hardware queue can be saved but currently this case is not optimized
   */
  maxEvqCount = MIN(limits.edl_max_rxq_count, limits.edl_max_txq_count);

  /* Compute EVQ count based on netQCount */
  if (maxEvqCount <= pAdapter->numNetQs) {
    /* Can't support RSS as available event queue is less than netQCount
     * also netQCount gets limited by the number of event queue available
     */
    pAdapter->numNetQs = maxEvqCount;
    pAdapter->numRSSQs = 0;
    limits.edl_max_evq_count = maxEvqCount;
  } else {
    limits.edl_max_evq_count = pAdapter->numNetQs;
  }

  /* Compute EVQ count if RSS support is required */
  if (pAdapter->numRSSQs) {
    /* If RSS is enabled there should be atleast three more EVQs to support RSS
     * break up of 3 additional event queues
     * 1 event queue corresponding to additional NetQ for RSSQ
     * 2 event queues as RSS as a feature is useful only when there
     * are at least 2 RSS Qs */
    if ((maxEvqCount - pAdapter->numNetQs) < 3) {
      pAdapter->numRSSQs = 0;
    } else {
      limits.edl_max_evq_count = MIN(maxEvqCount, (limits.edl_max_evq_count +
                                     modParams.rssQCount + 1));
    }
  }

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
void
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
** \param[in]  pAdapter      pointer to sfvmk_adapter_t
** \param[in]  pHelperName   Helper queue name
** \param[out] pHelper       Helper queue handle
**
** \return: VMK_OK [success] or VMK_FAILURE [failure]
**
*/
VMK_ReturnStatus
sfvmk_createHelper(sfvmk_adapter_t *pAdapter, char *pHelperName,
                   vmk_Helper *pHelper)
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
  vmk_NameFormat(&props.name, "%s-%s-helper",
                 vmk_NameToString(&sfvmk_modInfo.driverName),
                 pHelperName);
  props.heap = sfvmk_modInfo.heapID;
  props.preallocRequests = VMK_FALSE;
  props.blockingSubmit = VMK_FALSE;
  props.maxRequests = 16;
  props.mutables.minWorlds = 0;
  props.mutables.maxWorlds = 1;
  props.mutables.maxIdleTime = 0;
  props.mutables.maxRequestBlockTime = 0;
  props.tagCompare = NULL;
  props.constructor = NULL;
  props.constructorArg = (vmk_AddrCookie)NULL;

  status = vmk_HelperCreate(&props, pHelper);
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
** \param  helper    Helper queue handle
**
** \return: None
**
*/
void
sfvmk_destroyHelper(sfvmk_adapter_t *pAdapter, vmk_Helper helper)
{
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);
  vmk_HelperDestroy(helper);
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);
}

/* \brief  Handle packet completion. The function works in netPoll context.
**
** \param[in]  pCompCtx Pointer to context info (netPoll, panic, Others)
** \param[in]  pPkt     pointer to pkt
**
** \return: None
*/
static void
sfvmk_pktReleaseNetPoll(sfvmk_pktCompCtx_t *pCompCtx,
                        vmk_PktHandle *pPkt)
{
   VMK_ASSERT_EQ(pCompCtx->type, SFVMK_PKT_COMPLETION_NETPOLL);
   vmk_NetPollQueueCompPkt(pCompCtx->netPoll, pPkt);
}

/* \brief  Handle packet completion. The function works in panic context.
**
** \param[in]  pCompCtx Pointer to context info (netPoll, panic, Others)
** \param[in]  pPkt     pointer to pkt
**
** \return: None
*/
static void
sfvmk_pktReleasePanic(sfvmk_pktCompCtx_t *pCompCtx,
                      vmk_PktHandle *pPkt)
{
   VMK_ASSERT_EQ(pCompCtx->type, SFVMK_PKT_COMPLETION_PANIC);
   vmk_PktReleasePanic(pPkt);
}


/* \brief  Handle packet release request. The function works
**         in Others (other than netPoll and panic) context.
**
** \param[in]  pCompCtx Pointer to context info (netPoll, panic, Others)
** \param[in]  pPkt     pointer to pkt
**
** \return: None
*/
static void
sfvmk_pktReleaseOthers(sfvmk_pktCompCtx_t *pCompCtx,
                       vmk_PktHandle *pPkt)
{
   VMK_ASSERT_EQ(pCompCtx->type, SFVMK_PKT_COMPLETION_OTHERS);
   vmk_PktRelease(pPkt);
}

const sfvmk_pktOps_t sfvmk_packetOps[SFVMK_PKT_COMPLETION_MAX] = {
  [SFVMK_PKT_COMPLETION_NETPOLL] = { sfvmk_pktReleaseNetPoll },
  [SFVMK_PKT_COMPLETION_PANIC]   = { sfvmk_pktReleasePanic },
  [SFVMK_PKT_COMPLETION_OTHERS]  = { sfvmk_pktReleaseOthers },
};

/*! \brief  Routine to set bus mastering mode
**
** \param[in]  pAdapter Pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] or error code [failure]
*/
static VMK_ReturnStatus
sfvmk_setBusMaster(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 cmd;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  status = vmk_PCIReadConfig(vmk_ModuleCurrentID,
                             pAdapter->pciDevice,
                             VMK_PCI_CONFIG_ACCESS_16,
                             SFVMK_PCI_COMMAND, &cmd);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIReadConfig failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  if (!(cmd & SFVMK_PCI_COMMAND_BUS_MASTER)) {

    cmd |= SFVMK_PCI_COMMAND_BUS_MASTER;
    status = vmk_PCIWriteConfig(vmk_ModuleCurrentID,
                                pAdapter->pciDevice,
                                VMK_PCI_CONFIG_ACCESS_16,
                                SFVMK_PCI_COMMAND, cmd);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIWriteConfig(%u) failed status: %s",
                          cmd, vmk_StatusToString(status));
    }
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);

  return status;
}

#define SFVMK_DEFAULT_VXLAN_PORT_NUM  8472
#define SFVMK_DEFAULT_GENEVE_PORT_NUM 6081

/*! \brief Initialize and add tunnel port
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_tunnelInit(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  uint16_t vxlanPortNum = 0;
  uint16_t vxlanDefaultPort = 0;
#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
  uint16_t genevePortNum = 0;
  uint16_t geneveDefaultPort = 0;
#endif

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pAdapter);

  status = efx_tunnel_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_tunnel_init failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Configure default vxlan udp port number */
  if (pAdapter->isTunnelEncapSupported & SFVMK_VXLAN_OFFLOAD) {
    /* Get default vxlan port number */
    vxlanDefaultPort = vmk_BE16ToCPU(vmk_UplinkVXLANPortNBOGet());
    vxlanPortNum = (vxlanDefaultPort ?
                    vxlanDefaultPort :
                    SFVMK_DEFAULT_VXLAN_PORT_NUM);

    status = efx_tunnel_config_udp_add(pAdapter->pNic,
                                       vxlanPortNum,
                                       EFX_TUNNEL_PROTOCOL_VXLAN);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "efx_tunnel_config_udp_add [vxlan] failed status: %s",
                          vmk_StatusToString(status));
      goto failed_tunnel_port_add;
    }

    pAdapter->vxlanUdpPort = vxlanPortNum;
  }

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
  /* Configure default geneve udp port number */
  if (pAdapter->isTunnelEncapSupported & SFVMK_GENEVE_OFFLOAD) {
    /* Get default geneve port number */
    geneveDefaultPort = vmk_BE16ToCPU(vmk_GenevePortGet());
    genevePortNum = (geneveDefaultPort ?
                     geneveDefaultPort :
                     SFVMK_DEFAULT_GENEVE_PORT_NUM);

    status = efx_tunnel_config_udp_add(pAdapter->pNic,
                                       genevePortNum,
                                       EFX_TUNNEL_PROTOCOL_GENEVE);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "efx_tunnel_config_udp_add [geneve] failed status: %s",
                          vmk_StatusToString(status));
      goto failed_tunnel_port_add;
    }

    pAdapter->geneveUdpPort = genevePortNum;
  }
#endif

  pAdapter->startIOTunnelReCfgReqd = VMK_TRUE;

  status = VMK_OK;
  goto done;

failed_tunnel_port_add:
  efx_tunnel_fini(pAdapter->pNic);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Clear out tunnel configuration
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
static void
sfvmk_tunnelFini(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pAdapter);

  /* Remove vxlan port number if already configured */
  if (pAdapter->vxlanUdpPort) {
    status = efx_tunnel_config_udp_remove(pAdapter->pNic,
                                          pAdapter->vxlanUdpPort,
                                          EFX_TUNNEL_PROTOCOL_VXLAN);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "efx_tunnel_config_udp_remove (%d) failed status: %s",
                           pAdapter->vxlanUdpPort, vmk_StatusToString(status));
    }
  }

  efx_tunnel_fini(pAdapter->pNic);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

#ifdef SFVMK_SUPPORT_SRIOV
/*! \brief Retrieve the serial number of specified adapter.
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
**
** \return: VMK_OK on success, Error code otherwise.
*/
static VMK_ReturnStatus
sfvmk_getSerialNumber(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint8 vpdPayload[SFVMK_VPD_MAX_PAYLOAD] = {0};
  vmk_uint8 vpdLen = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);
  if ((status = sfvmk_vpdGetInfo(pAdapter, vpdPayload,
                                 SFVMK_VPD_MAX_PAYLOAD, 0x10,
                                 ('S' | 'N' << 8), &vpdLen)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Get VPD data failed with error %s",
                        vmk_StatusToString(status));
    goto done;
  }

  if (vpdLen >= SFVMK_SN_MAX_LEN) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Buffer overflow for serial number: %s",
                        vpdPayload);
    status = VMK_EOVERFLOW;
    goto done;
  }

  vmk_Strncpy(pAdapter->vpdSN, vpdPayload, SFVMK_SN_MAX_LEN);
  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                      "Device %s SN %s len %u", pAdapter->pciDeviceName.string,
                      pAdapter->vpdSN, vpdLen);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);
  return status;
}

/*! \brief Check if the two adapters belong to same controller.
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t for first port
** \param[in]  pOther    pointer to sfvmk_adapter_t for other port
**
** \return: VMK_TRUE if ports belong to same controller, VMK_FALSE otherwise.
*/
vmk_Bool inline
sfvmk_sameController(sfvmk_adapter_t *pAdapter, sfvmk_adapter_t *pOther)
{
  vmk_Bool match = VMK_FALSE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  if(vmk_Strncmp(pAdapter->vpdSN, pOther->vpdSN, SFVMK_SN_MAX_LEN))
    match = VMK_FALSE;
  else
    match = VMK_TRUE;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                      "SN1: %s, SN2: %s, match: %u",
                      pAdapter->vpdSN, pOther->vpdSN, match);
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);
  return match;
}

/*! \brief  If this is the first adapter in card, add it to the primary list.
**          If not, check if there is an adapter in primary list which belongs
**          to same card as this adapter and add it to its secondary list.
**
 ** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
**
*/
static void
sfvmk_addAdapterToList(sfvmk_adapter_t *pAdapter)
{
  sfvmk_adapter_t *pOther = NULL;
  vmk_ListLinks   *pLink = NULL;
  vmk_ListLinks   *pNext = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  sfvmk_MutexLock(sfvmk_modInfo.listsLock);
  if (pAdapter->pPrimary == pAdapter) {
    /* Scan entries from un-associated list and look for secondary adapters */
    VMK_LIST_FORALL_SAFE(&sfvmk_modInfo.unassociatedList, pLink, pNext) {
      pOther = VMK_LIST_ENTRY(pLink, sfvmk_adapter_t, adapterLink);
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                          "Un-associated list entry %s", pOther->pciDeviceName.string);
      if(sfvmk_sameController(pAdapter, pOther)) {
        vmk_ListRemove(&pOther->adapterLink);
        SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                            "Moving %s to secondary list of %s",
                            pOther->pciDeviceName.string, pAdapter->pciDeviceName.string);
        sfvmk_MutexLock(pAdapter->secondaryListLock);
        vmk_ListInsert(&pOther->adapterLink,
                       vmk_ListAtRear(&pAdapter->secondaryList));
        sfvmk_MutexUnlock(pAdapter->secondaryListLock);
        pOther->pPrimary = pAdapter;
      }
    }

    vmk_ListInsert(&pAdapter->adapterLink,
                   vmk_ListAtRear(&sfvmk_modInfo.primaryList));
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                        "Added %s to primary list",
                        pAdapter->pciDeviceName.string);
  } else {
    /* Adding secondary function; look for primary */
    VMK_LIST_FORALL_SAFE(&sfvmk_modInfo.primaryList, pLink, pNext) {
      pOther = VMK_LIST_ENTRY(pLink, sfvmk_adapter_t, adapterLink);
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                          "Primary list entry %s", pOther->pciDeviceName.string);
      if(sfvmk_sameController(pAdapter, pOther)) {
        SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                            "Adding %s to secondary list of %s",
                            pAdapter->pciDeviceName.string, pOther->pciDeviceName.string);
        sfvmk_MutexLock(pOther->secondaryListLock);
        vmk_ListInsert(&pAdapter->adapterLink,
                       vmk_ListAtRear(&pOther->secondaryList));
        sfvmk_MutexUnlock(pOther->secondaryListLock);
        pAdapter->pPrimary = pOther;
        goto done;
      }
    }

    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                        "Adding %s to un-associated list",
                        pAdapter->pciDeviceName.string);
    vmk_ListInsert(&pAdapter->adapterLink,
                   vmk_ListAtRear(&sfvmk_modInfo.unassociatedList));
  }

done:
  sfvmk_MutexUnlock(sfvmk_modInfo.listsLock);
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);
}

/*! \brief  Remove the adapter from the list it belongs to. Also, move its
**          secondary list entries to un-associated list, if any.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
**
*/
static void
sfvmk_removeAdapterFromList(sfvmk_adapter_t *pAdapter)
{
  sfvmk_adapter_t *pOther = NULL;
  vmk_ListLinks   *pLink = NULL;
  vmk_ListLinks   *pNext = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_DRIVER);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                      "Removing %s",
                      pAdapter->pciDeviceName.string);

  sfvmk_MutexLock(sfvmk_modInfo.listsLock);

  sfvmk_MutexLock(pAdapter->secondaryListLock);
  VMK_LIST_FORALL_SAFE(&pAdapter->secondaryList, pLink, pNext) {
    pOther = VMK_LIST_ENTRY(pLink, sfvmk_adapter_t, adapterLink);

    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                        "Moving secondary %s to unassociated list",
                        pOther->pciDeviceName.string);
      vmk_ListRemove(&pOther->adapterLink);

      vmk_ListInsert(&pOther->adapterLink,
                     vmk_ListAtRear(&sfvmk_modInfo.unassociatedList));

      pOther->pPrimary = NULL;
  }

  vmk_ListRemove(&pAdapter->adapterLink);
  pAdapter->pPrimary = NULL;
  sfvmk_MutexUnlock(pAdapter->secondaryListLock);

  sfvmk_MutexUnlock(sfvmk_modInfo.listsLock);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_DRIVER);
}

#endif

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
  const efx_nic_cfg_t *pNicCfg = NULL;
  vmk_uint8 vpdPayload[SFVMK_VPD_MAX_PAYLOAD] = {0};
  vmk_uint8 vpdLen = 0;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_DRIVER, "VMK device:%p", dev);

#ifdef SFVMK_SUPPORT_SRIOV
  sfvmk_sriovIncrementPfCount();
#endif

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

  status = sfvmk_setBusMaster(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_setBusMaster failed status: %s",
                        vmk_StatusToString(status));
    goto failed_set_bus_master;
  }

  status = sfvmk_createLock(pAdapter, "nicLock",
                            SFVMK_SPINLOCK_RANK_NIC_LOCK,
                            &pAdapter->nicLock.lock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_lock;
  }

  status = efx_nic_create(pAdapter->efxFamily,
                          (efsys_identifier_t *)pAdapter,
                          &pAdapter->bar,
                          &pAdapter->nicLock,
                          &pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_create failed status: %s",
                        vmk_StatusToString(status));
    goto failed_nic_create;
  }

  /* Register driver version with FW */
  status = efx_nic_set_drv_version(pAdapter->pNic,
                                   SFVMK_DRIVER_VERSION_STRING,
                                   sizeof(SFVMK_DRIVER_VERSION_STRING) - 1);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_set_drv_version failed status: %s",
		        vmk_StatusToString(status));
  }

  /* Initialize MCDI to talk to the management controller. */
  status = sfvmk_mcdiInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_mcdiInit failed status: %s",
		        vmk_StatusToString(status));
    goto failed_mcdi_init;
  }

  /* Probe  NIC and build the configuration data area. */
  status = efx_nic_probe(pAdapter->pNic, EFX_FW_VARIANT_FULL_FEATURED);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_probe VAR_FULL_FEATURED failed: %s",
                        vmk_StatusToString(status));
    status = efx_nic_probe(pAdapter->pNic, EFX_FW_VARIANT_DONT_CARE);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_probe VAR_DONT_CARE failed: %s",
                          vmk_StatusToString(status));
      goto failed_nic_probe;
    }
  }

#ifdef SFVMK_SUPPORT_SRIOV
  /* Initialize SR-IOV */
  status = sfvmk_sriovInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_sriovInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_sriov_init;
  }
#endif

  /* Initialize NVRAM. */
  status = efx_nvram_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nvram_init failed status: %s",
                        vmk_StatusToString(status));
    goto failed_nvram_init;
  }

  /* Initialize VPD. */
  status = efx_vpd_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_vpd_init failed status: %s",
                        vmk_StatusToString(status));
    goto failed_vpd_init;
  }

  /* Get VPD info to find out the interface which does not have valid VPD.
     The interface which does not have valid VPD should not be attached.
     This happens in PF partioning which is not supported by the driver.
     TODO: Needs to revisit when SRIOV support is added */
  status = sfvmk_vpdGetInfo(pAdapter, vpdPayload, SFVMK_VPD_MAX_PAYLOAD,
                            SFVMK_DYN_VPD_AREA_TAG, ('S' | 'N' << 8), &vpdLen);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_vpdGetInfo failed status: %s",
                        vmk_StatusToString(status));
    goto failed_vpd_get_info;
  }

  /* Reset NIC. */
  status = efx_nic_reset(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_reset failed status: %s",
                        vmk_StatusToString(status));
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

  status = sfvmk_monInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_monInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_mon_init;
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

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_cfg_get failed");
    goto failed_nic_cfg_get;
  }

  /* Prepare bit mask for supported encap offloads */
  if (pNicCfg->enc_tunnel_encapsulations_supported) {
    if (modParams.vxlanOffload)
      pAdapter->isTunnelEncapSupported = SFVMK_VXLAN_OFFLOAD;
#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
    if (modParams.geneveOffload)
      pAdapter->isTunnelEncapSupported |= SFVMK_GENEVE_OFFLOAD;
#endif
  }

  if (pAdapter->isTunnelEncapSupported) {
    status = sfvmk_tunnelInit(pAdapter);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_tunnelInit failed status: %s",
                          vmk_StatusToString(status));
      goto failed_tunnel_init;
    }
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

  status = sfvmk_createHelper(pAdapter, "drv", &pAdapter->helper);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createHelper failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_helper;
  }

#ifdef SFVMK_SUPPORT_SRIOV
  pAdapter->pfIndex = pNicCfg->enc_pf;

  status = sfvmk_getSerialNumber(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_getSerialNumber failed with error %s",
                        vmk_StatusToString(status));
    goto failed_get_serial_number;
  }

  if (pNicCfg->enc_assigned_port == 0) {
    pAdapter->pPrimary = pAdapter;
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_DRIVER, SFVMK_LOG_LEVEL_DBG,
                        "Primary adapter set");
  }

  sfvmk_addAdapterToList(pAdapter);
#endif

  efx_nic_fini(pAdapter->pNic);
  pAdapter->state = SFVMK_ADAPTER_STATE_REGISTERED;

  /* Initialize startIO completion event */
  pAdapter->startIO_compl_event = (vmk_WorldEventID)&pAdapter->startIO_compl_event;

  goto done;

#ifdef SFVMK_SUPPORT_SRIOV
failed_get_serial_number:
#endif
failed_create_helper:
failed_set_drvdata:
  sfvmk_uplinkDataFini(pAdapter);

failed_uplinkData_init:
  sfvmk_mutexDestroy(pAdapter->lock);
  pAdapter->lock = NULL;

failed_mutex_init:
  if (pAdapter->isTunnelEncapSupported)
    sfvmk_tunnelFini(pAdapter);

failed_nic_cfg_get:
failed_tunnel_init:
  sfvmk_rxFini(pAdapter);

failed_rx_init:
  sfvmk_txFini(pAdapter);

failed_tx_init:
  sfvmk_monFini(pAdapter);

failed_mon_init:
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
failed_vpd_get_info:
  efx_vpd_fini(pAdapter->pNic);

failed_vpd_init:
  efx_nvram_fini(pAdapter->pNic);

failed_nvram_init:
#ifdef SFVMK_SUPPORT_SRIOV
  sfvmk_sriovFini(pAdapter);
failed_sriov_init:
#endif
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
failed_set_bus_master:
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
** \return: VMK_OK [success] error code [failure]
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

#ifdef SFVMK_SUPPORT_SRIOV
  status = sfvmk_registerVFs(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_registerVFs failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }
#endif

done:
  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_DRIVER, "VMK device:%p", device);

  return status;
}

/*! \brief : Callback routine for the device layer to notify the driver to
** release control of a driver.
**
** \param[in]  dev  pointer to vmkDevice
**
** \return: VMK_OK [success] error code [failure]
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

  sfvmk_destroyHelper(pAdapter, pAdapter->helper);
  sfvmk_uplinkDataFini(pAdapter);
  sfvmk_mutexDestroy(pAdapter->lock);
  pAdapter->lock = NULL;

  if (pAdapter->isTunnelEncapSupported)
    sfvmk_tunnelFini(pAdapter);
  sfvmk_rxFini(pAdapter);
  sfvmk_txFini(pAdapter);
  /* Deinit mon */
  sfvmk_monFini(pAdapter);
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

#ifdef SFVMK_SUPPORT_SRIOV
  /*
   * Invoke sfvmk_proxyAuthFini unconditionally as proxy state
   * is allocated on the primary adapter even when no VFs are
   * enabled on the primary adapter.
   */
  sfvmk_proxyAuthFini(pAdapter);
  if (pAdapter->evbState == SFVMK_EVB_STATE_STARTED) {
    status = sfvmk_evbSwitchFini(pAdapter);
    if ((status != VMK_OK) && (status != VMK_BAD_PARAM)) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_evbSwitchFini failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }
    status = VMK_OK;
  }
#endif

  /* Tear down common code subsystems. */
  if (pAdapter->pNic != NULL) {
    efx_nvram_fini(pAdapter->pNic);
    efx_vpd_fini(pAdapter->pNic);
    efx_nic_reset(pAdapter->pNic);
  }

#ifdef SFVMK_SUPPORT_SRIOV
  sfvmk_removeAdapterFromList(pAdapter);
  /* De-init SR-IOV */
  status = sfvmk_sriovFini(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_sriovFini failed status: %s",
                        vmk_StatusToString(status));
  }

#endif

  if (pAdapter->pNic != NULL)
    efx_nic_unprobe(pAdapter->pNic);

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
#ifdef SFVMK_SUPPORT_SRIOV
  sfvmk_sriovDecrementPfCount();
#endif

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


