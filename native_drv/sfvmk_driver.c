/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"

#ifdef SFVMK_WITH_UNIT_TESTS
extern void sfvmk_run_ut();
#endif

/* driver callback functions */
static VMK_ReturnStatus sfvmk_attachDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_detachDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_scanDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_quiesceDevice(vmk_Device device);
static VMK_ReturnStatus sfvmk_startDevice(vmk_Device device);
static void sfvmk_forgetDevice(vmk_Device device);

/************************************************************************
 * Device Driver Operations
 ************************************************************************/

static vmk_DriverOps sfvmk_DriverOps = {
   .attachDevice  = sfvmk_attachDevice,
   .detachDevice  = sfvmk_detachDevice,
   .scanDevice    = sfvmk_scanDevice,
   .quiesceDevice = sfvmk_quiesceDevice,
   .startDevice   = sfvmk_startDevice,
   .forgetDevice  = sfvmk_forgetDevice,
};

/*! \brief  Callback routine for the device layer to announce device to the
** driver.
**
** \param[in]  dev  pointer to device
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_attachDevice(vmk_Device dev)
{

  vmk_LogMessage("AttachDevice is invoked (updated: call sfvmk_CreateLock !");
#ifdef SFVMK_WITH_UNIT_TESTS
  sfvmk_run_ut();
#endif

  return VMK_OK;
}

/*! \brief: Callback routine for the device layer to notify the driver to
** bring up the specified device.
**
** \param[in]  dev  pointer to device
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_startDevice(vmk_Device dev)
{
  vmk_LogMessage("StartDevice is invoked!");
  return VMK_OK;
}

/*! \brief: Callback routine for the device layer to notify the driver to
**          scan for new device
**
** \param[in]  dev  pointer to vmkDevice
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_scanDevice(vmk_Device device)
{
  vmk_LogMessage("ScanDevice is invoked!");
  return VMK_OK;
}


/*! \brief : Callback routine for the device layer to notify the driver to
** release control of a driver.
**
** \param[in]  dev  pointer to vmkDevice
**
** \return: VMK_OK <success> error code <failure>
*/
static VMK_ReturnStatus
sfvmk_detachDevice(vmk_Device dev)
{
  vmk_LogMessage("DetachDevice is invoked!");

  return VMK_OK;
}

/*! \brief : Callback routine for the device layer to notify the driver to
** shutdown the specified device.
**
** \param[in]  dev  pointer to vmkDevice
**
** \return: VMK_OK or VMK_FAILURE
*/
static VMK_ReturnStatus
sfvmk_quiesceDevice(vmk_Device dev)
{
  vmk_LogMessage("QuiesceDevice/drv_DeviceShutdown is invoked!");
  return VMK_OK;
}

/*! \brief: Callback routine for the device layer to notify the driver the
** specified device is not responsive.
**
** \param[in]  dev  pointer to vmkDevice
**
** \return: VMK_OK or VMK_FAILURE
*/
static void
sfvmk_forgetDevice(vmk_Device dev)
{
  vmk_LogMessage("ForgetDevice is invoked!");
  return;
}

/*! \brief: This function registers the driver as network driver
**
** \param[in] : None
**
** \return: VMK_OK or VMK_FAILURE
*/
VMK_ReturnStatus
sfvmk_driverRegister(void)
{
  VMK_ReturnStatus status;
  vmk_DriverProps driverProps;

  /* Populate driverProps */
  driverProps.moduleID = vmk_ModuleCurrentID;
  vmk_NameCopy(&driverProps.name, &sfvmk_modInfo.driverName);
  driverProps.ops = &sfvmk_DriverOps;
  driverProps.privateData = (vmk_AddrCookie)NULL;


  /* Register Driver with the device layer */
  status = vmk_DriverRegister(&driverProps, &sfvmk_modInfo.driverID);

  if (status == VMK_OK) {
    vmk_LogMessage("Initialization of SFC  driver successful");
  } else {
    vmk_LogMessage("Initialization of SFC driver failed with status: %d", status);
  }

  return status;
}

/*! \brief: Function to unregister the device that was previous registered.
**
** \param[in] : None
**
** \return: VMK_OK or VMK_FAILURE
*/
void
sfvmk_driverUnregister(void)
{
  vmk_DriverUnregister(sfvmk_modInfo.driverID);
}

