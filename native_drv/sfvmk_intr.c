/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

/* interrupt count for INTX */
#define NUM_INTX 1

/* helper functions */

static void sfvmk_isr(void *arg, vmk_IntrCookie intrCookie);
static void sfvmk_intrCleanup(sfvmk_adapter_t * pAdapter);

static VMK_ReturnStatus sfvmk_intrAckMSIX(void *clientData,
                                          vmk_IntrCookie intrCookie);
static VMK_ReturnStatus sfvmk_intrAckLine(void *clientData,
                                          vmk_IntrCookie intrCookie);
static VMK_ReturnStatus sfvmk_intXEnable(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_enableIntrs(sfvmk_adapter_t * pAdapter);
static VMK_ReturnStatus sfvmk_setupInterrupts(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_registerInterrupts(sfvmk_adapter_t * pAdapter,
                                                 vmk_uint32 numIntr,
                                                 vmk_IntrProps *pIntProps);

/*! \brief interrupt ack function for MSIX
**
** \param[in] arg pointer to client data passed while registering the interrupt
** \param[in] interCookie interrupt cookie
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_intrAckMSIX(void *arg, vmk_IntrCookie intrCookie)
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
    return VMK_IGNORE;

  (void)efx_intr_status_message(pNic, index, &fatal);

  if (fatal) {
    /* disable interrupt */
    (void)efx_intr_disable(pNic);
    (void)efx_intr_fatal(pNic);
    return VMK_IGNORE;
  }

  return VMK_OK;
}

/*! \brief interrupt ack function for line interrupt
**
** \param[in] arg pointer to client data passed while registering the interrupt
** \param[in] interCookie interrupt cookie
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_intrAckLine(void *arg, vmk_IntrCookie intrCookie)
{
  sfvmk_evq_t *pEvq;
  sfvmk_adapter_t *pAdapter;
  efx_nic_t *pNic;
  sfvmk_intr_t *intr;
  vmk_uint32 qMask;
  boolean_t fatal;

  /* get event queue handler*/
  pEvq = (sfvmk_evq_t *)arg;
  /* get associated adapter ptr */
  pAdapter = pEvq->pAdapter;
  pNic = pAdapter->pNic;
  intr = &pAdapter->intr;

  /* check if intr module has been started */
  if ((intr->state != SFVMK_INTR_STARTED))
    return VMK_IGNORE;

  (void)efx_intr_status_line(pNic, &fatal, &qMask);

  if (fatal) {
    /* disable interrupt */
    (void)efx_intr_disable(pNic);
    (void)efx_intr_fatal(pNic);
    return VMK_IGNORE;
  }

  if (qMask != 0) {
    return VMK_OK;
  }
  /* not handling bug#15671 and bug#17203
  assuming these have already fixed in medford */
  return VMK_NOT_THIS_DEVICE;
}

/*! \brief isr for all the event raised on event queue.
**
** \param[in] arg pointer to client data passed while registering the interrupt
** \param[in] interCookie interrupt cookie
**
** \return: void
*/
static void
sfvmk_isr(void *arg, vmk_IntrCookie intrCookie)
{
  sfvmk_evq_t *pEvq;

  /* get event queue handler*/
  pEvq = (sfvmk_evq_t *)arg;

  /* This info is required only for intr moderation testing */
  SFVMK_DBG(pEvq->pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_INFO,
      "Got MSIX interrupt");

  /* activate net poll to process the event */
  vmk_NetPollActivate(pEvq->netPoll);
}

/*! \brief routine to allocate INTX interrupt.
**
** \param[in] adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_intXEnable(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 numIntrsAlloc;
  VMK_ReturnStatus status;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_INTR);

  status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                    pAdapter->pciDevice,
                                    VMK_PCI_INTERRUPT_TYPE_LEGACY,
                                    NUM_INTX,
                                    NUM_INTX,
                                    NULL,
                                    pAdapter->intr.intrCookies, &numIntrsAlloc);
  if (status == VMK_OK) {
    SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_INFO,
              "Allocated %d INT-x intr for device", numIntrsAlloc);
    pAdapter->pEvq[0]->vector = pAdapter->intr.intrCookies[0];
  }
  else
  {
    pAdapter->intr.intrCookies[0] = VMK_INVALID_INTRCOOKIE;
    SFVMK_ERR(pAdapter, "Failed to allocate INT-x vector");
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_INTR);

  return (status);
}

/*! \brief routine for cleaning up all the interrupt allocated.
**
** \param[in] adapter pointer to sfvmk_adapter_t
**
** \return: void
*/
static void
sfvmk_intrCleanup(sfvmk_adapter_t * pAdapter)
{
  vmk_uint32 index;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_INTR);

  if (pAdapter->intr.intrCookies[0] != VMK_INVALID_INTRCOOKIE) {
    vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, pAdapter->pciDevice);
    for (index = 0; index < pAdapter->intr.numIntrAlloc; index++)
      pAdapter->intr.intrCookies[index] = VMK_INVALID_INTRCOOKIE;
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_INTR);
}

/*! \brief enable all MSIX interrupts
**
** \param[in] adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_enableIntrs(sfvmk_adapter_t * pAdapter)
{
  VMK_ReturnStatus status = VMK_OK;;
  vmk_int32 index;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_INTR);

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
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_INTR);

  return (status);
}

/*! \brief register numIntr with appropriate isr and ack handler
**
** \param[in] adapter   pointer to sfvmk_adapter_t
** \param[in] numIntr   number of interrupt to register
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_registerInterrupts(sfvmk_adapter_t * pAdapter,
                                     vmk_uint32 numIntr,
                                     vmk_IntrProps *pIntProps)
{
  vmk_int16 index;
  VMK_ReturnStatus status;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_INTR);

  for (index = 0; index < numIntr; index++) {

    vmk_NameFormat(&pIntProps->deviceName,
                    "%s-evq%d", pAdapter->pciDeviceName.string, index);

    pIntProps->handlerData = pAdapter->pEvq[index];
    /* Register the interrupt with the system.*/
    status = vmk_IntrRegister(vmk_ModuleCurrentID,
                              pAdapter->intr.intrCookies[index], pIntProps);
    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed to register MSIx Interrupt,status: %s, vect %d",
                vmk_StatusToString(status), index);
      goto sfvmk_intr_reg_fail;
    }
    /* Set the associated interrupt cookie with the poll. */
    status = vmk_NetPollInterruptSet(pAdapter->pEvq[index]->netPoll,
                                      pAdapter->pEvq[index]->vector);

    if (status != VMK_OK) {
      SFVMK_ERR(pAdapter, "Failed to set netpoll vector %s",
                vmk_StatusToString(status));

      vmk_IntrUnregister(vmk_ModuleCurrentID,
                          pAdapter->intr.intrCookies[index], pAdapter->pEvq[index]);
      goto sfvmk_intr_reg_fail;
    }

  }

  SFVMK_DBG(pAdapter, SFVMK_DBG_DRIVER, SFVMK_LOG_LEVEL_INFO,
            "Registered %d vectors", index);

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_INTR);

  return status;

sfvmk_intr_reg_fail:
  for (index--; index >= 0; index--) {
    vmk_NetPollInterruptUnSet(pAdapter->pEvq[index]->netPoll);
    vmk_IntrUnregister(vmk_ModuleCurrentID, pAdapter->intr.intrCookies[index], pAdapter->pEvq[index]);
  }

  return (status);
}

/*! \brief registered interrupt, if MSIx registeration failed go for INTX.
**
** \param[in] adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_setupInterrupts(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status;
  vmk_IntrProps intrProps;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_INTR);

  /* define interrupt properties */
  intrProps.device = pAdapter->device;
  intrProps.attrs = VMK_INTR_ATTRS_ENTROPY_SOURCE;

  /* check if interrupt is MSIX */
  if (pAdapter->intr.type == EFX_INTR_MESSAGE) {
    /* isr */
    intrProps.handler = sfvmk_isr;
    intrProps.acknowledgeInterrupt = sfvmk_intrAckMSIX;
    status = sfvmk_registerInterrupts(pAdapter, pAdapter->intr.numIntrAlloc, &intrProps );
    if (status != VMK_OK) {

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
    }
  }

  /* registering INTX */
  if (pAdapter->intr.type == EFX_INTR_LINE) {
    /* isr */
    intrProps.handler = sfvmk_isr;
    intrProps.acknowledgeInterrupt = sfvmk_intrAckLine;
    status = sfvmk_registerInterrupts(pAdapter, NUM_INTX, &intrProps);
    if (status == VMK_OK) {
      SFVMK_ERR(pAdapter , "Failed to register INT-X interrupt");
    }
  }
  /* enabling interrupt(S) */
  status = sfvmk_enableIntrs(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter , "Failed to enable interrupt(s)");
    sfvmk_intrCleanup(pAdapter);
  }

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_INTR);

  return status;
}

/*! \brief start common interrupt module and setup the interrupt.
**
** \param[in] adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
VMK_ReturnStatus
sfvmk_intrStart(sfvmk_adapter_t *pAdapter)
{
  sfvmk_intr_t *pIntr;
  int rc = -1;
  VMK_ReturnStatus status;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_INTR);

  pIntr = &pAdapter->intr;
  VMK_ASSERT_BUG(pIntr->state == SFVMK_INTR_INITIALIZED);

  /* Initialize common code interrupt bits. */
  if ((rc = efx_intr_init(pAdapter->pNic, pIntr->type, NULL)) != 0) {
    SFVMK_ERR(pAdapter, "Failed to init intr with err %d", rc);
    return VMK_FAILURE;
  }

  /* Register all the interrupts */
  SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_INFO,
            "sfvmk register interrupts");

  status = sfvmk_setupInterrupts(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ERR(pAdapter, "%s Failed to register interrupts",
                        pAdapter->pciDeviceName.string);
    goto sfvmk_intr_reg_fail;
  }

  pIntr->state = SFVMK_INTR_STARTED;

  /* Enable interrupts at the NIC */
  efx_intr_enable(pAdapter->pNic);

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_INTR);

  return status ;

sfvmk_intr_reg_fail:
  /* Tear down common code interrupt bits. */
  efx_intr_fini(pAdapter->pNic);
  pIntr->state = SFVMK_INTR_INITIALIZED;

  return status;
}

/*! \brief unregistered all the interrupt.
**
** \param[in] adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
VMK_ReturnStatus
sfvmk_intrStop(sfvmk_adapter_t * pAdapter)
{
  vmk_int16 index;
  VMK_ReturnStatus status = VMK_OK;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_INTR);

  /* disable interrupt in coomon module */
  efx_intr_disable(pAdapter->pNic);

  /* loop through all the interrupt allocated and disable all of them */
  for (index = 0; index < pAdapter->intr.numIntrAlloc; index++)
  {
    if (pAdapter->intr.intrCookies[index] != VMK_INVALID_INTRCOOKIE) {

      /* wait till interrupt is inactive on all cpus */
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

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_INTR);

  return status;
}

/*! \brief   Routine to allocate msix interrupt. if msix interrupt is not
**          supported,  allocate legacy interrupt.interrupt information
**          is populated in sfvmk_adapter_t's intr field.
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> or VMK_FAILURE <failure>
*/
VMK_ReturnStatus
sfvmk_intrInit(sfvmk_adapter_t *pAdapter)
{
  unsigned int numIntReq, numIntrsAlloc;
  unsigned int index = 0;
  VMK_ReturnStatus status;

  SFVMK_NULL_PTR_CHECK(pAdapter);

  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_INTR);

  numIntReq = pAdapter->evqMax;

  /* initializing interrupt cookie */
  for (index = 0; index < numIntReq; index++)
    pAdapter->intr.intrCookies[index] = VMK_INVALID_INTRCOOKIE;

    /* allocate msix interrupt */
    status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                    pAdapter->pciDevice,
                                    VMK_PCI_INTERRUPT_TYPE_MSIX,
                                    numIntReq, numIntReq, NULL,
                                    pAdapter->intr.intrCookies, &numIntrsAlloc);
  if (status == VMK_OK) {
    SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_DBG,
              "Allocated %d interrupts", numIntrsAlloc);

    pAdapter->intr.numIntrAlloc = numIntrsAlloc;
    pAdapter->intr.type = EFX_INTR_MESSAGE;

  } else {

    for (index = 0; index < numIntReq; index++)
      pAdapter->intr.intrCookies[index] = VMK_INVALID_INTRCOOKIE;

    SFVMK_ERR(pAdapter, "PCIAllocIntrCookie failed with error %s ",
              vmk_StatusToString(status));

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
                                        NUM_INTX, NUM_INTX , NULL,
                                        pAdapter->intr.intrCookies,
                                        &numIntrsAlloc);
      if (status == VMK_OK) {

        SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_DBG,
                  "Allocated  %d INTX intr", numIntrsAlloc);
        pAdapter->intr.numIntrAlloc = 1;
        pAdapter->intr.type = EFX_INTR_LINE;
      } else {

        pAdapter->intr.intrCookies[0] = VMK_INVALID_INTRCOOKIE;
        SFVMK_ERR(pAdapter, "Failed to allocate 1 INTX intr");

      }

    } else {
      SFVMK_DBG(pAdapter, SFVMK_DBG_INTR, SFVMK_LOG_LEVEL_DBG,
                 "Allocated 1 MSIx vector for device");
      pAdapter->intr.numIntrAlloc = 1;
      pAdapter->intr.type = EFX_INTR_MESSAGE;
    }

  }

  if(status == VMK_OK)
    pAdapter->intr.state = SFVMK_INTR_INITIALIZED ;

  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_INTR);
  return (status);
}

/*! \brief  Routine to free allocated interrupt
**
** \param[in]  adapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK or VMK_FAILURE
*/
void
sfvmk_freeInterrupts(sfvmk_adapter_t *pAdapter)
{
  SFVMK_DBG_FUNC_ENTRY(pAdapter, SFVMK_DBG_INTR);
  vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, pAdapter->pciDevice);
  SFVMK_DBG_FUNC_EXIT(pAdapter, SFVMK_DBG_INTR);
}

