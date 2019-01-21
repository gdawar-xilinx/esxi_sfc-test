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
import xml.etree.ElementTree as xmlparser

if os.name == 'nt':
    _check_image = ctypes.CDLL('libfetch_image.dll')
else:
    _check_image = ctypes.cdll.LoadLibrary('./libfetch_image.so')
_check_image.check_reflash_image.argtypes = (ctypes.c_void_p, ctypes.c_uint32,
                                                              ctypes.POINTER(ctypes.c_uint32))
_check_image.check_reflash_image.restypes = (ctypes.c_int)

class ImageOutputDir(object):
    """ Class stores the details of firmware variants """
    ivy_base_dir = "/project/ivy/solarflare/"
    vib_base_dir = "/root/stagedir/payloads/payload_data"
    encode_file_name = 'encode.py'

    def __init__(self, dir_arg):
        self.ivy_file = "ivys/ivy.xml"
        self.output_dir = dir_arg
        self.base_output_dir = dir_arg
        self.mcfw_dir = None
        self.sucfw_dir = None
        self.uefi_dir = None
        self.boot_dir = None

    def check_output_dir(self):
        """ Checks if the output directory exists. Prompts user for overwriting
            or creates the sub directrory"""
        try:
            if not os.path.exists(self.output_dir):
                print("Output Directory doesn't exists")
                return False
            modified_dir = os.path.join(self.output_dir, "firmware")
            if os.path.exists(modified_dir):
                print("Firmware sub directory present in:" + self.output_dir)
                overwrite = raw_input("Do you want to Overwrite it ?[y/n]")
                if not overwrite == 'y':
                    print("ERROR: Cannot Overwrite " + modified_dir + " Exiting")
                    return False
                else:
                    self.output_dir = os.path.join(self.output_dir, "firmware")
            else:
                self.output_dir = os.path.join(self.output_dir, "firmware")
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

    def writejsonfile(self):
        """ Creates the final JSON file """
        try:
            mcfw_file_dict = {'files' : self.mcfw_list}
            boot_file_dict = {'files' : self.bootrom_list}
            uefi_file_dict = {'files' : self.uefirom_list}
            sucfw_file_dict = {'files' : self.sucfw_list}
            json_file_dict = {'controller' : mcfw_file_dict,
                              'bootROM' : boot_file_dict,
                              'uefiROM' : uefi_file_dict,
                              'sucfw' : sucfw_file_dict}
            with open(self.json_file_name, 'a') as filehandle:
                json.dump(json_file_dict, filehandle)
        except:
            print("Fail to write output json file")
            raise


def fail(msg):
    """ Function prints the error message and exits """
    print(msg)
    sys.exit(1)

def read_image_hdr(destfilepath, image_type):
    """ Function checks the image header for type and subtype value """
    file_size = os.path.getsize(destfilepath)
    fd = open(destfilepath,'rb')
    data = fd.read()
    result = _check_image.check_reflash_image(data, file_size, image_type)
    fd.close()
    return result

def create_json(name, rev, outdir_handle, json_handle, image_type, newfilename, destfilepath):
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
    elif name.find("sucfw") > -1:
        json_handle.create_json_object(jsonobj, "sucfw")
    elif name.find("uefi") > -1:
        json_handle.create_json_object(jsonobj, "uefirom")
    else:
        json_handle.create_json_object(jsonobj, "bootrom")

def get_dat_file(name, ivydir, outdir, image_type, username, machinename, password):
    """ Using scp to get the dat image files from the repository"""
    try:
        if(name.find('mcfw') > -1):
           subtype_index = name.find('-')
           filename = "mcfw.dat"
        elif(name.find('sucfw') > -1):
           subtype_index = name.find('-')
           filename = "sucfw.dat"
        else:
           subtype_index = name.find('_')
           filename = "SfcNicDriver.dat"
        if subtype_index == -1:
            fail('Cannot determine firmware subtype')
        else:
            subtype_index += 1
        subtype = name[subtype_index:]
        newfilename = subtype.upper() + '.dat'
        srcfilepath = os.path.join(ivydir, filename)
        destfilepath = os.path.join(outdir, newfilename)
        image_file_path = outdir + '/' + filename

        if os.name != 'nt':
            ret_val = os.system('scp -q ' + username + '@' + machinename + ":" + srcfilepath
                                          + ' ' + outdir)
        else:
            ret_val = os.system('pscp -q -pw ' + password + ' ' + username + '@'
                                               + machinename + ":" + srcfilepath + ' '
                                               + outdir)
        if ret_val != 0:
            fail("Unable to get dat file. Exiting.")
        shutil.move(image_file_path, destfilepath)
        result = read_image_hdr(destfilepath, image_type)
        if result != 0:
            fail('Not able to Check MCFW image',result)
    except KeyError:
        fail("Fail to get Image details. Exiting")
    except OSError:
        if name.find("mcfw") > -1:
            fail("Fail to generate mcfw dat Image. Exiting")
        elif name.find("sucfw") > -1:
            fail("Fail to generate sucfw dat Image. Exiting")
        else:
            fail("Fail to generate uefirom dat Image. Exiting")
    return newfilename

def get_mc_uefi_file(name, rev, outdir_handle, json_handle, username, machinename, password):
    """ Gets MCFW and UEFIROM files from version specified in ivy.xml
        and copies them to output directory. Also creates a Json object
        of the image metadata """
    try:
        ivydir = ImageOutputDir.ivy_base_dir + name + '/default/' + rev + '/bins/'
        image_type = (ctypes.c_uint32 * 2)()
        if name.find("mcfw") > -1:
            outdir = outdir_handle.mcfw_dir
            newfilename = get_dat_file(name, ivydir, outdir, image_type,
                                                   username, machinename, password)
            print "{ FIRMWARE_MCFW, " + str(image_type[1]) + ", " + \
                                           "\"" + newfilename + "\"" + " },"
        elif name.find("sucfw") > -1:
            outdir = outdir_handle.sucfw_dir
            newfilename = get_dat_file(name, ivydir, outdir, image_type,
                                                  username, machinename, password)
            print "{ FIRMWARE_SUCFW, " + str(image_type[1]) + ", " + \
                                             "\"" + newfilename + "\"" + " },"
        else:
            outdir = outdir_handle.uefi_dir
            newfilename = get_dat_file(name, ivydir, outdir, image_type,
                                                  username, machinename, password)
            print "{ FIRMWARE_UEFIROM, " + str(image_type[1]) + ", " + \
                                              "\"" + newfilename + "\"" + " },"
        destfilepath = os.path.join(outdir, newfilename)
        if json_handle.create_json_file == 1:
            create_json(name, rev, outdir_handle, json_handle,
                                       image_type, newfilename, destfilepath)
    except KeyError:
        fail("Fail to get Image details. Exiting")
    except OSError:
        if name.find("mcfw") > -1:
            fail("Fail to generate mcfw dat Image. Exiting")
        elif name.find("sucfw") > -1:
            fail("Fail to generate sucfw dat Image. Exiting")
        else:
            fail("Fail to generate uefirom dat Image. Exiting")

def get_bootrom_file(name, rev, outdir_handle, json_handle,
                                username, machinename, password):
    """ Gets BOOTROM files from version specified in ivy.xml and
        copies them to output directory. Also creates a Json object of
        the image metadata. Support added for both medford and medford2
        boards """
    try:
        ivydir = ImageOutputDir.ivy_base_dir + name + '/default/' + rev + '/bins'
        curr_dir = os.getcwd()
        ini_dir = curr_dir + '/inis'
        mrom_dir = curr_dir + '/bins'
        medford_bootrom_file = 'SFC9220.mrom'
        medford_ini_file = 'SFC9220.mrom.ini'
        medford_bootrom_dat_file = 'SFC9220.dat'
        medford2_bootrom_file = 'SFC9250.mrom'
        medford2_ini_file = 'SFC9250.mrom.ini'
        medford2_bootrom_dat_file = 'SFC9250.dat'
        if os.name != 'nt':
            ret_val = os.system('scp -qr ' + username + '@' + machinename + ":"
                                           + ivydir + ' ' + curr_dir)
        else:
            ret_val = os.system('pscp -q -r -pw ' + password + ' ' + username + '@'
                                                  + machinename + ":" + ivydir + ' ' + curr_dir)

        if ret_val != 0:
            fail("Unable to get ini files. Exiting.")
        for updatefile in os.listdir(mrom_dir):
            if (updatefile.endswith(medford_bootrom_file)or
                    updatefile.endswith(medford2_bootrom_file)):
                inifilefound = 0
                if updatefile.endswith(medford_bootrom_file):
                    inifilename = medford_ini_file
                    datfilename = medford_bootrom_dat_file
                else:
                    inifilename = medford2_ini_file
                    datfilename = medford2_bootrom_dat_file
                srcfilepath = os.path.join(mrom_dir, updatefile)
                destfilepath = os.path.join(outdir_handle.base_output_dir,
                                            updatefile)
                shutil.copy(srcfilepath, destfilepath)
                inidir = ImageOutputDir.ivy_base_dir + name + '/default/' + rev + '/inis'
                if os.name != 'nt':
                    ret_val = os.system('scp -qr ' + username + '@' + machinename + ":"
                                                   + inidir + ' ' + curr_dir)
                else:
                    ret_val = os.system('pscp -q -r -pw ' + password + ' ' + username + '@'
                                                          + machinename + ":" + inidir + ' ' + curr_dir)

                if ret_val != 0:
                    fail("Unable to get ini files. Exiting.")
                for inifile in os.listdir(ini_dir):
                    if inifile.endswith(inifilename):
                        srcfilepath = os.path.join(ini_dir, inifile)
                        destinifilepath = os.path.join(outdir_handle.
                                                       base_output_dir, inifile)
                        shutil.copy(srcfilepath, destinifilepath)
                        inifilefound = 1
                        break
                if inifilefound == 1:
                    bootromfile = os.path.join(outdir_handle.boot_dir, datfilename)
                    encodefilepath = os.path.join(outdir_handle.base_output_dir,
                                                  ImageOutputDir.encode_file_name)
                    ret_val = os.system("python "+ encodefilepath + " " +
                              destfilepath + " " + bootromfile)
                    if ret_val != 0:
                        fail("Unable to crate bootrom file. Exiting.")
                        continue
                    image_type = (ctypes.c_uint32 * 2)()
                    result = read_image_hdr(bootromfile, image_type)
                    if result != 0:
                        fail('Not able to Check BOOTROM image',result)

                    print("{ FIRMWARE_BOOTROM, " + str(image_type[1]) + ", " + \
                                               "\"" + datfilename + "\"" + " },")
                    if json_handle.create_json_file == 1:
                        create_json(name, rev, outdir_handle, json_handle,
                                                   image_type, datfilename, bootromfile)
                    os.remove(destfilepath)
                    os.remove(destinifilepath)
                else:
                    fail("Cannot Create BOOTROM dat file")
        shutil.rmtree(mrom_dir)
        shutil.rmtree(ini_dir)
    except KeyError:
        fail("Fail to get Bootrom Image details. Exiting")
    except OSError:
        fail("Fail to generate Bootrom dat Image. Exiting")

def create_vib_of_all_images(vib_base_dir, outdir_handle):
    """ Creating a vib having all the firmware images"""
    try:
       opt_dir = vib_base_dir + "/opt"
       image_dir = opt_dir + '/sfc/'
       base_dir = outdir_handle.output_dir
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
       ret_val = os.system("vibauthor -C -t /root/stagedir -v fw_images.vib -f --force")
       if ret_val != 0:
           fail("Unable to create vib: Exiting.")
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
                                          firmware_family_version
                                          username
                                          machinename""",
                                       version="%prog 1.1")
        parser.add_option("-t", "--tag", dest='v5_tag',
                          help="tag/branch to access and retrieve files")
        parser.add_option("-c", "--create_vib", dest='vib_author',
                          help="[true] create a vib having all the images")

        options, args = parser.parse_args()
        if len(args) < 4:
            parser.print_help()
            fail("Exiting")
        if options.v5_tag is None:
            options.v5_tag = 'default'
        ivy_family_dir = "firmwarefamily/default/"
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
        curr_dir = os.getcwd()
        password = 'None'
        if not input_ivy_dir.startswith('v'):
            input_ivy_dir = 'v' + input_ivy_dir
        if ((options.vib_author is None) or
            (options.vib_author.lower() == "true")):
            json_handle = JsonParsing('FirmwareMetadata.json', 1)
            json_handle.check_json_file(outdir_handle)
        else:
            parser.print_help()
            fail("Please provide correct value for -v ")
        ivydir = ImageOutputDir.ivy_base_dir + ivy_family_dir
        input_ivy_dir = ivydir + input_ivy_dir + "/"
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
        tree = xmlparser.parse(ivy_xml_file)
        root_node = tree.getroot()
        for child in root_node:
            if child.tag == "dependencies":
                for dependency in child.getchildren():
                    if((dependency.get('name')).find('mcfw') > -1 or
                       (dependency.get('name')).find('uefi') > -1 or
                       (dependency.get('name')).find('sucfw') > -1):
                        if((dependency.get('conf')).find('siena') > -1 or
                            (dependency.get('conf')).find('huntington') > -1 or
		            (dependency.get('name')).find('aarch') > -1 or
		            (dependency.get('name')).find('siena') > -1 or
		            (dependency.get('name')).find('huntington') > -1):
                            continue
                        else:
                            get_mc_uefi_file(dependency.get('name'),
                                             dependency.get('rev'),
                                             outdir_handle,
                                             json_handle,
                                             username,
                                             machinename,
                                             password)
                    elif((dependency.get('name')).find('gpxe') > -1):
                        get_bootrom_file(dependency.get('name'),
                                         dependency.get('rev'),
                                         outdir_handle,
                                         json_handle,
                                         username,
                                         machinename,
                                         password)
        os.remove(ivy_xml_file)
        os.remove(encodefilepath)
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
            create_vib_of_all_images(ImageOutputDir.vib_base_dir, outdir_handle)
    except KeyError:
        fail("Image variant not determined. Exiting.")
    except OSError:
        fail("OS Environment error. Exiting.")
    except IOError:
        fail("Image File creation failed. Exiting.")

if __name__ == '__main__':
    main()
