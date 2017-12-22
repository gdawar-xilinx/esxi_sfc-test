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
#include "sfvmk_mgmt_interface.h"

/*! \brief  Get adapter pointer based on hash.
**
** \param[in] pMgmtParm pointer to managment param
**
** \return: pointer to sfvmk_adapter_t <success>  NULL <failure>
*/
static sfvmk_adapter_t *
sfvmk_mgmtFindAdapter(sfvmk_mgmtDevInfo_t *pMgmtParm)
{
  sfvmk_adapterHashEntry_t *pHashTblEntry = NULL;
  int rc;

  rc = vmk_HashKeyFind(sfvmk_modInfo.vmkdevHashTable,
                       pMgmtParm->deviceName,
                       (vmk_HashValue *)&pHashTblEntry);
  if (rc != VMK_OK) {
    SFVMK_ERROR("%s: Failed to find node in vmkDevice "
                "table status: %s", pMgmtParm->deviceName,
                vmk_StatusToString(rc));
    goto end;
  }

  if ((pHashTblEntry == NULL) || (pHashTblEntry->pAdapter == NULL)) {
    SFVMK_ERROR("%s: No vmkDevice (node: %p)",
                  pMgmtParm->deviceName, pHashTblEntry);
    pMgmtParm->status = VMK_NOT_FOUND;
    goto end;
  }

  return pHashTblEntry->pAdapter;

end:
  return NULL;
}

/*! \brief  A Mgmt callback routine to post MCDI commands
 **
 ** \param[in]  pCookies    Pointer to cookie
 ** \param[in]  pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[in/out]  pDevIface   Pointer to device interface structure
 ** \param[in/out]  pMgmtMcdi   Pointer to MCDI cmd struct
 **
 ** \return: VMK_OK  <success>
 **     Below error values are filled in the status field of
 **     sfvmk_mgmtDevInfo_t.
 **     VMK_NOT_FOUND:      In case of dev not found
 **     VMK_BAD_PARAM:      Invalid payload size or input param
 **
 */
VMK_ReturnStatus
sfvmk_mgmtMcdiCallback(vmk_MgmtCookies       *pCookies,
                        vmk_MgmtEnvelope     *pEnvelope,
                        sfvmk_mgmtDevInfo_t  *pDevIface,
                        sfvmk_mcdiRequest_t  *pMgmtMcdi)
{
  sfvmk_adapter_t *pAdapter = NULL;

  vmk_SemaLock(&sfvmk_modInfo.lock);
  if (!pDevIface) {
    SFVMK_ERROR("pDevIface: NULL pointer passed as input");
    goto end;
  }

  if (!pMgmtMcdi) {
    SFVMK_ERROR("pMgmtMcdi: NULL pointer passed as input");
    pDevIface->status = VMK_BAD_PARAM;
    goto end;
  }

  pDevIface->status = VMK_OK;

  pAdapter = sfvmk_mgmtFindAdapter(pDevIface);
  if (!pAdapter) {
    SFVMK_ERROR("Failed to find interface %s", pDevIface->deviceName);
    pDevIface->status = VMK_NOT_FOUND;
    goto end;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_MGMT, SFVMK_LOG_LEVEL_DBG,
                      "Received MCDI request for interface %s",
                      pDevIface->deviceName);

end:
  vmk_SemaUnlock(&sfvmk_modInfo.lock);
  return VMK_OK;
}

