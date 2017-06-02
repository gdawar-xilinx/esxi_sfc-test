/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_util.h"
#include "sfvmk_ev.h"
#define false 0 
#define true 1 


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
    lock = NULL;
  }
}


/*
 ***********************************************************************
 *
 * sfvmk_memPoolAlloc
 *
 *      Allocates memory from the mempool.
 *      param [in]  size           size of memory to be allocated
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
sfvmk_memPoolAlloc(vmk_uint64 size)
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

  //SFVMK_DEBUG(SFVMK_DBG_DRIVER, 2,"allocated mem va=%p, allocated size=%lu",
  //            (void *)va, size);

  return (void *)va;

  mapping_err:
  vmk_MemPoolFree(&request);
  allocation_err:
  return NULL;
}
/*
 ***********************************************************************
 *
 * sfvmk_memPoolFree
 *
 *      Frees memory allocated from the mempool.
 *
 *      param [in] va         pointer to virtual address to be
 *                            freed/unmapped
 *      param [in] size       size of the memory to be freed/unmapped
 *
 * Results:
 *      NULL.
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

void
sfvmk_memPoolFree(void *va,               // IN
                            vmk_uint64 size)      // IN
{
  VMK_ReturnStatus        status;
  vmk_MA                  ma;
  vmk_MpnRange            mpn_range;
  vmk_MemPoolAllocRequest alloc_request;

  /*
  * First try getting the MA from VA. This should not fail, but if
  * it does, all the rest operations should be skipped.
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
/*
***********************************************************************
*
* sfvmk_DMAMapMA
*
*      Frees memory allocated from the mempool.
*
*      param [in] va         pointer to virtual address to be
*                            freed/unmapped
*      param [in] size       size of the memory to be freed/unmapped
*
* Results:
*      NULL.
*
* Side effects:
*      None
*
***********************************************************************
*/

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
/*
***********************************************************************
*
* sfvmk_DMAMapMA
*
*      Frees memory allocated from the mempool.
*
*      param [in] va         pointer to virtual address to be
*                            freed/unmapped
*      param [in] size       size of the memory to be freed/unmapped
*
* Results:
*      NULL.
*
* Side effects:
*      None
*
***********************************************************************
*/

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
sfvmk_AllocCoherentDMAMapping(sfvmk_adapter *adapter, // IN:  adapter
                                            vmk_uint32 size,          // IN:  size
                                            vmk_IOA *ioAddr)          // OUT: IO address
{
  VMK_ReturnStatus status;
  void *va;
//  vmk_uint64 alloc_size;

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
  //svmk_memPoolFree(va, size);
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
sfvmk_FreeCoherentDMAMapping(sfvmk_adapter *adapter,                    // IN: adapter
                                            void *va,                   // IN: virtual address
                                            vmk_IOA ioAddr,             // IN: IO address
                                            vmk_uint32 size)            // IN: size
{
  sfvmk_DMAUnmap(adapter, ioAddr, size,
                  VMK_DMA_DIRECTION_BIDIRECTIONAL,
                  adapter->vmkDmaEngine);
  sfvmk_memPoolFree(va, size);
}
VMK_ReturnStatus
sfvmk_intAck(void *clientData, vmk_IntrCookie intrCookie)
{

  return VMK_OK;
}



static void
sfvmk_intr_message(void *arg, vmk_IntrCookie intrCookie)
{

#if 1 
	sfvmk_evq *evq;
	sfvmk_adapter *adapter;
	efx_nic_t *enp;
	struct sfvmk_intr *intr;
	unsigned int index;
	boolean_t fatal;

	evq = (struct sfvmk_evq *)arg;
	adapter = evq->adapter;
	enp = adapter->enp;
	intr = &adapter->sfvmkIntrInfo;
	index = evq->index;

	//VMK_ASSERT(intr != NULL);
	//VMK_ASSERT(intr->type == EFX_INTR_MESSAGE);

        //vmk_LogMessage("got MSIX interuupt\n");

	if ((intr->state != SFVMK_INTR_STARTED))
		return;

	(void)efx_intr_status_message(enp, index, &fatal);

	if (fatal) {
		(void)efx_intr_disable(enp);
		(void)efx_intr_fatal(enp);
		return;
	}
        vmk_NetPollActivate(adapter->nicPoll[0].netPoll);
//	(void)sfvmk_ev_qpoll(evq);

   #endif 
}





/*
 *****************************************************************************
 *
 * sfvmk_registerMSIxInterrupts
 *
 *    Register the MSIx interrupt vectors with the kernel.
 *
 *      param[in] devData        pointer to elxnet device
 *
 * Results:
 *      retval: VMK_OK         Regsitration of  interrupts succeeded
 *      retval: VMK_FAILURE    Failed to free registered interrupts
 *
 * Side effects:
 *    None
 *
 *****************************************************************************
 */

VMK_ReturnStatus
sfvmk_registerInterrupts(sfvmk_adapter * devData)
{
   vmk_IntrProps intrProps;
   vmk_int16 index;
   struct sfvmk_rxObj *rxo;
   VMK_ReturnStatus status;

   intrProps.device = devData->vmkDevice;
   intrProps.attrs = VMK_INTR_ATTRS_ENTROPY_SOURCE;
   intrProps.handler = sfvmk_intr_message;
   intrProps.acknowledgeInterrupt = sfvmk_intAck;

   for (index = 0; index < 1/*devData->sfvmkIntrInfo.numIntrAlloc*/; index++) {
      vmk_NameFormat(&intrProps.deviceName,
                     "%s-rxq%d", devData->pciDeviceName.string, index);
      intrProps.handlerData = devData->evq[index];
      status = vmk_IntrRegister(vmk_ModuleCurrentID,
                                devData->sfvmkIntrInfo.intrCookies[index], &intrProps);
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
     #if 1 
      status = vmk_NetPollInterruptSet(devData->nicPoll[index].netPoll,
                                       devData->nicPoll[index].vector);

      if (status != VMK_OK) {
         SFVMK_ERR(devData, "Failed to set netpoll vector %s",
                        vmk_StatusToString(status));
         vmk_IntrUnregister(vmk_ModuleCurrentID,
                            devData->sfvmkIntrInfo.intrCookies[index], devData->evq[index]);
         devData->nicPoll[index].vector_set = false;
         goto sfvmk_err_intr_reg;
      }
      devData->nicPoll[index].vector_set = true;
      SFVMK_DBG(devData, SFVMK_DBG_DRIVER, 3,
                     "nicPoll for i=%d,vector=%d",
                     index, devData->nicPoll[index].vector);
     #endif 
   }

//   devData->isr_registered = true;
   SFVMK_DBG(devData, SFVMK_DBG_DRIVER, 0, "Registered %d vectors", index);
   return (VMK_OK);

sfvmk_err_intr_reg:
   for (index--; index >= 0; index--) {
      rxo = &devData->rx_obj[index];
      if (devData->nicPoll[index].vector_set) {
         vmk_NetPollInterruptUnSet(devData->nicPoll[index].netPoll);
         devData->nicPoll[index].vector_set = false;
      }
      vmk_IntrUnregister(vmk_ModuleCurrentID, devData->sfvmkIntrInfo.intrCookies[index],  devData->evq[index]);
   }
   return (status);
}


/*
 *****************************************************************************
 *
 * elxnet_intrCleanup
 *
 *    Free any allocated interrupt vectors.
 *
 *      param[in] devData        pointer to elxnet device
 *
 * Results:
 *   None.
 *
 * Side effects:
 *   None
 *
 *****************************************************************************
 */

void
sfvmk_intrCleanup(sfvmk_adapter * devData)
{
   SFVMK_DBG(devData, SFVMK_DBG_DRIVER, 2, "Freeing Intr cookies");
   if (devData->sfvmkIntrInfo.intrCookies[0] != VMK_INVALID_INTRCOOKIE) {
      vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, devData->pciDevice);
      //sfvmk_initializeInterrupts(devData);
   }
}
static VMK_ReturnStatus
sfvmk_enableIntrs(sfvmk_adapter * devData)
{
   VMK_ReturnStatus status = VMK_OK;;
   vmk_int32 i;
   vmk_uint16 numQs = devData->sfvmkIntrInfo.numIntrAlloc; 
	 	

   SFVMK_DBG(devData, SFVMK_DBG_UPLINK, 2,
                  "sfvmk_EnableIntrs entered numQs=%d", numQs);
   for (i = 0; i < 1;/*numQs;*/ i++) {
      if (devData->sfvmkIntrInfo.intrCookies[i] != VMK_INVALID_INTRCOOKIE) {
         SFVMK_DBG(devData, SFVMK_DBG_UPLINK, 0,
                        "*** intrCookies[%d]= 0x%lx ***",
                        i, (vmk_uint64)devData->sfvmkIntrInfo.intrCookies[i]);
         status = vmk_IntrEnable(devData->sfvmkIntrInfo.intrCookies[i]);
         if (status != VMK_OK) {
            SFVMK_ERR(devData, "Interrupt Enable failed for vect %d", i);
            for (i--; i >= 0; i--) {
               vmk_IntrDisable(devData->sfvmkIntrInfo.intrCookies[i]);
            }
//            devData->intr_enabled = false;
            break;
         } else {
  //          devData->intr_enabled = true;
            SFVMK_DBG(devData, SFVMK_DBG_UPLINK, 0,
                           "Enabled interrupt i=%d", i);
         }
      }
   }
   return (status);
}








VMK_ReturnStatus
sfvmk_setup_Interrupts(sfvmk_adapter *adapter)
{
   VMK_ReturnStatus status;

//   if (adapter->sfvmkIntrInfo.type == EFX_INTR_MESSAGE) {
      status = sfvmk_registerInterrupts(adapter);
      if (status == VMK_OK) {
         sfvmk_enableIntrs(adapter);
         return (VMK_OK);
      } 
      #if 0 
      else {

         /*
          * TBD:
          *  Take care of a rare case where vmk_PCIAllocIntrCookie()
          *  succeeds for MSI-x but vmk_IntrRegister() fails.
          *
          *  Then driver tries to register INTx, in that case:
          *   1. We have already updated adapter->queueInfo.maxTxQueues as > 1
          *   2. We are not going to register for VMK_UPLINK_CAP_MULTI_QUEUE.
          *  Need to verify as #1 and #2 are not in sync.
          */
         sfvmk_intrCleanup(adapter);

         /* MSI-X failed to register, try INT-X */
         adapter->sfvmkIntrInfo.type  = EFX_INTR_LINE;
         status = sfvmk_INTxEnable(adapter);
         if (status != VMK_OK) {
            return status;
         }
         status = sfvmk_registerInterrupts(adapter);
	 if (status == VMK_OK) {
          return (VMK_OK);

      }
   }
   #endif 
   return VMK_FAILURE;
}


__inline uint16_t bswap16(uint16_t int16)
{
	const unsigned char *from;
	unsigned char *to;
	uint16_t t;

	from = (const unsigned char *) &int16;
	to = (unsigned char *) &t;

	to[0] = from[1];
	to[1] = from[0];

	return (t);
}
