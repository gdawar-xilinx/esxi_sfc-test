# Driver definition for native sfvmk driver.
#
# identification section
#

driver_name    = "sfvmk"
driver_ver     = "0.0.1.33"
driver_ver_hex = "0x" + ''.join('%02x' % int(i) for i in driver_ver.split('.'))

sfvmk_identification = {
   "name"            : driver_name,
   "module type"     : "device driver",
   "binary compat"   : "yes",
   "summary"         : "Network driver for solarflare Ethernet Controller",
   "description"     : "Native solarflare network driver for VMware ESXi",
   "version"         : driver_ver,
#   "version_bump"    : 1,
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
                         "sfvmk_ut.c",
                         "sfvmk_mcdi.c",
                         "sfvmk_ev.c",
                         "sfvmk_rx.c",
                         "sfvmk_tx.c",
                         "sfvmk_uplink.c",
                         "ef10_ev.c",
                         "ef10_filter.c",
                         "ef10_intr.c",
                         "ef10_mac.c",
                         "ef10_mcdi.c",
                         "ef10_nic.c",
                         "ef10_nvram.c",
                         "ef10_phy.c",
                         "ef10_rx.c",
                         "ef10_tx.c",
                         "ef10_vpd.c",
                         "efx_bootcfg.c",
                         "efx_crc32.c",
                         "efx_ev.c",
			 "efx_filter.c",
			 "efx_hash.c",
			 "efx_intr.c",
			 "efx_lic.c",
			 "efx_mac.c",
			 "efx_mcdi.c",
			 "efx_mon.c",
			 "efx_nvram.c",
			 "efx_phy.c",
			 "efx_port.c",
			 "efx_rx.c",
			 "efx_sram.c",
			 "efx_tx.c",
			 "efx_nic.c",
			 "efx_vpd.c",
			 "hunt_nic.c",
			 "mcdi_mon.c",
			 "medford_nic.c",
			 "siena_mac.c",
			 "siena_mcdi.c",
			 "siena_nic.c",
			 "siena_nvram.c",
			 "siena_phy.c",
			 "siena_sram.c",
			 "siena_vpd.c",
                       ],
   "cc warnings"     : [ "-Winline", ],
   "cc flags"        : [ ],
   "cc defs"         : { 'SFC_DRIVER_NAME=\\"%s\\"' % (driver_name)           : None,
                         'SFC_DRIVER_VERSION_STRING=\\"%s\\"' % (driver_ver)  : None,
                         'SFC_DRIVER_VERSION_NUM=%s' % (driver_ver_hex)       : None,
                         'SFC_ENABLE_DRIVER_STATS=1'                          : None,

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
                {'key': 'KB1234', 'link': 'http://vmware.com/kb/1234'},
                {'key': 'KB5678', 'link': 'http://vmware.com/kb/5678'}
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

       "maintenance-mode": True,

       "hwplatform":
             [
#                 {'model': 'model1', 'vendor': 'vendor1'},
#                 {'vendor': 'vendor2'},
#                 {'vendor': 'vendor3'}
             ],

      # Can use strings to define Boolean values  - see below
      "acceptance-level": 'certified',
      "live-install-allowed": False,
      "live-remove-allowed": False,
      "cimom-restart": False,
      "stateless-ready": True,
      "overlay": False,
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
