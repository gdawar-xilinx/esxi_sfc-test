"""
This script copies bootrom, uefirom and firmware(medford onwards)
images as mentioned in the input firmwareFamilyVersion's ivy.xml file
to the output directory mentioned.
The output directory consists the following structure -
       <output Dir>/firmware/mcfw
       <output Dir>/firmware/sucfw
       <output Dir>/firmware/bootrom
       <output Dir>/firmware/uefirom

Additionally generates a JSON file with the input name<jsonfile>
in the output directory
       <output Dir>/fimware/<jsonFile>

The script copies the encode.py from v5 repository
v5/scripts/encode.py and uses it to create .dat bootom files
The script has been tested on Linux machines and is expected to
be executed in Linux environment.
"""

import json
import os
import sys
import optparse
import shutil
import ctypes
import getpass
import ConfigParser
import xml.etree.ElementTree as xmlparser

INI_IM_SECTION = 'image' # Section of INI file in which image details are stored
rc_elements = {}

class ImageOutputDir(object):
    """ Class stores the details of firmware variants """
    ivy_base_dir = "/project/ivy/solarflare/"
    vib_base_dir = "payload_data"
    encode_file_name = 'encode.py'

    def __init__(self, dir_arg):
        self.ivy_file = "ivys/ivy.xml"
        self.output_dir = dir_arg
        self.base_output_dir = dir_arg
        self.mcfw_dir = None
        self.sucfw_dir = None
        self.uefi_dir = None
        self.boot_dir = None
        self.bundle_dir = None

    def check_output_dir(self):
        """ Checks if the output directory exists. Prompts user for overwriting
            or creates the sub directrory"""
        try:
            if not os.path.exists(self.output_dir):
                print("Output Directory doesn't exists")
                return False
            self.output_dir = os.path.join(self.output_dir, "firmware")
            if os.path.exists(self.output_dir):
                shutil.rmtree(self.output_dir)
            os.mkdir(self.output_dir)
            return True
        except OSError:
            print("Fail to create output directory")
            raise

    def check_output_subdir(self):
        """ Checks if the output directory structure exists else creates
            them """
        try:
            self.mcfw_dir = os.path.join(self.output_dir, "mcfw")
            if not os.path.exists(self.mcfw_dir):
                os.mkdir(self.mcfw_dir)

            self.sucfw_dir = os.path.join(self.output_dir, "sucfw")
            if not os.path.exists(self.sucfw_dir):
                os.mkdir(self.sucfw_dir)

            self.uefi_dir = os.path.join(self.output_dir, "uefirom")
            if not os.path.exists(self.uefi_dir):
                os.mkdir(self.uefi_dir)

            self.boot_dir = os.path.join(self.output_dir, "bootrom")
            if not os.path.exists(self.boot_dir):
                os.mkdir(self.boot_dir)

            self.bundle_dir = os.path.join(self.output_dir, "bundle")
            if not os.path.exists(self.bundle_dir):
                os.mkdir(self.bundle_dir)
        except OSError:
            print("Fail to create output sub directory")
            raise


class JsonParsing(object):
    """ Class to encode json file """
    json_file_name = None
    create_json_file = 0
    mcfw_list = []
    bootrom_list = []
    uefirom_list = []
    sucfw_list = []
    bundle_list = []

    def __init__(self, file_name, flag):
        self.create_json_file = flag
        self.json_file_name = file_name

    def check_json_file(self, outdir_handle):
        """ Checks if the output Json file exists, if yes removes it
            and creates it"""
        try:
            self.json_file_name = os.path.join(outdir_handle.output_dir,
                                               self.json_file_name)
            if os.path.exists(self.json_file_name):
                print(self.json_file_name + " file exists. Over writing it")
                os.remove(self.json_file_name)
            filehandle = os.open(self.json_file_name, os.O_CREAT)
            os.close(filehandle)
        except OSError:
            print("Fail to check output json file")
            raise
        except IOError:
            print("Fail to create output json file")
            raise

    def create_image_metadata(self, nameval, firmwaretype, subtype,
                              versionstring, path):
        """ Returns a dictionary object containing an image metadata """
        imagedata = {'name':nameval, 'type':firmwaretype, 'subtype':subtype,
                     'versionString':versionstring, 'path':path}
        return imagedata

    def create_json_object(self, json_obj, firmware_type):
        """ Creates a Json Object with Image details and adds to image type
            file list """
        if firmware_type == 'mcfw':
            self.mcfw_list.append(json_obj)
        elif firmware_type == 'bootrom':
            self.bootrom_list.append(json_obj)
        elif firmware_type == 'uefirom':
            self.uefirom_list.append(json_obj)
        elif firmware_type == 'sucfw':
            self.sucfw_list.append(json_obj)
        elif firmware_type == 'bundle':
            self.bundle_list.append(json_obj)

    def writejsonfile(self):
        """ Creates the final JSON file """
        try:
            mcfw_file_dict = {'files' : self.mcfw_list}
            boot_file_dict = {'files' : self.bootrom_list}
            uefi_file_dict = {'files' : self.uefirom_list}
            sucfw_file_dict = {'files' : self.sucfw_list}
            bundle_file_dict = {'files' : self.bundle_list}
            json_file_dict = {'controller' : mcfw_file_dict,
                              'bootROM' : boot_file_dict,
                              'uefiROM' : uefi_file_dict,
                              'sucfw' : sucfw_file_dict,
                              'bundle' : bundle_file_dict}
            with open(self.json_file_name, 'a') as filehandle:
                json.dump(json_file_dict, filehandle)
        except:
            print("Fail to write output json file")
            raise


def fail(msg):
    """ Function prints the error message and exits """
    print(msg)
    sys.exit(1)

def create_json(name, rev, outdir_handle, json_handle, image_type, newfilename,
                destfilepath, fw_family_ver):
    """ Function creates a json object """
    basedirlen = len(outdir_handle.base_output_dir) - 1
    temppath = destfilepath[basedirlen:]
    jsonobj = json_handle.create_image_metadata(newfilename,
                                                image_type[0],
                                                image_type[1],
                                                rev,
                                                temppath)
    if name.find("mcfw") > -1:
        json_handle.create_json_object(jsonobj, "mcfw")
        jsonobj['firmwarefamily'] = fw_family_ver
    elif name.find("sucfw") > -1:
        json_handle.create_json_object(jsonobj, "sucfw")
    elif name.find("uefi") > -1:
        json_handle.create_json_object(jsonobj, "uefirom")
    elif name.find("bundle") > -1:
        json_handle.create_json_object(jsonobj, "bundle")
    else:
        json_handle.create_json_object(jsonobj, "bootrom")

def get_fw_family_ver(curr_dir, ivy_file, password, username, machinename):
    """ Get firmware family version and bundle dat files"""
    try:
        if os.name != 'nt':
            ret_val = os.system('scp -q ' + username + '@' + machinename
                                          + ":" + ivy_file + ' ' + curr_dir)
        else:
            password = getpass.getpass()
            ret_val = os.system('pscp -q -pw ' + password + ' ' + username + '@'
                                               + machinename + ":" + ivy_file
                                               + ' ' + curr_dir)
        if ret_val != 0:
            fail("Unable to get release_collection ivy_xml file. Exiting.")

        ivy_xml_file = curr_dir + '/ivy.xml'
        tree = xmlparser.parse(ivy_xml_file)
        root_node = tree.getroot()
        for child in root_node:
            if child.tag == "dependencies":
                for dependency in child.getchildren():
                    if((dependency.get('name')).find('firmwarefamily') > -1):
                        rc_elements['firmwarefamily'] = dependency.get('rev')
                    if((dependency.get('name')).find('bundle') > -1):
                        bundle_name = dependency.get('name')
                        rc_elements[bundle_name] = dependency.get('rev')

    except KeyError:
        fail("Image variant not determined. Exiting.")
    except OSError:
        fail("OS Environment error. Exiting.")
def get_dat_ini_file(name, rev, outdir_handle, json_handle, username, machinename, password):
    """ Using scp to get the dat and ini files from the repository"""
    try:
        ivydir = ImageOutputDir.ivy_base_dir + name + '/default/' + rev + '/bins/'
        if name.find("uefi") > -1:
            inidir = ImageOutputDir.ivy_base_dir + name + '/default/' + rev + '/includes/'
        else:
            inidir = ImageOutputDir.ivy_base_dir + name + '/default/' + rev + '/inis/'

        if(name.find('mcfw') > -1):
           dat_filename = "mcfw.dat"
           ini_filename = "mcfw.update.ini"
        elif(name.find('sucfw') > -1):
           dat_filename = "sucfw.dat"
           ini_filename = "sucfw.update.ini"
        elif(name.find('bundle') > -1):
           dat_filename = "bundle.dat"
           ini_filename = "bundle.update.ini"
        else:
           dat_filename = "SfcNicDriver.dat"
           ini_filename = "SfcNicDriver.rom.ini"

        datfilepath = os.path.join(ivydir, dat_filename)
        inifilepath = os.path.join(inidir, ini_filename)
        outdir = "./" + name

        if not os.path.exists(outdir):
               os.mkdir(outdir)

        if os.name != 'nt':
            ret_val = os.system('scp -q ' + username + '@' + machinename + ":" + datfilepath
                                              + ' ' + outdir)
            if ret_val != 0:
                fail("Unable to get dat file. Exiting.")

            ret_val = os.system('scp -q ' + username + '@' + machinename + ":" + inifilepath
                                              + ' ' + outdir)
            if ret_val != 0:
                fail("Unable to get ini file. Exiting.")

        else:
            ret_val = os.system('pscp -q -r -pw ' + password + ' ' + username + '@' + machinename +
                                                ":" + datfilepath + ' ' + outdir)
            if ret_val != 0:
                fail("Unable to get dat file. Exiting.")

            ret_val = os.system('pscp -q -r -pw ' + password + ' ' + username + '@' + machinename +
                                                 ":" + inifilepath + ' ' + outdir)
            if ret_val != 0:
               fail("Unable to get ini file. Exiting.")

    except KeyError:
        fail("Fail to get File. Exiting")
    except OSError:
        if name.find("mcfw") > -1:
            fail("Fail to get mcfw dat and ini file. Exiting")
        elif name.find("sucfw") > -1:
            fail("Fail to get sucfw dat and ini file. Exiting")
        elif name.find("bundle") > -1:
            fail("Fail to get bundle dat and ini file. Exiting")
        else:
            fail("Fail to get uefirom dat and ini file. Exiting")

def create_package(name, rev, outdir_handle, json_handle, image_type, fw_family_ver, datpath):
    """ Create package and JSON file """

    try:
        if(name.find('gpxe') > -1):
            outdir = outdir_handle.boot_dir
            dir, newfilename = datpath.split('/')
        else:
            if(name.find('mcfw') > -1):
                subtype_index = name.find('-')
                outdir = outdir_handle.mcfw_dir
            elif(name.find('sucfw') > -1):
                subtype_index = name.find('-')
                outdir = outdir_handle.sucfw_dir
            elif(name.find('bundle') > -1):
                subtype_index = name.find('-')
                outdir = outdir_handle.bundle_dir
            else:
                subtype_index = name.find('_')
                outdir = outdir_handle.uefi_dir
            if subtype_index == -1:
                fail('Cannot determine firmware subtype')
            else:
                subtype_index += 1
            subtype = name[subtype_index:]
            newfilename = subtype.upper() + '.dat'

        destfilepath = os.path.join(outdir, newfilename)
        shutil.copy(datpath, destfilepath)
        if json_handle.create_json_file == 1:
            create_json(name, rev, outdir_handle, json_handle,
                                       image_type, newfilename,
                                       destfilepath, fw_family_ver)
    except KeyError:
        fail("Fail to get Image details. Exiting")
    except IOError:
        fail("Not able to copy the image. Exiting")
    except OSError:
        fail("Fail to generate Image package. Exiting")

def parse_ini(filename, hardware_id, image_type):
  """ Read an INI file describing the characteristics of an image"""
  config = ConfigParser.ConfigParser()
  config.read(filename)
  if not config.has_section(INI_IM_SECTION):
    return None
  try:
    image_type[0] = config.getint(INI_IM_SECTION, 'type')
    image_type[1] = config.getint(INI_IM_SECTION, 'subtype')
    image_version = config.get(INI_IM_SECTION, 'version')
  except ValueError, e:
    print >>sys.stderr, e
    return None
  return image_version

def parse_fw_family_xml(filename, ivy_xml_file):
     """ Parsing the Fw_family xmlfile and returning the revison
        of the matched image name """

     tree = xmlparser.parse(ivy_xml_file)
     root_node = tree.getroot()
     for child in root_node:
         if child.tag == "dependencies":
             for dependency in child.getchildren():
                 if((dependency.get('name')) == filename):
                     return (dependency.get('rev'))
                 else:
                     continue
     return None

def pre_post_cleanup():
    """removing unwanted image files if any"""
    for firmware_dir in os.listdir("./"):
        if((firmware_dir.find("mcfw")) > -1 or
            (firmware_dir.find("sucfw")) > -1 or
            (firmware_dir.find("gpxe")) > -1 or
            (firmware_dir.find("uefi")) > -1 or
            (firmware_dir.find("bundle")) > -1):
            os.system('rm -rf ' + firmware_dir)
        else:
            continue

def get_bootrom_file(name, rev, inifile, outdir_handle, json_handle, encodefilepath,
                                username, machinename, password, datpath):
    """ Gets BOOTROM files from version specified in ivy.xml and
        copies them to output directory. Also creates a Json object of
        the image metadata. Support added for both medford and medford2
        boards """
    try:
        ivydir = ImageOutputDir.ivy_base_dir + name + '/default/' + rev + '/bins'
        inidir = ImageOutputDir.ivy_base_dir + name + '/default/' + rev + '/inis'
        outdir = "./" + name

        if not os.path.exists(outdir):
               os.mkdir(outdir)
        mrom_file = inifile.strip(".ini")
        mromfilepath = os.path.join(ivydir, mrom_file)
        inifilepath = os.path.join(inidir, inifile)
        bootrompath = os.path.join(outdir, mrom_file)
        if os.name != 'nt':
            ret_val = os.system('scp -q ' + username + '@' + machinename + ":" + mromfilepath
                                              + ' ' + outdir)
            if ret_val != 0:
                fail("Unable to get dat file. Exiting.")

            ret_val = os.system('scp -q ' + username + '@' + machinename + ":" + inifilepath
                                              + ' ' + outdir)
            if ret_val != 0:
                fail("Unable to get ini file. Exiting.")
        else:
            ret_val = os.system('pscp -q -r -pw ' + password + ' ' + username + '@' + machinename +
                                                 ":" + mromfilepath + ' ' + outdir)
            if ret_val != 0:
                fail("Unable to get dat file. Exiting.")

            ret_val = os.system('pscp -q -r -pw ' + password + ' ' + username + '@' + machinename +
                                                 ":" + inifilepath + ' ' + outdir)
            if ret_val != 0:
                fail("Unable to get ini file. Exiting.")

        ret_val = os.system("python "+ encodefilepath + " " + "--combo-hdr" +
                  " " + bootrompath + " " + datpath)
        if ret_val != 0:
            fail("Unable to create bootrom file. Exiting.")
    except KeyError:
        fail("Fail to get Bootrom Image details. Exiting")
    except OSError:
        fail("Fail to generate Bootrom dat Image. Exiting")

def parse_manifest_list(list_path, ivy_xml_file, encodefilepath,
                                   outdir_handle, json_handle,
                                   username, machinename,
                                   password, fw_family_ver):

  """ Parsing the manifest list supplied and creating the package """

  rc = 0
  image_type = (ctypes.c_uint32 * 2)()
  f = open(list_path, 'r')
  try:
    for line in f:
      if line.startswith(';'):
        continue
      arr = line.split()
      if len(arr) >= 2:
        # Check for input path modification
        if len(arr) > 2:
            ini_path = os.path.join(os.path.join("./", arr[2].strip()),arr[0].strip())
        else:
            ini_path = os.path.join("./",arr[0].strip())

        # Check for hardware_id qualification
        if len(arr) > 3:
            hardware_id = arr[3].strip()
        else:
            hardware_id = ""

        filename,inifile = arr[0].split("/")
        datpath = arr[1].strip()

        if((filename.find("bundle")) > -1):
            for bundle_name, version in rc_elements.items():
                if(bundle_name == filename):
                    get_dat_ini_file(filename,
                                     version,
                                     outdir_handle,
                                     json_handle,
                                     username,
                                     machinename,
                                     password)
        else:
            firmware_revision = parse_fw_family_xml(filename, ivy_xml_file)
            if firmware_revision is None:
                print("No firmware image found, Check the image name: ", filename)
                fail("Exiting")

            if((filename.find("gpxe")) > -1):
                get_bootrom_file(filename, firmware_revision, inifile,
                                 outdir_handle, json_handle, encodefilepath,
                                 username, machinename, password, datpath)
            else:
                get_dat_ini_file(filename,
                                 firmware_revision,
                                 outdir_handle,
                                 json_handle,
                                 username,
                                 machinename,
                                 password)

        version_info = parse_ini(ini_path, hardware_id, image_type)
        if version_info is None:
            print ('Warning: Missing firmware metadata: ', ini_path)
            fail("Exiting.")
        create_package(filename, version_info, outdir_handle,
                       json_handle, image_type, fw_family_ver,
                       datpath)
        rc = 1
  finally:
    f.close()
    return rc

def create_vib_of_all_images(vib_base_dir, outdir_handle, version_num):
    """ Creating a vib having all the firmware images"""
    try:
       opt_dir = vib_base_dir + "/opt"
       image_dir = opt_dir + '/sfc/'
       base_dir = outdir_handle.output_dir
       rev_container = "1.1.1.1000"
       if os.name == 'nt':
           fail(" This operation will be performed only on Development VM")
       else:
           if not os.path.exists(opt_dir):
               os.mkdir(opt_dir)
           if not os.path.exists(image_dir):
               os.mkdir(image_dir)
       ret_val = os.system("cp -r " + base_dir + " " + opt_dir + "/sfc/")
       if ret_val != 0:
           fail("Unable to copy firmware directory to create vib")
       ver = version_num.replace('-','.')
       f = open("Makefile.fwvib",'rt')
       out_file = open("Makefile",'wt')
       for line in f:
           out_file.write(line.replace(rev_container, ver))
       f.close()
       out_file.close()
       if os.name != 'nt':
           ret_val = os.system('make clean')
           if ret_val != 0:
               fail("Unable to make clean. Exiting.")
           ret_val = os.system('make')
           if ret_val != 0:
               fail("Unable to make vib file. Exiting.")

    except OSError:
        fail("OS Environment error. Exiting.")
    except IOError:
        fail("Image File creation failed. Exiting.")

def main():
    """ Starting point of script execution. Parses the inputs
        calls functions to copy images and generate json file. """
    try:
        parser = optparse.OptionParser(usage=
                                       """Usage: %prog [options]
                                          output_directory
                                          release collection version
                                          username
                                          machinename
                                          manifest_list""",
                                       version="%prog 1.1")
        parser.add_option("-t", "--tag", dest='v5_tag',
                          help="tag/branch to access and retrieve files")
        parser.add_option("-c", "--create_vib", dest='vib_author',
                          help="[true] create a vib having all the images")

        options, args = parser.parse_args()
        if len(args) < 5:
            parser.print_help()
            fail("Exiting")
        if options.v5_tag is None:
            options.v5_tag = 'default'
        ivy_rc_dir = "fw_release_collection/default/"
        fw_family_dir = "firmwarefamily/default/"
        encode_file_dir = '/hg/incoming/v5/rawfile/' + options.v5_tag
        encode_file_path = encode_file_dir + '/scripts/'
        encode_file_mc = 'http://source.uk.solarflarecom.com'
        encode_file_url = encode_file_mc + encode_file_path
        encode_file_url = encode_file_url + ImageOutputDir.encode_file_name
        outdir_handle = ImageOutputDir(args[0])
        if not outdir_handle.check_output_dir():
            fail('')
        outdir_handle.check_output_subdir()
        input_ivy_dir = args[1]
        username = args[2]
        machinename = args[3]
        manifest_list = args[4]
        curr_dir = os.getcwd()
        password = 'None'
        #pre cleanup
        pre_post_cleanup()
        if not input_ivy_dir.startswith('v'):
            version_num = input_ivy_dir
            input_ivy_dir = 'v' + input_ivy_dir
        else:
            version_num = input_ivy_dir[1:]
        if ((options.vib_author is None) or
            (options.vib_author.lower() == "true")):
            json_handle = JsonParsing('FirmwareMetadata.json', 1)
            json_handle.check_json_file(outdir_handle)
        else:
            parser.print_help()
            fail("Please provide correct value for -v ")
        ivydir = ImageOutputDir.ivy_base_dir + ivy_rc_dir
        fw_family_version = input_ivy_dir
        input_ivy_dir = ivydir + input_ivy_dir + "/"
        ivy_file = input_ivy_dir + outdir_handle.ivy_file
        #read release_collection ivy file
        get_fw_family_ver(curr_dir, ivy_file, password, username, machinename)
        fw_family_version = rc_elements.get('firmwarefamily')
        fwdir = ImageOutputDir.ivy_base_dir + fw_family_dir
        input_ivy_dir = fwdir + fw_family_version + "/"
        outdir_handle.ivy_file = input_ivy_dir + outdir_handle.ivy_file
        if os.name != 'nt':
            ret_val = os.system('scp -q ' + username + '@' + machinename
                                          + ":" + outdir_handle.ivy_file + ' ' + curr_dir)
        else:
            password = getpass.getpass()
            ret_val = os.system('pscp -q -pw ' + password + ' ' + username + '@'
                                               + machinename + ":" + outdir_handle.ivy_file
                                               + ' ' + curr_dir)
        if ret_val != 0:
            fail("Unable to get ivy_xml file. Exiting.")
       #getting encode.py from v5 repo
        encodefilepath = outdir_handle.base_output_dir + ImageOutputDir.encode_file_name
        if not os.path.exists(encodefilepath):
            ret_val = os.system('wget -q -P ' + outdir_handle.base_output_dir
                      + ' ' + encode_file_url)
            if ret_val != 0:
                fail("Unable to get encode file. Exiting.")
        ivy_xml_file = curr_dir + '/ivy.xml'
        manifest_list_path = "./" + manifest_list

        manifest_list_path = "./" + manifest_list

        #Parsing the manifest list and creating package
        rc = parse_manifest_list(manifest_list_path, ivy_xml_file, encodefilepath,
                                                     outdir_handle, json_handle,
                                                     username, machinename,
                                                     password, fw_family_version)

        os.remove(ivy_xml_file)
        os.remove(encodefilepath)

        if rc == 0:
            fail("No image Description found in manifest list. Exiting")

        if json_handle.create_json_file == 1:
            json_handle.writejsonfile()
        if ((options.vib_author is None) or
            (options.vib_author.lower() != 'true')):
            print('Not creating vib')
        else:
            if not os.path.exists(ImageOutputDir.vib_base_dir):
                print("vib base directory does not exist. Creating.....")
                ret_val = os.system('mkdir -p ' + ImageOutputDir.vib_base_dir)
                if ret_val != 0:
                    fail("Not able to create:",ImageOutputDir.vib_base_dir)
            create_vib_of_all_images(ImageOutputDir.vib_base_dir, outdir_handle, version_num)
            os.remove('Makefile')
        #post clean up
        pre_post_cleanup()
    except KeyError:
        fail("Image variant not determined. Exiting.")
    except OSError:
        fail("OS Environment error. Exiting.")
    except IOError:
        fail("Image File creation failed. Exiting.")

if __name__ == '__main__':
    main()
