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
//#include "sfvmk_utils.h"
#include "efx.h"

#define	SFVMK_RX_SCALE_MAX	EFX_MAXRSS
#define	SFVMK_MODERATION	30
/* Constants/Defines */

/* Size of heap to be allocated */
/* TBD: Add a function to calculate the Heap Size */
#define SFVMK_HEAP_EST	(4 * VMK_MEGABYTE)
#define SFVMK_SBDF_FMT	"%04x:%02x:%02x.%x"


#define	SFVMK_NDESCS	1024
#ifndef CACHE_LINE_SIZE
/* This should be right on most machines the driver will be used on, and
 * we needn't care too much about wasting a few KB per interface.
 */
#define	CACHE_LINE_SIZE 128
#endif



enum sfvmk_flush_state {
	SFVMK_FLUSH_DONE = 0,
	SFVMK_FLUSH_REQUIRED,
	SFVMK_FLUSH_PENDING,
	SFVMK_FLUSH_FAILED
};


/* Define it to include  Unit test code.
TBD : Move it to Makefile */
//#define SFVMK_WITH_UNIT_TESTS

typedef struct {
   vmk_Name           driverName;
   vmk_Driver         driverID;
   vmk_HeapID         heapID;
   vmk_MemPool        memPoolID;
   vmk_LogComponent   logID;
   vmk_LogComponent   logThrottledID;
   vmk_LockDomainID   lockDomain;
   vmk_DumpFileHandle dumpFile;
   vmk_ListLinks      adapterList;
} sfvmk_ModInfo_t;

extern sfvmk_ModInfo_t sfvmk_ModInfo;

/* structure and macros for logging */

typedef enum {
  SFVMK_DBG_DRIVER = 1 << 0,
  SFVMK_DBG_UTILS  = 1 << 1,
  SFVMK_DBG_MGMT   = 1 << 2,
  SFVMK_DBG_UPLINK = 1 << 3,
  SFVMK_DBG_QUEUE  = 1 << 4,
  SFVMK_DBG_RSS    = 1 << 5,
  SFVMK_DBG_INTR   = 1 << 6,
  SFVMK_DBG_HW     = 1 << 7,
  SFVMK_DBG_TX     = 1 << 8,
  SFVMK_DBG_RX     = 1 << 9,
} SfvmkDebugMask;

#define SFVMK_DBG_ALL (vmk_uint32)(-1)
#define SFVMK_DBG_DEFAULT (SFVMK_DBG_DRIVER |     	\
                              SFVMK_DBG_UPLINK |     	\
                              SFVMK_DBG_QUEUE  |     	\
                              SFVMK_DBG_HW)

#define SFVMK_IS_NAME_EMPTY(name)   (*((name).string) == '\0')
#define SFVMK_NAME_TO_STRING(name)  vmk_NameToString(&(name))
#define SFVMK_NAME(adapter) \
           !SFVMK_IS_NAME_EMPTY((adapter)->uplinkName) ?     \
           SFVMK_NAME_TO_STRING((adapter)->uplinkName) :     \
           !SFVMK_IS_NAME_EMPTY((adapter)->pciDeviceName) ?  \
           SFVMK_NAME_TO_STRING((adapter)->pciDeviceName) :  \
           "unknown"




/* regular debug logging */
#define _SFVMK_DBG(adapter, logID, mask, lvl, fmt, ...)              \
   do {                                                                 \
      if ((adapter)->debugMask & (mask)) {                              \
         vmk_LogLevel(VMK_LOG_URGENCY_NORMAL,                            \
                      (logID), (lvl),                                 \
                      "%s: %s:%d: [%s] " fmt "\n",                      \
                      vmk_LogGetName(logID),                            \
                      __FUNCTION__, __LINE__,                           \
                      SFVMK_NAME(adapter),       \
                      ##__VA_ARGS__);                                   \
      }                                                                 \
   } while (0)

#define SFVMK_DBG(adapter, mask, lvl, fmt, ...)      \
   _SFVMK_DBG((adapter), sfvmk_ModInfo.logID,      \
                 (mask), (lvl), fmt, ##__VA_ARGS__)

#define SFVMK_DBG_THROTTLED(adapter, mask, lvl, fmt, ...)    \
   _SFVMK_DBG((adapter), sfvmk_ModInfo.logThrottledID,     \
                 (mask), (lvl), fmt, ##__VA_ARGS__)

#ifdef VMX86_DEBUG
#define SFVMK_DBG_ONLY_THROTTLED(adapter, mask, lvl, fmt, ...)       \
   SFVMK_DBG_THROTTLED(adapter, mask, lvl, fmt, ##__VA_ARGS__)
#else
#define SFVMK_DBG_ONLY_THROTTLED(adapter, mask, lvl, fmt, ...)
#endif


/* Errors (never masked) */
#define _SFVMK_ERR(adapter, logID, fmt, ...)                         \
   do {                                                                 \
      vmk_Warning((logID), "[%s] "fmt,                                  \
                  SFVMK_NAME(adapter),           \
                  ##__VA_ARGS__);                                       \
   } while (0)

#define SFVMK_ERR(adapter, fmt, ...)                 \
      _SFVMK_ERR((adapter), sfvmk_ModInfo.logID,   \
                    fmt, ##__VA_ARGS__)


#define SFVMK_ERROR(fmt, ...)                                     \
   do {                                                              \
      vmk_Warning(sfvmk_ModInfo.logID, fmt, ##__VA_ARGS__);        \
   } while (0)



/* Mem Allocation */
static inline void *
sfvmk_MemAlloc(vmk_uint32 size)
{
   unsigned int  *addr; 
   addr = vmk_HeapAlloc(sfvmk_ModInfo.heapID, size);
   vmk_Memset(addr , 0 , size);
   return addr; 
  
}

static inline void
sfvmk_MemFree(void *memPtr)
{
   VMK_ASSERT(memPtr);
   vmk_HeapFree(sfvmk_ModInfo.heapID, memPtr);
}

#define sfvmk_LE64ToCPU(x)      ((uint64_t)(x))
#define sfvmk_LE32ToCPU(x)      ((uint32_t)(x))
#define sfvmk_CPUToLE32(x)      ((uint32_t)(x))
#define sfvmk_CPUToLE64(x)      ((uint64_t)(x))

#endif /* __SFVMK_H__ */

