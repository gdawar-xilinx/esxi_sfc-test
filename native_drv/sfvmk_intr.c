/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

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

