/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

/*! \brief Interrupt ack function for MSIX
**
** \param[in] arg          Pointer to client data passed while registering the interrupt
** \param[in] interCookie  Interrupt cookie
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_intrAckMSIX(void *arg, vmk_IntrCookie intrCookie)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;
  sfvmk_adapter_t *pAdapter = NULL;
  VMK_ReturnStatus status = VMK_IGNORE;
  boolean_t fatal;

  if (pEvq == NULL) {
    status = VMK_IGNORE;
    goto done;
  }

  pAdapter = pEvq->pAdapter;
  if (pAdapter == NULL) {
    status = VMK_IGNORE;
    goto done;
  }

  efx_intr_status_message(pAdapter->pNic, pEvq->index, &fatal);

  if (fatal) {
    /* Disable interrupt */
    efx_intr_disable(pAdapter->pNic);
    efx_intr_fatal(pAdapter->pNic);
    status = VMK_IGNORE;
    goto done;
  }

  status = VMK_OK;

done:
  return status;
}

/*! \brief Interrupt ack function for Legacy interrupt
**
** \param[in] arg          Pointer to client data passed while registering the interrupt
** \param[in] interCookie  Interrupt cookie
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_intrAckLine(void *arg, vmk_IntrCookie intrCookie)
{
  sfvmk_evq_t * pEvq = (sfvmk_evq_t *)arg;
  sfvmk_adapter_t *pAdapter = NULL;
  VMK_ReturnStatus status = VMK_IGNORE;
  vmk_uint32 qMask;
  boolean_t fatal;

  if (pEvq == NULL) {
    status = VMK_IGNORE;
    goto done;
  }

  pAdapter = pEvq->pAdapter;
  if (pAdapter == NULL) {
    status = VMK_IGNORE;
    goto done;
  }

  efx_intr_status_line(pAdapter->pNic, &fatal, &qMask);

  if (fatal) {
    /* Disable interrupt */
    efx_intr_disable(pAdapter->pNic);
    efx_intr_fatal(pAdapter->pNic);
    status = VMK_IGNORE;
    goto done;
  }

  if (qMask == 0)
    status = VMK_NOT_THIS_DEVICE;
  else
    status = VMK_OK;

done:
  return status;
}

/*! \brief ISR handler to service the MSIX or legacy interrupt.
**
** \param[in] arg         Pointer to client data passed while registering the interrupt
** \param[in] interCookie Interrupt cookie
**
** \return: void
*/
static void
sfvmk_isr(void *arg, vmk_IntrCookie intrCookie)
{
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)arg;

  /* Activate net poll to process the event */
  if (pEvq != NULL)
    vmk_NetPollActivate(pEvq->netPoll);
}

/*! \brief Register interrupts.
**
** \param[in] pAdapter   Pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_registerInterrupts(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 index;
  vmk_IntrProps intrProps = {0};
  vmk_IntrCookie *pIntrCookies = NULL;
  sfvmk_evq_t *pEvq = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_INTR);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Define interrupt properties */
  intrProps.device = pAdapter->device;
  intrProps.attrs = VMK_INTR_ATTRS_ENTROPY_SOURCE;
  intrProps.handler = sfvmk_isr;

  switch (pAdapter->intr.type) {
  case EFX_INTR_MESSAGE:
    intrProps.acknowledgeInterrupt = sfvmk_intrAckMSIX;
    break;
  case EFX_INTR_LINE:
    intrProps.acknowledgeInterrupt = sfvmk_intrAckLine;
    break;
  default:
    SFVMK_ADAPTER_ERROR(pAdapter, "Unknown type of interrupt");
    status = VMK_FAILURE;
    goto done;
  }

  pIntrCookies = pAdapter->intr.pIntrCookies;

  for (index = 0; index < pAdapter->intr.numIntrAlloc; index++) {
    pEvq = pAdapter->ppEvq[index];
    if (pEvq == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL EVQ ptr");
      status = VMK_FAILURE;
      goto failed_intr_reg;
    }

    vmk_NameFormat(&intrProps.deviceName,
                   "%s-intr%d", pAdapter->uplink.name.string, index);

    intrProps.handlerData = pEvq;

    /* Register interrupt with the system. */
    status = vmk_IntrRegister(vmk_ModuleCurrentID,
                              pIntrCookies[index], &intrProps);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_IntrRegister(%u) failed status: %s",
                          index, vmk_StatusToString(status));
      goto failed_intr_reg;
    }

    /* Set the associated interrupt cookie with the poll. */
    status = vmk_NetPollInterruptSet(pEvq->netPoll, pIntrCookies[index]);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_NetPollInterruptSet failed status: %s",
                          vmk_StatusToString(status));
      goto failed_netpoll_set;
    }
  }
  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_INTR, SFVMK_LOG_LEVEL_INFO,
                      "Registered %u interrupts", index);
  goto done;

failed_netpoll_set:
  vmk_IntrUnregister(vmk_ModuleCurrentID, pIntrCookies[index], pEvq);

failed_intr_reg:
  while (index--) {
    pEvq = pAdapter->ppEvq[index];
    vmk_NetPollInterruptUnSet(pEvq->netPoll);
    vmk_IntrUnregister(vmk_ModuleCurrentID, pIntrCookies[index], pEvq);
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_INTR);

  return status;
}

/*! \brief Unregister interrupts.
**
** \param[in] pAdapter   Pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_unregisterInterrupts(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 index;
  sfvmk_evq_t *pEvq = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_INTR);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  for (index = 0; index < pAdapter->intr.numIntrAlloc; index++) {
    pEvq = pAdapter->ppEvq[index];
    if (pEvq == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL EVQ ptr");
      status = VMK_FAILURE;
      goto done;
    }

    status = vmk_NetPollInterruptUnSet(pEvq->netPoll);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_NetPollInterruptUnSet(%u) failed status: %s",
                          index, vmk_StatusToString(status));
    }

    status = vmk_IntrUnregister(vmk_ModuleCurrentID,
                                pAdapter->intr.pIntrCookies[index], pEvq);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_IntrUnregister(%u) failed status: %s",
                          index, vmk_StatusToString(status));
    }
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_INTR);

  return status;
}

/*! \brief Enable interrupts.
**
** \param[in] pAdapter   Pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_enableInterrupts(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 index;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_INTR);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  for (index = 0; index < pAdapter->intr.numIntrAlloc; index++) {
    status = vmk_IntrEnable(pAdapter->intr.pIntrCookies[index]);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_IntrEnable(%u) failed status: %s",
                          index, vmk_StatusToString(status));
      goto failed_intr_enable;
    }
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_INTR, SFVMK_LOG_LEVEL_INFO,
                      "Enabled %u interrupts", index);

  goto done;

failed_intr_enable:
  while (index--)
    vmk_IntrDisable(pAdapter->intr.pIntrCookies[index]);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_INTR);

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
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_INTR);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pAdapter->intr.state != SFVMK_INTR_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Interrupt is not initialized");
    status = VMK_FAILURE;
    goto done;
  }

  status  = efx_intr_init(pAdapter->pNic, pAdapter->intr.type, NULL);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_intr_init failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Register all the interrupts */
  status = sfvmk_registerInterrupts(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_registerInterrupts failed status: %s",
                        vmk_StatusToString(status));
    goto failed_intr_register;
  }

  status = sfvmk_enableInterrupts(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_enableInterrupts failed status: %s",
                        vmk_StatusToString(status));
    goto failed_intr_enable;
  }

  pAdapter->intr.state = SFVMK_INTR_STATE_STARTED;

  /* Enable interrupts at the NIC */
  efx_intr_enable(pAdapter->pNic);

  status = VMK_OK;
  goto done;

failed_intr_enable:
  sfvmk_unregisterInterrupts(pAdapter);

failed_intr_register:
  /* Tear down common code interrupt bits. */
  efx_intr_fini(pAdapter->pNic);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_INTR);

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
  vmk_uint32 index;
  VMK_ReturnStatus status = VMK_FAILURE;

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_INTR);

  efx_intr_disable(pAdapter->pNic);

  /* Loop through all the interrupt allocated and disable all of them */
  for (index = 0; index < pAdapter->intr.numIntrAlloc; index++) {
    if (pAdapter->intr.pIntrCookies[index] != VMK_INVALID_INTRCOOKIE) {

      /* Wait till interrupt is inactive on all cpus */
      status = vmk_IntrSync(pAdapter->intr.pIntrCookies[index]);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "vmk_IntrSync(%u) failed status %s:",
                            index, vmk_StatusToString(status));
      }

      /* Disable interrupt */
      status = vmk_IntrDisable(pAdapter->intr.pIntrCookies[index]);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "vmk_IntrDisable(%u) failed status %s:",
                            index, vmk_StatusToString(status));
      }
    }
  }

  status = sfvmk_unregisterInterrupts(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_unregisterInterrupts failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Tear down common code interrupt bits. */
  efx_intr_fini(pAdapter->pNic);

  pAdapter->intr.state = SFVMK_INTR_STATE_INITIALIZED;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_INTR);

  return status;
}

/*! \brief  Routine to allocate MSIX interrupt. if MSIX interrupt is not
**          supported, allocate 1 MSI interrupt if MSI is also not supported,
**          allocate legacy interrupt.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> or VMK_FAILURE <failure>
*/
VMK_ReturnStatus
sfvmk_intrInit(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 index = 0;
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_INTR);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  const efx_nic_cfg_t *pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_cfg_get failed");
    status = VMK_FAILURE;
    goto done;
  }

  pAdapter->intr.numIntrDesired = MIN(pAdapter->numEvqsDesired,
                                      pNicCfg->enc_intr_limit);

  /* Allocate memory for cookie */
  pAdapter->intr.pIntrCookies = vmk_HeapAlloc(sfvmk_modInfo.heapID,
                                              sizeof(vmk_IntrCookie) *
                                              pAdapter->intr.numIntrDesired);
  if (pAdapter->intr.pIntrCookies == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HeapAlloc failed");
    status = VMK_NO_MEMORY;
    goto done;
  }

  /* Initializing interrupt cookie */
  for (index = 0; index < pAdapter->intr.numIntrDesired; index++)
    pAdapter->intr.pIntrCookies[index] = VMK_INVALID_INTRCOOKIE;

  /* Allocate MSIX interrupt */
  status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                  pAdapter->pciDevice,
                                  VMK_PCI_INTERRUPT_TYPE_MSIX,
                                  pAdapter->intr.numIntrDesired, 1, NULL,
                                  pAdapter->intr.pIntrCookies,
                                  &pAdapter->intr.numIntrAlloc);
  if (status == VMK_OK) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_INTR, SFVMK_LOG_LEVEL_DBG,
                        "Allocated %d MSIX interrupts",
                        pAdapter->intr.numIntrAlloc);
    pAdapter->intr.type = EFX_INTR_MESSAGE;
    goto done;
  }

  SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIAllocIntrCookie failed for MSIX status: %s",
                      vmk_StatusToString(status));

  /* MSIX interrupts allocation failed will try single MSI interrupt */
  status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                  pAdapter->pciDevice,
                                  VMK_PCI_INTERRUPT_TYPE_MSI,
                                  1, 1, NULL,
                                  pAdapter->intr.pIntrCookies,
                                  &pAdapter->intr.numIntrAlloc);
  if (status == VMK_OK) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_INTR, SFVMK_LOG_LEVEL_DBG,
                        "Allocated %d MSI interrupts", pAdapter->intr.numIntrAlloc);
    pAdapter->intr.type = EFX_INTR_MESSAGE;
    goto done;
  }

  SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIAllocIntrCookie failed for MSI status: %s",
                      vmk_StatusToString(status));

  /* Single MSI interrupt allocation failed try legacy interrupt */
  status = vmk_PCIAllocIntrCookie(vmk_ModuleCurrentID,
                                  pAdapter->pciDevice,
                                  VMK_PCI_INTERRUPT_TYPE_LEGACY,
                                  1, 1, NULL,
                                  pAdapter->intr.pIntrCookies,
                                  &pAdapter->intr.numIntrAlloc);
  if (status == VMK_OK) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_INTR, SFVMK_LOG_LEVEL_DBG,
                        "Allocated  %d INTX interrupts", pAdapter->intr.numIntrAlloc);
    pAdapter->intr.type = EFX_INTR_LINE;
    goto done;
  }

  SFVMK_ADAPTER_ERROR(pAdapter,
                      "vmk_PCIAllocIntrCookie failed for Legacy intr status: %s",
                      vmk_StatusToString(status));

  vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter->intr.pIntrCookies);

done:
  if (status == VMK_OK)
    pAdapter->intr.state = SFVMK_INTR_STATE_INITIALIZED;

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_INTR);
  return status;
}

/*! \brief  Routine to free allocated interrupts
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
*/
VMK_ReturnStatus
sfvmk_intrFini(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_BAD_PARAM;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_INTR);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  if (pAdapter->intr.state != SFVMK_INTR_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Interrupt is not initialized");
    status = VMK_FAILURE;
    goto done;
  }

  status = vmk_PCIFreeIntrCookie(vmk_ModuleCurrentID, pAdapter->pciDevice);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_PCIFreeIntrCookie failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter->intr.pIntrCookies);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_INTR);

  return status;
}

