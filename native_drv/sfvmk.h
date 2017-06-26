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
/* used to format pci device name */
#define SFVMK_SBDF_FMT  "%04x:%02x:%02x.%x"

/* Define it to include  Unit test code.
TBD : Move it to Makefile */
//#define SFVMK_WITH_UNIT_TESTS

#define SFVMK_ONE_MILISEC 1000  /* in terms of microsecconds  */

#ifndef CACHE_LINE_SIZE
/* This should be right on most machines the driver will be used on, and
 * we needn't care too much about wasting a few KB per interface. */
#define CACHE_LINE_SIZE 128
#endif

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
  SFVMK_DBG_RSS    = 1 << 4,
  SFVMK_DBG_INTR   = 1 << 5,
  SFVMK_DBG_HW     = 1 << 6,
  SFVMK_DBG_TX     = 1 << 7,
  SFVMK_DBG_RX     = 1 << 8,
  SFVMK_DBG_EVQ    = 1 << 9,
  SFVMK_DBG_PORT   = 1 << 10,
  SFVMK_DBG_COMMON_CODE  = 1 << 11,
} SfvmkDebugMask;

typedef enum sfvmk_logLevel {
  SFVMK_LOG_LEVEL_ERROR = 1,
  SFVMK_LOG_LEVEL_WARN,
  SFVMK_LOG_LEVEL_INFO,
  SFVMK_LOG_LEVEL_DBG,
  SFVMK_LOG_LEVEL_FUNCTION,
} sfvmk_logLevel;

#define SFVMK_DBG_ALL (vmk_uint32)(-1)
#define SFVMK_DBG_DEFAULT ( SFVMK_DBG_DRIVER |                               \
                            SFVMK_DBG_UPLINK |                               \
                            SFVMK_DBG_EVQ    |                               \
                            SFVMK_DBG_TX     |                               \
                            SFVMK_DBG_HW)

#define SFVMK_IS_NAME_EMPTY(name)   (*((name).string) == '\0')
#define SFVMK_NAME_TO_STRING(name)  vmk_NameToString(&(name))

extern struct sfvmk_modParams_s modParams;

#define SFVMK_NAME(adapter)                                                  \
  !SFVMK_IS_NAME_EMPTY((adapter)->uplinkName) ?                              \
  SFVMK_NAME_TO_STRING((adapter)->uplinkName) :                              \
  !SFVMK_IS_NAME_EMPTY((adapter)->pciDeviceName) ?                           \
  SFVMK_NAME_TO_STRING((adapter)->pciDeviceName) :                           \
  "unknown"

/* regular debug logging */
#define _SFVMK_DBG(adapter, logID, mask, lvl, fmt, ...)                      \
  do {                                                                       \
    if (modParams.debugMask & (mask)) {                                      \
      vmk_LogLevel(VMK_LOG_URGENCY_NORMAL, (logID), (lvl),                   \
      "%s: %s:%d: [%s] " fmt "\n", vmk_LogGetName(logID),                    \
      __FUNCTION__, __LINE__, SFVMK_NAME(adapter),                           \
      ##__VA_ARGS__);                                                        \
    }                                                                        \
  } while (0)

#define SFVMK_DBG(adapter, mask, lvl, fmt, ...)                              \
  _SFVMK_DBG((adapter), sfvmk_ModInfo.logID,                                 \
  (mask), (lvl), fmt, ##__VA_ARGS__)

#define SFVMK_DEBUG(mask, lvl, fmt, ...)                                     \
  do {                                                                       \
    if (modParams.debugMask & (mask)) {                                      \
      vmk_LogLevel(VMK_LOG_URGENCY_NORMAL,                                   \
      sfvmk_ModInfo.logID, (lvl),                                            \
      "%s: %s:%d: " fmt "\n",                                                \
      vmk_LogGetName(sfvmk_ModInfo.logID),                                   \
      __FUNCTION__, __LINE__, ##__VA_ARGS__);                                \
    }                                                                        \
  } while (0)

/* Errors (never masked) */
#define SFVMK_ERR(adapter, fmt, ...)                                         \
  vmk_WarningMessage("[%s] "fmt, SFVMK_NAME(adapter), ##__VA_ARGS__)

#define SFVMK_ERROR(fmt, ...)                                                \
  vmk_WarningMessage(fmt, ##__VA_ARGS__)

#define  __SFVMK_DBG_FUNC(pAdapter, mask, string, fmt, ...)                  \
  SFVMK_DBG(pAdapter, mask, SFVMK_LOG_LEVEL_FUNCTION, string                 \
  " %s (" fmt ")", __FUNCTION__, ##__VA_ARGS__);

#define  __SFVMK_DEBUG_FUNC(mask, string, fmt, ...)                          \
  SFVMK_DEBUG(mask, SFVMK_LOG_LEVEL_FUNCTION, string                         \
  " %s (" fmt ")", __FUNCTION__, ##__VA_ARGS__);

#define SFVMK_DBG_FUNC_ENTRY(pAdapter, mask, ...)                            \
  __SFVMK_DBG_FUNC(pAdapter, mask, "Entered ", __VA_ARGS__)

#define SFVMK_DBG_FUNC_EXIT(pAdapter, mask,  ...)                            \
  __SFVMK_DBG_FUNC(pAdapter, mask, "Exiting ", __VA_ARGS__)

#define SFVMK_DEBUG_FUNC_ENTRY(mask, ...)                                    \
  __SFVMK_DEBUG_FUNC(mask, "Entered ",  __VA_ARGS__)

#define SFVMK_DEBUG_FUNC_EXIT(mask, ...)                                     \
  __SFVMK_DEBUG_FUNC(mask,"Exiting ",  __VA_ARGS__)

#define SFVMK_NULL_PTR_CHECK(ptr)                                            \
  VMK_ASSERT_BUG(NULL != ptr, "NULL %s ptr", #ptr);

/********************************************************************************
* Module Parameters                                                             *
*                                                                               *
* Parameters to PARAMS are,                                                     *
* (param, defval, type1, type2, min, max, desc)                                 *
* NOTE:                                                                         *
* - "type1" is the data type to store the variable,                             *
* - "type2" is the type passed to VMK_MODPARAM_NAMED. bool can be used.         *
* - "min/max" represent acceptable ranges of value. Both of them being zero     *
*   means no constraint of value. The range check will be done at module        *
*   load time.                                                                  *
********************************************************************************/
#define SFVMK_MOD_PARAMS_LIST                                                \
          PARAMS(debugMask, SFVMK_DBG_DEFAULT,                               \
                  vmk_uint32, uint,                                          \
                  0, 0,                                                      \
                  "Debug Logging Bit Masks")                                 \

#define PARAMS(param, defval, type1, type2, min, max, desc)                  \
               type1 param;

typedef struct sfvmk_modParams_s {
  SFVMK_MOD_PARAMS_LIST
} sfvmk_modParams_t;

#undef PARAMS

/* Mem Allocation */
static inline void *
sfvmk_MemAlloc(vmk_uint32 size)
{
  return vmk_HeapAlloc(sfvmk_ModInfo.heapID, size);
}

static inline void
sfvmk_MemFree(void *memPtr)
{
  VMK_ASSERT(memPtr);
  vmk_HeapFree(sfvmk_ModInfo.heapID, memPtr);
}

/* to handle endianness */
#define sfvmk_LE64ToCPU(x)      ((uint64_t)(x))
#define sfvmk_LE32ToCPU(x)      ((uint32_t)(x))
#define sfvmk_CPUToLE32(x)      ((uint32_t)(x))
#define sfvmk_CPUToLE64(x)      ((uint64_t)(x))


/* VLAN handling related macros */
#define SFVMK_VLAN_HDR_START_OFFSET     12
#define SFVMK_ETH_TYPE_SIZE             2

#define SFVMK_VLAN_PRIO_SHIFT           13
#define SFVMK_VLAN_PRIO_MASK            0xe000
#define SFVMK_VLAN_VID_MASK             0x0fff


#endif /* __SFVMK_H__ */

