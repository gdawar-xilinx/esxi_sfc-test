/* **********************************************************
 * Copyright 2013 - 2016 VMware, Inc.  All rights reserved.
 * -- VMware Confidential
 * **********************************************************/

/*
 * nvmxnet3_uplink.c --
 *
 *      Uplink interface implementation of native vmxnet3 driver.
 */

#include "nvmxnet3.h"

/*
 * Local Functions
 */
static VMK_ReturnStatus sfvmk_UplinkTx(vmk_AddrCookie driverData,
                                         vmk_PktList pktList);
static VMK_ReturnStatus sfvmk_UplinkMTUSet(vmk_AddrCookie driverData,
                                             vmk_uint32 mtu);
static VMK_ReturnStatus sfvmk_UplinkStateSet(vmk_AddrCookie driverData,
                                               vmk_UplinkState state);
static VMK_ReturnStatus sfvmk_UplinkStatsGet(vmk_AddrCookie driverData,
                                               vmk_UplinkStats *stats);
static VMK_ReturnStatus sfvmk_UplinkAssociate(vmk_AddrCookie driverData,
                                                vmk_Uplink uplink);
static VMK_ReturnStatus sfvmk_UplinkDisassociate(vmk_AddrCookie driverData);
static VMK_ReturnStatus sfvmk_UplinkCapEnable(vmk_AddrCookie driverData,
                                                vmk_UplinkCap cap);
static VMK_ReturnStatus sfvmk_UplinkCapDisable(vmk_AddrCookie driverData,
                                                 vmk_UplinkCap cap);
static VMK_ReturnStatus sfvmk_UplinkStartIO(vmk_AddrCookie driverData);
static VMK_ReturnStatus sfvmk_UplinkQuiesceIO(vmk_AddrCookie driverData);
static VMK_ReturnStatus sfvmk_UplinkReset(vmk_AddrCookie driverData);
static VMK_ReturnStatus sfvmk_RemoveUplinkDevice(vmk_Device device);

static VMK_ReturnStatus sfvmk_ActivateDev(sfvmk_adapter *adapter);
static void sfvmk_QuiesceDev(sfvmk_adapter *adapter);
static void sfvmk_SetRxMode(sfvmk_adapter *adapter);

/*
 * Global Data
 */
vmk_DeviceOps nvmxnet3UplinkDeviceOps = {
   .removeDevice = sfvmk_RemoveUplinkDevice,
};

vmk_UplinkOps nvmxnet3UplinkOps = {
   .uplinkTx            = sfvmk_UplinkTx,
   .uplinkMTUSet        = sfvmk_UplinkMTUSet,
   .uplinkStateSet      = sfvmk_UplinkStateSet,
   .uplinkStatsGet      = sfvmk_UplinkStatsGet,
   .uplinkAssociate     = sfvmk_UplinkAssociate,
   .uplinkDisassociate  = sfvmk_UplinkDisassociate,
   .uplinkCapEnable     = sfvmk_UplinkCapEnable,
   .uplinkCapDisable    = sfvmk_UplinkCapDisable,
   .uplinkStartIO       = sfvmk_UplinkStartIO,
   .uplinkQuiesceIO     = sfvmk_UplinkQuiesceIO,
   .uplinkReset         = sfvmk_UplinkReset,
};

static VMK_ReturnStatus
sfvmk_RemoveUplinkDevice(vmk_Device device)
{
   sfvmk_adapter *adapter;
   vmk_DeviceGetRegisteringDriverData(device, (vmk_AddrCookie *)&adapter);
   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 0,
                "Remove uplink device %p", device);
   return vmk_DeviceUnregister(device);
}
