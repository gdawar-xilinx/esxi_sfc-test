/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/


#include "sfvmk_driver.h"
#include "sfvmk_ev.h"

/**-----------------------------------------------------------------------------
 *
 * sfvmk_CreateLock --
 *
 * @brief It creates a spin lock with specified name and lock rank.
 *
 * @param[in]  lockName  brief name for the spinlock
 * @param[in]  rank      lock rank)
 * @param[out] lock      lock pointer to create
 *
 * @result: VMK_OK on success, and lock created. Error code if otherwise.
 *
 *-----------------------------------------------------------------------------*/

VMK_ReturnStatus
sfvmk_CreateLock(const char *lockName,
                       vmk_LockRank rank,
                       vmk_Lock *lock)
{
   vmk_SpinlockCreateProps lockProps;
   VMK_ReturnStatus status;
   lockProps.moduleID = vmk_ModuleCurrentID;
   lockProps.heapID = sfvmk_ModInfo.heapID;

   if((lock == NULL) || (lockName == NULL))
     return VMK_BAD_PARAM;

   vmk_NameFormat(&lockProps.name, "sfvmk-%s", lockName);

   lockProps.type = VMK_SPINLOCK;
   lockProps.domain = sfvmk_ModInfo.lockDomain;
   lockProps.rank = rank;
   status = vmk_SpinlockCreate(&lockProps, lock);
   if (status != VMK_OK) {
      vmk_LogMessage("Failed to create spinlock (%x)", status);
   }
   return status;
}

/**-----------------------------------------------------------------------------
 *
 * sfvmk_DestroyLock --
 *
 * @brief It destroys the spin lock.
 *
 * @param[in,out] lock  lock pointer to create
 *
 * @result  None
 *
 *-----------------------------------------------------------------------------*/

void
sfvmk_DestroyLock(vmk_Lock lock)
{
  if (lock) {
    vmk_SpinlockDestroy(lock);
  }
}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_CreateLock --
 *
 * @brief It creates a spin lock with specified name and lock rank.
 *
 * @param[in]  lockName  brief name for the spinlock
 * @param[in]  rank      lock rank)
 * @param[out] lock      lock pointer to create
 *
 * @result: VMK_OK on success, and lock created. Error code if otherwise.
 *
 *-----------------------------------------------------------------------------*/

void *
sfvmk_MemPoolAlloc(vmk_uint64 size)
{
	VMK_ReturnStatus status;
	vmk_VA va;
	vmk_MemPoolAllocRequest request;
	vmk_MemPoolAllocProps props;
	vmk_MapRequest map_req;
	vmk_MpnRange mpn_range;

	size = VMK_UTIL_ROUNDUP(size, VMK_PAGE_SIZE);

	/* First convert bytes to pages and allocate from the memory pool */
	props.physContiguity = VMK_MEM_PHYS_CONTIGUOUS;
	props.physRange = VMK_PHYS_ADDR_ANY;
	props.creationTimeoutMS = 10;

	request.numPages = size >> VMK_PAGE_SHIFT;
	request.numElements = 1;
	request.mpnRanges = &mpn_range;


	status = vmk_MemPoolAlloc(sfvmk_ModInfo.memPoolID, &props, &request);
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


	return (void *)va;

mapping_err:
	vmk_MemPoolFree(&request);
allocation_err:
	return NULL;
}

/**-----------------------------------------------------------------------------
 *
 * sfvmk_CreateLock --
 *
 * @brief It creates a spin lock with specified name and lock rank.
 *
 * @param[in]  lockName  brief name for the spinlock
 * @param[in]  rank      lock rank)
 * @param[out] lock      lock pointer to create
 *
 * @result: VMK_OK on success, and lock created. Error code if otherwise.
 *
 *-----------------------------------------------------------------------------*/
void
sfvmk_MemPoolFree(void *va, vmk_uint64 size)
{
	VMK_ReturnStatus        status;
	vmk_MA                  ma;
	vmk_MpnRange            mpn_range;
	vmk_MemPoolAllocRequest alloc_request;

	/*
	* First try getting the MA from VA. This should not fail, but if
	* it does, rest of the operations should be skipped.
	*/
	status = vmk_VA2MA((vmk_VA)va, size, &ma);
	if (status != VMK_OK) {
		SFVMK_ERROR("vmk_VA2MA failed: %s", vmk_StatusToString(status));
		VMK_ASSERT(0);
	}

	/* Unmap */
	vmk_Unmap((vmk_VA)va);

	/* Free pages */
	mpn_range.startMPN = vmk_MA2MPN(ma);
	mpn_range.numPages = (size/VMK_PAGE_SIZE);

	alloc_request.numPages    = mpn_range.numPages;
	alloc_request.numElements = 1;
	alloc_request.mpnRanges   = &mpn_range;

	status = vmk_MemPoolFree(&alloc_request);
	if (status != VMK_OK) {
		SFVMK_ERROR("vmk_MemPoolFree failed: %s",
		vmk_StatusToString(status));
	}

}
static VMK_ReturnStatus
sfvmk_DMAMapMA(sfvmk_adapter *adapter,    // IN: adapter
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
  status = vmk_DMAMapElem(engine, direction,&in, VMK_TRUE, &out, &err);

  if (VMK_UNLIKELY(status != VMK_OK)) {
    if (status == VMK_DMA_MAPPING_FAILED) {
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




static VMK_ReturnStatus
sfvmk_DMAMapVA(sfvmk_adapter *adapter,        // IN: adapter
                      void *va,                     // IN: virtual address
                      vmk_uint32 size,              // IN: size
                      vmk_DMADirection direction,   // IN: DMA mapping direction
                      vmk_DMAEngine engine,         // IN: DMA engine
                      vmk_IOA *ioa)                 // OUT: starting IO address
{
  vmk_MA ma;
  vmk_VA2MA((vmk_VA)va, size, &ma);
  return sfvmk_DMAMapMA(adapter, ma, size, direction, engine, ioa);
}


void *
sfvmk_AllocCoherentDMAMapping(sfvmk_adapter *adapter, vmk_uint32 size,          
                              vmk_IOA *ioAddr)          
{
  VMK_ReturnStatus status;
  void *va;

  *ioAddr = 0;
  va = sfvmk_MemPoolAlloc(size);
  if (va == NULL) {
    goto sfvmk_mempool_alloc_fail;
  }
  status = sfvmk_DMAMapVA(adapter, va, size,
                          VMK_DMA_DIRECTION_BIDIRECTIONAL,
                          adapter->vmkDmaEngine, ioAddr);
  if (VMK_UNLIKELY(status != VMK_OK)) {
    goto sfvmk_dma_map_fail;
  }

  return va;

sfvmk_dma_map_fail:
  sfvmk_MemPoolFree(va, size);
sfvmk_mempool_alloc_fail:
  return NULL;
}



static void
sfvmk_DMAUnmap(sfvmk_adapter *adapter,            // IN: adapter
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
    SFVMK_DBG_ONLY_THROTTLED(adapter, SFVMK_DBG_UTILS, 2,
                            "Failed to unmap range [0x%lx, 0x%lx] (%x)",
                            elem.ioAddr, elem.ioAddr + elem.length - 1,
                            status);
  }
}


void
sfvmk_FreeCoherentDMAMapping(sfvmk_adapter *adapter,                    // IN: adapter
                                            void *va,                   // IN: virtual address
                                            vmk_IOA ioAddr,             // IN: IO address
                                            vmk_uint32 size)            // IN: size
{
  sfvmk_DMAUnmap(adapter, ioAddr, size,
                  VMK_DMA_DIRECTION_BIDIRECTIONAL,
                  adapter->vmkDmaEngine);
  sfvmk_MemPoolFree(va, size);
}


VMK_ReturnStatus sfvmk_MutexInit(const char *lckName,vmk_LockRank rank,vmk_Mutex *mutex)           // OUT: created lock
{
   vmk_MutexCreateProps lockProps;
   VMK_ReturnStatus status;
   lockProps.moduleID = vmk_ModuleCurrentID;
   lockProps.heapID = sfvmk_ModInfo.heapID;
   vmk_NameFormat(&lockProps.className, "sfvmk-%s", lckName);

   lockProps.type = VMK_MUTEX;
   lockProps.domain = sfvmk_ModInfo.lockDomain;
   lockProps.rank = rank;
   status = vmk_MutexCreate(&lockProps ,mutex);
   if (VMK_UNLIKELY(status != VMK_OK)) {
      vmk_LogMessage("Failed to create mutex (%x)", status);
   }

   return status ;

}

void sfvmk_MutexDestroy(vmk_Mutex mutex) 
{
  if(mutex)
    vmk_MutexDestroy(mutex);  
}
VMK_ReturnStatus
sfvmk_INTxEnable(sfvmk_adapter *devData)
{
   vmk_uint32 numIntrsAlloced;
   VMK_ReturnStatus status;

   status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                   devData->pciDevice,
                                   VMK_PCI_INTERRUPT_TYPE_LEGACY,
                                   1,
                                   1,
                                   NULL,
                                   devData->intr.intrCookies, &numIntrsAlloced);
   if (status == VMK_OK) {
      SFVMK_DBG(devData, SFVMK_DBG_DRIVER, 2,
                     "Alloced %d INT-x vectors for device", numIntrsAlloced);
      devData->evq[0]->vector = devData->intr.intrCookies[0];

   } else {
      devData->intr.intrCookies[0] = VMK_INVALID_INTRCOOKIE;
      SFVMK_ERR(devData, "Failed to allocate 1 INT-x vector");
   }
   return (status);
}


void
sfvmk_IntrCleanup(sfvmk_adapter * devData)
{
   SFVMK_DBG(devData, SFVMK_DBG_DRIVER, 2, "Freeing Intr cookies");
   if (devData->intr.intrCookies[0] != VMK_INVALID_INTRCOOKIE) {
      vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, devData->pciDevice);
      //sfvmk_initializeInterrupts(devData);
   }
}
static VMK_ReturnStatus
sfvmk_EnableIntrs(sfvmk_adapter * devData)
{
   VMK_ReturnStatus status = VMK_OK;;
   vmk_int32 i;
   vmk_uint16 numQs = 1;//devData->intr.numIntrAlloc;


   SFVMK_DBG(devData, SFVMK_DBG_UPLINK, 2,
                  "sfvmk_EnableIntrs entered numQs=%d", numQs);
   for (i = 0; i < numQs; i++) {
      if (devData->intr.intrCookies[i] != VMK_INVALID_INTRCOOKIE) {
         SFVMK_DBG(devData, SFVMK_DBG_UPLINK, 0,
                        "*** intrCookies[%d]= 0x%lx ***",
                        i, (vmk_uint64)devData->intr.intrCookies[i]);
         status = vmk_IntrEnable(devData->intr.intrCookies[i]);
         if (status != VMK_OK) {
            SFVMK_ERR(devData, "Interrupt Enable failed for vect %d", i);
            for (i--; i >= 0; i--) {
               vmk_IntrDisable(devData->intr.intrCookies[i]);
            }
            break;
         } else {
            SFVMK_DBG(devData, SFVMK_DBG_UPLINK, 0,
                           "Enabled interrupt i=%d", i);
         }
      }
   }
   return (status);
}




VMK_ReturnStatus
sfvmk_RegisterInterrupts(sfvmk_adapter * devData , vmk_IntrHandler handler , vmk_IntrAcknowledge ack)
{
   vmk_IntrProps intrProps;
   vmk_int16 index;
   VMK_ReturnStatus status;

   intrProps.device = devData->vmkDevice;
   intrProps.attrs = VMK_INTR_ATTRS_ENTROPY_SOURCE;
   intrProps.handler =  handler;
   intrProps.acknowledgeInterrupt = ack;

   for (index = 0; index < 2;/*devData->intr.numIntrAlloc;*/ index++) {
      vmk_NameFormat(&intrProps.deviceName,
                     "%s-rxq%d", devData->pciDeviceName.string, index);
      intrProps.handlerData = devData->evq[index];
      status = vmk_IntrRegister(vmk_ModuleCurrentID,
                                devData->intr.intrCookies[index], &intrProps);
      if (status != VMK_OK) {
         SFVMK_ERR(devData, "Failed to register MSIx Interrupt, "
                        "status: 0x%x, vect %d", status, index);
         goto sfvmk_err_intr_reg;
      }

      /*
       * Set the associated interrupt cookie with the poll.
       * Vmkernel can have an interrupt cookie be associated with the network
       * poll so that the interrupt handler can be affinitized with the poll
       * routine.
       */

      status = vmk_NetPollInterruptSet((devData->evq[index])->netPoll,
                                       devData->evq[index]->vector);

      if (status != VMK_OK) {
         SFVMK_ERR(devData, "Failed to set netpoll vector %s",
                        vmk_StatusToString(status));
         vmk_IntrUnregister(vmk_ModuleCurrentID,
                            devData->intr.intrCookies[index], devData->evq[index]);
         goto sfvmk_err_intr_reg;
      }
      SFVMK_DBG(devData, SFVMK_DBG_DRIVER, 3,
                     "netPoll for i=%d,vector=%d",
                     index, devData->evq[index]->vector);
   	}

   SFVMK_DBG(devData, SFVMK_DBG_DRIVER, 0, "Registered %d vectors", index);
   return (VMK_OK);

sfvmk_err_intr_reg:
   for (index--; index >= 0; index--) {
     vmk_NetPollInterruptUnSet(devData->evq[index]->netPoll);
     vmk_IntrUnregister(vmk_ModuleCurrentID, devData->intr.intrCookies[index],  devData->evq[index]);
  }
   return (status);
}






VMK_ReturnStatus
sfvmk_SetupInterrupts(sfvmk_adapter *adapter, vmk_IntrHandler handler , vmk_IntrAcknowledge ack)
{
   VMK_ReturnStatus status;

   if (adapter->intr.type == EFX_INTR_MESSAGE) {
      status = sfvmk_RegisterInterrupts(adapter, handler , ack);
      if (status == VMK_OK) {
         sfvmk_EnableIntrs(adapter);
         return (VMK_OK);
      }
   #if 0 
      else {

         sfvmk_IntrCleanup(adapter);

         /* MSI-X failed to register, try INT-X */
         adapter->intr.type  = EFX_INTR_LINE;
         status = sfvmk_INTxEnable(adapter);
         if (status != VMK_OK) {
            return status;
         }
         status = sfvmk_RegisterInterrupts(adapter, handler , ack);
	 if (status == VMK_OK) {
          return (VMK_OK);

      }
   }
   #endif 
  }
   return VMK_FAILURE;
}

VMK_ReturnStatus
sfvmk_IntrStop(sfvmk_adapter * devData)
{
   vmk_int16 i;
   VMK_ReturnStatus status = VMK_OK;


   for (i =0 ; i<1; i++)
   	{
      if (devData->intr.intrCookies[i] != VMK_INVALID_INTRCOOKIE) {
              vmk_IntrSync(devData->intr.intrCookies[i]);
             vmk_IntrDisable(devData->intr.intrCookies[i]);
	          vmk_NetPollInterruptUnSet(devData->evq[i]->netPoll);
	     
        status = vmk_IntrUnregister(vmk_ModuleCurrentID,
                                     devData->intr.intrCookies[i], devData->evq[i]);
         if (status != VMK_OK) {
            SFVMK_ERR(devData, "Failed to unregister interrupt, status: "
                           "0x%x for vect %d", status, i);
         }
       }
      }

    return status; 
   }






