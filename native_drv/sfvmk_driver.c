/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk.h"

static VMK_ReturnStatus sfvmk_AttachDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_DetachDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_ScanDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_ShutdownDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_StartDevice(vmk_Device device);
static void sfvmk_ForgetDevice(vmk_Device device);

#ifdef SFVMK_WITH_UNIT_TESTS
extern void sfvmk_run_ut();
#endif

/************************************************************************
 * Device Driver Operations
 ************************************************************************/

static vmk_DriverOps sfvmk_DriverOps = {
   .attachDevice  = sfvmk_AttachDevice,
   .detachDevice  = sfvmk_DetachDevice,
   .scanDevice    = sfvmk_ScanDevice,
   .quiesceDevice = sfvmk_ShutdownDevice,
   .startDevice   = sfvmk_StartDevice,
   .forgetDevice  = sfvmk_ForgetDevice,
};

/************************************************************************
 * sfvmk_AttachDevice --
 *
 * @brief  Callback routine for the device layer to announce device to the
 * driver.
 *
 * @param  dev	pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_AttachDevice(vmk_Device dev)
{

  vmk_LogMessage("AttachDevice is invoked (updated: call sfvmk_CreateLock !");
#ifdef SFVMK_WITH_UNIT_TESTS
  sfvmk_run_ut();
#endif

  return VMK_OK;
}

/************************************************************************
 * sfvmk_StartDevice --
 *
 * @brief: Callback routine for the device layer to notify the driver to
 * bring up the specified device.
 *
 * @param  dev	pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_StartDevice(vmk_Device dev)
{
  vmk_LogMessage("StartDevice is invoked!");
  return VMK_OK;
}

/************************************************************************
 * sfvmk_ScanDevice --
 *
 * Callback routine for the device layer to notify the driver to scan
 * for new devices.
 *
 * @param  dev	pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_ScanDevice(vmk_Device device)
{
  vmk_LogMessage("ScanDevice is invoked!");
  return VMK_OK;
}

/************************************************************************
 * sfvmk_DetachDevice --
 *
 * @brief : Callback routine for the device layer to notify the driver to
 * release control of a driver.
 *
 * @param  dev	pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/

static VMK_ReturnStatus
sfvmk_DetachDevice(vmk_Device dev)
{
  vmk_LogMessage("DetachDevice is invoked!");

  return VMK_OK;
}

/************************************************************************
 * sfvmk_ShutdownDevice --
 *
 * @brief : Callback routine for the device layer to notify the driver to
 * shutdown the specified device.
 *
 * @param  dev	pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static VMK_ReturnStatus
sfvmk_ShutdownDevice(vmk_Device dev)
{
  vmk_LogMessage("QuiesceDevice/drv_DeviceShutdown is invoked!");
  return VMK_OK;
}

/************************************************************************
 * sfvmk_ForgetDevice --
 *
 * @brief: Callback routine for the device layer to notify the driver the
 * specified device is not responsive.
 *
 * @param  dev	pointer to vmkDevice
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
static void
sfvmk_ForgetDevice(vmk_Device dev)
{
  vmk_LogMessage("ForgetDevice is invoked!");
  return;
}

/************************************************************************
 * sfvmk_DriverRegister --
 *
 * @brief: This function registers the driver as network driver
 *
 * @param : None
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
VMK_ReturnStatus
sfvmk_DriverRegister(void)
{
  VMK_ReturnStatus status;
  vmk_DriverProps driverProps;

  /* Populate driverProps */
  driverProps.moduleID = vmk_ModuleCurrentID;
  vmk_NameCopy(&driverProps.name, &sfvmk_ModInfo.driverName);
  driverProps.ops = &sfvmk_DriverOps;
  driverProps.privateData = (vmk_AddrCookie)NULL;


  /* Register Driver with the device layer */
  status = vmk_DriverRegister(&driverProps, &sfvmk_ModInfo.driverID);

  if (status == VMK_OK) {
    vmk_LogMessage("Initialization of SFC  driver successful");
  } else {
    vmk_LogMessage("Initialization of SFC driver failed with status: %d", status);
  }

  return status;
}

/************************************************************************
 * sfvmk_DriverUnregister --
 *
 * @brief: Function to unregister the device that was previous registered.
 *
 * @param : None
 *
 * @return: VMK_OK or VMK_FAILURE
 *
 ************************************************************************/
void
sfvmk_DriverUnregister(void)
{
  vmk_DriverUnregister(sfvmk_ModInfo.driverID);
}

