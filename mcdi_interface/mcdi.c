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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <vmkapi.h>
#include "sfvmk_mgmtInterface.h"

extern vmk_MgmtApiSignature driverMgmtSig;

/*! \brief Initialize MCDI mgmt handle
 **
 ** \return Mgmt handle as void pointer
 **         if successful, otherwise return
 **         NULL
 **
 */
void *setup_mcdiHandle(void)
{
  vmk_MgmtUserHandle mgmtHandle = NULL;
  int myCookie = 0;
  int rc = 0;

  rc = vmk_MgmtUserInit(&driverMgmtSig, myCookie, &mgmtHandle);
  if (rc != 0) {
    fprintf(stderr, "MCDI Handle Initialization failed\n");
    return NULL;
  }

  return (void *)mgmtHandle;
}

/*! \brief  Routine to post MCDI commands
 **
 ** \param[in]      handle   Pointer to mgmt iface handle
 ** \param[in]      nic_name Interface name
 ** \param[in/out]  mcdiReq  pointer to MCDI cmd struct
 **
 ** \return 0 on success,
 **        -EINVAL if Invocation of mgmt callback failed
 **        -EIO    if Invocation is successful but an internal
 **                error returned in mgmtParm.status
 **
 */
int post_mcdiCommand(void *handle, char *nic_name, sfvmk_mcdiRequest2_t *mcdiReq)
{
  vmk_MgmtUserHandle mgmtHandle;
  sfvmk_mgmtDevInfo_t mgmtParm;

  if (!nic_name || !mcdiReq || !handle) {
    fprintf(stderr, "%s: Invalid arguments\n", __func__);
    return -EINVAL;
  }

  mgmtHandle = (vmk_MgmtUserHandle)handle;
  strncpy ((char *)mgmtParm.deviceName, nic_name, SFVMK_DEV_NAME_LEN);

  if ((vmk_MgmtUserCallbackInvoke(mgmtHandle, VMK_MGMT_NO_INSTANCE_ID,
                                  SFVMK_CB_MCDI_REQUEST_2, &mgmtParm,
                                  mcdiReq)) != VMK_OK) {
     fprintf(stderr, "Invocation of mgmt callbacks failed\n");
     return -EINVAL;
  }

  if (mgmtParm.status != VMK_OK) {
    fprintf(stderr, "MCDI REQUEST failed, Status 0x%x\n", mgmtParm.status);
    return -EIO;
  }

  return 0;
}

/*! \brief Release MCDI mgmt handle
 **
 ** \param[in] Pointer to mgmt iface handle
 **
 ** \return
 **
 */
void release_mcdiHandle(void *handle)
{
  vmk_MgmtUserHandle mgmtHandle;

  if (!handle) {
    fprintf(stderr, "%s: Invalid arguments\n", __func__);
    return;
  }

  mgmtHandle = (vmk_MgmtUserHandle)handle;
  vmk_MgmtUserDestroy(mgmtHandle);  /* Shut down the user-kernel channel */
}
