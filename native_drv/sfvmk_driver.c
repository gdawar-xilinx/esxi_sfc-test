/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"
#include "sfvmk_utils.h"
#include "sfvmk_ev.h"
#include "sfvmk_rx.h"
#include "sfvmk_tx.h"
#include "sfvmk_uplink.h"



static int sfvmk_tx_ring_entries = SFVMK_NDESCS;
static int sfvmk_rx_ring_entries = SFVMK_NDESCS;


static VMK_ReturnStatus sfvmk_AttachDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_DetachDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_ScanDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_ShutdownDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_StartDevice(vmk_Device device);
static void sfvmk_ForgetDevice(vmk_Device device);





static VMK_ReturnStatus sfvmk_VerifyDevice(sfvmk_adapter *adapter);


#ifdef SFVMK_WITH_UNIT_TESTS
extern void sfvmk_run_ut();
#endif
/************************************************************************
 * sfvmk_VerifyDevice --
 *
 * @brief  Routine to check adapter's family, raise the error if proper family
 * is not found.
 *
 * @param  adapter pointer to sfvmk_adapter
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_VerifyDevice(sfvmk_adapter *adapter)
{
	int rc;

	/*check adapter's family */
	rc = efx_family(adapter->pciDeviceID.vendorID,
									adapter->pciDeviceID.deviceID, &adapter->efxFamily);
	if (rc != 0) {
		SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 0, "efx_family fail %d", rc);
		return VMK_FAILURE;
	}
	/* driver support only Medford Family */
	if (adapter->efxFamily == EFX_FAMILY_MEDFORD) {
		//praveen needs to check counter part of this in vmkernel
		//device_set_desc(dev, "Solarflare SFC9200 family");
		return VMK_OK;
	}

	SFVMK_ERROR("impossible controller family %d", adapter->efxFamily);
	return VMK_FAILURE;
}

/************************************************************************
 * sfvmk_VerifyDevice --
 *
 * @brief  Routine to check adapter's family, raise the error if proper family
 * is not found.
 *
 * @param  adapter pointer to sfvmk_adapter
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_CreateDMAEngine(sfvmk_adapter *adapter)
{
	VMK_ReturnStatus status;
	vmk_DMAConstraints dmaConstraints;
	vmk_DMAEngineProps dmaProps;

	vmk_Memset(&dmaConstraints, 0, sizeof(dmaConstraints));
							dmaConstraints.addressMask = VMK_ADDRESS_MASK_64BIT;

	vmk_Memset(&dmaProps, 0, sizeof(dmaProps));
	vmk_NameCopy(&dmaProps.name, &sfvmk_ModInfo.driverName);

	dmaProps.module = vmk_ModuleCurrentID;
	dmaProps.device = adapter->vmkDevice;
	dmaProps.constraints = &dmaConstraints;
	dmaProps.flags = VMK_DMA_ENGINE_FLAGS_COHERENT;

	status = vmk_DMAEngineCreate(&dmaProps, &adapter->vmkDmaEngine);
	if (status == VMK_OK) {
	} else {
		dmaConstraints.addressMask = VMK_ADDRESS_MASK_32BIT;
		status = vmk_DMAEngineCreate(&dmaProps, &adapter->vmkDmaEngine);
	}

	return (status);
}
/************************************************************************
 * sfvmk_VerifyDevice --
 *
 * @brief  Routine to check adapter's family, raise the error if proper family
 * is not found.
 *
 * @param  adapter pointer to sfvmk_adapter
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_MapBAR(sfvmk_adapter *sfAdapter)
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
	}
       
	return (status);
}
/************************************************************************
 * sfvmk_VerifyDevice --
 *
 * @brief  Routine to check adapter's family, raise the error if proper family
 * is not found.
 *
 * @param  adapter pointer to sfvmk_adapter
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_UnmapBAR(sfvmk_adapter *adapter)
{
	VMK_ReturnStatus status;

        sfvmk_DestroyLock(adapter->bar.esb_lock);
	if (adapter->bar.esb_base) {
		status = vmk_PCIUnmapIOResource(vmk_ModuleCurrentID, adapter->pciDevice,
																		EFX_MEM_BAR);
		VMK_ASSERT(status == VMK_OK);
		SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 2, "Freed Bar %d ", EFX_MEM_BAR);
	}
	return (VMK_OK);
}
static void
sfvmk_DestroyDMAEngine(sfvmk_adapter *adapter)
{
	vmk_DMAEngineDestroy(adapter->vmkDmaEngine);
}

static VMK_ReturnStatus
sfvmk_SetBusMaster(sfvmk_adapter *adapter)
{
	vmk_uint32 cmd;
	VMK_ReturnStatus status = VMK_OK;

	status = vmk_PCIReadConfig(vmk_ModuleCurrentID, adapter->pciDevice,
	                          VMK_PCI_CONFIG_ACCESS_16,
	                          SFVMK_PCI_COMMAND, &cmd);
	if (status != VMK_OK) {
	  SFVMK_ERROR("failed to read PCI config : %u (%x)",
	               SFVMK_PCI_COMMAND, status);
	  goto sfvmk_pci_err;
	}

	if (!(cmd & SFVMK_PCI_COMMAND_BUS_MASTER)) {
	  SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 0, "enabling bus mastering");
	  cmd |= SFVMK_PCI_COMMAND_BUS_MASTER;
	  status = vmk_PCIWriteConfig(vmk_ModuleCurrentID, adapter->pciDevice,
	                              VMK_PCI_CONFIG_ACCESS_16,
	                              SFVMK_PCI_COMMAND, cmd);
	  if (status != VMK_OK) {
	     SFVMK_ERROR("Failed to write PCI config. %u, data: %u (%x)",
	                  SFVMK_PCI_COMMAND, cmd, status);
	     goto sfvmk_pci_err;
	  }
	}

	return VMK_OK;

sfvmk_pci_err:
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


   evq_max = MIN(limits.edl_max_rxq_count ,  
	 	             (limits.edl_max_txq_count - SFVMK_TXQ_NTYPES +1));
  // tuning parameter
	if (adapter->max_rss_channels > 0)
		evq_max = MIN(evq_max, adapter->max_rss_channels);
	vmk_LogMessage(" event queue max = %d \n ", adapter->evq_max);

	limits.edl_min_evq_count = 1;
	limits.edl_max_evq_count = evq_max;
	limits.edl_min_txq_count = SFVMK_TXQ_NTYPES;
	limits.edl_min_rxq_count = 1;
	limits.edl_max_rxq_count = evq_max;

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
	vmk_LogMessage(" event queue max = %d \n ", adapter->evq_max);
	adapter->evq_max = MIN(rxq_allocated, adapter->evq_max);
	vmk_LogMessage(" event queue max = %d \n ", adapter->evq_max);
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

VMK_ReturnStatus
sfvmk_IntrInit(sfvmk_adapter *adapter)
{
   unsigned int numIntReq, numIntrsAlloc;
   unsigned int index =0;
   VMK_ReturnStatus status;

   numIntReq = adapter->evq_max;

   status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                   adapter->pciDevice,
                                   VMK_PCI_INTERRUPT_TYPE_MSIX,
                                   numIntReq,
                                   numIntReq, NULL,
                                   adapter->intr.intrCookies, &numIntrsAlloc);
   if (status == VMK_OK) {
      SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 0,
                     "Alloced %d vectors for device", numIntrsAlloc);
      adapter->intr.numIntrAlloc = numIntrsAlloc;
			adapter->intr.type = EFX_INTR_MESSAGE;
   } else {
      for (index = 0; index < numIntReq; index++) {
         adapter->intr.intrCookies[index] = VMK_INVALID_INTRCOOKIE;
      }
      SFVMK_ERR(adapter,
                     "PCIAllocIntrCookie failed with error 0x%x "
                     "for %d vectors", status, numIntReq);

      /*
       * Try single msix vector before giving up to try legacy..
       */
      status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                      adapter->pciDevice,
                                      VMK_PCI_INTERRUPT_TYPE_MSIX,
                                      1,
                                      1, NULL,
                                     adapter->intr.intrCookies, &numIntrsAlloc);
      if (status != VMK_OK) {
         SFVMK_ERR(adapter,
                        "PCIAllocIntrCookie failed for 1 MSIx, using Legacy");
         adapter->intr.intrCookies[0] = VMK_INVALID_INTRCOOKIE;
      } else {
         SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 2,
                        "Alloced 1 MSIx vector for device");
         adapter->intr.numIntrAlloc = 1;
				 adapter->intr.type = EFX_INTR_MESSAGE;
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
 * @param  dev	pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_AttachDevice(vmk_Device dev)
{

	vmk_LogMessage("AttachDevice is called!");
       	efx_nic_t *enp;
        efsys_lock_t *enp_lock;
	VMK_ReturnStatus status = VMK_OK;
	sfvmk_adapter *adapter = NULL;
	vmk_AddrCookie pciDeviceCookie;
        unsigned int error; 


	/* allocation memory for adapter */
	adapter = sfvmk_MemPoolAlloc(sizeof(sfvmk_adapter));
	if (adapter == NULL) {
		status = VMK_FAILURE;
		goto sfvmk_adapter_alloc_fail;
	}

	adapter->max_rss_channels =0;
	adapter->vmkDevice = dev;
	adapter->debugMask = -1;
	adapter->test = 0;

	/*Initializing ring enteries */
	/* these are tunable parameter */
	adapter->rxq_entries = sfvmk_rx_ring_entries;
	adapter->txq_entries = sfvmk_tx_ring_entries;

	SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 2,	"*** allocated devData=%p, "
						"requested size=%lu \n",(void *)adapter, sizeof(sfvmk_adapter));

  /* get pci device */
	status = vmk_DeviceGetRegistrationData(dev, &pciDeviceCookie);
	if (status != VMK_OK) {
		SFVMK_ERROR("Get device from kernel failed status: 0x%x", status);
		goto sfvmk_kernel_dev_fail;
	}

	adapter->pciDevice = pciDeviceCookie.ptr;

	/* query PCI device information */
	status = vmk_PCIQueryDeviceID(adapter->pciDevice, &adapter->pciDeviceID);
	if (status != VMK_OK) {
		SFVMK_ERROR("Get deviceID from kernel device failed status: 0x%x",status);
		goto sfvmk_kernel_dev_fail;
	}

	status = sfvmk_VerifyDevice(adapter);
	if (status != VMK_OK) {
		goto sfvmk_kernel_dev_fail;
	}

	SFVMK_DBG(adapter,SFVMK_DBG_DRIVER, 2,"Found kernel device with following "
						"Dev Id:  %x\n Vend ID:  %x\n Sub Dev ID: %x\n Sub Vend ID: %x\n ",
						adapter->pciDeviceID.deviceID, adapter->pciDeviceID.vendorID,
						adapter->pciDeviceID.subDeviceID, adapter->pciDeviceID.subVendorID);


	status = vmk_PCIQueryDeviceAddr(adapter->pciDevice,&adapter->pciDeviceAddr);
	if (status != VMK_OK) {
		SFVMK_ERROR("Get deviceAddr for device failed, status 0x%x",status);
		goto sfvmk_kernel_dev_fail;
	}

	vmk_StringFormat(adapter->pciDeviceName.string,
										sizeof(adapter->pciDeviceName.string),
										NULL,	SFVMK_SBDF_FMT,	adapter->pciDeviceAddr.seg,
										adapter->pciDeviceAddr.bus, adapter->pciDeviceAddr.dev,
										adapter->pciDeviceAddr.fn);


	SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 2, "Found kernel device");

	status = sfvmk_CreateDMAEngine(adapter);
	if (status != VMK_OK) {
		SFVMK_ERR(adapter, "DMA Engine create failed");
		goto sfvmk_DMA_eng_create_fail;
	}
	status = sfvmk_MapBAR(adapter);
	if (status != VMK_OK) {
		goto sfvmk_map_bar_fail;
	}
        
        status = sfvmk_SetBusMaster(adapter);
	if (status != VMK_OK) 
		goto sfvmk_bus_master_fail;
        
        enp_lock = &adapter->enp_lock;
        sfvmk_CreateLock("enp", VMK_SPINLOCK_RANK_HIGHEST-1, &enp_lock->lock);
        if ((error = efx_nic_create(adapter->efxFamily, (efsys_identifier_t *)adapter,
			 &adapter->bar, &adapter->enp_lock, &enp)) != 0)
	{
	    SFVMK_ERR(adapter, "failed in creating nic");
		goto sfvmk_nic_create_fail;
	}


	// store nic pointer
	adapter->enp = enp;
	/* Initialize MCDI to talk to the microcontroller. */
        SFVMK_DBG(adapter,SFVMK_DBG_DRIVER, 2, "mcdi_init...");
	if ((error = sfvmk_mcdi_init(adapter)) != 0)
		goto sfvmk_mcdi_init_fail;

	/* Probe the NIC and build the configuration data area. */
        SFVMK_DBG(adapter,SFVMK_DBG_DRIVER, 2, "nic_probe...");
	if ((error = efx_nic_probe(enp)) != 0)
		goto sfvmk_nic_probe_fail;

	/* Initialize the NVRAM. */
        SFVMK_DBG(adapter,SFVMK_DBG_DRIVER, 2, "nvram_init...");
	if ((error = efx_nvram_init(enp)) != 0)
		goto sfvmk_nvram_init_fail;

	/* Initialize the VPD. */
        SFVMK_DBG(adapter,SFVMK_DBG_DRIVER, 2, "vpd_init...");
	if ((error = efx_vpd_init(enp)) != 0)
		goto sfvmk_vpd_init_fail;

        SFVMK_DBG(adapter,SFVMK_DBG_DRIVER, 2, "mcdi_new_epoch...");
	efx_mcdi_new_epoch(enp);
	/* Reset the NIC. */
        SFVMK_DBG(adapter,SFVMK_DBG_DRIVER, 2, "nic_reset...");
	if ((error = efx_nic_reset(enp)) != 0)
		goto sfvmk_nic_reset_fail;


        sfvmk_estimate_rsrc_limits(adapter);

      	status = sfvmk_IntrInit(adapter);
	if (status != VMK_OK) 
		goto sfvmk_intr_init_fail;
       	status = sfvmk_EvInit(adapter);
	if (status != VMK_OK) 
		goto sfvmk_ev_init_fail;


       	status = sfvmk_RxInit(adapter);
	if (status != VMK_OK) 
		goto sfvmk_rx_init_fail;

       	status = sfvmk_TxInit(adapter);
	if (status != VMK_OK) 
		goto sfvmk_tx_init_fail;

       	status = sfvmk_CreateUplinkData(adapter);
	if (status != VMK_OK) 
		goto sfvmk_creat_uplink_data_fail;

	status = vmk_DeviceSetAttachedDriverData(dev, adapter);
	if (status != VMK_OK) {
		goto sfvmk_set_drvdata_fail;
	}
 
#ifdef SFVMK_WITH_UNIT_TESTS
  sfvmk_run_ut();
#endif
       vmk_LogMessage("leaving AttachDevice invoked!");

	return VMK_OK;

sfvmk_set_drvdata_fail:
 	sfvmk_DestroyUplinkData(adapter);
sfvmk_creat_uplink_data_fail:
	sfvmk_TxFini(adapter);
sfvmk_tx_init_fail:
	sfvmk_RxFini(adapter);
sfvmk_rx_init_fail:
	sfvmk_EvFini(adapter);
sfvmk_ev_init_fail:
	sfvmk_FreeInterrupts(adapter);
sfvmk_intr_init_fail:
	efx_nic_fini(enp);
sfvmk_nic_reset_fail:
	efx_vpd_fini(enp);
sfvmk_vpd_init_fail:
	efx_nvram_fini(enp);
sfvmk_nvram_init_fail:
	efx_nic_unprobe(enp);
sfvmk_nic_probe_fail:
	sfvmk_mcdi_fini(adapter);
sfvmk_mcdi_init_fail:
        adapter->enp = NULL; 
	efx_nic_destroy(enp);
        sfvmk_DestroyLock(enp_lock->lock);
sfvmk_nic_create_fail:
sfvmk_bus_master_fail:
sfvmk_map_bar_fail:
	sfvmk_DestroyDMAEngine(adapter);

sfvmk_DMA_eng_create_fail:
sfvmk_kernel_dev_fail:
	sfvmk_MemPoolFree(adapter, sizeof(sfvmk_adapter));
sfvmk_adapter_alloc_fail:

	return status;
}

/************************************************************************
 * sfvmk_StartDevice --
 *
 * @brief: Callback routine for the device layer to notify the driver to
 * bring up the specified device.
 *
 * @param  dev	pointer to vmkDevice
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


VMK_ReturnStatus
sfvmk_removeUplinkDevice(vmk_Device sfvmk_device)
{
  VMK_ReturnStatus status;

  vmk_LogMessage("sfvmk_removeUplinkDevice: device=%p", sfvmk_device);

  status = vmk_DeviceUnregister(sfvmk_device);

  return status;
}

static vmk_DeviceOps sfvmk_UplinkDevOps = {
   .removeDevice = sfvmk_removeUplinkDevice
};


/************************************************************************
 * sfvmk_ScanDevice --
 *
 * Callback routine for the device layer to notify the driver to scan
 * for new devices.
 *
 * @param  dev	pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_ScanDevice(vmk_Device device)
{
  vmk_LogMessage("ScanDevice is invoked!");

VMK_ReturnStatus status = 0;
  sfvmk_adapter *adapter;
  vmk_Name busName;
  vmk_DeviceID *deviceID;
  vmk_DeviceProps deviceProps;
  
  status = vmk_DeviceGetAttachedDriverData(device, (vmk_AddrCookie *) &adapter);

  deviceID = &adapter->deviceID;

  VMK_ASSERT(status == VMK_OK);
  VMK_ASSERT(adapter != NULL);


  status = vmk_NameInitialize(&busName, VMK_LOGICAL_BUS_NAME);
  VMK_ASSERT(status == VMK_OK);
  status = vmk_BusTypeFind(&busName, &deviceID->busType);
  VMK_ASSERT(status == VMK_OK);

  status = vmk_LogicalCreateBusAddress(sfvmk_ModInfo.driverID,
									   adapter->vmkDevice,
									   0,
									   &deviceID->busAddress,
									   &deviceID->busAddressLen);
  if (status != VMK_OK) {
	 vmk_LogMessage("vmk_LogicalCreateBusAddress failed, status %s", vmk_StatusToString(status));
  }

  deviceID->busIdentifier = VMK_UPLINK_DEVICE_IDENTIFIER;
  deviceID->busIdentifierLen = sizeof(VMK_UPLINK_DEVICE_IDENTIFIER) - 1;
   
  deviceProps.registeringDriver = sfvmk_ModInfo.driverID;
  deviceProps.deviceID = deviceID;
  deviceProps.deviceOps = &sfvmk_UplinkDevOps;
  deviceProps.registeringDriverData.ptr = adapter;
  deviceProps.registrationData.ptr = &adapter->regData;

  status = vmk_DeviceRegister(&deviceProps,
                             device, &adapter->uplinkDevice);

  if (status != VMK_OK) {
    vmk_LogMessage("Failed to register device: %s", vmk_StatusToString(status));
    status = VMK_FAILURE;
  }

  vmk_LogicalFreeBusAddress(sfvmk_ModInfo.driverID, deviceID->busAddress);
  vmk_BusTypeRelease(deviceID->busType);

 
  return VMK_OK;




}

/************************************************************************
 * sfvmk_DetachDevice --
 *
 * @brief : Callback routine for the device layer to notify the driver to
 * release control of a driver.
 *
 * @param  dev	pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/

static VMK_ReturnStatus
sfvmk_DetachDevice(vmk_Device dev)
{
  vmk_LogMessage("DetachDevice is invoked!");

  efx_nic_t *enp = NULL ; 

	sfvmk_adapter *adapter = NULL;
	vmk_DeviceGetAttachedDriverData(dev, (vmk_AddrCookie *)&adapter);
      
 if (NULL != adapter)
 {

 	sfvmk_DestroyUplinkData(adapter);
	sfvmk_TxFini(adapter);
	sfvmk_RxFini(adapter);
	sfvmk_EvFini(adapter);
	sfvmk_FreeInterrupts(adapter);
        /* Tear down common code subsystems. */

        enp = adapter->enp; 
	efx_nic_fini(enp);
	efx_vpd_fini(enp);
	efx_nvram_fini(enp);
	efx_nic_unprobe(enp);

	/* Tear down MCDI. */
	sfvmk_mcdi_fini(adapter);
	/* Destroy common code context. */
	adapter->enp = NULL;
	efx_nic_destroy(enp);
        sfvmk_DestroyLock(adapter->enp_lock.lock);

        /* vmk related resource deallocation */ 
	sfvmk_UnmapBAR(adapter);
	sfvmk_DestroyDMAEngine(adapter);
	sfvmk_MemPoolFree(adapter, sizeof(sfvmk_adapter));
  vmk_LogMessage("DetachDevice1 is invoked!");
 }
  return VMK_OK;
}

/************************************************************************
 * sfvmk_ShutdownDevice --
 *
 * @brief : Callback routine for the device layer to notify the driver to
 * shutdown the specified device.
 *
 * @param  dev	pointer to vmkDevice
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
 * @param  dev	pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static void
sfvmk_ForgetDevice(vmk_Device dev)
{

  vmk_LogMessage("ForgetDevice is invoked!");
#if 0 
   vmk_DeviceID *deviceID;
  sfvmk_adapter *adapter;
VMK_ReturnStatus status = 0;

 status = vmk_DeviceGetAttachedDriverData(dev, (vmk_AddrCookie *) &adapter);


  deviceID = &adapter->deviceID;

   vmk_LogicalFreeBusAddress(sfvmk_ModInfo.driverID, deviceID->busAddress);
  vmk_BusTypeRelease(deviceID->busType);
#endif 
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

