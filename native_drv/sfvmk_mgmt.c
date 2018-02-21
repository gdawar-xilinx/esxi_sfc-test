/*
 * Copyright (c) 2017, Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "sfvmk_driver.h"
#include "sfvmk_mgmt_interface.h"

/*! \brief  A Mgmt callback routine to post MCDI commands
 **
 ** \param[in]  pCookies    Pointer to cookie
 ** \param[in]  pEnvelope   Pointer to vmk_MgmtEnvelope
 ** \param[in,out]  pDevIface   Pointer to device interface structure
 ** \param[in,out]  pMgmtMcdi   Pointer to MCDI cmd struct
 **
 ** \return: VMK_OK  [success]
 **
 */
int
sfvmk_mgmtMcdiCallback(vmk_MgmtCookies       *pCookies,
                        vmk_MgmtEnvelope     *pEnvelope,
                        sfvmk_mgmtDevInfo_t  *pDevIface,
                        sfvmk_mcdiRequest_t  *pMgmtMcdi)
{
  VMK_ReturnStatus status;

  status = VMK_FAILURE;

  SFVMK_ERROR("MCDI callback failed with error: %s", vmk_StatusToString(status));
  return status;
}

