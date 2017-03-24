/* **********************************************************
 * Copyright 2017 - 2018 Solarflare Inc.  All rights reserved.
 * -- Solarflare Confidential
 * **********************************************************/

#ifndef __SFC_H__
#define __SFC_H__

#include <vmkapi.h>
#include "esxi_sfc_driver.h"

/*
 * sfc.h --
 *
 *      Header file for native solarflare driver.
 */

/*
 * Constants/Defines
 */
#define SFC_DUMP_FILE_NAME SFC_DRIVER_NAME"_dump"
#define SFC_HEAP_EST       (2 * VMK_MEGABYTE)
#define SFC_MAX_ADAPTERS   10
#define SFC_SBDF_FMT       "%04x:%02x:%02x.%x"
#define SFC_PCIID_FMT      "%04x:%04x %04x:%04x"
#define SFC_BAR0           0
#define SFC_BAR1           1
#define SFC_DEVICE_MAX_TX_QUEUES 8
#define SFC_DEVICE_MAX_RX_QUEUES 8


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
} SfcModInfo;

extern SfcModInfo sfcModInfo;
/*
 * Global Data
 */
//extern SfcModInfo sfcModInfo;

#endif /* __SFC_H__ */




