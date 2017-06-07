
/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"



/* helper functions */

static void sfvmk_intrMessage(void *arg, vmk_IntrCookie intrCookie);
static void sfvmk_intrCleanup(sfvmk_adapter_t * pAdapter);

static VMK_ReturnStatus sfvmk_intrAck(void *clientData,
                                            vmk_IntrCookie intrCookie);
static VMK_ReturnStatus sfvmk_intXEnable(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_enableIntrs(sfvmk_adapter_t * pAdapter);
static VMK_ReturnStatus sfvmk_setupInterrupts(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_registerInterrupts(sfvmk_adapter_t *pAdapter,
                                                              vmk_uint32 numIntr);



/**-----------------------------------------------------------------------------
 *
 * sfvmk_intrAck --
 *
 * @brief interrupt ack function
 *
 * @param[in]
 * @param[in]
 *
 * @result: VMK_OK on success otherwise VMK_FAILURE
 *
 *-----------------------------------------------------------------------------*/
static VMK_ReturnStatus
sfvmk_intrAck(void *clientData, vmk_IntrCookie intrCookie)
{
  return VMK_OK;
}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_intrMessage --
 *
 * @brief isr for all the event raised on event queue.
 *
 * @param[in] arg pointer to client data passed while registering the interrupt
 * @param[in] interCookie interrupt cookie
 *
 * @result: void
 *
 *-----------------------------------------------------------------------------*/
static void
sfvmk_intrMessage(void *arg, vmk_IntrCookie intrCookie)
{

  sfvmk_evq_t *pEvq;
  sfvmk_adapter_t *pAdapter;
  efx_nic_t *pNic;
  sfvmk_intr_t *intr;
  vmk_uint32 index;
  boolean_t fatal;

  /* get event queue handler*/
  pEvq = (sfvmk_evq_t *)arg;
  /* get associated adapter ptr */
  pAdapter = pEvq->pAdapter;
  pNic = pAdapter->pNic;
  intr = &pAdapter->intr;
  /* get event queue index */
  index = pEvq->index;

  /* check if intr module has been started */
  if ((intr->state != SFVMK_INTR_STARTED))
   return;

  (void)efx_intr_status_message(pNic, index, &fatal);

  if (fatal) {
    /* disable interrupt */
    (void)efx_intr_disable(pNic);
    (void)efx_intr_fatal(pNic);
    return;
  }
  /* activate net poll to process the event */
  vmk_NetPollActivate(pEvq->netPoll);

}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_intXEnable --
 *
 * @brief routine to allocate INTX interrupt.
 *
 * @param adapter pointer to sfvmk_adapter_t
 *
 * @result: void
 *
 *-----------------------------------------------------------------------------*/
static VMK_ReturnStatus
sfvmk_intXEnable(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 numIntrsAlloc;
  VMK_ReturnStatus status;

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
            "sfvmk_intXEnable entered ");
  status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                    pAdapter->pciDevice,
                                    VMK_PCI_INTERRUPT_TYPE_LEGACY,
                                    1,
                                    1,
                                    NULL,
                                    pAdapter->intr.intrCookies, &numIntrsAlloc);
  if (status == VMK_OK) {
    SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, 2, "Allocated %d INT-x intr for device",
                numIntrsAlloc);
    pAdapter->pEvq[0]->vector = pAdapter->intr.intrCookies[0];
  }
  else
  {
    pAdapter->intr.intrCookies[0] = VMK_INVALID_INTRCOOKIE;
    SFVMK_ERR(pAdapter, "Failed to allocate INT-x vector");
  }

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
            "sfvmk_intXEnable exit ");

  return (status);
}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_intrCleanup --
 *
 * @brief routine for cleaning up all the interrupt allocated.
 *
 * @param adapter pointer to sfvmk_adapter_t
 *
 * @result: void
 *
 *-----------------------------------------------------------------------------*/
static void
sfvmk_intrCleanup(sfvmk_adapter_t * pAdapter)
{
  vmk_uint32 index;

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
            "sfvmk_intrCleanup entered ");

  if (pAdapter->intr.intrCookies[0] != VMK_INVALID_INTRCOOKIE) {
    vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, pAdapter->pciDevice);
    for (index = 0; index < pAdapter->intr.numIntrAlloc; index++)
      pAdapter->intr.intrCookies[index] = VMK_INVALID_INTRCOOKIE;
  }

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
            "sfvmk_intrCleanup exit");
}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_enableIntrs --
 *
 * @brief enable all MSIxc interrupts
 *
 * @param adapter pointer to sfvmk_adapter_t
 *
 * @result: void
 *
 *-----------------------------------------------------------------------------*/
static VMK_ReturnStatus
sfvmk_enableIntrs(sfvmk_adapter_t * pAdapter)
{
  VMK_ReturnStatus status = VMK_OK;;
  vmk_int32 index;

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
            "sfvmk_enableIntrs entered ");

  for (index = 0; index < pAdapter->intr.numIntrAlloc; index++) {

    if (pAdapter->intr.intrCookies[index] != VMK_INVALID_INTRCOOKIE) {
      SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, 0, " intrCookies[%d]= %lx",
                  index, (vmk_uint64)pAdapter->intr.intrCookies[index]);

      status = vmk_IntrEnable(pAdapter->intr.intrCookies[index]);
      if (status != VMK_OK) {
        SFVMK_ERR(pAdapter, "Interrupt Enable failed for vect %d", index);
        for (index--; index >= 0; index--) {
          vmk_IntrDisable(pAdapter->intr.intrCookies[index]);
        }
        break;
      }
      else
      {
        SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, 0, "Enabled interrupt index=%d",
                  index);
      }
    }
  }
  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
            "sfvmk_enableIntrs exit");

  return (status);
}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_registerInterrupts --
 *
 * @brief register numIntr with appropriate isr and ack handler
 *
 * @param[in] adapter   pointer to sfvmk_adapter_t
 * @param[in] numIntr   number of interrupt to register
 *
 * @result: VMK_OK on success otherwise VMK_FAILURE
 *
 *-----------------------------------------------------------------------------*/
static VMK_ReturnStatus
sfvmk_registerInterrupts(sfvmk_adapter_t * pAdapter, vmk_uint32 numIntr)
{
  vmk_IntrProps intrProps;
  vmk_int16 index;
  VMK_ReturnStatus status;

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
            "sfvmk_registerInterrupts entered ");

  /* define interrupt properties */
  intrProps.device = pAdapter->device;
  intrProps.attrs = VMK_INTR_ATTRS_ENTROPY_SOURCE;
  /* isr */
  intrProps.handler = sfvmk_intrMessage;
  intrProps.acknowledgeInterrupt = sfvmk_intrAck;

  for (index = 0; index < numIntr; index++) {

    vmk_NameFormat(&intrProps.deviceName,
                    "%s-evq%d", pAdapter->pciDeviceName.string, index);

    intrProps.handlerData = pAdapter->pEvq[index];
    /* Register the interrupt with the system.*/
    status = vmk_IntrRegister(vmk_ModuleCurrentID,
                              pAdapter->intr.intrCookies[index], &intrProps);
    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed to register MSIx Interrupt,status: %x, vect %d",
                status, index);
      goto sfvmk_intr_reg_fail;
    }
    /* Set the associated interrupt cookie with the poll. */
    status = vmk_NetPollInterruptSet(pAdapter->pEvq[index]->netPoll,
                                      pAdapter->pEvq[index]->vector);

    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed to set netpoll vector %s", vmk_StatusToString(status));

      vmk_IntrUnregister(vmk_ModuleCurrentID,
                          pAdapter->intr.intrCookies[index], pAdapter->pEvq[index]);
      goto sfvmk_intr_reg_fail;
    }

  }

  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_INFO,
            "Registered %d vectors", index);

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
            "sfvmk_registerInterrupts exit ");

  return status;

sfvmk_intr_reg_fail:
  for (index--; index >= 0; index--) {
    vmk_NetPollInterruptUnSet(pAdapter->pEvq[index]->netPoll);
    vmk_IntrUnregister(vmk_ModuleCurrentID, pAdapter->intr.intrCookies[index], pAdapter->pEvq[index]);
  }

  return (status);
}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_setupInterrupts --
 *
 * @brief registered interrupt, if MSIx registeration failed go for INTX.
 *
 * @param adapter pointer to sfvmk_adapter_t
 *
 * @result: VMK_OK on success otherwise VMK_FAILURE
 *
 *-----------------------------------------------------------------------------*/
static VMK_ReturnStatus
sfvmk_setupInterrupts(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status;

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
              "sfvmk_setupInterrupts entered ");

  /* check if interrupt is MSIX */
  if (pAdapter->intr.type == EFX_INTR_MESSAGE) {
    status = sfvmk_registerInterrupts(pAdapter, pAdapter->intr.numIntrAlloc);
    if (status == VMK_OK) {
      sfvmk_enableIntrs(pAdapter);
      return (VMK_OK);
    }
    else
    {
      SFVMK_ERR(pAdapter , "Failed to register MSI-X interrupt");
      /* clean up all the msix interrupt allocated before */
      sfvmk_intrCleanup(pAdapter);
      /* MSI-X failed, try to register INT-X */
      pAdapter->intr.type  = EFX_INTR_LINE;
      status = sfvmk_intXEnable(pAdapter);
      if (status != VMK_OK) {
        SFVMK_ERR(pAdapter , "Failed to enable INT-X interrupt");
        return status;
      }
      status = sfvmk_registerInterrupts(pAdapter, 1);
      if (status == VMK_OK) {
        SFVMK_ERR(pAdapter , "Failed to register INT-X interrupt");
        return (VMK_OK);
      }
    }
  }
  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
              "sfvmk_setupInterrupts exit");


  return VMK_FAILURE;
}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_intrStart --
 *
 * @brief start common interrupt module and setup the interrupt.
 *
 * @param adapter pointer to sfvmk_adapter_t
 *
 * @result: VMK_OK on success otherwise VMK_FAILURE
 *
 *-----------------------------------------------------------------------------*/
VMK_ReturnStatus
sfvmk_intrStart(sfvmk_adapter_t *pAdapter)
{

  sfvmk_intr_t *pIntr;
  VMK_ReturnStatus status;

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
              "sfvmk_intrStart entered ");

  pIntr = &pAdapter->intr;
  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_INITIALIZED);

  /* Initialize common code interrupt bits. */
  efx_intr_init(pAdapter->pNic, pIntr->type, NULL);

  /* Register all the interrupts */
  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, 3, "sfvmk register interrupts");

  status = sfvmk_setupInterrupts(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "%s Failed to register interrupts",
                        pAdapter->pciDeviceName.string);
    goto sfvmk_intr_reg_fail;
  }

  pIntr->state = SFVMK_INTR_STARTED;

  /* Enable interrupts at the NIC */
  efx_intr_enable(pAdapter->pNic);

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
              "sfvmk_intrStart exit ");

  return status ;

sfvmk_intr_reg_fail:
  /* Tear down common code interrupt bits. */
  efx_intr_fini(pAdapter->pNic);
  pIntr->state = SFVMK_INTR_INITIALIZED;

  return status;
}
/**-----------------------------------------------------------------------------
 *
 * sfvmk_intrStop --
 *
 * @brief unregistered all the interrupt.
 *
 * @param adapter pointer to sfvmk_adapter_t
 *
 * @result: VMK_OK on success otherwise VMK_FAILURE
 *
 *-----------------------------------------------------------------------------*/
VMK_ReturnStatus
sfvmk_intrStop(sfvmk_adapter_t * pAdapter)
{
  vmk_int16 index;
  VMK_ReturnStatus status = VMK_OK;

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
              "sfvmk_intrStop entered ");

  /* disable interrupt in coomon module */
  efx_intr_disable(pAdapter->pNic);

  /* loop through all the interrupt allocated and disable all of them */
  for (index =0 ; index<  pAdapter->intr.numIntrAlloc; index++)
  {
    if (pAdapter->intr.intrCookies[index] != VMK_INVALID_INTRCOOKIE) {

      /* wait till interrupt is inactive on all cpus*/
      status = vmk_IntrSync(pAdapter->intr.intrCookies[index]);
      if (status != VMK_OK) {
        SFVMK_ERR(pAdapter, "Failed failed in vmk_IntrSync with error %s: "
                    "for vect %d", vmk_StatusToString(status), index);
      }

      /* disable interrupt */
      vmk_IntrDisable(pAdapter->intr.intrCookies[index]);
      if (status != VMK_OK) {
        SFVMK_ERR(pAdapter, "Failed in vmk_IntrDisable with error %s: "
                    "for vect %d",vmk_StatusToString(status), index);
      }

      /*Clear the interrupt cookie registered with the poll */
      vmk_NetPollInterruptUnSet(pAdapter->pEvq[index]->netPoll);
      if (status != VMK_OK) {
        SFVMK_ERR(pAdapter, "Failed in vmk_NetPollInterruptUnSet with error %s: "
                    "for vect %d",vmk_StatusToString(status), index);
      }

      /* Unregister a previously registered interrupt */
      status = vmk_IntrUnregister(vmk_ModuleCurrentID,
                                  pAdapter->intr.intrCookies[index],
                                  pAdapter->pEvq[index]);
      if (status != VMK_OK) {
        SFVMK_ERR(pAdapter, "Failed to unregister interrupt with error %s: "
                    "for vect %d", vmk_StatusToString(status), index);
      }
    }
  }
  /* Tear down common code interrupt bits. */
  efx_intr_fini(pAdapter->pNic);

  pAdapter->intr.state = SFVMK_INTR_INITIALIZED;

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_FUNCTION,
              "sfvmk_intrStop exit ");

  return status;
}

