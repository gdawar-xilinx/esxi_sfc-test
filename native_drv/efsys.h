/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 *************************************************************************/

#ifndef _SYS_EFSYS_H
#define _SYS_EFSYS_H

#include <vmkapi.h>
#include <base/vmkapi_time.h>

#include "sfvmk.h"
#include "efsys_errno.h"

/* ESXi 6.5 only supports x86_64 platform */
/* Byte Order */
#define EFSYS_IS_BIG_ENDIAN 0
#define EFSYS_IS_LITTLE_ENDIAN 1

/*  Base Type */
#define EFSYS_HAS_UINT64 1
#define EFSYS_USE_UINT64 1

/* Helper Macros */
#ifndef IS_P2ALIGNED
#define IS_P2ALIGNED(v, a)      ((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)
#endif

#ifndef P2ROUNDUP
#define P2ROUNDUP(x, align)     (-(-(x) & -(align)))
#endif

#ifndef P2ALIGN
#define P2ALIGN(_x, _a)         ((_x) & -(_a))
#endif

#ifndef IS2P
#define ISP2(x)                 (((x) & ((x) - 1)) == 0)
#endif

#define MAX(_x, _y)             (((_x) > (_y)) ? (_x) : (_y))
#define MIN(_x, _y)             (((_x) <= (_y)) ? (_x) : (_y))

typedef vmk_int8                int8_t;
typedef vmk_uint8               uint8_t;
typedef vmk_int16               int16_t;
typedef vmk_uint16              uint16_t;
typedef vmk_int32               int32_t;
typedef vmk_uint32              uint32_t;
typedef vmk_int64               int64_t;
typedef vmk_uint64              uint64_t;

typedef vmk_ByteCount           size_t;
typedef vmk_Bool                boolean_t;
typedef char*                   caddr_t;
typedef vmk_uintptr_t           uintptr_t;


typedef struct __efsys_identifier_s efsys_identifier_t;

#include "imported/efx_types.h"

/* SAL annotations used for Windows builds */
#define __in
#define __in_opt
#define __in_ecount(_n)
#define __in_ecount_opt(_n)
#define __in_bcount(_n)
#define __in_bcount_opt(_n)

#define __out
#define __out_opt
#define __out_ecount(_n)
#define __out_ecount_opt(_n)
#define __out_bcount(_n)
#define __out_bcount_opt(_n)
#define __out_bcount_part(_n, _l)
#define __out_bcount_part_opt(_n, _l)

#define __deref_out

#define __inout
#define __inout_opt
#define __inout_ecount(_n)
#define __inout_ecount_opt(_n)
#define __inout_bcount(_n)
#define __inout_bcount_opt(_n)
#define __inout_bcount_full_opt(_n)

#define __deref_out_bcount_opt(n)

#define __checkReturn
#define __success(_x)

#define __drv_when(_p, _c)

#define B_FALSE VMK_FALSE
#define B_TRUE VMK_TRUE

/* Compilation options */

#define EFSYS_OPT_NAMES 1

#define EFSYS_OPT_SIENA 0
#define EFSYS_OPT_HUNTINGTON 1
#define EFSYS_OPT_MEDFORD 1

#ifdef DEBUG
#define EFSYS_OPT_CHECK_REG 1
#else
#define EFSYS_OPT_CHECK_REG 0
#endif

#define EFSYS_OPT_MCDI 1
#define EFSYS_OPT_MCDI_LOGGING 0
#define EFSYS_OPT_MCDI_PROXY_AUTH 0

#define EFSYS_OPT_MAC_STATS 1
#define EFSYS_OPT_DECODE_INTR_FATAL 0

#define EFSYS_OPT_LOOPBACK 0

#define EFSYS_OPT_MON_MCDI 0
#define EFSYS_OPT_MON_STATS 0

#define EFSYS_OPT_PHY_STATS 0
#define EFSYS_OPT_BIST 1
#define EFSYS_OPT_PHY_LED_CONTROL 1
#define EFSYS_OPT_PHY_FLAGS 0

#define EFSYS_OPT_VPD 1
#define EFSYS_OPT_NVRAM 1
#define EFSYS_OPT_BOOTCFG 0

#define EFSYS_OPT_DIAG 0
#define EFSYS_OPT_RX_SCALE 1
#define EFSYS_OPT_QSTATS 1
#define EFSYS_OPT_FILTER 1
#define EFSYS_OPT_RX_SCATTER 0

#define EFSYS_OPT_EV_PREFETCH 0


#define EFSYS_OPT_LICENSING 0

#define EFSYS_OPT_ALLOW_UNCONFIGURED_NIC 0

#define EFSYS_OPT_RX_PACKED_STREAM 0

/* Memory Allocation/Deallocation */
typedef vmk_IOA  efsys_dma_addr_t;
#define SFVMK_UNREFERENCED_LOCAL_VARIABLE(x)  (void)(x)

#define EFSYS_KMEM_ALLOC(_esip, _size, _p)                              \
        do {                                                            \
            SFVMK_UNREFERENCED_LOCAL_VARIABLE(_esip);                   \
            (_p) = sfvmk_MemAlloc(_size);                               \
            if (NULL != (_p))                                             \
              vmk_Memset((_p), 0 , _size);                                \
        } while (B_FALSE)

#define EFSYS_KMEM_FREE(_esip, _size, _p)                               \
        do {                                                            \
           SFVMK_UNREFERENCED_LOCAL_VARIABLE(_esip);                    \
           SFVMK_UNREFERENCED_LOCAL_VARIABLE(_size);                    \
           sfvmk_MemFree(_p);                                           \
        } while (B_FALSE)


/* LOCK */
typedef unsigned int efsys_lock_state_t;

typedef struct efsys_lock_s {
        vmk_Lock        lock;
        vmk_Name        lockName;
} efsys_lock_t;

#define EFSYS_LOCK(_lockp, _state)                                      \
        do {                                                            \
            SFVMK_UNREFERENCED_LOCAL_VARIABLE(_state);                  \
            vmk_SpinlockLock((_lockp)->lock);                             \
           } while (B_FALSE)

#define EFSYS_UNLOCK(_lockp, _state)                                    \
        do {                                                            \
            SFVMK_UNREFERENCED_LOCAL_VARIABLE(_state);                  \
            vmk_SpinlockUnlock((_lockp)->lock);                         \
        } while (B_FALSE)

/* BARRIERS */
#define EFSYS_MEM_READ_BARRIER()     vmk_CPUMemFenceRead()
#define EFSYS_PIO_WRITE_BARRIER()    vmk_CPUMemFenceWrite()

/* DMA SYNC */
#define EFSYS_DMA_SYNC_FOR_KERNEL(_esmp, _offset, _size)                \
        do {                                                            \
            vmk_DMAFlushElem((_esmp)->esmHandle,                        \
            VMK_DMA_DIRECTION_TO_MEMORY, &(_esmp)->ioElem);             \
        } while (B_FALSE)

#define EFSYS_DMA_SYNC_FOR_DEVICE(_esmp, _offset, _size)                \
        do {                                                            \
            vmk_DMAFlushElem((_esmp)->esmHandle,                        \
            VMK_DMA_DIRECTION_FROM_MEMORY, &(_esmp)->ioElem);           \
        } while (B_FALSE)

typedef struct efsys_mem_s {
        vmk_DMAEngine       esmHandle;  // This is actually a pointer to vmk_DMAEngineInt
        uint8_t             *pEsmBase;   // virtual address
        vmk_SgElem          ioElem;    // addr and size
} efsys_mem_t;


#define EFSYS_MEM_ZERO(_esmp, _size)                                    \
        do {                                                            \
            (void) vmk_Memset((_esmp)->pEsmBase, 0, (_size));           \
        } while (B_FALSE)

#define EFSYS_MEM_READD(_esmp, _offset, _edp)                           \
        do {                                                            \
            uint32_t *addr;                                             \
                                                                        \
            VMK_ASSERT(IS_P2ALIGNED(_offset, sizeof (efx_dword_t)),     \
                ("not power of 2 aligned"));                            \
                                                                        \
            addr = (void *)((_esmp)->pEsmBase + (_offset));             \
                                                                        \
            (_edp)->ed_u32[0] = *addr;                                  \
                                                                        \
            EFSYS_PROBE2(mem_readd, unsigned int, (_offset),            \
                uint32_t, (_edp)->ed_u32[0]);                           \
                                                                        \
        } while (B_FALSE)

#define EFSYS_MEM_READQ(_esmp, _offset, _eqp)                           \
        do {                                                            \
            uint64_t *addr;                                             \
                                                                        \
            VMK_ASSERT(IS_P2ALIGNED(_offset, sizeof (efx_qword_t)),     \
                ("not power of 2 aligned"));                            \
                                                                        \
            addr = (void *)((_esmp)->pEsmBase + (_offset));             \
                                                                        \
            (_eqp)->eq_u64[0] = *addr;                                  \
                                                                        \
            EFSYS_PROBE3(mem_readq, unsigned int, (_offset),            \
                uint32_t, (_eqp)->eq_u32[1],                            \
                uint32_t, (_eqp)->eq_u32[0]);                           \
                                                                        \
        } while (B_FALSE)

#define EFSYS_MEM_READO(_esmp, _offset, _eop)                           \
        do {                                                            \
            uint64_t *addr;                                             \
                                                                        \
            VMK_ASSERT(IS_P2ALIGNED(_offset, sizeof (efx_oword_t)),     \
                ("not power of 2 aligned"));                            \
                                                                        \
            addr = (void *)((_esmp)->pEsmBase + (_offset));             \
                                                                        \
            (_eop)->eo_u64[0] = *addr++;                                \
            (_eop)->eo_u64[1] = *addr;                                  \
                                                                        \
            EFSYS_PROBE5(mem_reado, unsigned int, (_offset),            \
                uint32_t, (_eop)->eo_u32[3],                            \
                uint32_t, (_eop)->eo_u32[2],                            \
                uint32_t, (_eop)->eo_u32[1],                            \
                uint32_t, (_eop)->eo_u32[0]);                           \
                                                                        \
        } while (B_FALSE)

#define EFSYS_MEM_WRITED(_esmp, _offset, _edp)                          \
        do {                                                            \
            uint32_t *addr;                                             \
                                                                        \
            VMK_ASSERT(IS_P2ALIGNED(_offset, sizeof (efx_dword_t)),     \
                ("not power of 2 aligned"));                            \
                                                                        \
            EFSYS_PROBE2(mem_writed, unsigned int, (_offset),           \
                uint32_t, (_edp)->ed_u32[0]);                           \
                                                                        \
            addr = (void *)((_esmp)->pEsmBase + (_offset));             \
                                                                        \
            *addr = (_edp)->ed_u32[0];                                  \
                                                                        \
        } while (B_FALSE)

#define EFSYS_MEM_WRITEQ(_esmp, _offset, _eqp)                          \
        do {                                                            \
            uint64_t *addr;                                             \
                                                                        \
            VMK_ASSERT(IS_P2ALIGNED(_offset, sizeof (efx_qword_t)),     \
                ("not power of 2 aligned"));                            \
                                                                        \
            EFSYS_PROBE3(mem_writeq, unsigned int, (_offset),           \
                uint32_t, (_eqp)->eq_u32[1],                            \
                uint32_t, (_eqp)->eq_u32[0]);                           \
                                                                        \
            addr = (void *)((_esmp)->pEsmBase + (_offset));             \
                                                                        \
            *addr   = (_eqp)->eq_u64[0];                                \
                                                                        \
        } while (B_FALSE)


#define EFSYS_MEM_WRITEO(_esmp, _offset, _eop)                          \
        do {                                                            \
            uint64_t *addr;                                             \
                                                                        \
            VMK_ASSERT(IS_P2ALIGNED((_offset), sizeof (efx_oword_t)),   \
                ("not power of 2 aligned"));                            \
                                                                        \
            EFSYS_PROBE5(mem_writeo, unsigned int, (_offset),           \
                uint32_t, (_eop)->eo_u32[3],                            \
                uint32_t, (_eop)->eo_u32[2],                            \
                uint32_t, (_eop)->eo_u32[1],                            \
                uint32_t, (_eop)->eo_u32[0]);                           \
                                                                        \
            addr = (void *)((_esmp)->pEsmBase + (_offset));             \
                                                                        \
            *addr++ = (_eop)->eo_u64[0];                                \
            *addr   = (_eop)->eo_u64[1];                                \
                                                                        \
        } while (B_FALSE)

#define EFSYS_MEM_SIZE(_esmp)                                           \
        ((_esmp)->ioElem.length)

#define EFSYS_MEM_ADDR(_esmp)                                           \
        ((_esmp)->ioElem.ioAddr)

#define EFSYS_MEM_IS_NULL(_esmp)                                        \
        ((_esmp)->pEsmBase == NULL)



/* PCI BAR access */

typedef struct efsys_bar_s {
  vmk_MappedResourceAddress esbBase;
  vmk_Lock                  esbLock;
  vmk_uint32                index;
} efsys_bar_t;

#define EFSYS_BAR_READD(_esbp, _offset, _edp, _lock)                    \
  do {                                                                  \
    if(_lock)                                                           \
      vmk_SpinlockLock(_esbp->esbLock);                                 \
    vmk_MappedResourceRead32(&(_esbp->esbBase) ,(_offset),              \
                              &((_edp)->ed_u32[0]));                    \
    if(_lock)                                                           \
      vmk_SpinlockUnlock((_esbp)->esbLock);                             \
  } while (B_FALSE)

#define EFSYS_BAR_READQ(_esbp, _offset, _eqp)                           \
  do {                                                                  \
    SFVMK_UNREFERENCED_LOCAL_VARIABLE(_esbp);                           \
    vmk_MappedResourceRead64(&((_esbp)->esbBase) , (_offset),           \
                              &((_eqp)->eq_u64[0]));                    \
  } while (B_FALSE)

#define EFSYS_BAR_READO(_esbp, _offset, _eop, _lock)                    \
  do {                                                                  \
    SFVMK_UNREFERENCED_LOCAL_VARIABLE(_esbp);                           \
    if(_lock)                                                           \
      vmk_SpinlockLock((_esbp)->esbLock);                               \
    vmk_MappedResourceRead64(&((_esbp)->esbBase), (_offset),            \
                              &((_eop)->eo_u64[0]));                    \
    vmk_MappedResourceRead64(&((_esbp)->esbBase), ((_offset) +          \
                            sizeof(vmk_uint64)), &((_eop)->eo_u64[1])); \
    if(_lock)                                                           \
      vmk_SpinlockUnlock((_esbp)->esbLock);                             \
  } while (B_FALSE)

#define EFSYS_BAR_WRITED(_esbp, _offset, _edp, _lock)                   \
  do {                                                                  \
    if(_lock)                                                           \
      vmk_SpinlockLock((_esbp)->esbLock);                               \
    vmk_MappedResourceWrite32(&((_esbp)->esbBase), (_offset),           \
                              (_edp)->ed_u32[0]);                       \
    if(_lock)                                                           \
      vmk_SpinlockUnlock((_esbp)->esbLock);                             \
  } while (B_FALSE)

#define EFSYS_BAR_WRITEQ(_esbp, _offset, _eqp)                          \
  do {                                                                  \
      vmk_MappedResourceWrite64(&((_esbp)->esbBase) , (_offset),        \
                                (_eqp)->eq_u64[0]);                     \
  } while (B_FALSE)

/* TODO: EFSYS_BAR_WC_WRITEQ  implementation  is incomplete
  will cause failure in PIO write in TX path */
#define EFSYS_BAR_WC_WRITEQ(_esbp, _offset, _eqp)                       \
  do {                                                                  \
      SFVMK_UNREFERENCED_LOCAL_VARIABLE(_esbp);                         \
  } while (B_FALSE)


#define EFSYS_BAR_WRITEO(_esbp, _offset, _eop, _lock)                   \
  do {                                                                  \
    if(_lock)                                                           \
      vmk_SpinlockLock(_esbp->esbLock);                                 \
    vmk_MappedResourceWrite64(&((_esbp)->esbBase), (_offset),           \
                              (_eop)->eo_u64[0]);                       \
    vmk_MappedResourceWrite64(&((_esbp)->esbBase), ((_offset) +         \
                            sizeof(vmk_uint64)), (_eop)->eo_u64[1]);    \
    if(_lock)                                                           \
      vmk_SpinlockUnlock((_esbp)->esbLock);                             \
  } while (B_FALSE)



/* Use the standard octo-word write for doorbell writes */
#define EFSYS_BAR_DOORBELL_WRITEO(_esbp, _offset, _eop)                 \
        do {                                                            \
            EFSYS_BAR_WRITEO((_esbp), (_offset), (_eop), B_FALSE);      \
        } while (B_FALSE)

/* CPU spin and sleep */
#define EFSYS_SPIN(_us)                                                 \
        do {                                                            \
            vmk_DelayUsecs(_us);                                        \
        } while (B_FALSE)

/* TBD: VmWare does not provide sleep API, we may need to implement sleep function */
#define EFSYS_SLEEP     EFSYS_SPIN

/* Timestamps */
typedef  vmk_TimeVal efsys_timestamp_t;

#define EFSYS_TIMESTAMP(_usp)                                           \
        do {                                                            \
            efsys_timestamp_t now;                                      \
            vmk_GetUptime(&now);                                        \
            *(_usp) = (now.sec * 1000000) + now.usec;                   \
        } while (B_FALSE)

/* Statistics */
typedef uint64_t    efsys_stat_t;

#define EFSYS_STAT_INCR(_knp, _delta)                                   \
        do {                                                            \
            *(_knp) += (_delta);                                        \
        } while (B_FALSE)

#define EFSYS_STAT_DECR(_knp, _delta)                                   \
        do {                                                            \
           *(_knp) -= (_delta);                                         \
        } while (B_FALSE)

#define EFSYS_STAT_SET(_knp, _val)                                      \
        do {                                                            \
            *(_knp) = (_val);                                           \
        } while (B_FALSE)

#define EFSYS_STAT_SET_QWORD(_knp, _valp)                               \
        do {                                                            \
            *(_knp) = sfvmk_LE64ToCPU((_valp)->eq_u64[0]);              \
        } while (B_FALSE)

#define EFSYS_STAT_SET_DWORD(_knp, _valp)                               \
        do {                                                            \
            *(_knp) = sfvmk_LE32ToCPU((_valp)->ed_u32[0]);              \
        } while (B_FALSE)

#define EFSYS_STAT_INCR_QWORD(_knp, _valp)                              \
        do {                                                            \
            *(_knp) += sfvmk_LE64ToCPU((_valp)->eq_u64[0]);             \
        } while (B_FALSE)

#define EFSYS_STAT_SUBR_QWORD(_knp, _valp)                              \
        do {                                                            \
            *(_knp) -= sfvmk_LE64ToCPU((_valp)->eq_u64[0]);             \
        } while (B_FALSE)

/* Assertions */
#define EFSYS_ASSERT(_exp) do {                                         \
        if (!(_exp))                                                    \
            VMK_ASSERT(_exp);                                           \
        } while (0)

#define EFSYS_ASSERT3(_x, _op, _y, _t) do {                             \
        const _t __x = (_t)(_x);                                        \
        const _t __y = (_t)(_y);                                        \
        if (!(__x _op __y))                                             \
          VMK_ASSERT("assertion failed at %s:%u",__FILE__, __LINE__);   \
        } while(0)

#define EFSYS_ASSERT3U(_x, _op, _y)     EFSYS_ASSERT3(_x, _op, _y, uint64_t)
#define EFSYS_ASSERT3S(_x, _op, _y)     EFSYS_ASSERT3(_x, _op, _y, int64_t)
#define EFSYS_ASSERT3P(_x, _op, _y)     EFSYS_ASSERT3(_x, _op, _y, uintptr_t)

/* Probes */

#define EFSYS_PROBES

#ifndef EFSYS_PROBES

#define EFSYS_PROBE(_name)

#define EFSYS_PROBE1(_name, _type1, _arg1)

#define EFSYS_PROBE2(_name, _type1, _arg1, _type2, _arg2)

#define EFSYS_PROBE3(_name, _type1, _arg1, _type2, _arg2,               \
            _type3, _arg3)

#define EFSYS_PROBE4(_name, _type1, _arg1, _type2, _arg2,               \
            _type3, _arg3, _type4, _arg4)

#define EFSYS_PROBE5(_name, _type1, _arg1, _type2, _arg2,               \
            _type3, _arg3, _type4, _arg4, _type5, _arg5)

#define EFSYS_PROBE6(_name, _type1, _arg1, _type2, _arg2,               \
            _type3, _arg3, _type4, _arg4, _type5, _arg5,                \
            _type6, _arg6)

#define EFSYS_PROBE7(_name, _type1, _arg1, _type2, _arg2,               \
            _type3, _arg3, _type4, _arg4, _type5, _arg5,                \
            _type6, _arg6, _type7, _arg7)

#else /* EFSYS_PROBES */

#define EFSYS_PROBE(_name)                                              \
        vmk_LogMessage("%s","#_name")

#define EFSYS_PROBE1(_name, _type1, _arg1)                              \
        vmk_LogMessage("FUNC: %s, LINE# %d, %s, \n\
        %s:%lu",                                                        \
        __FUNCTION__ , __LINE__ , #_name,                               \
        #_arg1, (uint64_t)_arg1)

#define EFSYS_PROBE2(_name, _type1, _arg1, _type2, _arg2)               \
        vmk_LogMessage("FUNC: %s, LINE# %d, %s, \n\
        %s:%lu, %s:%lu",                                                \
        __FUNCTION__ , __LINE__ , #_name,                               \
        #_arg1, (uint64_t)_arg1,                                        \
        #_arg2, (uint64_t)_arg2)

#define EFSYS_PROBE3(_name, _type1, _arg1, _type2, _arg2,               \
        _type3, _arg3)                                                  \
        vmk_LogMessage("FUNC: %s, LINE# %d, %s, \n\
        %s:%lu, %s:%lu, %s:%lu",                                        \
        __FUNCTION__ , __LINE__ , #_name,                               \
        #_arg1, (uint64_t)_arg1,                                        \
        #_arg2, (uintptr_t)_arg2,                                       \
        #_arg3, (uintptr_t)_arg3)

#define EFSYS_PROBE4(_name, _type1, _arg1, _type2, _arg2,               \
        _type3, _arg3, _type4, _arg4)                                   \
        vmk_LogMessage("FUNC: %s, LINE# %d, %s, \n\
        %s:%lu, %s:%lu, %s:%lu, %s:%lu",                                \
        __FUNCTION__ , __LINE__ , #_name,                               \
        #_arg1, (uint64_t)_arg1,                                        \
        #_arg2, (uint64_t)_arg2,                                        \
        #_arg3, (uint64_t)_arg3,                                        \
        #_arg4, (uint64_t)_arg4)

#define EFSYS_PROBE5(_name, _type1, _arg1, _type2, _arg2,               \
        _type3, _arg3, _type4, _arg4, _type5, _arg5)                    \
        vmk_LogMessage("FUNC: %s, LINE# %d, %s, \n\
        %s:%lu, %s:%lu, %s:%lu, %s:%lu,  %s:%lu",                       \
        __FUNCTION__ , __LINE__ , #_name,                               \
        #_arg1, (uint64_t)_arg1,                                        \
        #_arg2, (uint64_t)_arg2,                                        \
        #_arg3, (uint64_t)_arg3,                                        \
        #_arg4, (uint64_t)_arg4,                                        \
        #_arg5, (uint64_t)_arg5)

#define EFSYS_PROBE6(_name, _type1, _arg1, _type2, _arg2,               \
        _type3, _arg3, _type4, _arg4, _type5, _arg5,                    \
        _type6, _arg6)                                                  \
        vmk_LogMessage("FUNC: %s, LINE# %d, %s, \n\
        %s:%lu, %s:%lu, %s:%lu, %s:%lu, %s:%lu, %s:%lu",                \
        __FUNCTION__ , __LINE__ , #_name,                               \
        #_arg1, (uint64_t)_arg1,                                        \
        #_arg2, (uint64_t)_arg2,                                        \
        #_arg3, (uint64_t)_arg3,                                        \
        #_arg4, (uint64_t)_arg4,                                        \
        #_arg5, (uint64_t)_arg5,                                        \
        #_arg6, (uint64_t)_arg6)

#define EFSYS_PROBE7(_name, _type1, _arg1, _type2, _arg2,               \
        _type3, _arg3, _type4, _arg4, _type5, _arg5,                    \
        _type6, _arg6, _type7, _arg7)                                   \
        vmk_LogMessage("FUNC: %s, LINE# %d, %s, \n\
        %s:%lu, %s:%lu, %s:%lu, %s:%lu, %s:%lu, %s:%lu, %s:%lu",        \
        __FUNCTION__ , __LINE__ , #_name,                               \
        #_arg1, (uint64_t)_arg1,                                        \
        #_arg2, (uint64_t)_arg2,                                        \
        #_arg3, (uint64_t)_arg3,                                        \
        #_arg4, (uint64_t)_arg4,                                        \
        #_arg5, (uint64_t)_arg5,                                        \
        #_arg6, (uint64_t)_arg6,                                        \
        #_arg7, (uint64_t)_arg7)

#endif /* EFSYS_PROBES */


/* Some functions with different names in the VMK */
#define memmove(dest, src, n)   vmk_Memmove(dest, src, n)
#define memcpy(dest, src, n)    vmk_Memcpy(dest, src, n)
#define memset(dest, byte, n)   vmk_Memset(dest, byte, n)
#define memcmp(src1, src2, n)   vmk_Memcmp(src1, src2, n)

#define strncpy(src1, src2, n)  vmk_Strncpy(src1, src2, n)
#define strnlen(src, n)         vmk_Strnlen(src, n)

#endif  /* _SYS_EFSYS_H */

