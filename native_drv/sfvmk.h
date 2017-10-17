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
   vmk_MgmtHandle     mgmtHandle;
} sfvmk_modInfo_t;

extern sfvmk_modInfo_t sfvmk_modInfo;

/* Structure for module params */
typedef struct sfvmk_modParams_s {
  vmk_uint32 debugMask;
} sfvmk_modParams_t;

extern struct sfvmk_modParams_s modParams;

/* Structure and macros for logging */
typedef enum sfvmk_debugMask_e {
  SFVMK_DEBUG_DRIVER = 1 << 0,
  SFVMK_DEBUG_UTILS  = 1 << 1,
  SFVMK_DEBUG_MGMT   = 1 << 2,
  SFVMK_DEBUG_UPLINK = 1 << 3,
  SFVMK_DEBUG_RSS    = 1 << 4,
  SFVMK_DEBUG_INTR   = 1 << 5,
  SFVMK_DEBUG_HW     = 1 << 6,
  SFVMK_DEBUG_TX     = 1 << 7,
  SFVMK_DEBUG_RX     = 1 << 8,
  SFVMK_DEBUG_EVQ    = 1 << 9,
  SFVMK_DEBUG_PORT   = 1 << 10,
  SFVMK_DEBUG_MCDI   = 1 << 11,
  SFVMK_DEBUG_COMMON_CODE  = 1 << 12
} sfvmk_debugMask_t;

typedef enum sfvmk_logLevel_e {
  SFVMK_LOG_LEVEL_ERROR = 1,
  SFVMK_LOG_LEVEL_WARN,
  SFVMK_LOG_LEVEL_INFO,
  SFVMK_LOG_LEVEL_DBG,
  SFVMK_LOG_LEVEL_FUNCTION,
} sfvmk_logLevel_t;

#define SFVMK_DEBUG_ALL (sfvmk_debugMask_t)(-1)
#define SFVMK_DEBUG_DEFAULT (SFVMK_DEBUG_DRIVER |                            \
                             SFVMK_DEBUG_UPLINK |                            \
                             SFVMK_DEBUG_EVQ    |                            \
                             SFVMK_DEBUG_HW)

#define SFVMK_ADAPTER_DEBUG(pAdapter, mask, lvl, fmt, ...)                   \
  do {                                                                       \
    if (modParams.debugMask & (mask)) {                                      \
      vmk_LogLevel(VMK_LOG_URGENCY_NORMAL, sfvmk_modInfo.logID,              \
                   (lvl), "%s: %s:%d: [%s] " fmt "\n",                       \
                   vmk_LogGetName(sfvmk_modInfo.logID), __FUNCTION__,        \
                   __LINE__, (pAdapter) ?                                    \
                   vmk_NameToString(&pAdapter->devName) : "sfvmk",           \
                   ##__VA_ARGS__);                                           \
    }                                                                        \
  } while (0)

#define SFVMK_DEBUG(mask, lvl, fmt, ...)                                     \
  do {                                                                       \
    if (modParams.debugMask & (mask)) {                                      \
      vmk_LogLevel(VMK_LOG_URGENCY_NORMAL, sfvmk_modInfo.logID, (lvl),       \
                   "%s: %s:%d:" fmt "\n",                                    \
                   vmk_LogGetName(sfvmk_modInfo.logID),                      \
                   __FUNCTION__, __LINE__, ##__VA_ARGS__);                   \
    }                                                                        \
  } while (0)

/* Errors (never masked) */
#define SFVMK_ADAPTER_ERROR(pAdapter, fmt, ...)                              \
  do {                                                                       \
      vmk_WarningMessage("[%s] "fmt, (pAdapter) ?                            \
                         vmk_NameToString(&pAdapter->devName) : "sfvmk",     \
                         ##__VA_ARGS__);                                     \
  } while (0)

#define SFVMK_ERROR(fmt, ...)                                                \
  vmk_WarningMessage(fmt, ##__VA_ARGS__)

/* Debug macro for function entry and exit */
#define SFVMK_ADAPTER_DEBUG_FUNC(pAdapter, mask, string, fmt, ...)           \
  SFVMK_ADAPTER_DEBUG(pAdapter, mask, SFVMK_LOG_LEVEL_FUNCTION, "%s " fmt,   \
                      string, ##__VA_ARGS__)

#define SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, mask, ...)                  \
  SFVMK_ADAPTER_DEBUG_FUNC(pAdapter, mask, "Entered ", __VA_ARGS__)

#define SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, mask,  ...)                  \
  SFVMK_ADAPTER_DEBUG_FUNC(pAdapter, mask, "Exiting ", __VA_ARGS__)

#define SFVMK_DEBUG_FUNC(mask, string, fmt, ...)                             \
  SFVMK_DEBUG(mask, SFVMK_LOG_LEVEL_FUNCTION, "%s " fmt, string,             \
              ##__VA_ARGS__)

#define SFVMK_DEBUG_FUNC_ENTRY(mask, ...)                                    \
  SFVMK_DEBUG_FUNC(mask, "Entered ", __VA_ARGS__)

#define SFVMK_DEBUG_FUNC_EXIT(mask,  ...)                                    \
  SFVMK_DEBUG_FUNC(mask, "Exiting ", __VA_ARGS__)

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

