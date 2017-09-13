/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/


#ifndef __SFVMK_H__
#define __SFVMK_H__

#include <vmkapi.h>
#include <lib/vmkapi_types.h>
#include "sfvmk_driver.h"
#include "sfvmk_utils.h"
#include "efx.h"

/* Constants/Defines */

/* Size of heap to be allocated */
/* TBD: Add a function to calculate the Heap Size */
#define SFVMK_HEAP_EST  (2 * VMK_MEGABYTE)

/* Define it to include  Unit test code.
TBD : Move it to Makefile */
//#define SFVMK_WITH_UNIT_TESTS

typedef struct sfvmk_modInfo_s {
   vmk_Name           driverName;
   vmk_Driver         driverID;
   vmk_HeapID         heapID;
   vmk_MemPool        memPoolID;
   vmk_LogComponent   logID;
   vmk_LogComponent   logThrottledID;
   vmk_LockDomainID   lockDomain;
   vmk_DumpFileHandle dumpFile;
   vmk_ListLinks      adapterList;
} sfvmk_modInfo_t;

extern sfvmk_modInfo_t sfvmk_modInfo;

/* Mem Allocation */
static inline void *
sfvmk_MemAlloc(vmk_uint32 size)
{
  return vmk_HeapAlloc(sfvmk_modInfo.heapID, size);
}

static inline void
sfvmk_MemFree(void *memPtr)
{
  VMK_ASSERT(memPtr);
  vmk_HeapFree(sfvmk_modInfo.heapID, memPtr);
}

/* to handle endianness */
#define sfvmk_LE64ToCPU(x)      ((uint64_t)(x))
#define sfvmk_LE32ToCPU(x)      ((uint32_t)(x))
#define sfvmk_CPUToLE32(x)      ((uint32_t)(x))
#define sfvmk_CPUToLE64(x)      ((uint64_t)(x))

#endif /* __SFVMK_H__ */

