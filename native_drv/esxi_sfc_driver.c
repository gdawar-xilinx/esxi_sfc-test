/* **********************************************************
 * Copyright 2017 - 2018 Solarflare Inc.  All rights reserved.
 * -- Solarflare Confidential
 * **********************************************************/

#include "esxi_sfc.h"

static VMK_ReturnStatus esxi_sfc_DeviceAttach(vmk_Device device);
static VMK_ReturnStatus esxi_sfc_DeviceDetach(vmk_Device device);
static VMK_ReturnStatus esxi_sfc_DeviceScan(vmk_Device device);
static VMK_ReturnStatus esxi_sfc_DeviceShutdown(vmk_Device device);
static VMK_ReturnStatus esxi_sfc_DeviceStart(vmk_Device device);
static void esxi_sfc_DeviceForget(vmk_Device device);

/*
 ***********************************************************************
 * Device Driver Operations
 ***********************************************************************
 */

static vmk_DriverOps sfc_DriverOps = {
   .attachDevice  = esxi_sfc_DeviceAttach,
   .detachDevice  = esxi_sfc_DeviceDetach,
   .scanDevice    = esxi_sfc_DeviceScan,
   .quiesceDevice = esxi_sfc_DeviceShutdown,
   .startDevice   = esxi_sfc_DeviceStart,
   .forgetDevice  = esxi_sfc_DeviceForget,
};

/*
 ***********************************************************************
 * esxi_sfc_DeviceAttach --                                            */ /**
 *
 * Callback routine for the device layer to announce device to the
 * driver.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */
static VMK_ReturnStatus
esxi_sfc_DeviceAttach(vmk_Device dev)
{
  vmk_LogMessage("AttachDevice is invoked!");
  return VMK_OK;
}

/*
 ***********************************************************************
 * esxi_sfc_DeviceStart --                                             */ /**
 *
 * Callback routine for the device layer to notify the driver to bring
 * up the specified device.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */
static VMK_ReturnStatus
esxi_sfc_DeviceStart(vmk_Device dev)
{
  vmk_LogMessage("StartDevice is invoked!");
  return VMK_OK;
}

/*
 ***********************************************************************
 * esxi_sfc_DeviceScan --                                              */ /**
 *
 * Callback routine for the device layer to notify the driver to scan
 * for new devices.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
esxi_sfc_DeviceScan(vmk_Device device)
{
  vmk_LogMessage("ScanDevice is invoked!");
  return VMK_OK;
}

/*
 ***********************************************************************
 * esxi_sfc_DeviceDetach --                                            */ /**
 *
 * Callback routine for the device layer to notify the driver to release
 * control of a device.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
esxi_sfc_DeviceDetach(vmk_Device dev)
{
  vmk_LogMessage("DetachDevice is invoked!");
  return VMK_OK;
}

/*
 ***********************************************************************
 * esxi_sfc_DeviceShutdown --                                          */ /**
 *
 * Callback routine for the device layer to notify the driver to
 * shutdown the specified device.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */
static VMK_ReturnStatus
esxi_sfc_DeviceShutdown(vmk_Device dev)
{
  vmk_LogMessage("QuiesceDevice/drv_DeviceShutdown is invoked!");
  return VMK_OK;
}

/*
 ***********************************************************************
 * esxi_sfc_DeviceForget --                                            */ /**
 *
 * Callback routine for the device layer to notify the driver the
 * specified device is not responsive.
 *
 * Return values:
 *  None
 *
 ***********************************************************************
 */

static void
esxi_sfc_DeviceForget(vmk_Device dev)
{
  vmk_LogMessage("ForgetDevice is invoked!");
  return ;
}


/*
 ***********************************************************************
 * esxi_sfc_DriverRegister --      */ /**
 *
 * Register this driver as network driver
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */
VMK_ReturnStatus
esxi_sfc_DriverRegister()
{
  VMK_ReturnStatus status;
  vmk_DriverProps sfcDriverProps;

  /* Populate sfcDriverProps */
  sfcDriverProps.moduleID = vmk_ModuleCurrentID;
  vmk_NameCopy(&sfcDriverProps.name, &sfcModInfo.driverName);
  sfcDriverProps.ops = &sfc_DriverOps;
  sfcDriverProps.privateData = (vmk_AddrCookie)NULL;


  /* Register Driver with with device layer */
  status = vmk_DriverRegister(&sfcDriverProps, &sfcModInfo.driverID);

  if (status == VMK_OK) {
    vmk_LogMessage("Initialization of SFC  driver successful");
  } else {
    vmk_LogMessage("Initialization of SFC driver failed:");
  }

  return status;
}

/*
 ***********************************************************************
 * SFC_NATIVE_DriverUnregister --    */ /**
 *
 * Function to unregister the device that was previous registered.
 *
 * Return values:
 *  VMK_OK
 *  VMK_FAILURE
 *
 ***********************************************************************
 */
void
esxi_sfc_DriverUnregister()
{
  vmk_DriverUnregister(sfcModInfo.driverID);
}

