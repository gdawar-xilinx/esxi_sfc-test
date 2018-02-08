#!/usr/bin/python
#**********************************************
# Reporting tool for Solarflare under ESXi
# Usage: python sfreport.py <options>
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
from collections import defaultdict
from operator import itemgetter

# sfreport version to be incremented for any changes made before releases:
# major minor build
sfreport_version = "v0.1.0"

#- function to terminate a process
def terminate(process,timeout,cmd):
    timeout["value"] = True
    process.kill()
    print("ERROR: Following command took longer than expected:\n%s" +cmd)
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
       return out
    else:
       return 1

def file_header(output_file,current_time,mode,sf_ver):
    output_file.write('<HTML><HEAD>')
    output_file.write('<TITLE>Solarflare system report</TITLE></HEAD>')
    output_file.write('<h1 style="font-size:36px;"> Solarflare System Report (version: %s)</H1>'% sf_ver)
    output_file.write("Time:"+current_time)
    output_file.write("</br>Report generation: " + mode+"</br>")
    if mode != "vcli":
       user = (execute('echo $USER'))
       user = "(User : " + user + " )"
       output_file.write(user)
       output_file.write('<h1 <br></H1>')
       output_file.write('<h1 style="font-size:26px;"> System uptime:</H1>')
       uptime = (execute('uptime'))
       output_file.write(uptime)
    return 0

#- function to fetch system information.
def system_summary(output_file,server,mode):
    system_values = []
    if mode == "esxi":
       system_summary_header = ['OS Name','Version','Architecture','Kernel Command Line','Distribution',\
                                'System Name','System Manufacturer','System Model','Processor',\
                                'BIOS Version/Date','Total Physical Memory','Available Physical Memory',\
                                'Total Virtual Memory','Available Virtual Memory','Page File Space']
       system_values.append(execute("uname -o"))  # OS Name
       system_values.append(execute("uname -v"))  # Version
       system_values.append(execute("uname -i"))  # Architecture
       system_values.append("")  # Kernel Command Line
       system_values.append(execute("vmware --version"))  # Distribution
       system_values.append(execute("uname -n"))  # SystemName
       # Capture the logs specific to system
       system_manufacturer = execute("smbiosDump | grep -A4  'System Info'")
       try:
          manufacturer_name = re.search('(.*)Manufacturer\:\s(.*)Product\:\s(.*)(\n)',\
                                          system_manufacturer,re.DOTALL)
          system_values.append(manufacturer_name.group(2))  # System Manufacturer
          system_values.append(manufacturer_name.group(3))  # System Model
       except Exception:
          system_values.append("not updated")
          system_values.append("not updated")

       # Capture the logs specific to Processor
       processor_info = execute("smbiosDump | grep -m1 -A20 'Processor Info:'")
       try:
          processor_info = re.search('(.*)Version\:(.*)Processor',processor_info,re.DOTALL)
          processor_version = processor_info.group(2) #Processor version
          system_values.append(processor_version)
       except Exception:
          system_values.append("not updated")
       # Capture the logs specific to BIOS
       bios_info = execute("smbiosDump | grep -A4  'BIOS Info'")
       try:
          bios_info = re.search('(.*)Vendor\:\s(.*)Version\:\s(.*)Date\:\s(.*)',\
                                  bios_info, re.DOTALL)
          vendor = bios_info.group(2) #BIOS vendor
          version = bios_info.group(3) #BIOS version
          date = bios_info.group(4) #BIOS date
       except Exception:
          vendor = "not updated"
          version = "not updated"
          date = "not updated"

       bios_info = vendor + ',' + version + ',' + date
       system_values.append(bios_info)
    elif mode == "vcli":
         system_summary_header = ['System Name','System Manufacturer','System Model','Processor',\
                                  'CPU Cores','Total Physical Memory','VMotion Enabled',\
                                  'In Maintenance Mode','Last Boot Time','OS Name','Version',\
                                  'Build','Update','Patch']
         host_info_cmd = "vicfg-hostops  "+ server +" -o info"
         host_info = execute(host_info_cmd)
         for line in host_info.split('\n'):
             if line != "":
                try:
                   line = re.search('(.*)\:\s+(.*)',line)
                   system_values.append(line.group(2))
                except Exception:
                   system_values.append("not updated")
         system_info_cmd = "esxcli "+ server + " system version get"
         system_info = execute(system_info_cmd)
         for line in system_info.split('\n'):
             if line != "":
                try:
                   line = re.search('(.*)\:\s+(.*)', line)
                   system_values.append(line.group(2))
                except Exception:
                   system_values.append("not updated")

    #create html table for all system_summary_header elements.
    output_file.write('<h1 id="System Summary"style="font-size:26px;"> System Summary: </H1>')
    table = '<table><table border="1">'
    for key, value in zip(system_summary_header, system_values):
        table += '<tr><th align=left> %s' % key + '</th><td> %s </td><tr>' % value
    table += '</table>'
    output_file.write(table)

    return 0

#- function to fetch driver specific information
def driver_binding(output_file,server,mode):
    output_file.write('<h1 id="Driver Bindings"style="font-size:26px;"> Driver Bindings: <br></H1>')
    cmd = 'esxcli ' + server + ' network nic list |grep sfvmk'
    sf_nw_list = execute(cmd)
    sf_nw_list = sf_nw_list.split('\n')
    html = '<table><table border="1">'
    for hdr in ('address','interface','driver_name'):
        html += '<th>%s' % hdr + '</th>'
    for line in sf_nw_list:
        if line != "":
           try:
              line = re.search('(\w+)\s+(\w+\:\w+\:\w+\.\w+)\s+(\w+)',line)
              interface = line.group(1)
              tlp = line.group(2)
              driver_name = line.group(3)
           except Exception:
              interface = "not updated"
              tlp = "not updated"
              driver_name = "not updated"
           html += '</tr><td>%s' % tlp + '<td>%s' % interface + '<td>%s' % driver_name
    html += '</table>'
    output_file.write(html)
    return 0

#- function to fetch sfvmk specific vmkernel logs [dmesg]
def vmkernel_logs(output_file):
    output_file.write('<h1 id="VMkernel logs"style="font-size:26px;"> VMkernel logs: <br></H1>')
    dmesg_logs = execute('dmesg -c |grep sfvmk')
    lines = ('<p>')
    if dmesg_logs == 1:
       lines += 'No logs specific to sfvmk were found' 
       output_file.write('%s</p>'%lines)
       return
    dmesg = dmesg_logs.split('\n')
    for line in dmesg:
        lines += '<small>%s</small><br>'%line
    output_file.write('%s</p>'%lines)
    return 0

#- function to fetch the parameter list of sfvmk driver.
def sfvmk_parameter_info(output_file,sfvmk_adapters,mode):
    output_file.write('<h1 id="SFVMK Parameters"style="font-size:26px;"> Parameters of sfvmk : <br></H1>')
    # intrpt_html table for interrupt moderation settings.
    intrpt_html = '<table><th>Interrupt moderation table</th></tr><table border="1">'
    intrpt_mod_hdr_list = ['NIC','rx_microsecs','rx_max_frames','tx_microsecs',\
                           'tx_max_frames','adaptive_rx','adaptive_tx',\
                           'sample_interval_secs']
    intrpt_val_list = []
    for hdr in intrpt_mod_hdr_list:
        intrpt_html += '<th>%s' % hdr + '</th>'
    intrpt_html += '</tr>'
    # rxtx_ring_html table for rx/tx ring settings.
    rxtx_ring_html = '<table><th>RX_TX Ring/TSO-CSO table</th></tr><table border="1">'
    rxtx_ring_hdr_list = ['NIC', 'RX', 'RX_Mini', 'RX_Jumbo', 'TX','TSO','CSO']
    rxtx_ring_val_list = []
    for hdr in rxtx_ring_hdr_list:
        rxtx_ring_html += '<th>%s' % hdr + '</th>'
    rxtx_ring_html += '</tr>'
    # Pause Params settings
    pause_params_html = '<table><th>Pause parameters table</th></tr><table border="1">'
    pause_params_hdr_list = ['NIC','Pause_Support','Pause_RX','Pause_TX','Auto_Neg',\
                             'Auto_Neg_Res_Avail','RX_Auto_Neg_Res','RX_Auto_Neg_Res']
    pause_params_val_list = []
    for hdr in pause_params_hdr_list:
        pause_params_html += '<th>%s' % hdr + '</th>'
    pause_params_html += '</tr>'
    # fetch Pause Params settings.
    get_pause_cmd = "esxcli "+ server + " network nic pauseParams list"
    get_pause_settings = execute(get_pause_cmd)
    get_pause_settings = get_pause_settings.split('\n')
    # Queue count configurations
    queue_count_html = '<table><th>Queue Count table</th></tr><table border="1">'
    queue_hdr_list = ['NIC','Tx_netqueue_count','Rx_netqueue_count']
    queue_val_list = []
    queue_count_cmd = "esxcli "+ server + " network nic queue count get"
    queue_count_config = execute(queue_count_cmd)
    queue_count_config = queue_count_config.split('\n')
    for hdr in queue_hdr_list:
        queue_count_html += '<th>%s' % hdr + '</th>'
    queue_count_html += '</tr>'

    # fetch the sfvmk parameters for all sfvmk adapters.
    for interface in sfvmk_adapters:
        # fetch interface specific interrupt mod settings
        get_interrupt_cmd = "esxcli "+ server + " network nic coalesce get -n " + interface
        get_interrupt_params = execute(get_interrupt_cmd)
        get_interrupt_params = get_interrupt_params.split('\n')
        # fetch interface specific rx/tx ring settings
        get_ring_cmd =  "esxcli "+ server + " network nic ring current get -n " + interface
        get_ring_values = execute(get_ring_cmd)
        get_ring_values = get_ring_values.split('\n')
        # fetch tso settings.
        get_tso_cmd = "esxcli "+ server + " network nic tso get -n " + interface
        get_tso_values = execute(get_tso_cmd)
        get_tso_values = get_tso_values.split('\n')
        # fetch cso settings.
        get_cso_cmd = "esxcli "+ server + " network nic cso get -n " + interface
        get_cso_values = execute(get_cso_cmd)
        get_cso_values = get_cso_values.split('\n')

        for line in get_interrupt_params:
            if not line.startswith("---") and line.startswith(interface):
               intrpt_val_list = line.split(" ")
               for val in intrpt_val_list:
                   if val != "":
                       intrpt_html += '<td>%s' % val
            intrpt_html += '</tr>'
        rxtx_ring_html += '<td>%s' % interface
        for line in get_ring_values:
            if line != "":
               try:
                  line = re.search('(.*)\:\s(\d+)',line)
                  ring_val = line.group(2)
               except Exception:
                  ring_val = "not updated"
               rxtx_ring_html += '<td>%s' % ring_val
        for tso,cso in zip(get_tso_values,get_cso_values):
            if (not tso.startswith("---") and tso.startswith(interface)) and \
               (not tso.startswith("---") and tso.startswith(interface)):
               try:
                  tso = re.search('(\w+)\s+(\w+)', tso)
                  tso_val = tso.group(2)
               except Exception:
                  tso_val = "not updated"
               try:
                  cso = re.search('(\w+)\s+(\w+)', cso)
                  cso_val = cso.group(2)
               except Exception:
                  cso_val = "not udpated"
               rxtx_ring_html += '<td>%s' % tso_val + '<td>%s' % cso_val
        rxtx_ring_html += '</tr>'
        for line in get_pause_settings:
            if not line.startswith("---") and line.startswith(interface):
               pause_val_list = line.split(" ")
               for val in pause_val_list:
                   if val != "":
                      pause_params_html += '<td>%s' % val
            pause_params_html += '</tr>'
        for line in queue_count_config:
            if not line.startswith("---") and line.startswith(interface):
               queue_count_val = line.split(" ")
               for val in queue_count_val:
                   if val != "":
                      queue_count_html += '<td>%s' % val
               queue_count_html += '</tr>'

    intrpt_html += '</table>'
    rxtx_ring_html += '</table>'
    pause_params_html += '</table>'
    queue_count_html += '</table>'
    output_file.write(intrpt_html)
    output_file.write(rxtx_ring_html)
    output_file.write(pause_params_html)
    output_file.write(queue_count_html)

#- function to fetch sf pci info
def sf_pci_devices(output_file,sfvmk_tlp_list,server):
    output_file.write('<h1 id="SF PCI devices"style="font-size:26px;"> Solarflare PCI devices: <br></H1>')
    tlp_dict = defaultdict(list)
    tlp_count = 0
    #create html table for all system_summary_header elements.
    table = '<table><table border="1">'
    for tlp in sfvmk_tlp_list:
        sf_tlp_cmd = "esxcli "+ server + " hardware pci list |grep -A30 " + tlp
        sf_tlp_info = execute(sf_tlp_cmd)
        for line in sf_tlp_info.split('\n'):
             if line != "" and line != tlp:
                try:
                   line = re.search('(.*)\:\s+(.*)', line)
                   tlp_param =(line.group(1))
                   tlp_val =(line.group(2))
                except Exception:
                   tlp_param = "not updated"
                   tlp_val = "not updated"
                if tlp_count == 0:
                   if tlp_param not in tlp_dict:
                      tlp_dict[tlp_param] = []
                   tlp_dict[tlp_param].append(tlp_val)
                else:
                   tlp_dict[tlp_param].append(tlp_val)
        tlp_count += 1
    tlp_dict = sorted(tlp_dict.items())
    for key,values in tlp_dict:
        table += '<th align=left>%s</th>' % key
        value_len = len(values)
        for value in values:
            table += '<td> %s</td>'% value
        table += '</tr>'
    table += '</table>'
    output_file.write(table)
    return 0

#- function to fetch PCI configuration space dump
def pci_configuration_space(output_file):
    output_file.write('<h1 id="PCI configuration"style="font-size:26px;"> PCI configuration space: <br></H1>')
    dmesg = subprocess.Popen(["lspci -d"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
    lines = ('<p>')
    for line in io.TextIOWrapper(dmesg.stdout, encoding="utf-8"):
        lines += '<small>%s</small><br>'%line
    output_file.write('%s</p>'%lines)
    return 0

#- function to fetch the solarflare specific kernel modules on the server
def sf_kernel_modules(output_file):
    output_file.write('<h1 id="Known Kernel Modules"style="font-size:26px;"> Known kernel modules for Solarflare PCI IDs: <br></H1>')
    kernel_modules = execute('lspci -p |grep sfvmk')
    kernel_modules = kernel_modules.split('\n')
    html = '<table><table border="1">'
    for hdr in ('module_name','pci address','vendor','device','subvendor','subdevice','class','driver_data'):
        html += '<th>%s' % hdr + '</th>'
    for line in kernel_modules:
        if line != "":
           try:
              line = re.search('(\w+\:\w+\:\w+\.\w+)\s(\w+)\:(\w+)\s(\w+)\:(\w+)(.*)',line)
              module_name = 'sfvmk'
              tlp = line.group(1)
              vendor = line.group(2)
              device = line.group(3)
              subvendor = line.group(4)
              subdevice = line.group(5)
           except Exception:
              module_name = 'sfvmk'
              tlp = "not updated"
              vendor = "not updated"
              device = "not updated"
              subvendor = "not updated"
              subdevice = "not updated"
           html += '</tr><td>%s' % module_name + '<td>%s' % tlp + '<td>%s' %vendor + '<td>%s' % device + '<td>%s' % subvendor + '<td>%s' % subdevice
    html += '</table>'
    output_file.write(html)
    return 0

# function to fetch network configuration informations:
def network_configuration(output_file,server):
    output_file.write('<h1 id="Network Configurations"style="font-size:26px;"> Network Configurations: <br></H1>')
    nw_config_cmd = "esxcli "+ server + " network nic list"
    nw_config_val = execute(nw_config_cmd)
    nw_config_val = nw_config_val.split('\n')
    table = '<table><table border="1">'
    for hdr in ('Name','PCI dev','Driver','Admin Status','Link Status','Speed','Duplex'\
                 ,'Mac Address','MTU','Description'):
        table += '<th>%s' % hdr + '</th>'
    for line in nw_config_val:
        if line != "" and not line.startswith("---") and not line.startswith("Name"):
           try:
              line = re.search('(\w+)\s+(\w+\:\w+\:\w+\.\w+)\s+(\w+)\s+(\w+)\s+(\w+)\s+(\w+)\s+(\w+)\s+(\w+\:\w+\:\w+\:\w+\:\w+\:\w+)\s+(\w+)\s+(.*)',line)
              name = line.group(1)
              pci_id = line.group(2)
              driver = line.group(3)
              admin_stat = line.group(4)
              link_stat = line.group(5)
              speed = line.group(6)
              duplex = line.group(7)
              mac = line.group(8)
              mtu = line.group(9)
              detail = line.group(10)
           except Exception:
              name = "not updated"
              pci_id = "not updated"
              driver = "not updated"
              admin_stat = "not updated"
              link_stat = "not updated"
              speed = "not updated"
              duplex = "not updated"
              mac = "not updated"
              mtu = "not updated"
              detail = "not updated"
           table += '</tr><td>%s'%name  +'<td>%s'%pci_id +'<td>%s'%driver +'<td>%s'%admin_stat +'<td>%s'%link_stat +'<td>%s'%speed +'<td>%s'%duplex +'<td>%s'%mac +'<td>%s'%mtu +'<td>%s'%detail
    table += '</table>'
    output_file.write(table)
    ip_list = ['v4','v6']
    for ip_type in ip_list:
        ipv_ip_cmd = "esxcli "+ server + " network ip interface ip"+ ip_type+" address list"
        ipv_ip_info = execute(ipv_ip_cmd)
        ipv_ip_info = ipv_ip_info.split('\n')
        lines = ('<p>')
        if ip_type == "v4":
           ip4_table = '<table style="font-size:18px;"><th>IP%s configurations: </th></tr><table border="1">'% ip_type
           hdr_list = ('Interface','IPV4 address','IPv4 netmask','IPv4Bcast address','Type','Gateway','DHCP DNS')
           for hdr in hdr_list:
               ip4_table += '<th>%s' % hdr + '</th>'
        elif ip_type == "v6":
           ip6_table = '<table style="font-size:18px;"><th>IP%s configurations: </th></tr><table border="1">' % ip_type
           hdr_list = ('Interface','Address','Netmask','Type','Status')
           for hdr in hdr_list:
               ip6_table += '<th>%s' % hdr + '</th>'


        for line in ipv_ip_info:
          if line != "" and not line.startswith("---") and not line.startswith("Name")and not line.startswith("Interface"):
            if ip_type == "v4":
               try:
                  line = re.search('(\w+)\s+(\d+\.\d+\.\d+\.\d+)\s+(\d+\.\d+\.\d+\.\d+)\s+(\d+\.\d+\.\d+\.\d+)\s+(\w+)\s+(\d+\.\d+\.\d+\.\d+)\s+(\w+)',line)
                  interface = line.group(1)
                  ip_addr = line.group(2)
                  netmask = line.group(3)
                  bcast_ip = line.group(4)
                  type = line.group(5)
                  gateway = line.group(6)
                  dhcp = line.group(7)
               except:
                  interface = "not updated"
                  ip_addr = "not updated"
                  netmask = "not updated"
                  bcast_ip = "not updated"
                  type = "not updated"
                  gateway = "not updated"
                  dhcp = "not updated"
               ip4_table += '</tr><td>%s'%interface +'<td>%s'%ip_addr +'<td>%s'%netmask +'<td>%s'%bcast_ip +'<td>%s'%type +'<td>%s'%gateway +'<td>%s'%dhcp
            elif ip_type == "v6":
               line = re.search('(\w+)\s+(\w+\:\:\w+\:\w+\:\w+\:\w+)\s+(\w+)\s+(\w+)\s+(.*)',line)
               interface = line.group(1)
               address = line.group(2)
               netmask = line.group(3)
               type = line.group(4)
               status = line.group(5)
               ip6_table += '</tr><td>%s' % interface + '<td>%s' % address + '<td>%s' % netmask + '<td>%s' % type + '<td>%s' % status

    ip4_table += '</table>'
    ip6_table += '</table>'
    output_file.write(ip4_table)
    output_file.write(ip6_table)
    route_table = '<table><table border="1">'
    output_file.write('<h1 style="font-size:26px;"> IPv4 Routing Table: <br></H1>')
    ipv4_route_cmd = "esxcli "+ server + " network ip route ipv4 list"
    ipv4_route_info = execute(ipv4_route_cmd)
    ipv4_route_info = ipv4_route_info.split('\n')
    for hdr in ('Network','Netmask','Gateway','Interface','Source'):
        route_table += '<th>%s' % hdr + '</th>'

    for line in ipv4_route_info:
        if line != "" and not line.startswith("---") and not line.startswith("Network"):
           try:
              line = re.search('(.*)\s+(\d+\.\d+\.\d+\.\d+)\s+(\d+\.\d+\.\d+\.\d+)\s+(\w+)\s+(\w+)',line)
              network = line.group(1)
              netmask = line.group(2)
              gateway = line.group(3)
              interface = line.group(4)
              source = line.group(5)
           except Exception:
              network = "not updated"
              netmask = "not updated"
              gateway = "not updated"
              interface = "not updated"
              source = "not updated"
           route_table += '</tr><td>%s'%network +'<td>%s'%netmask +'<td>%s'%gateway +'<td>%s'%interface +'<td>%s'%source
    route_table += '</table>'
    output_file.write(route_table)
    return 0

def ethernet_settings(output_file,sfvmk_adapter_list,server):
    for interface in sfvmk_adapter_list:
        output_file.write('<h1 id="Ethernet Settings"style="font-size:26px;"> Ethernet Settings for %s: <br></H1>' % interface)
        nic_info_cmd = "esxcli " + server + " network nic get -n " + interface
        nic_info = execute(nic_info_cmd)
        nic_info = nic_info.split('\n')
        lines = ('<p>')
        for line in nic_info:
            lines += '%s<br>' % line
        output_file.write('%s</p>' % lines)
        ring_info_cmd = "esxcli " + server + " network nic ring current get -n " + interface
        ring_info = execute(ring_info_cmd)
        ring_info = ring_info.split('\n')
        lines = ('<p>')
        output_file.write('<h1 style="font-size:18px;"> Rx/Tx Ring configurations for %s: </H1>'% interface)
        for line in ring_info:
            lines += '%s<br>' % line
        output_file.write('%s</p>' % lines)
    return

def interface_statistics(output_file,sfvmk_adapter_list,server,mode):
    output_file.write('<h1 id="Interface Statistics"style="font-size:26px;"> Interface Statistics: <br></H1>')
    stats_dict = defaultdict(list)
    interface_count = 1
    table = '<table><table border="1">'
    table += '<th>Interface_name</th>'
    no_of_interfaces = len(sfvmk_adapter_list)
    for interface in sfvmk_adapter_list:
        table += '<td> %s </td>'% interface
        if interface_count == no_of_interfaces:
           table += '</tr>'
        stats_cmd = "esxcli " + server + " network nic stats get -n " + interface
        stats_values = execute(stats_cmd)
        for line in stats_values.split('\n'):
            if not line.startswith("NIC") and line != "":
               try:
                  line = re.search('(.*)\:\s+(.*)', line)
                  param_name = line.group(1)
                  param_value = line.group(2)
               except Exception:
                  param_name = "not updated"
                  param_value = "not updated"
               if interface_count == 1:
                  if param_name not in stats_dict:
                     stats_dict[param_name] = []
                     stats_dict[param_name].append(param_value)
               else:
                  stats_dict[param_name].append(param_value)
        interface_count += 1
    stats_dict = sorted(stats_dict.items())
    for key,values in stats_dict:
        table += '<th align=left>%s</th>' % key
        for value in values:
            table += '<td> %s </td>'% value
        table += '</tr>'
    table += '</table>'
    output_file.write(table)
    # fetch the private stats for the sf adapters
    if mode == "esxi":
       interface_count = 1
       priv_table = '<table><th>Private Statistics:</th></tr><table border="1">'
       priv_table += '<th>Interface_name</th>'

       priv_stats_dict = defaultdict(list)
       #output_file.write('<h1 style="font-size:18px;"> Private Statistics: <br></H1>')
       for interface in sfvmk_adapter_list:
           priv_table += '<td> %s </td>' % interface
           if interface_count == no_of_interfaces:
               priv_table += '</tr>'
           private_stats_cmd = "vsish -e cat /net/pNics/"+ interface+"/stats"
           private_stats_values = execute(private_stats_cmd)
           for line in private_stats_values.split('\n'):
               if not "device {" in line and line != "" and not "--" in line \
                  and not "}" in line:
                   try:
                      line = re.search('(.*)\:(.*)', line)
                      param_name = line.group(1)
                      param_value = line.group(2)
                   except Exception:
                      param_name = "not updated"
                      param_value = "not updated"
                   if interface_count == 1:
                       if param_name not in priv_stats_dict:
                           priv_stats_dict[param_name] = []
                           priv_stats_dict[param_name].append(param_value)
                   else:
                       priv_stats_dict[param_name].append(param_value)
           interface_count += 1
       priv_stats_dict = sorted(priv_stats_dict.items())
       for key, values in priv_stats_dict:
           priv_table += '<th align=left>%s</th>' % key
           for value in values:
               priv_table += '<td> %s </td>' % value
           priv_table += '</tr>'
       priv_table += '</table>'
       output_file.write(priv_table)
    return 0

def sf_module_file(output_file):
    lines = ('<p>')
    output_file.write('<h1 id="module file"style="font-size:26px;"> SF Module file names : <br></H1>')
    module_cmd = "vmkload_mod -s sfvmk"
    module_info = execute(module_cmd)
    module_info = module_info.split('\n')
    for line in module_info:
        lines += '<small>%s</small><br>' % line
    output_file.write('%s</p>' % lines)
    return 0

#- function to retrieve info on Netqueue status
def net_queue_status(output_file,sfvmk_adapter_list):
    output_file.write('<h1 id="NetQueue Status"style="font-size:26px;"> NetQueue Status:<br></H1>')
    for interface in sfvmk_adapter_list:
        lines = ('<p>')
        output_file.write('<h1"style="font-size:18px;"> Network queue status of %s: <br></H1>' % interface)
        netq_cmd = "vsish -e cat /net/pNics/"+ interface +"/txqueues/queues/0/stats"
        netq_status = execute(netq_cmd)
        netq_status = netq_status.split('\n')
        for line in netq_status:
            lines += '<small>%s</small><br>' % line
        output_file.write('%s</p>' % lines)
    return 0

#- function to get file properties of SF drivers
def file_properties(output_file,server):
    file_cmd = "esxcli "+ server + " software vib get"
    file_status = execute(file_cmd)
    file_status = re.search('(.*)Name\:\ssfvmk(.*)Payloads\:\ssfvmk',file_status,re.DOTALL)
    sfvmk_info = file_status.group(2)
    table = '<table id="File Properties"style="font-size:26px;"><th>Driver File properties:</th><table border="1">'
    for line in sfvmk_info.split('\n'):
        if len(line.strip()) != 0:
           try:
              line = re.search('(.*)\:(.*)',line)
              param_name = line.group(1)
              param_val = line.group(2)
           except Exception:
              param_name = "not updated"
              param_val = "not updated"
           table += '<tr><th align=left> %s' % param_name + '</th><td> %s </td><tr>' % param_val
    table += '</table>'
    output_file.write(table)
    return 0

#- function to fetch arp information.
def arp_cache(output_file,server):
    table = '<table id="ARP Cache"style="font-size:26px;"><th>ARP cache:</th><table border="1">'
    for hdr in ('Neighbor', 'MAC Address','Vmknic','Expiry','State/Type'):
        table += '<th>%s' % hdr + '</th>'
    arp_cmd = "esxcli " + server + " network ip neighbor list"
    arp_info = execute(arp_cmd)
    for line in arp_info.split('\n'):
        if line != "" and not line.startswith("---") and not line.startswith("Neighbor"):
           try:
              line = re.search('(\d+\.\d+\.\d+\.\d+)\s+(\w+\:\w+\:\w+\:\w+\:\w+\:\w+)\s+(\w+)\s+(\w+\s\w+)\s+(.*)',line)
              neighbor = line.group(1)
              mac_addr = line.group(2)
              vmknic = line.group(3)
              expiry = line.group(4)
              state = line.group(5)
           except Exception:
              neighbor = "not updated"
              mac_addr = "not updated"
              vmknic = "not updated"
              expiry = "not updated"
              state = "not updated"
           table += '</tr><td>%s'%neighbor +'<td>%s'%mac_addr +'<td>%s'%vmknic +'<td>%s'%expiry +'<td>%s'%state
    table += '</table>'
    output_file.write(table)
    return 0

#- funtion to fetch Virtual Machine information on the host.
def virtual_machine_info(output_file,server):
    table = '<table id="Virtual Machine"style="font-size:26px;"><th>Virtual Machine Information:</th><table border="1">'
    vm_cmd = "esxcli " + server + " network vm list"
    for hdr in ('World ID','Name','Num Ports','Networks'):
        table += '<th>%s' % hdr + '</th>'
    vm_info = execute(vm_cmd)
    if vm_info == 1:
       table = '<table id="Virtual Machine"style="font-size:26px;"><th>Virtual Machine Information:</th><table border="1">'
       table += "INFO:No Active Virtual Machines found.."
       output_file.write(table)
       return
    for line in vm_info.split('\n'):
        if line != "" and not line.startswith("---") and not line.startswith("World"):
           try:
              line = re.search('(\d+)\s+(\w+)\s+(\d+)\s+(.*)',line)
              world_id = line.group(1)
              vm_name = line.group(2)
              num_port = line.group(3)
              network = line.group(4)
           except Exception:
              world_id = "not updated"
              vm_name = "not updated"
              num_port = "not updated"
              network = "not updated"
           table +=  '</tr><td>%s'%world_id + '<td>%s'%vm_name + '<td>%s'%num_port + '<td>%s'%network
    table += '</table>'
    output_file.write(table)
    return 0


#- function to fetch Vswitch portgroup informations.
def portgroup_details(output_file,server):
    table = '<table id="Portgroup Information"style="font-size:26px;"><th>Portgroup Information:</th><table border="1">'
    pg_cmd = "esxcli " + server + " network vswitch standard portgroup list"
    for hdr in ('Name','vSwitch','Active_Clients','VLAN-id'):
        table += '<th>%s' % hdr + '</th>'
    pg_info = execute(pg_cmd)
    for line in pg_info.split('\n'):
        if line != "" and not line.startswith("---") and not line.startswith("Name"):
           try:
              line = re.search('(.*)\s+(\w+)\s+(\d+)\s+(\d+)',line)
              name = line.group(1)
              vswitch = line.group(2)
              active_client = line.group(3)
              vlan_id = line.group(4)
           except Exception:
              name = "not updated"
              vswitch = "not updated"
              active_client = "not updated"
              vlan_id = "not updated"
           table += '</tr><td>%s' % name + '<td>%s' % vswitch + '<td>%s' % active_client + '<td>%s' % vlan_id
    table += '</table>'
    output_file.write(table)
    return 0

#- function to fetch Vswitch portgroup informations.
def vswitch_details(output_file,server):
    output_file.write('<h1 id="vSwitch"style="font-size:26px;">vSwitch Information: <br></H1>')
    vswitch_cmd = "esxcli " + server + " network vswitch standard list"
    vswitch_info = execute(vswitch_cmd)
    vswitch_info = vswitch_info.split('\n')
    lines = ('<p>')
    for line in vswitch_info:
        lines += '<small>%s</small><br>' % line
    output_file.write('%s</p>' % lines)
    output_file.write('<h1 style="font-size:26px;">Distributed vSwitch Information: <br></H1>')
    dvs_cmd = "esxcli " + server + " network vswitch dvs vmware list"
    dvs_info = execute(dvs_cmd)
    if dvs_info == 1:
       output_file.write("INFO:No Distributed vSwitches were found")
       return
    dvs_info = dvs_info.split('\n')
    lines = ('<p>')
    for line in dvs_info:
        lines += '<small>%s</small><br>' % line
    output_file.write('%s</p>' % lines)
    return 0

#- function to fetch the memory information.
def numa_information(output_file):
    output_file.write('<h1 id="NUMA info"style="font-size:26px;">NUMA Information: <br></H1>')
    meminfo_cmd = "vsish -e cat /memory/memInfo"
    mem_info = execute(meminfo_cmd)
    mem_info = mem_info.split('\n')
    lines = ('<p>')
    for line in mem_info:
        lines += '<small>%s</small><br>' % line
    output_file.write('%s</p>' % lines)
    return 0

#- function to fetch the dns information.
def dns_information(output_file):
    output_file.write('<h1 style="font-size:26px;">Configuration file /etc/hosts: <br></H1>')
    dns_cmd = "cat /etc/hosts"
    dns_info = execute(dns_cmd)
    dns_info = dns_info.split('\n')
    get_dns = "dnsdomainname"
    srvr_dns = execute(get_dns)
    lines = ('<p>')
    for line in dns_info:
        if not line.startswith("#"):
           lines += '<small>%s</small><br>' % line
    output_file.write("DNS: "+srvr_dns)
    output_file.write('%s</p>' % lines)
    return 0

#- start here
if __name__=="__main__":
    parser = optparse.OptionParser(usage="usage: %prog [options]")
    parser.add_option("-m", "--mode",
                      action="store",
                      dest="mode",
                      default="esxi",
                      help="vcli/esxi[default=esxi]")
    parser.add_option("-s", "--server",
                      action="store",
                      dest="server",
                      default="",
                      help="<server_name>[needed while using vcli only]")
    options, args = parser.parse_args()
    server = options.server
    mode = options.mode
    mode = mode.lower()
    sfvmk_adapter_list = []
    sfvmk_tlp_list = []
    os_type = execute("uname -o")
    if mode not in ['vcli','esxi']:
       print("\nOption Error: -m|--mode can be either 'vcli' or 'esxi'\n")
       sys.exit()
    # Check when mode=vcli then os type should not be ESXi.
    # Proceed only if vcli is run on vcli enabled Linux OS.
    if mode == "vcli" and "esxi" in os_type.lower():
       print("\nOption Error: when --mode=vcli, OS type should be vcli enabled Linux\n")
       sys.exit()
    elif not "esxi" in os_type.lower() and mode == "esxi":
       print("\nError: Cannot run mode=esxi on current host for SolarFlare System Report generation")
       sys.exit()
    # Proceed only if vcli is specified with server option.
    elif mode == "vcli" and not server:
       print("\nError: while running mode=vcli, --server option should be specified")
       sys.exit()
    # Proceed only if any SF adapters are found.
    if mode == "vcli":
       server = "--server " + server
    cmd = 'esxcli ' + server + ' network nic list |grep sfvmk'
    sf_adapters = execute(cmd)
    if sf_adapters == 1:
       print("\nCAUTION: Either sfvmk driver is NOT loaded OR Solarflare NIC is NOT visible\n")
       sys.exit()
    if sf_adapters:
       for line in sf_adapters.split('\n'):
           if line != "":
              interface = re.search('(\w+)\s+(\w+\:\w+\:\w+\.\w+)\s(.*)',line)
              sfvmk_adapter_list.append(interface.group(1))
              sfvmk_tlp_list.append(interface.group(2))
    if "sfvmk" in sf_adapters:
       print("sfreport version: "+sfreport_version)
       print("Solarflare Adapters detected..")
       print("Please be patient.\nSolarFlare system report generation is in progress....")
       current_time = (execute('date +"%Y-%m-%d-%H-%M-%S"'))
       html_file = 'sfreport-' + current_time.strip('\n') + '.html'
       output_file = open(html_file, 'w')
       # Call the feature specific funtions.
       file_header(output_file, current_time, mode,sfreport_version)
       output_file.write('<h1 style="font-size:26px;">-> Report Index:<br></H1>')
       output_file.write('<a href="#System Summary">-> System Summary</a><br>')
       output_file.write('<a href="#Driver Bindings">-> Driver Bindings</a><br>')
       output_file.write('<a href="#SFVMK Parameters">-> sfvmk Parameters</a><br>')
       output_file.write('<a href="#Ethernet Settings">-> Ethernet Settings</a><br>')
       output_file.write('<a href="#Network Configurations">-> Network Configurations</a><br>')
       output_file.write('<a href="#SF PCI devices">-> SF PCI devices</a><br>')
       output_file.write('<a href="#Interface Statistics">-> Interface Statistics</a><br>')
       output_file.write('<a href="#File Properties">-> File Properties</a><br>')
       output_file.write('<a href="#ARP Cache">-> ARP Cache</a><br>')
       output_file.write('<a href="#Virtual Machine">-> Virtual Machine</a><br>')
       output_file.write('<a href="#vSwitch">-> vSwitch Information</a><br>')
       output_file.write('<a href="#Portgroup Information">-> Portgroup Information</a><br>')
       if mode == "esxi":
           output_file.write('<a href="#module file">-> SF Module File names </a><br>')
           output_file.write('<a href="#Known Kernel Modules">-> Known SF Kernel Modules</a><br>')
           output_file.write('<a href="#NUMA info">-> NUMA info</a><br>')
           output_file.write('<a href="#NetQueue Status">-> NetQueue Status</a><br>')
           output_file.write('<a href="#VMkernel logs">-> VMkernel logs</a><br>')
           output_file.write('<a href="#PCI configuration">-> PCI configuration</a><br>')

       system_summary(output_file,server,mode)
       driver_binding(output_file,server,mode)
       sfvmk_parameter_info(output_file,sfvmk_adapter_list,mode)
       sf_pci_devices(output_file,sfvmk_tlp_list,server)
       network_configuration(output_file,server)
       ethernet_settings(output_file, sfvmk_adapter_list,server)
       interface_statistics(output_file, sfvmk_adapter_list, server,mode)
       file_properties(output_file,server)
       arp_cache(output_file, server)
       virtual_machine_info(output_file,server)
       vswitch_details(output_file, server)
       portgroup_details(output_file, server)
       # run esxi specific functions under this check.
       if mode == "esxi":
          sf_module_file(output_file)
          sf_kernel_modules(output_file)
          numa_information(output_file)
          net_queue_status(output_file, sfvmk_adapter_list)
          dns_information(output_file)
          vmkernel_logs(output_file)
          pci_configuration_space(output_file)
       print('Generated output file: '+html_file)
    else:
       print("No SolarFlare Adapters were found")
       sys.exit()
