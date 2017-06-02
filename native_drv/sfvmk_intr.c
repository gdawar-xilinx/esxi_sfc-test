
/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"

VMK_ReturnStatus
sfvmk_intrAck(void *clientData, vmk_IntrCookie intrCookie)
{
  return VMK_OK;
}


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







VMK_ReturnStatus
sfvmk_intXEnable(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 numIntrsAlloc;
  VMK_ReturnStatus status;

  status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                    pAdapter->pciDevice,
                                    VMK_PCI_INTERRUPT_TYPE_LEGACY,
                                    1,
                                    1,
                                    NULL,
                                    pAdapter->intr.intrCookies, &numIntrsAlloc);
  if (status == VMK_OK) {
    SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, 2, "Allocated %d INT-x intr for device", numIntrsAlloc);
    pAdapter->pEvq[0]->vector = pAdapter->intr.intrCookies[0];
  }
  else
  {
    pAdapter->intr.intrCookies[0] = VMK_INVALID_INTRCOOKIE;
    SFVMK_ERR(pAdapter, "Failed to allocate INT-x vector");
  }

  return (status);
}





void
sfvmk_intrCleanup(sfvmk_adapter_t * pAdapter)
{

  vmk_uint32 index;
  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 2, "Freeing Intr cookies");

  if (pAdapter->intr.intrCookies[0] != VMK_INVALID_INTRCOOKIE) {
    vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, pAdapter->pciDevice);
    for (index = 0; index < pAdapter->intr.numIntrAlloc; index++)
      pAdapter->intr.intrCookies[index] = VMK_INVALID_INTRCOOKIE;
  }
}
static VMK_ReturnStatus
sfvmk_enableIntrs(sfvmk_adapter_t * pAdapter)
{
  VMK_ReturnStatus status = VMK_OK;;
  vmk_int32 index;

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, 5, "sfvmk_enableIntrs entered ");

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

  return (status);
}


VMK_ReturnStatus
sfvmk_registerInterrupts(sfvmk_adapter_t * pAdapter, vmk_uint32 numIntr)
{
  vmk_IntrProps intrProps;
  vmk_int16 index;
  VMK_ReturnStatus status;

  intrProps.device = pAdapter->device;
  intrProps.attrs = VMK_INTR_ATTRS_ENTROPY_SOURCE;
  intrProps.handler = sfvmk_intrMessage;
  intrProps.acknowledgeInterrupt = sfvmk_intrAck;

  for (index = 0; index < numIntr; index++) {

    vmk_NameFormat(&intrProps.deviceName,
                    "%s-evq%d", pAdapter->pciDeviceName.string, index);

    intrProps.handlerData = pAdapter->pEvq[index];

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

  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, 0, "Registered %d vectors", index);

  return status;

sfvmk_intr_reg_fail:
  for (index--; index >= 0; index--) {
    vmk_NetPollInterruptUnSet(pAdapter->pEvq[index]->netPoll);
    vmk_IntrUnregister(vmk_ModuleCurrentID, pAdapter->intr.intrCookies[index], pAdapter->pEvq[index]);
  }

  return (status);
}


VMK_ReturnStatus
sfvmk_setupInterrupts(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status;

  if (pAdapter->intr.type == EFX_INTR_MESSAGE) {
    status = sfvmk_registerInterrupts(pAdapter, pAdapter->intr.numIntrAlloc);
    if (status == VMK_OK) {
      sfvmk_enableIntrs(pAdapter);
      return (VMK_OK);
    }
    else
    {
      sfvmk_intrCleanup(pAdapter);
      /* MSI-X failed, try to register INT-X */
      pAdapter->intr.type  = EFX_INTR_LINE;
      status = sfvmk_intXEnable(pAdapter);
      if (status != VMK_OK) {
        return status;
      }
      status = sfvmk_registerInterrupts(pAdapter, 1);
      if (status == VMK_OK) {
        return (VMK_OK);
      }
    }
  }
  return VMK_FAILURE;
}

VMK_ReturnStatus
sfvmk_intrStart(sfvmk_adapter_t *pAdapter)
{

  sfvmk_intr_t *pIntr;
  VMK_ReturnStatus status;

  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, 5, "entered sfvmk_intrStart");

  pIntr = &pAdapter->intr;
  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_INITIALIZED);

  /* Initialize common code interrupt bits. */
  efx_intr_init(pAdapter->pNic, pIntr->type, NULL);

  /* Register all the interrupts */
  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, 3, "sfvmk register interrupts");

  status = sfvmk_setupInterrupts(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "%s sfvmk driver: Failed to register interrupts",
                        pAdapter->pciDeviceName.string);
    goto sfvmk_intr_reg_fail;
  }

  pIntr->state = SFVMK_INTR_STARTED;

  /* Enable interrupts at the NIC */
  efx_intr_enable(pAdapter->pNic);

  return status ;

sfvmk_intr_reg_fail:
  /* Tear down common code interrupt bits. */
  efx_intr_fini(pAdapter->pNic);
  pIntr->state = SFVMK_INTR_INITIALIZED;

  return status;
}


VMK_ReturnStatus
sfvmk_intrStop(sfvmk_adapter_t * pAdapter)
{
  vmk_int16 index;
  VMK_ReturnStatus status = VMK_OK;

  for (index =0 ; index<1; index++)
  {
    if (pAdapter->intr.intrCookies[index] != VMK_INVALID_INTRCOOKIE) {
      vmk_IntrSync(pAdapter->intr.intrCookies[index]);
      vmk_IntrDisable(pAdapter->intr.intrCookies[index]);
      vmk_NetPollInterruptUnSet(pAdapter->pEvq[index]->netPoll);

      status = vmk_IntrUnregister(vmk_ModuleCurrentID,
                                  pAdapter->intr.intrCookies[index],
                                  pAdapter->pEvq[index]);
      if (status != VMK_OK) {
        SFVMK_ERR(pAdapter, "Failed to unregister interrupt, status: "
                    "0x%x for vect %d", status, index);
      }
    }
  }

  return status;
}

