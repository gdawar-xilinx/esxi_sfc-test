#!/usr/bin/python
#**********************************************
# Reporting tool for Solarflare under ESXi
# Usage: python sfreport.py
# Output html: sfreport-<date-time>.html
# Copyright Solarflare Communications 2017
#***********************************************
import os
import io
import subprocess
import re
import sys
import optparse
from threading import Timer

#- function to terminate a process
def terminate(process,timeout,cmd):
    timeout["value"] = True
    process.kill()
    print("ERROR: Following command took longer than expected:\n%s",cmd)
    return

#- function to execute the command on the cmdline using subprocess.
def execute(cmd):
    timeout_sec = 5
    output_list = []
    process = subprocess.Popen((cmd), stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
    timeout = {"value": False}
    timer = Timer(timeout_sec, terminate, [process, timeout, cmd])
    timer.start()
    out,err = process.communicate()
    timer.cancel()

    if err:
       print("ERROR:",err)
       print("Error while running command:",cmd)
       return
    elif out:
       out = (out).decode('ascii')
       for line in out:
           output_list.append(line)
    #- convert list into string.
    output = ''.join(output_list)
    return output

def file_header(output_file):
    user = execute('echo $USER')
    output_file.write('<HTML><HEAD>')
    output_file.write('<TITLE>Solarflare system report</TITLE></HEAD>')
    output_file.write('<h1 style="font-size:36px;"> SOLARFLARE System Report</H1>')
    output_file.write(current_time)
    user = "(User : " + user + " )"
    output_file.write(user)
    output_file.write('<h1 <br></H1>')
    uptime = execute('uptime')
    output_file.write(uptime)
    return 0

#- function to fetch system information.
def system_summary(output_file):
    system_values = []
    system_summary_header = ['OS Name','Version','Architecture','Kernel Command Line','Distribution','System Name',\
                             'System Manufacturer','System Model','Processor','BIOS Version/Date','Total Physical Memory',\
                             'Available Physical Memory','Total Virtual Memory','Available Virtual Memory','Page File Space']
    system_values.append(execute("uname -o"))  # OS Name
    system_values.append(execute("uname -v"))  # Version
    system_values.append(execute("uname -i"))  # Architecture
    system_values.append("??")  # Kernel Command Line
    system_values.append(execute("vmware --version"))  # Distribution
    system_values.append(execute("uname -n"))  # SystemName
    # Capture the logs specific to system
    system_manufacturer = execute("smbiosDump | grep -A4  'System Info'")
    try:
       manufacturer_name = re.search('(.*)Manufacturer\:\s(.*)Product\:\s(.*)(\n)', system_manufacturer, re.DOTALL)
       system_values.append(manufacturer_name.group(2))  # System Manufacturer
       system_values.append(manufacturer_name.group(3))  # System Model
    except Exception:
       print("ERROR parsing system manufacturer information output:",manufacturer_name)
       return 

    # Capture the logs specific to Processor
    processor_info = execute("smbiosDump | grep -A20  'Processor Info: #1'")
    try:
       processor_info = re.search('(.*)Version\:\s(.*)Processor(.*)Core\sCount\:\s(\W+\d+)(.*)Thread\sCount\:\s(\W+\d+)',
                                    processor_info, re.DOTALL)
       processor_version = processor_info.group(2) # Processor version
       processor_core_count = processor_info.group(4) #Processor core count
       processor_thread_count = processor_info.group(6) #Processor thread count
    except Exception:
       print("ERROR parsing processor_info output:",processor_info)
       return 
    processor_info = processor_version + ",Core(s)" + processor_core_count + ",Logical Processor(s)" + processor_thread_count
    system_values.append(processor_info)
    # Capture the logs specific to BIOS
    bios_info = execute("smbiosDump | grep -A4  'BIOS Info'")
    try:
       bios_info = re.search('(.*)Vendor\:\s(.*)Version\:\s(.*)Date\:\s(.*)', bios_info, re.DOTALL)
       vendor = bios_info.group(2) #BIOS vendor
       version = bios_info.group(3) #BIOS version
       date = bios_info.group(4) #BIOS date
    except Exception:
       print("ERROR parsing bios_info output:",bios_info)
       return 
    bios_info = vendor + ',' + version + ',' + date
    system_values.append(bios_info)

    #create html table for all system_summary_header elements.
    output_file.write('<h1 style="font-size:26px;"> System Summary: </H1>')
    table = '<table><table border="1">'
    for key, value in zip(system_summary_header, system_values):
        table += '<tr><th> %s' % key + '</th><td> %s </td><tr>' % value
    table += '</table>'
    output_file.write(table)
    return 0

#- start here
if __name__=="__main__":
    #- Proceed only if any SF adapters are found.
    sf_adapters = execute("lspci | grep 'Solarflare'")
    if sf_adapters:
       current_time = execute('date +"%Y-%m-%d-%H-%M-%S"')
       html_file = 'sfreport-' + current_time.rstrip() + '.html'
       output_file = open(html_file, 'w')
       file_header(output_file)
       system_summary(output_file)
       print("Generated output file:",html_file)
    else:
       print("No SolarFlare Adapters were found")
       sys.exit()
