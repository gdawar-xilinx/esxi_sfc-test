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
sfvmk_createLock(const char *lockName,
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

/*! \brief It destroys the spin lock.
**
** \param[in] lock  vmk_lock to destory
**
** \return  void
*/

void
sfvmk_destroyLock(vmk_Lock lock)
{
  if (lock) {
    vmk_SpinlockDestroy(lock);
    lock = NULL;
  }
}


