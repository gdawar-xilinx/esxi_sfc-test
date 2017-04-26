/* **********************************************************
 * Copyright 2017 - 2018 Solarflare Inc.  All rights reserved.
 * -- Solarflare Confidential
 * **********************************************************/

#ifndef __SFVMK_H__
#define __SFVMK_H__

#include <vmkapi.h>
#include <lib/vmkapi_types.h>
//#include "sfvmk_driver.h"
#include "efsys.h"

/*
 * sfvmk.h --
 *
 *      Header file for native solarflare driver.
 */

/*
 * Constants/Defines
 */

#define sfvmk_LE64ToCPU(x)      ((uint64_t)(x))
#define sfvmk_LE32ToCPU(x)      ((uint32_t)(x))

#define SFC_DUMP_FILE_NAME SFC_DRIVER_NAME"_dump"
#define SFC_HEAP_EST       (2 * VMK_MEGABYTE)
#define SFC_MAX_ADAPTERS   10
#define SFC_SBDF_FMT       "%04x:%02x:%02x.%x"
#define SFC_PCIID_FMT      "%04x:%04x %04x:%04x"
#define SFC_BAR0           0
#define SFC_BAR1           1
#define SFC_DEVICE_MAX_TX_QUEUES 8
#define SFC_DEVICE_MAX_RX_QUEUES 8


#define	SFVMK_RX_SCALE_MAX	EFX_MAXRSS
//praveen needs to decide later
#define SFVMK_MAX_MSIX_VECTORS 20
/* Define to include UT code */
#define SFVMK_UT

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
#ifdef VMX86_DEBUG
#define SFVMK_DBG_DEFAULT  SFVMK_DBG_ALL
#else
#define SFVMK_DBG_DEFAULT (SFVMK_DBG_DRIVER |     \
                              SFVMK_DBG_UPLINK |     \
                              SFVMK_DBG_QUEUE  |     \
                              SFVMK_DBG_HW)
#endif
#define SFVMK_IS_NAME_EMPTY(name)   (*((name).string) == '\0')
#define SFVMK_NAME_TO_STRING(name)  vmk_NameToString(&(name))


/*
 * Macros
 */
/*
 * Regular debug prints
 */
#define _SFVMK_DBG(adapter, logID, mask, lvl, fmt, ...)              \
   do {                                                                 \
      if ((adapter)->debugMask & (mask)) {                              \
         vmk_LogLevel(VMK_LOG_URGENCY_NORMAL,                            \
                      (logID), (lvl),                                 \
                      "%s: %s:%d: [%s] " fmt "\n",                      \
                      vmk_LogGetName(logID),                            \
                      __FUNCTION__, __LINE__,                           \
                      SFVMK_NAME_TO_STRING((adapter)->idName),       \
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

#define SFVMK_DEBUG(mask, lvl, fmt, ...)                     \
   do {                                                         \
      if (nvmxnet3ModParams.debugMask & (mask)) {               \
         vmk_LogLevel(VMK_LOG_URGENCY_DEBUG,                    \
                      sfvmk_ModInfo.logID, (lvl),             \
                      "%s: %s:%d: " fmt "\n",                   \
                      vmk_LogGetName(sfvmk_ModInfo.logID),    \
                      __FUNCTION__, __LINE__, ##__VA_ARGS__);   \
      }                                                         \
   } while (0)

/*
 * Errors (never masked)
 */
#define _SFVMK_ERR(adapter, logID, fmt, ...)                         \
   do {                                                                 \
      vmk_Warning((logID), "[%s] "fmt,                                  \
                  SFVMK_NAME_TO_STRING((adapter)->idName),           \
                  ##__VA_ARGS__);                                       \
   } while (0)

#define SFVMK_ERR(adapter, fmt, ...)                 \
      _SFVMK_ERR((adapter), sfvmk_ModInfo.logID,   \
                    fmt, ##__VA_ARGS__)

#define SFVMK_ERR_THROTTLED(adapter, fmt, ...)               \
      _SFVMK_ERR((adapter), sfvmk_ModInfo.logThrottledID,  \
                    fmt, ##__VA_ARGS__)

#define SFVMK_ERROR(fmt, ...)                                     \
   do {                                                              \
      vmk_Warning(sfvmk_ModInfo.logID, fmt, ##__VA_ARGS__);        \
   } while (0)



extern sfvmk_ModInfo_t sfvmk_ModInfo;

/* Mem Allocaton */
static inline void *
sfvmk_MemAlloc(vmk_uint32 size)
{
  void *mem = NULL;  
  mem =  vmk_HeapAlloc(sfvmk_ModInfo.heapID, size);
  if (NULL != mem)
    vmk_Memset(mem, 0 , size);
  return mem; 
}

static inline void
sfvmk_MemFree(void *memPtr)
{
   VMK_ASSERT(memPtr);
   vmk_HeapFree(sfvmk_ModInfo.heapID, memPtr);
}
//extern VMK_ReturnStatus sfvmk_(const char *lckName, vmk_LockRank rank, vmk_Lock *lock);
extern VMK_ReturnStatus sfvmk_MutexInit(const char *lckName,vmk_LockRank rank, vmk_Mutex *mutex);
extern void sfvmk_MutexDestroy(vmk_Mutex mutex);
//int
//sfvmk_mcdi_init(sfvmk_adapter *sfAdapter);
#endif /* __SFVMK_H__ */

