/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#ifndef __SFVMK_UTILS_H__
#define __SFVMK_UTILS_H__

#include "sfvmk.h"

/* spinklock  handlers */
VMK_ReturnStatus
sfvmk_createLock(const char *pLockName, vmk_LockRank rank, vmk_Lock *pLock);

void sfvmk_destroyLock(vmk_Lock lock);
#endif /*  __SFVMK_UTILS_H__ */
