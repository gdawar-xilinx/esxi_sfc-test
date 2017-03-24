# Driver definition for native vmxnet3 driver.
#
# When developing a driver for release through the async program:
#  * set "vendor" to the name of your company
#  * set "license" to one of the VMK_MODULE_LICENSE_* strings if applicable;
#    otherwise, set it to a 1-word identification of your module's license
#  * set "vendor_code" to your company's VMware-assigned Vendor Code
#  * set "vendor_email" to the contact e-mail provided by your company
#  * increment the version number if the source has come from VMware
#  * remove "version_bump" if present
#
# When bringing an async driver inbox at VMware:
#  * leave "version" as is from the async release
#  * set "license" to VMK_MODULE_LICENSE_BSD (unquoted)
#  * set "version_bump" to 1
#  * set "vendor" to 'VMware, Inc.'
#  * set "vendor_code" to "VMW"
#  * set "vendor_email" to the VMware contact e-mail address
#
# If updating the driver at VMware:
#  * increment "version bump" or contact the IHV for a new version number
#
# If updating the driver at an async vendor:
#  * increment the version number (do not touch version bump)

#
# identification section
#

driver_name    = "sfc_native"
driver_ver     = "0.0.0.1"
driver_ver_hex = "0x" + ''.join('%02x' % int(i) for i in driver_ver.split('.'))

sfc_identification = {
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
   "identification"  : sfc_identification,
   "source files"    : [ "esxi_sfc_module.c",
                         "esxi_sfc_driver.c",
                       ],
   "cc warnings"     : [ "-Winline", ],
   "cc flags"        : [ ],
   "cc defs"         : { 'SFC_DRIVER_NAME=\\"%s\\"' % (driver_name)           : None,
                         'SFC_DRIVER_VERSION_STRING=\\"%s\\"' % (driver_ver)  : None,
                         'SFC_DRIVER_VERSION_NUM=%s' % (driver_ver_hex)       : None,
                         'SFC_ENABLE_DRIVER_STATS=1'                          : None,

                       },
}
sfc_module = defineKernelModule(module_def)
#
# Build the Driver's Device Definition
#
device_def = {
   "identification"  : sfc_identification,
   "device spec"     : "esxi_sfc_devices.py",
}
sfc_device_def = defineDeviceSpec(device_def)

#
# Build the VIB
#
sfc_vib_def = {
   "identification"  : sfc_identification,
   "payload"         : [ sfc_module,
                         sfc_device_def,
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

       "maintenance-mode": {'on-remove':True, 'on-install':'True'},

       "hwplatform":
             [
#                 {'model': 'model1', 'vendor': 'vendor1'},
#                 {'vendor': 'vendor2'},
#                 {'vendor': 'vendor3'}
             ],

      # Can use strings to define Boolean values  - see below
      "acceptance-level": 'certified',
      "live-install-allowed": True,
      "live-remove-allowed": 'True',
      "cimom-restart": False,
      "stateless-ready": 'True',
      "overlay": False,
   }
}
sfc_vib = defineModuleVib(sfc_vib_def)

#
# Build the Offline Bundle
#
sfc_bulletin_def = {
   "identification" : sfc_identification,
   "vib"            : sfc_vib,

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
sfc_bundle =  defineOfflineBundle(sfc_bulletin_def)
