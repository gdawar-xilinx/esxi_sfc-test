#*************************************************************************
# Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
# Use is subject to license terms.
#
# -- Solarflare Confidential
#************************************************************************/

# Driver definition for native sfvmk driver.
#
# identification section
#

driver_name    = "sfvmk"
driver_ver     = "0.0.0.2"
driver_ver_hex = "0x" + ''.join('%02x' % int(i) for i in driver_ver.split('.'))

sfvmk_identification = {
   "name"            : driver_name,
   "module type"     : "device driver",
   "binary compat"   : "yes",
   "summary"         : "Network driver for Solarflare Ethernet Controller",
   "description"     : "Native Network Driver for Solarflare SFC9240 Controller",
   "version"         : driver_ver,
   "license"         : VMK_MODULE_LICENSE_BSD,
   "vendor"          : "Solarflare",
   "vendor_code"     : "SFC",
   "vendor_email"    : "support@solarflare.com",
}
#
# Build the Driver Module
#
module_def = {
   "identification"  : sfvmk_identification,
   "source files"    : [ "sfvmk_module.c",
                         "sfvmk_driver.c",
                         "sfvmk_mcdi.c",
                         "sfvmk_intr.c",
                         "sfvmk_ev.c",
                         "sfvmk_tx.c",
                         "sfvmk_rx.c",
                         "sfvmk_uplink.c",
                         "sfvmk_port.c",
                         "sfvmk_utils.c",
                         "sfvmk_ut.c",
                         "imported/ef10_ev.c",
                         "imported/ef10_filter.c",
                         "imported/ef10_intr.c",
                         "imported/ef10_mac.c",
                         "imported/ef10_mcdi.c",
                         "imported/ef10_nic.c",
                         "imported/ef10_nvram.c",
                         "imported/ef10_phy.c",
                         "imported/ef10_rx.c",
                         "imported/ef10_tx.c",
                         "imported/ef10_vpd.c",
                         "imported/efx_bootcfg.c",
                         "imported/efx_crc32.c",
                         "imported/efx_ev.c",
			 "imported/efx_filter.c",
			 "imported/efx_hash.c",
			 "imported/efx_intr.c",
			 "imported/efx_lic.c",
			 "imported/efx_mac.c",
			 "imported/efx_mcdi.c",
			 "imported/efx_mon.c",
			 "imported/efx_nvram.c",
			 "imported/efx_phy.c",
			 "imported/efx_port.c",
			 "imported/efx_rx.c",
			 "imported/efx_sram.c",
			 "imported/efx_tx.c",
			 "imported/efx_nic.c",
			 "imported/efx_vpd.c",
			 "imported/hunt_nic.c",
			 "imported/mcdi_mon.c",
			 "imported/medford_nic.c",
                       ],
   "includes"        : [
                         "./imported",
                       ],
   "cc warnings"     : [ "-Winline", ],
   "cc flags"        : [ ],
   "cc defs"         : { 'SFC_DRIVER_NAME=\\"%s\\"' % (driver_name)           : None,
                         'SFC_DRIVER_VERSION_STRING=\\"%s\\"' % (driver_ver)  : None,
                         'SFC_DRIVER_VERSION_NUM=%s' % (driver_ver_hex)       : None,

                       },
}
sfvmk_module = defineKernelModule(module_def)
#
# Build the Driver's Device Definition
#
device_def = {
   "identification"  : sfvmk_identification,
   "device spec"     : "sfvmk_devices.py",
}
sfvmk_device_def = defineDeviceSpec(device_def)

#
# Build the VIB
#
sfvmk_vib_def = {
   "identification"  : sfvmk_identification,
   "payload"         : [ sfvmk_module,
                         sfvmk_device_def,
                       ],
   "vib properties"  : {
      "urls":[
#                {'key': 'KB1234', 'link': 'http://vmware.com/kb/1234'},
#                {'key': 'KB5678', 'link': 'http://vmware.com/kb/5678'}
             ],

      "provides":
             [
#                 {'name': 'ABCD', 'version': '123.456'},
#                 {'name': 'XYZ'}
             ],
      "depends":
             [
                  {'name': 'vmkapi_2_2_0_0'},
             ],
      "conflicts":
             [
#                 {'name': 'FirstConflict'},
#                 {'name': 'SecondConflict','relation': '>>','version': '1.2.3.4'}
             ],

       "maintenance-mode": {'on-remove':True, 'on-install':'True'},

       "hwplatform":
             [
#                 {'model': 'model1', 'vendor': 'vendor1'},
#                 {'vendor': 'vendor2'},
#                 {'vendor': 'vendor3'}
             ],

      # Can use strings to define Boolean values  - see below
      "acceptance-level"	: 'certified',
      # Determines if a live install is permitted
      "live-install-allowed"	: True,
      # Determines if a live remove is permitted
      "live-remove-allowed"	: True,
      "cimom-restart"		: False,
      "stateless-ready"		: True,
      "overlay"			: False,
   }
}
sfvmk_vib = defineModuleVib(sfvmk_vib_def)

#
# Build the Offline Bundle
#
sfvmk_bulletin_def = {
   "identification" : sfvmk_identification,
   "vib"            : sfvmk_vib,

   "bulletin" : {
      # These elements show the default values for the corresponding items in bulletin.xml file
      # Uncomment a line if you need to use a different value
      #'severity'    : 'general',
      #'category'    : 'Enhancement',
      #'releaseType' : 'extension',
      #'urgency'     : 'Important',

      'kbUrl'       : 'http://kb.vmware.com/kb/example.html',

      # 1. At least one target platform needs to be specified with 'productLineID'
      # 2. The product version number may be specified explicitly, like 7.8.9,
      # or, when it's None or skipped, be a default one for the devkit
      # 3. 'locale' element is optional
      'platforms'   : [ {'productLineID':'ESXi'},
      #                 {'productLineID':'ESXi', 'version':"7.8.9", 'locale':''}
                      ]
   }
}
sfvmk_bundle =  defineOfflineBundle(sfvmk_bulletin_def)
