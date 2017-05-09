/* **********************************************************
 * Copyright 2017 - 2018 Solarflare Inc.  All rights reserved.
 * -- Solarflare Confidential
 * **********************************************************/

#include "sfvmk.h"
#include "efsys.h"
#include "sfvmk_driver.h"
#include "sfvmk_tx.h"
#include "sfvmk_ev.h"
#include "sfvmk_rx.h"
#include "sfvmk_uplink.h"
#include "sfvmk_util.h"


static VMK_ReturnStatus sfvmk_DeviceAttach(vmk_Device device);
static VMK_ReturnStatus sfvmk_DeviceDetach(vmk_Device device);
static VMK_ReturnStatus sfvmk_DeviceScan(vmk_Device device);
static VMK_ReturnStatus sfvmk_DeviceShutdown(vmk_Device device);
static VMK_ReturnStatus sfvmk_DeviceStart(vmk_Device device);
static void sfvmk_DeviceForget(vmk_Device device);
extern VMK_ReturnStatus sfvmk_CreateLock(const char *lckName, vmk_LockRank rank, vmk_Lock *lock);
extern void sfvmk_DestroyLock(vmk_Lock lock);
VMK_ReturnStatus sfvmk_removeUplinkDevice();

static int sfvmk_tx_ring_entries = SFVMK_NDESCS;
static int sfvmk_rx_ring_entries = SFVMK_NDESCS;
static void elxnet_initializeInterrupts(sfvmk_adapter *devData);
static void
elxnet_initializeInterrupts(sfvmk_adapter *devData)
{
   vmk_int16 i;

   for (i = 0; i < 30; i++) {
      devData->sfvmkIntrInfo.intrCookies[i] = VMK_INVALID_INTRCOOKIE;
   }
}

/*
 ***********************************************************************
 * Device Driver Operations
 ***********************************************************************
 */

static vmk_DeviceOps sfvmk_UplinkDevOps = {
   .removeDevice = sfvmk_removeUplinkDevice
};

static vmk_DriverOps sfvmk_DriverOps = {
   .attachDevice  = sfvmk_DeviceAttach,
   .detachDevice  = sfvmk_DeviceDetach,
   .scanDevice    = sfvmk_DeviceScan,
   .quiesceDevice = sfvmk_DeviceShutdown,
   .startDevice   = sfvmk_DeviceStart,
   .forgetDevice  = sfvmk_DeviceForget,
};
#if 0 
/*
 ***********************************************************************
 *
 * sfvmk_memPoolAlloc
 *
 *      Allocates memory from the mempool.
 *
 *      param [in]  size           size of memory to be allocated
 *      param [out] allocSize      actual size that got allocated.
 *
 * Results:
 *      Pointer to Mem VA or NULL.
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

void *
sfvmk_memPoolAlloc(vmk_uint64 size, vmk_uint64 *allocSize)
{
	VMK_ReturnStatus status;
	vmk_VA va;
	vmk_MemPoolAllocRequest request;
	vmk_MemPoolAllocProps props;
	vmk_MapRequest map_req;
	vmk_MpnRange mpn_range;

//	vmk_LogMessage("sfvmk_memPoolAlloc is invoked!");

	size = VMK_UTIL_ROUNDUP(size, VMK_PAGE_SIZE);

	/* First convert bytes to pages and allocate from the memory pool */
	props.physContiguity = VMK_MEM_PHYS_CONTIGUOUS;
	props.physRange = VMK_PHYS_ADDR_ANY;
	props.creationTimeoutMS = 10;

        request.numPages = size >> VMK_PAGE_SHIFT;
        request.numElements = 1;
        request.mpnRanges = &mpn_range;
        
        
	status = vmk_MemPoolAlloc(sfvmk_ModInfo.memPoolID, &props,
	                         &request);
	if (status != VMK_OK) {
	  SFVMK_ERROR("vmk_MemPoolAlloc failed: %s", vmk_StatusToString(status));
	  goto allocation_err;
	}

	/* Call vmk_Map to get virtual address */
	map_req.mapType     = VMK_MAPTYPE_DEFAULT;
	map_req.mapAttrs    = VMK_MAPATTRS_READWRITE;
	map_req.numElements = 1;
	map_req.mpnRanges   = &mpn_range;

	status = vmk_Map(vmk_ModuleCurrentID, &map_req, &va);
	if (status != VMK_OK) {
	  SFVMK_ERROR("vmk_Map failed: %s", vmk_StatusToString(status));
	  goto mapping_err;
	}

	if ((void *)va != NULL) {
	  vmk_Memset((void *)va, 0, size);
	}

	*allocSize = size;

//	vmk_LogMessage("allocated mem va=%p, allocated size=%lu",
//	                (void *)va, size);


	return (void *)va;

	mapping_err:
	vmk_MemPoolFree(&request);
	allocation_err:
	return NULL;
}

#endif 

/*
 *****************************************************************************
 *
 * sfvmk_createDMAEngine
 *
 *    Device DMA engine for device
 *
 *      param[in] devData        pointer to elxnet device
 *
 * Results:
 *      retval: VMK_OK         Successful creation of DMA engine
 *      retval: VMK_FAILURE    Failure to create DMA engine
 *
 * Side effects:
 *    None
 *
 *****************************************************************************
 */

static VMK_ReturnStatus
sfvmk_createDMAEngine(sfvmk_adapter *sfAdapter)
{
	VMK_ReturnStatus status;
	vmk_DMAConstraints dmaConstraints;
	vmk_DMAEngineProps dmaProps;

	vmk_Memset(&dmaConstraints, 0, sizeof(dmaConstraints));
	dmaConstraints.addressMask = VMK_ADDRESS_MASK_64BIT;

	vmk_Memset(&dmaProps, 0, sizeof(dmaProps));
	vmk_NameCopy(&dmaProps.name, &sfvmk_ModInfo.driverName);
	dmaProps.module = vmk_ModuleCurrentID;
	dmaProps.device = sfAdapter->vmkDevice;
	dmaProps.constraints = &dmaConstraints;
	dmaProps.flags = VMK_DMA_ENGINE_FLAGS_COHERENT;

	status = vmk_DMAEngineCreate(&dmaProps, &sfAdapter->vmkDmaEngine);
	if (status == VMK_OK) {
		// [praveen] needs to check
	 // sfAdapter->flags |= ELXNET_FLAGS_64BIT_DMA_CAP;
	} else {
	  dmaConstraints.addressMask = VMK_ADDRESS_MASK_32BIT;
	  status = vmk_DMAEngineCreate(&dmaProps, &sfAdapter->vmkDmaEngine);
	}

	return (status);
}


/*
 *****************************************************************************
 *
 * sfvmk_bar_init --
 *
 *      Map all the BARs for the device.
 *
 *      param[in] devData        pointer to elxnet device
 *
 * Results:
 *      retval: VMK_OK:        Successfully mapped all BARS of device
 *      retval: VMK_FAILURE:   FAiled to map BARS of device
 *
 * Side effects:
 *    None
 *
 *****************************************************************************
 */

static VMK_ReturnStatus
sfvmk_bar_init(sfvmk_adapter *sfAdapter)
{
	VMK_ReturnStatus status;
	efsys_bar_t	*bar;

	bar = &sfAdapter->bar;

	status = vmk_PCIMapIOResource(vmk_ModuleCurrentID,sfAdapter->pciDevice,
			EFX_MEM_BAR, NULL,(vmk_VA *)&bar->esb_base);
	if (status != VMK_OK) {
	 SFVMK_ERR(sfAdapter, "Failed to map BAR2 (%x)", status);
	}
	status = sfvmk_CreateLock("mem-bar", VMK_SPINLOCK_RANK_HIGHEST-2, &bar->esb_lock);
        if (status != VMK_OK) {
         SFVMK_ERR(sfAdapter, "Failed to create lock for BAR2 (%x)", status);
	vmk_LogMessage("sfvmk bar init failed ");
        }

 

	vmk_LogMessage("sfvmk bar init ....bar address is %x\n", (unsigned int )bar->esb_base);

	return (status);
}

static VMK_ReturnStatus
sfvmk_unmapBars(sfvmk_adapter *sfAdapter)
{
   VMK_ReturnStatus status;

   if (sfAdapter->bar.esb_base) {
      status = vmk_PCIUnmapIOResource(vmk_ModuleCurrentID,
                                      sfAdapter->pciDevice,
                                      EFX_MEM_BAR);
      VMK_ASSERT(status == VMK_OK);
      SFVMK_DBG(sfAdapter, SFVMK_DBG_DRIVER, 2, "Freed Bar %d ",
                     EFX_MEM_BAR);
   }
   return (VMK_OK);
}

static VMK_ReturnStatus
sfvmk_checkDevice(sfvmk_adapter *sfAdapter)
{
	int rc;

	rc = efx_family(sfAdapter->pciDeviceID.vendorID,
			sfAdapter->pciDeviceID.deviceID, &sfAdapter->efxFamily);
	if (rc != 0) {
		SFVMK_DBG(sfAdapter, SFVMK_DBG_DRIVER, 0, "efx_family fail %d", rc);
		return VMK_FAILURE;
	}

	if (sfAdapter->efxFamily == EFX_FAMILY_MEDFORD) {
		//praveen needs to check counter part of this in vmkernel
		//device_set_desc(dev, "Solarflare SFC9200 family");
		return VMK_OK;
	}

	SFVMK_ERROR("impossible controller family %d", sfAdapter->efxFamily);
	return VMK_FAILURE;
}

static VMK_ReturnStatus
sfvmk_SetBusMaster(sfvmk_adapter *adapter)
{
	vmk_uint32 cmd;
	VMK_ReturnStatus status = VMK_OK;

	status = vmk_PCIReadConfig(vmk_ModuleCurrentID, adapter->pciDevice,
	                          VMK_PCI_CONFIG_ACCESS_16,
	                          SFC_PCI_COMMAND, &cmd);
	if (status != VMK_OK) {
	  SFVMK_ERROR("Failed to read PCI config. offset: %u (%x)",
	               SFC_PCI_COMMAND, status);
	  goto out;
	}

	if (!(cmd & SFC_PCI_COMMAND_BUS_MASTER)) {
	  SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 0, "Enable bus mastering");
	  cmd |= SFC_PCI_COMMAND_BUS_MASTER;
	  status = vmk_PCIWriteConfig(vmk_ModuleCurrentID, adapter->pciDevice,
	                              VMK_PCI_CONFIG_ACCESS_16,
	                              SFC_PCI_COMMAND, cmd);
	  if (status != VMK_OK) {
	     SFVMK_ERROR("Failed to write PCI config. "
	                  "offset: %u, data: %u (%x)",
	                  SFC_PCI_COMMAND, cmd, status);
	     goto out;
	  }
	}

	return VMK_OK;

	out:
	SFVMK_ERROR("Failed to enable bus mastering!");
	return status;
}
static int
sfvmk_estimate_rsrc_limits(sfvmk_adapter *adapter)
{
	efx_drv_limits_t limits;
	int rc;
	unsigned int evq_max;
	uint32_t evq_allocated;
	uint32_t rxq_allocated;
	uint32_t txq_allocated;
        VMK_ReturnStatus status = VMK_OK;

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
				VMK_ASSERT(0);
		 }
    
           vmk_LogMessage("Max number of txq %d and rxq %d supported by uplink\n", limits.edl_max_txq_count
                 ,limits.edl_max_rxq_count); 

   evq_max = limits.edl_max_txq_count;
  // tuning parameter
	if (adapter->max_rss_channels > 0)
		evq_max = MIN(evq_max, adapter->max_rss_channels);

	limits.edl_min_evq_count = 1;
	limits.edl_max_evq_count = evq_max;
	limits.edl_min_txq_count = SFVMK_TXQ_NTYPES;
//	limits.edl_max_txq_count = evq_max + SFVMK_TXQ_NTYPES - 1;
	limits.edl_min_rxq_count = 1;
	limits.edl_max_rxq_count = evq_max - SFVMK_TXQ_NTYPES + 1;

	efx_nic_set_drv_limits(adapter->enp, &limits);

	if ((rc = efx_nic_init(adapter->enp)) != 0)
		return (rc);

	rc = efx_nic_get_vi_pool(adapter->enp, &evq_allocated, &rxq_allocated,
				 &txq_allocated);
	if (rc != 0) {
		efx_nic_fini(adapter->enp);
		return (rc);
	}

	VMK_ASSERT(txq_allocated >= SFXGE_TXQ_NTYPES);

	adapter->evq_max = MIN(evq_allocated, evq_max);
	adapter->evq_max = MIN(rxq_allocated, adapter->evq_max);
	adapter->evq_max = MIN(txq_allocated - (SFVMK_TXQ_NTYPES - 1),
			  adapter->evq_max);

	VMK_ASSERT(adapter->evq_max <= evq_max);
         
	vmk_LogMessage(" txq allocated = %d rxq_allocated = %d\n ",rxq_allocated, txq_allocated );
	vmk_LogMessage(" event queue max = %d \n ", adapter->evq_max);
	/*
	 * NIC is kept initialized in the case of success to be able to
	 * initialize port to find out media types.
	 */
	return (0);
}
#if 0 
VMK_ReturnStatus
sfvmk_DMAMapMA(sfvmk_adapter *adapter,   // IN: adapter
                 vmk_MA ma,                  // IN: machine address
                 vmk_uint32 size,            // IN: size
                 vmk_DMADirection direction, // IN: DMA mapping direction
                 vmk_DMAEngine engine,       // IN: DMA engine
                 vmk_IOA *ioa)               // OUT: starting IO address
{
   VMK_ReturnStatus status;
   vmk_SgElem in, out;
   vmk_DMAMapErrorInfo err;

   in.addr = ma;
   in.length = size;
   status = vmk_DMAMapElem(engine,
                           direction,
                           &in, VMK_TRUE, &out, &err);
   if (VMK_UNLIKELY(status != VMK_OK)) {
      if (status == VMK_DMA_MAPPING_FAILED) {
         //praveen logging
         SFVMK_DBG_ONLY_THROTTLED(adapter, SFVMK_DBG_UTILS, 2,
                                     "Failed to map range [0x%lx, 0x%lx]: %s",
                                     in.addr, in.addr + in.length - 1,
                                     vmk_DMAMapErrorReasonToString(err.reason));

      } else {

         SFVMK_DBG_ONLY_THROTTLED(adapter, SFVMK_DBG_UTILS, 2,
                                     "Failed to map range [0x%lx, 0x%lx] (%x)",
                                     in.addr, in.addr + in.length - 1, status);

      }
   } else {
      VMK_ASSERT_EQ(in.length, out.length);
      *ioa = out.ioAddr;
   }
   return status;
}
VMK_ReturnStatus
sfvmk_DMAMapVA(sfvmk_adapter *adapter,    // IN: adapter
                 void *va,                    // IN: virtual address
                 vmk_uint32 size,             // IN: size
                 vmk_DMADirection direction,  // IN: DMA mapping direction
                 vmk_DMAEngine engine,        // IN: DMA engine
                 vmk_IOA *ioa)                // OUT: starting IO address
{
   vmk_MA ma;
   vmk_VA2MA((vmk_VA)va, size, &ma);
   return sfvmk_DMAMapMA(adapter, ma, size, direction, engine, ioa);
}



void *
sfvmk_AllocCoherentDMAMapping(sfvmk_adapter *adapter, // IN:  adapter
                                vmk_uint32 size,          // IN:  size
                                vmk_IOA *ioAddr)          // OUT: IO address
{
   VMK_ReturnStatus status;
   void *va;

   *ioAddr = 0;
   va = sfvmk_memPoolAlloc(size);
   if (va == NULL) {
      goto fail_mempool_alloc;
   }
   status = sfvmk_DMAMapVA(adapter, va, size,
                             VMK_DMA_DIRECTION_BIDIRECTIONAL,
                             adapter->vmkDmaEngine, ioAddr);
   if (VMK_UNLIKELY(status != VMK_OK)) {
      goto fail_dma_map;
   }
   return va;

  fail_dma_map:
   //praveen
   //svmk_MemPoolFree(va, size);
  fail_mempool_alloc:
   return NULL;
}

/*
 *-----------------------------------------------------------------------------
 *
 * Nvmxnet3DMAUnmap --
 *
 *      Unmap specified MA range.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
void
sfvmk_DMAUnmap(sfvmk_adapter *adapter,   // IN: adapter
                 vmk_IOA ioAddr,             // IN: IO address
                 vmk_uint32 size,            // IN: size
                 vmk_DMADirection direction, // IN: DMA mapping direction
                 vmk_DMAEngine engine)       // IN: DMA engine
{
   vmk_SgElem elem;
   VMK_ReturnStatus status;

   elem.ioAddr = ioAddr;
   elem.length = size;
   status = vmk_DMAUnmapElem(engine, direction, &elem);
   if (VMK_UNLIKELY(status != VMK_OK)) {
      SFVMK_DBG_ONLY_THROTTLED(adapter, NVMXNET3_DBG_UTILS, 2,
                                  "Failed to unmap range [0x%lx, 0x%lx] (%x)",
                                  elem.ioAddr, elem.ioAddr + elem.length - 1,
                                  status);
   }
}

/*
 *-----------------------------------------------------------------------------
 *
 * Nvmxnet3FreeCoherentDMAMapping --
 *
 *      Unmap DMA mapping and free allocated kernel memory from mempool.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      None.
 *
 *-----------------------------------------------------------------------------
 */
void
sfvmk_FreeCoherentDMAMapping(sfvmk_adapter *adapter,  // IN: adapter
                               void *va,                  // IN: virtual address
                               vmk_IOA ioAddr,            // IN: IO address
                               vmk_uint32 size)           // IN: size
{
   sfvmk_DMAUnmap(adapter, ioAddr, size,
                    VMK_DMA_DIRECTION_BIDIRECTIONAL,
                    adapter->vmkDmaEngine);
   sfvmk_memPoolFree(va, size);
}
#endif 


VMK_ReturnStatus
sfvmk_intrInit(sfvmk_adapter *adapter)
{
   unsigned int numVec, numIntrsAlloced, i = 0;
   VMK_ReturnStatus status;

   numVec = adapter->evq_max;

   status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                   adapter->pciDevice,
                                   VMK_PCI_INTERRUPT_TYPE_MSIX,
                                   numVec,
                                   numVec, NULL,
                                   adapter->sfvmkIntrInfo.intrCookies, &numIntrsAlloced);
   if (status == VMK_OK) {

      vmk_LogMessage("number of interrupt allocated is %d\n", numIntrsAlloced);
      SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 0,
                     "Alloced %d vectors for device", numIntrsAlloced);
      adapter->sfvmkIntrInfo.numIntrAlloc = numIntrsAlloced;
			//praveen
      //adapter->sfvmkIntrInfo.msix_enabled = true;
			adapter->sfvmkIntrInfo.type = EFX_INTR_MESSAGE;
   } else {
      for (i = 0; i < numVec; i++) {
         adapter->sfvmkIntrInfo.intrCookies[i] = VMK_INVALID_INTRCOOKIE;
      }
      SFVMK_ERR(adapter,
                     "PCIAllocIntrCookie failed with error 0x%x "
                     "for %d vectors", status, numVec);

      /*
       * Try single msix vector before giving up to try legacy..
       */
      status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                      adapter->pciDevice,
                                      VMK_PCI_INTERRUPT_TYPE_MSIX,
                                      1,
                                      1, NULL,
                                     adapter->sfvmkIntrInfo.intrCookies, &numIntrsAlloced);
      if (status != VMK_OK) {
         SFVMK_ERR(adapter,
                        "PCIAllocIntrCookie failed for 1 MSIx, using Legacy");
         adapter->sfvmkIntrInfo.intrCookies[0] = VMK_INVALID_INTRCOOKIE;
      } else {
         SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 2,
                        "Alloced 1 MSIx vector for device");
         adapter->sfvmkIntrInfo.numIntrAlloc = 1;
				 //praveen needs to check
        // adapter->sfvmkIntrInfo.msix_enabled = true;
				 adapter->sfvmkIntrInfo.type = EFX_INTR_MESSAGE;
      }

      /* If RSS is enabled, disable RSS. */
   }
   return (status);
}

static void
sfvmk_FreeInterrupts(sfvmk_adapter *adapter)
{
   vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, adapter->pciDevice);
}
static void
sfvmk_DestroyDMAEngine(sfvmk_adapter *adapter)
{
   vmk_DMAEngineDestroy(adapter->vmkDmaEngine);
}


/*
 *****************************************************************************
 *
 * sfvmk_updateDevData --
 *
 *    Local function to update shared dev data during init and reset
 *
 *    param[in]     devData    Handle to device being attached to driver
 *
 * Results:
 *    None
 *
 * Side effects:
 *    None
 *
 *****************************************************************************
 */

void
sfvmk_updateDevData(sfvmk_adapter * devData)
{
   vmk_UplinkSharedData *sharedData;

   sharedData = &devData->sharedData;

   vmk_NameInitialize(&sharedData->driverInfo.driver, "sfvmk");
   vmk_NameInitialize(&sharedData->driverInfo.moduleInterface, "native");

}






/*
 *********************************************************************
 * Callback routine for the device layer to announce device to the
 * driver.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_DeviceAttach(vmk_Device dev)
{
	vmk_LogMessage("AttachDevice is invoked!");
	VMK_ReturnStatus status = VMK_OK;
	sfvmk_adapter *sfAdapter = NULL;
	vmk_AddrCookie pciDeviceCookie;
	efx_nic_t *enp;
        efsys_lock_t *enp_lock; 
	int error ;

	// praveen needs to check id second parameter is required
	sfAdapter = sfvmk_memPoolAlloc(sizeof(sfvmk_adapter));
	if (sfAdapter == NULL) {
		status = VMK_FAILURE;
		goto fail;
		// goto fail_adapter_alloc;
	}
	vmk_LogMessage("sfvmk_SetBusMaster1");
	sfAdapter->max_rss_channels =0;
	sfAdapter->vmkDevice = dev;
	sfAdapter->debugMask = (vmk_uint32)(-1);
        sfAdapter->rxq_enteries = sfvmk_rx_ring_entries; 
        sfAdapter->txq_enteries = sfvmk_tx_ring_entries; 

	SFVMK_DBG(sfAdapter, SFVMK_DBG_DRIVER, 2,
						"*** allocated devData=%p, "
						"requested size=%lu \n",
						(void *)sfAdapter, sizeof(sfvmk_adapter));

	status = vmk_DeviceGetRegistrationData(dev, &pciDeviceCookie);
	if (status != VMK_OK) {
		SFVMK_ERROR("Get device from kernel failed status: 0x%x", status);
		goto sfvmk_kernel_dev_fail;
	}
	vmk_LogMessage("sfvmk_SetBusMaster2");

	sfAdapter->pciDevice = pciDeviceCookie.ptr;
	vmk_LogMessage("vmk_DeviceGetRegistrationData");

	status = vmk_PCIQueryDeviceID(sfAdapter->pciDevice, &sfAdapter->pciDeviceID);
	if (status != VMK_OK) {
		SFVMK_ERROR("Get deviceID from kernel device failed status: 0x%x",status);
		goto sfvmk_kernel_dev_fail;
	}

	vmk_LogMessage("sfvmk_SetBusMaster3");
	status = sfvmk_checkDevice(sfAdapter);
	if (status != VMK_OK) {
		goto sfvmk_kernel_dev_fail;
	}

	SFVMK_DBG(sfAdapter,SFVMK_DBG_DRIVER, 2,
						"Found kernel device with following "
						"Dev Id 0x%04x, "
						"Vend ID 0x%04x, "
						"Sub Dev ID 0x%04x, "
						"Sub Vend ID 0x%04x,",
						sfAdapter->pciDeviceID.deviceID,
						sfAdapter->pciDeviceID.vendorID,
						sfAdapter->pciDeviceID.subDeviceID,
						sfAdapter->pciDeviceID.subVendorID);


	status = vmk_PCIQueryDeviceAddr(sfAdapter->pciDevice,&sfAdapter->pciDeviceAddr);
	if (status != VMK_OK) {
		SFVMK_ERROR("Get deviceAddr for device failed, status 0x%x",status);
		goto sfvmk_kernel_dev_fail;
	}
	vmk_LogMessage("sfvmk_SetBusMaster4");
	vmk_StringFormat(sfAdapter->pciDeviceName.string,
										sizeof(sfAdapter->pciDeviceName.string),
										NULL,
										SFC_SBDF_FMT,
										sfAdapter->pciDeviceAddr.seg,
										sfAdapter->pciDeviceAddr.bus,
										sfAdapter->pciDeviceAddr.dev, sfAdapter->pciDeviceAddr.fn);


	SFVMK_DBG(sfAdapter, SFVMK_DBG_DRIVER, 2, "Found kernel device");

	status = sfvmk_createDMAEngine(sfAdapter);
	if (status != VMK_OK) {
		SFVMK_ERR(sfAdapter, "DMA Engine create failed");
		goto sfvmk_DMA_eng_create_fail;
	}

	status = sfvmk_bar_init(sfAdapter);
	if (status != VMK_OK) {
		goto sfvmk_map_bar_fail;
	}

	sfvmk_SetBusMaster(sfAdapter);


//praveeen dummy funtion for initializing lock
   enp_lock = &sfAdapter->enp_lock; 
   sfvmk_CreateLock("enp", VMK_SPINLOCK_RANK_HIGHEST-1, &enp_lock->lock);
   if ((error = efx_nic_create(sfAdapter->efxFamily, (efsys_identifier_t *)sfAdapter,
			 &sfAdapter->bar, &sfAdapter->enp_lock, &enp)) != 0)
	{
	    vmk_LogMessage("failed in creating nic");
		goto fail;
	}

	// store nic pointer
	sfAdapter->enp = enp;

	/* Initialize MCDI to talk to the microcontroller. */
        SFVMK_DBG(sfAdapter,SFVMK_DBG_DRIVER, 2, "mcdi_init...");
	if ((error = sfvmk_mcdi_init(sfAdapter)) != 0)
		goto fail;

	/* Probe the NIC and build the configuration data area. */
        SFVMK_DBG(sfAdapter,SFVMK_DBG_DRIVER, 2, "nic_probe...");
	if ((error = efx_nic_probe(enp)) != 0)
		goto fail;

	/* Initialize the NVRAM. */
        SFVMK_DBG(sfAdapter,SFVMK_DBG_DRIVER, 2, "nvram_init...");
	if ((error = efx_nvram_init(enp)) != 0)
		goto fail;

	/* Initialize the VPD. */
        SFVMK_DBG(sfAdapter,SFVMK_DBG_DRIVER, 2, "vpd_init...");
	if ((error = efx_vpd_init(enp)) != 0)
		goto fail;

        SFVMK_DBG(sfAdapter,SFVMK_DBG_DRIVER, 2, "mcdi_new_epoch...");
	efx_mcdi_new_epoch(enp);

	/* Reset the NIC. */
        SFVMK_DBG(sfAdapter,SFVMK_DBG_DRIVER, 2, "nic_reset...");
	if ((error = efx_nic_reset(enp)) != 0)
		goto fail;

     sfvmk_estimate_rsrc_limits(sfAdapter);
   /* Initialize interrupt vectors to invalid value */
       elxnet_initializeInterrupts(sfAdapter);

      SFVMK_DBG(sfAdapter,SFVMK_DBG_DRIVER, 2, "sfvmk_intrInit...");
	sfvmk_intrInit(sfAdapter);
      SFVMK_DBG(sfAdapter,SFVMK_DBG_DRIVER, 2, "sfvmk_ev_init...");
	sfvmk_ev_init(sfAdapter);
       
      SFVMK_DBG(sfAdapter,SFVMK_DBG_DRIVER, 2, "sfvmk_rx_init...");
     vmk_LogMessage("sfvmk_rx_init...");
	sfvmk_rx_init(sfAdapter);
     vmk_LogMessage("sfvmk_tx_init...");
	sfvmk_tx_init(sfAdapter);
     vmk_LogMessage("sfvmk_tx_init done...");
      SFVMK_DBG(sfAdapter,SFVMK_DBG_DRIVER, 2, "sfvmk_rx_init done...");
      sfvmk_createUplinkData(sfAdapter);
      sfvmk_updateDevData(sfAdapter);
       status = vmk_DeviceSetAttachedDriverData(dev, sfAdapter);
        {
         efx_phy_media_type_t medium_type =0;
        efx_phy_media_type_get(sfAdapter->enp, &medium_type);
        vmk_LogMessage("phy type is %x\n", medium_type);
        }
        return status;


  /* UnLock */
  //EFSYS_UNLOCK(eslp, lock_state);

  /* Lock Destroy  */
 // if(esl.lock)
 //   sfvmk_DestroyLock(esl.lock);
sfvmk_map_bar_fail:
	vmk_DMAEngineDestroy(sfAdapter->vmkDmaEngine);
sfvmk_DMA_eng_create_fail:
sfvmk_kernel_dev_fail:
	sfvmk_memPoolFree(sfAdapter, sfAdapter->memSize);

fail:
  return VMK_OK;
}

/*
 ***********************************************************************
 * sfvmk_DeviceStart --                                             */ /**
 *
 * Callback routine for the device layer to notify the driver to bring
 * up the specified device.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */
static VMK_ReturnStatus
sfvmk_DeviceStart(vmk_Device dev)
{
  vmk_LogMessage("StartDevice is invoked!");
  return VMK_OK;
}

/*
 ***********************************************************************
 * sfvmk_DeviceScan --                                              */ /**
 *
 * Callback routine for the device layer to notify the driver to scan
 * for new devices.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */


static VMK_ReturnStatus
sfvmk_DeviceScan(vmk_Device sfvmk_Device)
{
  VMK_ReturnStatus status = 0;
  sfvmk_adapter *adapter;
  vmk_Name busName;
  vmk_DeviceID deviceID;
  vmk_DeviceProps deviceProps;

  vmk_LogMessage("ScanDevice is invoked!");
  status = vmk_DeviceGetAttachedDriverData(sfvmk_Device, (vmk_AddrCookie *) &adapter);

  VMK_ASSERT(status == VMK_OK);
  VMK_ASSERT(adapter != NULL);

  status = vmk_NameInitialize(&busName, VMK_LOGICAL_BUS_NAME);
  VMK_ASSERT(status == VMK_OK);
  status = vmk_BusTypeFind(&busName, &deviceID.busType);
  VMK_ASSERT(status == VMK_OK);

  status = vmk_LogicalCreateBusAddress(sfvmk_ModInfo.driverID,
									   adapter->vmkDevice,
									   0,
									   &deviceID.busAddress,
									   &deviceID.busAddressLen);
  if (status != VMK_OK) {
	 vmk_LogMessage("vmk_LogicalCreateBusAddress failed, status %s", vmk_StatusToString(status));
  }

  vmk_LogMessage("Bus address is %s\n " ,deviceID.busAddress);

  deviceID.busIdentifier = VMK_UPLINK_DEVICE_IDENTIFIER;
  deviceID.busIdentifierLen = sizeof(VMK_UPLINK_DEVICE_IDENTIFIER) - 1;
  deviceProps.registeringDriver = sfvmk_ModInfo.driverID;
  deviceProps.deviceID = &deviceID;
  deviceProps.deviceOps = &sfvmk_UplinkDevOps;
  deviceProps.registeringDriverData.ptr = adapter;
  deviceProps.registrationData.ptr = &adapter->regData;

  status = vmk_DeviceRegister(&deviceProps,
                             sfvmk_Device, &adapter->uplinkDevice);

  if (status != VMK_OK) {
    vmk_LogMessage("Failed to register device: %s", vmk_StatusToString(status));
    status = VMK_FAILURE;
  }

  return VMK_OK;
}

/*
 ***********************************************************************
 * sfvmk_DeviceDetach --                                            */ /**
 *
 * Callback routine for the device layer to notify the driver to release
 * control of a device.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_DeviceDetach(vmk_Device dev)
{
  vmk_LogMessage("DetachDevice is invoked!");
  return VMK_OK;
   sfvmk_adapter *adapter;
   vmk_DeviceGetAttachedDriverData(dev, (vmk_AddrCookie *)&adapter);

   //Nvmxnet3DestroyAdapterDMAMapping(adapter);
   sfvmk_DestroyDMAEngine(adapter);
   sfvmk_DestroyLock(adapter->enp_lock.lock);
   sfvmk_unmapBars(adapter);
   sfvmk_FreeInterrupts(adapter);
   sfvmk_memPoolFree(adapter, sizeof(*adapter));
   return VMK_OK;

  return VMK_OK;
}

/*
 ***********************************************************************
 * sfvmk_DeviceShutdown --                                          */ /**
 *
 * Callback routine for the device layer to notify the driver to
 * shutdown the specified device.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */
static VMK_ReturnStatus
sfvmk_DeviceShutdown(vmk_Device dev)
{
  vmk_LogMessage("QuiesceDevice/drv_DeviceShutdown is invoked!");
  return VMK_OK;
}

/*
 ***********************************************************************
 * sfvmk_DeviceForget --                                            */ /**
 *
 * Callback routine for the device layer to notify the driver the
 * specified device is not responsive.
 *
 * Return values:
 *  None
 *
 ***********************************************************************
 */

static void
sfvmk_DeviceForget(vmk_Device dev)
{
  vmk_LogMessage("ForgetDevice is invoked!");
  return ;
}


/*
 ***********************************************************************
 * sfvmk_DriverRegister --      */ /**
 *
 * Register this driver as network driver
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */
VMK_ReturnStatus
sfvmk_DriverRegister()
{
  VMK_ReturnStatus status;
  vmk_DriverProps sfvmk_DriverProps;

  /* Populate sfcDriverProps */
  sfvmk_DriverProps.moduleID = vmk_ModuleCurrentID;
  vmk_NameCopy(&sfvmk_DriverProps.name, &sfvmk_ModInfo.driverName);
  sfvmk_DriverProps.ops = &sfvmk_DriverOps;
  sfvmk_DriverProps.privateData = (vmk_AddrCookie)NULL;


  /* Register Driver with with device layer */
  status = vmk_DriverRegister(&sfvmk_DriverProps, &sfvmk_ModInfo.driverID);

  if (status == VMK_OK) {
    vmk_LogMessage("Initialization of SFC  driver successful");
  } else {
    vmk_LogMessage("Initialization of SFC driver failed:");
  }

  return status;
}

/*
 ***********************************************************************
 * SFC_NATIVE_DriverUnregister --    */ /**
 *
 * Function to unregister the device that was previous registered.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */
void
sfvmk_DriverUnregister()
{
  vmk_DriverUnregister(sfvmk_ModInfo.driverID);
}

/*
 ***********************************************************************
 * sfvmk_removeUplinkDevice --    */ /**
 *
 * Function to unregister the device that was previous registered.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */
VMK_ReturnStatus
sfvmk_removeUplinkDevice(vmk_Device sfvmk_device)
{
  VMK_ReturnStatus status;

  vmk_LogMessage("sfvmk_removeUplinkDevice: device=%p", sfvmk_device);

  status = vmk_DeviceUnregister(sfvmk_device);

  return status;
}

