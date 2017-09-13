/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#include "sfvmk_driver.h"

/*! \brief It creates a spin lock with specified name and lock rank.
**
** \param[in]  lockName  brief name for the spinlock
** \param[in]  rank      lock rank)
** \param[out] lock      lock pointer to create
**
** \return: VMK_OK on success, and lock created. Error code if otherwise.
*/
VMK_ReturnStatus
sfvmk_createLock(const char *pLockName,
                 vmk_LockRank rank,
                 vmk_Lock *pLock)
{
  vmk_SpinlockCreateProps lockProps;
  VMK_ReturnStatus status;

  if((pLock == NULL) || (pLockName == NULL))
    return VMK_BAD_PARAM;

  vmk_Memset(&lockProps, 0, sizeof(vmk_SpinlockCreateProps));

  lockProps.moduleID = vmk_ModuleCurrentID;
  lockProps.heapID = sfvmk_modInfo.heapID;
  lockProps.domain = sfvmk_modInfo.lockDomain;
  lockProps.type = VMK_SPINLOCK;
  lockProps.rank = rank;
  vmk_NameFormat(&lockProps.name, "sfvmk-%s", pLockName);

  status = vmk_SpinlockCreate(&lockProps, pLock);
  if (status != VMK_OK) {
     vmk_WarningMessage("Failed to create spinlock (%s)", vmk_StatusToString(status));
  }

  return status;
}

/*! \brief It destroys the spin lock.
**
** \param[in] lock  vmk_lock to destory
**
** \return  void
*/

void
sfvmk_destroyLock(vmk_Lock lock)
{
  if (lock)
    vmk_SpinlockDestroy(lock);
}
