/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/


#ifndef __SFVMK_UTILS_H__
#define __SFVMK_UTILS_H__

#include "sfvmk.h"

extern VMK_ReturnStatus
sfvmk_CreateLock(const char *lockName,
                       vmk_LockRank rank,
                       vmk_Lock *lock);

extern void
sfvmk_DestroyLock(vmk_Lock lock);


#endif /*  __SFVMK_UTILS_H__ */
