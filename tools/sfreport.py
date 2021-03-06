# Copyright (c) 2017-2020 Xilinx, Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
# EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#!/usr/bin/python
"""
 Reporting tool for Solarflare under ESXi
 Usage: python sfreport.py <options>
 Output html: sfreport-<date-time>.html
 Copyright (c) 2020 Xilinx, Inc.
"""
from __future__ import print_function
import subprocess
import re
import sys
import optparse
import traceback
from threading import Timer
from collections import defaultdict

global TXT_FILE
global out
global vcli_connectivity

# sfreport version to be incremented for any changes made before releases:
# major minor build
SFREPORT_VERSION = "2.4.0.2012"

def terminate(process, timeout, cmd, mode):
    """ function to terminate a process """
    global vcli_connectivity
    timeout["value"] = True
    process.kill()
    print("ERROR: Following command took longer than expected:\ncmd: " +cmd)
    if mode == "vcli":
        print("CAUTION: Check connectivity with the target machine\n\t\tOR"
              " \n\t Make sure thumbprints were added\n")
        print ("INFO: Press enter to return to prompt")
        vcli_connectivity = "false"
        sys.exit()
    return 1

def execute(cmd, mode='esxi'):
    """ function to execute the command on the cmdline using subprocess """
    global TXT_FILE, out
    timeout_sec = 10
    if not "date" in cmd:
       TXT_FILE.write("\ncmd:"+cmd)
    process = subprocess.Popen((cmd), stdout=subprocess.PIPE, \
                               stderr=subprocess.PIPE,\
                               shell=True)
    timeout = {"value": False}
    timer = Timer(timeout_sec, terminate, [process, timeout, cmd, mode])
    timer.start()
    out, err = process.communicate()
    timer.cancel()

    if err and "warning" not in err.lower():
        print("ERROR:", err)
        print("Error while running command:", cmd)
        if not "date" in cmd:
          TXT_FILE.write('\n' + str(err))
        return 1
    elif out:
        out = (out).decode('ascii')
        if not "date" in cmd:
           TXT_FILE.write('\n' + out)
        return out

def call_jscript(output_file):
    """ function to call java script"""
    output_file.write('<script>')
    output_file.write('var coll = document.getElementsByClassName("collapsible");\
        var i;\
        for (i = 0; i < coll.length; i++) {\
          coll[i].addEventListener("click", function() {\
            this.classList.toggle("active");\
            var content = this.nextElementSibling;\
            if (content.style.display === "block") {\
              content.style.display = "none";\
            } else {\
              content.style.display = "block";\
            }\
          });\
        }')
    output_file.write('</script>')
    return 0

def collapse(output_file, txt, button):
    """ function to collapse the contents of the HTML"""
    output_file.write('<style>')
    output_file.write('.collapsible {\
        background-color: lightslategray;\
        color: white;\
        cursor: pointer;\
        padding: 1px;\
        width: 100%;\
        border: 1;\
        text-align: left;\
        outline: none;\
        font-size: 15px;\
    }')
    output_file.write('.active,.collapsible:hover\
    {\
        background - color:  # 555;\
    }')
    output_file.write('.collapsible:after { \
        content: "\002+"; \
        font - size: 13px; \
        color: white;\
        float: right;\
        margin - left: 5px;\
    }')
    output_file.write('.active:after { \
        content: "\221-"; \
    }')
    output_file.write('.content {\
      padding: 0 18px;\
      display: none;\
      overflow: hidden;\
      background-color: #f1f1f1;\
    }')
    output_file.write('</style>')
    output_file.write('<body>')
    output_file.write(' <button class="collapsible">%s</button> '%button)
    output_file.write(' <div class="content">\
                 <p><PRE>%s</PRE></p>\
                 </div> ' % txt)
    output_file.write('</body>')
    return 0



def file_header(output_file, time, mode, sf_ver):
    """function to create file header"""
    output_file.write('<HTML><HEAD>')
    output_file.write('<TITLE>Solarflare system report</TITLE></HEAD>')
    output_file.write('<h1 style="font-size:36px;"> Solarflare System Report \
                     (version: %s)</H1>'% sf_ver)
    output_file.write("Time:"+time)
    output_file.write("</br>Report generation: " + mode+"</br>")
    if mode != "vcli":
        user = (execute('echo $USER', mode))
        if user == 1:
            return
        user = "(User : " + user + " )"
        output_file.write(user)
        output_file.write('<h1 <br></H1>')
        output_file.write('<h1 style="font-size:26px;"> System uptime:</H1>')
        uptime = (execute('uptime', mode))
        if uptime == 1:
            return
        output_file.write(uptime)
    return 0

def system_summary(output_file, server, mode):
    """function to gather system summary"""
    system_values = []
    if mode == "esxi":
        output_file.write('<h1 id="System Summary"style="font-size:26px;"\
                                      > System Summary: </H1>')
        system_summary_header = ['OS Name', 'Version', 'Architecture',\
                                 'Kernel Command Line', 'Distribution',\
                                 'System Name', 'System Manufacturer',\
                                 'System Model', 'Processor', 'CPU Packages',\
                                 'CPU Cores', 'CPU Threads', 'Hyperthreading\
                                  Active', 'Hyperthreading Supported',\
                                 'Hyperthreading Enabled', 'HV Support', \
                                 'HV Replay Capable', 'HV Replay Disabled \
                                  Reasons', 'BIOS Version/Date', 'Total \
                                  Physical Memory', 'Available Physical Memory',\
                                 'Total Virtual Memory', 'Available Virtual \
                                  Memory', 'Page File Space']
        os_name = execute("uname -o")
        if os_name == 1:
            return
        system_values.append(os_name)  # OS Name
        os_ver = execute("uname -v")
        if os_name == 1:
            return
        system_values.append(os_ver)  # Version
        os_arch = execute("uname -i")
        if os_arch == 1:
            return
        system_values.append(os_arch)  # Architecture
        system_values.append("")  # Kernel Command Line
        os_dist = execute("vmware --version")
        if os_dist == 1:
            return
        system_values.append(os_dist)  # Distribution
        sys_name = execute("uname -n")
        if sys_name == 1:
            return
        system_values.append(sys_name)  # SystemName
        # Capture the logs specific to system
        system_manufacturer = execute("smbiosDump | grep -A4  'System Info'")
        if system_manufacturer == 1:
            return
        try:
            manufacturer_name = re.search('(.*)Manufacturer\:\s(.*)Product\:\s'\
                                '(.*)(\n)', system_manufacturer, re.DOTALL)
            system_values.append(manufacturer_name.group(2)) # System Manufacturer
            system_values.append(manufacturer_name.group(3)) # System Model
        except AttributeError:
            system_values.append("not updated")
            system_values.append("not updated")

        # Capture the logs specific to Processor
        processor_info = execute("smbiosDump | grep -m1 -A20 'Processor Info'")
        if processor_info == 1:
            return
        try:
            processor_info = re.search('(.*)Version\:(.*)Processor',\
                                       processor_info, re.DOTALL)
            processor_version = processor_info.group(2) #Processor version
            system_values.append(processor_version)
        except AttributeError:
            system_values.append("not updated")
        cpu_info = execute("esxcli hardware cpu global get")
        if cpu_info == 1 or cpu_info == None:
            return
        for line in cpu_info.splitlines():
            rhs, lhs = line.split(':')
            system_values.append(lhs)
        # Capture the logs specific to BIOS
        bios_info = execute("smbiosDump | grep -A4  'BIOS Info'")
        if bios_info == 1:
            return
        try:
            bios_info = re.search('(.*)Vendor\:\s(.*)Version\:\s'\
                                   '(.*)Date\:\s(.*)',\
                                  bios_info, re.DOTALL)
            vendor = bios_info.group(2) #BIOS vendor
            version = bios_info.group(3) #BIOS version
            date = bios_info.group(4) #BIOS date
        except AttributeError:
            vendor = "not updated"
            version = "not updated"
            date = "not updated"

        bios_info = vendor + ',' + version + ',' + date
        system_values.append(bios_info)
    elif mode == "vcli":
        system_summary_header = ['System Name', 'System Manufacturer',\
                                 'System Model', 'Processor', 'CPU Cores',\
                                 'Total Physical Memory', 'VMotion Enabled',\
                                 'In Maintenance Mode', 'Last Boot Time',\
                                 'OS Name', 'Version', 'Build', 'Update',\
                                 'Patch']
        host_info_cmd = "vicfg-hostops  "+ server +" -o info"
        host_info = execute(host_info_cmd)
        if host_info == 1:
            return
        output_file.write('<h1 id="System Summary"style="font-size:26px;"\
                              > System Summary: </H1>')
        # check if host_info_cmd returns error-bug#81723
        if host_info == 1:
            output_file.write("ERROR: System Summary Unavailable")
            return 1
        for line in host_info.split('\n'):
            if line != "":
                try:
                    line = re.search('(.*)\:\s+(.*)', line)
                    system_values.append(line.group(2))
                except AttributeError:
                    system_values.append("not updated")
        system_info_cmd = "esxcli "+ server + " system version get"
        system_info = execute(system_info_cmd)
        if system_info == 1:
            return
        for line in system_info.split('\n'):
            if line != "":
                try:
                    line = re.search('(.*)\:\s+(.*)', line)
                    system_values.append(line.group(2))
                except AttributeError:
                    system_values.append("not updated")
    #create html table for all system_summary_header elements.
    table = '<table><table border="1">'
    for key, value in zip(system_summary_header, system_values):
        table += '<tr><th align=left> %s' % key + '</th><td> %s </td><tr>'\
                 % value
    table += '</table>'
    output_file.write(table)
    return 0

def sw_versions(output_file, server, nic_list, mode, sf_ver, cli_vib):
    """function to fetch Solarflare Module versions"""
    cnt = 0
    output_file.write('<h1 id="Software Versions"style="font-size:26px;"\
                          > Software Versions: <br></H1>')
    html = '<table><table border="1">'
    for key, val in sf_ver.items():
        html += '<th align=left> %s &nbsp</th>' % key
        html += '<td>%s &nbsp</td>' % val
    html += '</table>'
    output_file.write(html)
    if cli_vib:
        for nic in nic_list:
            if cnt == 0:
                fw_html = '<table><th>Firmware Version Table</th></tr><table border="1">'
            else:
                fw_html = '<table><table border="1">'
            keys = ['VMNIC']
            values = [nic]
            cmd = 'esxcli ' + server + ' sfvmk firmware get -n ' + nic
            out = execute(cmd, mode)
            if out == 1 or out == None:
                fw_html += '<Not Available /table>'
                output_file.write(fw_html)
                return
            for line in out.splitlines():
                if line.startswith('Solarflare') or line == "":
                    continue
                else:
                    lhs, rhs = line.split(':', 1)
                    if lhs.startswith('vmnic'):
                        lhs = "MAC"
                    keys.append(lhs)
                    values.append(rhs)
            for key in keys:
                fw_html += '<th>%s &nbsp' % key + '</th>'
            fw_html += '</tr>'
            for value in values:
                fw_html += '<td> %s &nbsp</td>' % value
            fw_html += '</tr>'
            cnt += 1
            fw_html += '</table>'
            output_file.write(fw_html)
    return 0

def driver_binding(output_file, server, mode):
    """function to fetch driver specific information"""
    output_file.write('<h1 id="Driver Bindings"style="font-size:26px;"\
                      > Driver Bindings: <br></H1>')
    cmd = 'esxcli ' + server + ' network nic list |grep sfvmk'
    sf_nw_list = execute(cmd, mode)
    if sf_nw_list == 1:
        output_file.write('Not Available')
        return
    sf_nw_list = sf_nw_list.split('\n')
    html = '<table><table border="1">'
    for hdr in ('address', 'interface', 'driver_name'):
        html += '<th>%s' % hdr + '</th>'
    for line in sf_nw_list:
        if line != "":
            try:
                line = re.search('(\w+)\s+(\w+\:\w+\:\w+\.\w+)\s+(\w+)', line)
                interface = line.group(1)
                tlp = line.group(2)
                driver_name = line.group(3)
            except AttributeError:
                interface = "not updated"
                tlp = "not updated"
                driver_name = "not updated"
            html += '</tr><td>%s' % tlp + '<td>%s' % interface + '<td>%s'\
                    % driver_name
    html += '</table>'
    output_file.write(html)
    return 0

def vpd_information(output_file, server, mode, adapter_list):
    """function to fetch VPD & FEC information"""
    output_file.write('<h1 id="VPD Information"style="font-size:26px;">\
                              VPD Information: <br></H1>')
    vpd_table = '<table><th>VPD Information</th></tr><table border="1">'
    vpd_table += '<th align=left> Interface_name</th>'
    interface_count = 1
    vpd_dict = defaultdict()
    no_of_interfaces = len(adapter_list)
    for nic in adapter_list:
        vpd_table += '<td> %s </td>' % nic
        if interface_count == no_of_interfaces:
            vpd_table += '</tr>'
        cmd = "esxcli " + server + " sfvmk vpd get -n " + nic
        vpd_info = execute(cmd, mode)
        if vpd_info == 1 or vpd_info == None:
            output_file.write('Not Available')
            return
        for line in vpd_info.splitlines():
            if line == "":
                continue
            lhs, rhs = line.split(":", 1)
            if interface_count == 1:
                vpd_dict[lhs] = []
                vpd_dict[lhs].append(rhs)
            else:
                vpd_dict[lhs].append(rhs)
        interface_count += 1
    for key, values in sorted(vpd_dict.items()):
        vpd_table += '<th align=left>%s</th>' % key
        for value in values:
            vpd_table += '<td> %s </td>' % value
        vpd_table += '</tr>'
    vpd_table += '</table>'
    output_file.write(vpd_table)

def get_sensor_info(output_file, server, mode, adapter_list):
    """function to fetch Sensor information"""
    output_file.write('<h1 id="Sensor Information"style="font-size:26px;">\
                                  Sensor Information: <br></H1>')
    for nic in adapter_list:
        cmd = "esxcli " + server + " sfvmk sensor get -n " + nic
        result = execute(cmd, mode)
        if result == 1:
            output_file.write('Not available')
            return
        collapse(output_file, result, "%s Sensor logs"%nic)
    return 0


def vmkernel_logs(output_file):
    """function to fetch sfvmk specific vmkernel logs [dmesg]"""
    output_file.write('<h1 id="VMkernel Logs"style="font-size:26px;">\
                      VMkernel Logs: <br></H1>')
    dmesg_logs = execute('dmesg -c |grep sfvmk')
    lines = ('<p>')
    if dmesg_logs == 1 or dmesg_logs is None:
        lines += 'No logs specific to sfvmk were found'
        output_file.write('%s</p>'%lines)
        return 1
    dmesg = dmesg_logs.split('\n')
    for line in dmesg:
        lines += '<small>%s</small><br>'%line
    collapse(output_file, lines, "Kernel logs")
    return 0

def cim_logs(output_file):
    """function to fetch cim specific logs"""
    output_file.write('<h1 id="CIM Logs"style="font-size:26px;">\
                      CIM Logs: <br></H1>')
    logs = execute('cat /var/log/syslog.log |grep sfcb-solarflare')
    lines = ('<p>')
    if logs == 1 or logs is None:
        lines += 'No logs specific to CIM were found'
        output_file.write('%s</p>'%lines)
        return 1
    cim_log = logs.split('\n')
    for line in cim_log:
        lines += '<small>%s</small><br>'%line
    output_file.write('%s</p>'%lines)
    return 0

def sfvmk_parameter_info(output_file, server, sfvmk_adapter_list, mode,
                         cli_vib):
    """function to fetch the parameter list of sfvmk driver"""
    output_file.write('<h1 id="SFVMK Parameters"style="font-size:26px;">\
                      SFVMK Parameters: <br></H1>')
    # module parameters list
    mod_param_cmd = "esxcli " + server + " system module parameters list -m sfvmk"
    mod_param_list = execute(mod_param_cmd, mode)
    if mod_param_list == 1 or mod_param_list == None:
        output_file.write('Not Available')
        return
    html = '<table style="font-size:18px;"><th>Module Parameters\
                         </th></tr><table border="1">'
    cnt = 0
    for line in mod_param_list.splitlines():
        if not line.startswith("--"):
            if cnt == 0:
                for word in re.split(r'\s{2,}', line):
                    if len(word) != 0:
                        html += '<th>%s &nbsp' % word + '</th>'
            else:
                l = re.search('((?i)\w+)\s+(\w+)\s+(\w+)\s+(.*)',
                              line, re.DOTALL)
                name = l.group(1)
                var = l.group(2)
                val = l.group(3)
                description = l.group(4)
                # to handle the blank value of the mod params,
                # following handling is needed
                if val.isalpha():
                    description = val + description
                    val = ''
                html += '</tr><td>%s' % name + '<td>%s' % var + '<td>%s' % val \
                       + '<td>%s' % description
            html += '</tr>'
            cnt += 1
    html += '</table>'
    output_file.write(html)
    # fetch mclog status of all vmnic interfaces
    if cli_vib:
        mclog_html = '<table><th>MC-log status</th></tr>\
                      <table border="1">'
        mclog_html += '<th> Interface <th> Status</th>'
        for nic in sfvmk_adapter_list:
            cmd = "esxcli " + server + " sfvmk mclog get -n " + nic
            mclog_status = execute(cmd, mode)
            if mclog_status == 1:
                output_file.write('Not Available')
                return
            mclog_html += '</tr><td>%s' % nic + '<td>%s' % mclog_status
            mclog_html += '</tr>'
        mclog_html += '</table>'
        output_file.write(mclog_html)
    # intrpt_html table for interrupt moderation settings.
    intrpt_html = '<table><th>Interrupt Moderation Table</th></tr>\
                   <table border="1">'
    intrpt_mod_hdr_list = ['NIC', 'rx_microsecs', 'rx_max_frames',\
                           'tx_microsecs', 'tx_max_frames',\
                           'adaptive_rx', 'adaptive_tx',\
                           'sample_interval_secs']
    intrpt_val_list = []
    for hdr in intrpt_mod_hdr_list:
        intrpt_html += '<th>%s' % hdr + '</th>'
    intrpt_html += '</tr>'
    # rxtx_ring_html table for rx/tx ring settings.
    rxtx_ring_html = '<table><th>RX_TX Ring/TSO-CSO Table</th></tr>\
                      <table border="1">'
    rxtx_ring_hdr_list = ['NIC', 'RX', 'RX_Mini', 'RX_Jumbo', 'TX', 'TSO',\
                          'CSO']
    for hdr in rxtx_ring_hdr_list:
        rxtx_ring_html += '<th>%s' % hdr + '</th>'
    rxtx_ring_html += '</tr>'
    # Pause Params settings
    pause_params_html = '<table><th>Pause parameters Table</th></tr>\
                         <table border="1">'
    pause_params_hdr_list = ['NIC', 'Pause_Support', 'Pause_RX', 'Pause_TX',\
                             'Auto_Neg', 'Auto_Neg_Res_Avail',\
                             'RX_Auto_Neg_Res', 'RX_Auto_Neg_Res']
    for hdr in pause_params_hdr_list:
        pause_params_html += '<th>%s' % hdr + '</th>'
    pause_params_html += '</tr>'
    # fetch Pause Params settings.
    get_pause_cmd = "esxcli "+ server + " network nic pauseParams list"
    get_pause_settings = execute(get_pause_cmd)
    if get_pause_settings == 1:
        output_file.write('Not Available')
        return
    get_pause_settings = get_pause_settings.split('\n')
    if mode == 'esxi':
        output_file.write('<h1 style="font-size:18px;">Rx/Tx Queue Info Tables:\
                                              </H1>')
        for intf in sfvmk_adapter_list:
            rxq_html = '<table><th>%s RX Queue Info Table</th></tr>\
                                        <table border="1">' % intf
            rxq_cmd = "vsish -e cat /net/pNics/%s/rxqueues/info"%intf
            rxq_count = execute(rxq_cmd)
            if rxq_count == 1 or rxq_count == None:
                output_file.write('Not Available')
                return
            for l in rxq_count.splitlines():
                if not l.startswith("rx ") and not l.endswith('}'):
                    lhs, rhs = l.split(":", 1)
                    rxq_html += '<th align=left>%s</th>' % lhs
                    rxq_html += '<td> %s</td>'% rhs
                    rxq_html += '</tr>'
            rxq_html += '</table>'
            collapse(output_file, rxq_html, "%s rxQ info"%intf)
            txq_html = '<table><th>%s TX Queue Info Table</th></tr>\
                                                 <table border="1">' % intf
            txq_cmd = "vsish -e cat /net/pNics/%s/txqueues/info" % intf
            txq_count = execute(txq_cmd)
            if txq_count == 1 or txq_count == None:
                output_file.write('Not Available')
                return
            for t in txq_count.splitlines():
                if not t.startswith("tx ") and not t.endswith('}'):
                    tlhs, trhs = t.split(":", 1)
                    txq_html += '<th align=left>%s</th>' % tlhs
                    txq_html += '<td> %s</td>' % trhs
                    txq_html += '</tr>'
            txq_html += '</table>'
            collapse(output_file, txq_html, "%s txQ info"%intf)
    # fetch the sfvmk parameters for all sfvmk adapters.
    for interface in sfvmk_adapter_list:
        # fetch interface specific interrupt mod settings
        get_interrupt_cmd = "esxcli "+ server + " network nic coalesce get -n "\
                            + interface
        get_interrupt_params = execute(get_interrupt_cmd)
        if get_interrupt_params == 1:
            output_file.write('Not Available')
            return
        get_interrupt_params = get_interrupt_params.split('\n')
        # fetch interface specific rx/tx ring settings
        get_ring_cmd = "esxcli "+ server + " network nic ring current get -n "\
                       + interface
        get_ring_values = execute(get_ring_cmd)
        if get_ring_values == 1:
            output_file.write('Not Available')
            return
        get_ring_values = get_ring_values.split('\n')
        # fetch tso settings.
        get_tso_cmd = "esxcli "+ server + " network nic tso get -n " + interface
        get_tso_values = execute(get_tso_cmd)
        if get_tso_values == 1:
            output_file.write('Not Available')
            return
        get_tso_values = get_tso_values.split('\n')
        # fetch cso settings.
        get_cso_cmd = "esxcli "+ server + " network nic cso get -n " + interface
        get_cso_values = execute(get_cso_cmd)
        if get_cso_values == 1:
            output_file.write('Not Available')
            return
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
                    line = re.search('(.*)\:\s(\d+)', line)
                    ring_val = line.group(2)
                except AttributeError:
                    ring_val = "not updated"
                rxtx_ring_html += '<td>%s' % ring_val
        for tso, cso in zip(get_tso_values, get_cso_values):
            if (not tso.startswith("---") and tso.startswith(interface)) and \
               (not tso.startswith("---") and tso.startswith(interface)):
                try:
                    tso = re.search('(\w+)\s+(\w+)', tso)
                    tso_val = tso.group(2)
                except AttributeError:
                    tso_val = "not updated"
                try:
                    cso = re.search('(\w+)\s+(\w+)', cso)
                    cso_val = cso.group(2)
                except AttributeError:
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
    intrpt_html += '</table>'
    rxtx_ring_html += '</table>'
    pause_params_html += '</table>'
    output_file.write(intrpt_html)
    output_file.write(rxtx_ring_html)
    output_file.write(pause_params_html)
    return 0

def sf_pci_devices(output_file, sfvmk_tlp_list, server, mode):
    """function to fetch sf pci info"""
    output_file.write('<h1 id="SF PCI Devices"style="font-size:26px;">\
                      SF PCI Devices: <br></H1>')
    tlp_dict = defaultdict(list)
    tlp_count = 0
    #create html table for all system_summary_header elements.
    table = '<table><table border="1">'
    for tlp in sfvmk_tlp_list:
        sf_tlp_cmd = "esxcli "+ server + " hardware pci list |grep -A30 " + tlp
        sf_tlp_info = execute(sf_tlp_cmd, mode)
        if sf_tlp_info == 1:
            output_file.write('Not Available')
            return
        for line in sf_tlp_info.split('\n'):
            if line != "" and line != tlp:
                try:
                    line = re.search('(.*)\:\s+(.*)', line)
                    tlp_param = (line.group(1))
                    tlp_val = (line.group(2))
                except AttributeError:
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
    for key, values in tlp_dict:
        table += '<th align=left>%s</th>' % key
        for value in values:
            table += '<td> %s</td>'% value
        table += '</tr>'
    table += '</table>'
    output_file.write(table)
    return 0

def pci_configuration_space(output_file):
    """function to fetch PCI Configuration space dump"""
    output_file.write('<h1 id="PCI Configuration"style="font-size:26px;">\
                      PCI Configuration: <br></H1>')
    dmesg = execute("lspci -d")
    if dmesg == 1 or dmesg == None:
        output_file.write('Not Available')
        return
    lines = ('<p>')
    for line in dmesg.splitlines():
        lines += '<small>%s</small><br>'%line
    collapse(output_file, lines, "PCI space")
    return 0

def sf_kernel_modules(output_file):
    """function to fetch the solarflare specific kernel modules on the server"""
    output_file.write('<h1 id="Known Kernel Modules"style="font-size:26px;">\
                      Known Kernel Modules: <br></H1>')
    kernel_modules = execute('lspci -p |grep sfvmk')
    if kernel_modules == 1:
        output_file.write('Not Available')
        return
    kernel_modules = kernel_modules.split('\n')
    html = '<table><table border="1">'
    for hdr in ('module_name', 'pci address', 'vendor', 'device', 'subvendor',\
                'subdevice', 'class', 'driver_data'):
        html += '<th>%s' % hdr + '</th>'
    for line in kernel_modules:
        if line != "":
            try:
                line = re.search('(\w+\:\w+\:\w+\.\w+)\s(\w+)\:(\w+)\s(\w+)'\
                                 '\:(\w+)(.*)', line)
                module_name = 'sfvmk'
                tlp = line.group(1)
                vendor = line.group(2)
                device = line.group(3)
                subvendor = line.group(4)
                subdevice = line.group(5)
            except AttributeError:
                module_name = 'sfvmk'
                tlp = "not updated"
                vendor = "not updated"
                device = "not updated"
                subvendor = "not updated"
                subdevice = "not updated"
            html += '</tr><td>%s' % module_name + '<td>%s' % tlp + '<td>%s'\
                     %vendor + '<td>%s' % device + '<td>%s' % subvendor \
                     + '<td>%s' % subdevice
    html += '</table>'
    output_file.write(html)
    return 0

def network_configuration(output_file, server, mode, esx_ver):
    """function to fetch network configuration informations"""
    output_file.write('<h1 id="Network Configuration"style="font-size:26px;"> \
                      Network Configuration: <br></H1>')
    nw_config_cmd = "esxcli "+ server + " network nic list"
    nw_config_val = execute(nw_config_cmd)
    if nw_config_val == 1:
        output_file.write('Not Available')
        return
    nw_config_val = nw_config_val.split('\n')
    table = '<table><table border="1">'
    for hdr in ('Name', 'PCI dev', 'Driver', 'Admin Status', 'Link Status',\
                'Speed', 'Duplex', 'Mac Address', 'MTU', 'Description'):
        table += '<th>%s' % hdr + '</th>'
    for line in nw_config_val:
        if line != "" and not line.startswith("---") \
                      and not line.startswith("Name"):
            try:
                line = re.search('(\w+)\s+(\w+\:\w+\:\w+\.\w+)\s+(\w+)\s+(\w+)\s+(\w+)'\
                                  '\s+(\w+)\s+(\w+)\s+(\w+\:\w+\:\w+\:\w+\:\w+\:\w+)'\
                                  '\s+(\w+)\s+(.*)', line)
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
            except AttributeError:
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
            table += '</tr><td>%s'%name  +'<td>%s'%pci_id +'<td>%s'%driver \
                     + '<td>%s'%admin_stat +'<td>%s'%link_stat +'<td>%s'%speed \
                     +'<td>%s'%duplex +'<td>%s'%mac +'<td>%s'%mtu +'<td>%s'%detail
    table += '</table>'
    output_file.write(table)
    ip_list = ['v4', 'v6']
    for ip_type in ip_list:
        if ip_type == "v4":
            ip4_table = '<table style="font-size:18px;"><th>IP%s Configuration:\
                         </th></tr><table border="1">'% ip_type
            hdr_list = ['Interface', 'IPV4 address', 'IPv4 netmask', \
                        'IPv4Bcast address', 'Type', 'Gateway', 'DHCP DNS']
            if "6.0.0" in esx_ver:
                ipv_ip_cmd = "esxcli " + server + " network ip interface ip" +\
                             ip_type + " get"
                hdr_list.remove('Gateway')
            else:
                ipv_ip_cmd = "esxcli " + server + " network ip interface ip"\
                              + ip_type + " address list"
            for hdr in hdr_list:
                ip4_table += '<th>%s' % hdr + '</th>'
        elif ip_type == "v6":
            ip6_table = '<table style="font-size:18px;"><th>IP%s Configuration: \
                         </th></tr><table border="1">' % ip_type
            hdr_list = ('Interface', 'Address', 'Netmask', 'Type', 'Status')
            ipv_ip_cmd = "esxcli " + server + " network ip interface ip" + \
                         ip_type + " address list"
            for hdr in hdr_list:
                ip6_table += '<th>%s' % hdr + '</th>'
        ipv_ip_info = execute(ipv_ip_cmd)
        if ipv_ip_info == 1:
            output_file.write('Not Available')
            return
        ipv_ip_info = ipv_ip_info.split('\n')
        for line in ipv_ip_info:
            if line != "" and not line.startswith("---") \
                    and not line.startswith("Name")\
                    and not line.startswith("Interface"):
                if ip_type == "v4":
                    try:
                        line = re.search('(\w+)\s+(\d+\.\d+\.\d+\.\d+)\s+'
                                         '(\d+\.\d+\.\d+\.\d+)\s+(\d+\.\d+\.\d+\.\d+)'
                                         '\s+(\w+)\s+([\d+\.\d+\.\d+\.\d+])*\s+(\w+)'
                                         , line)
                        interface = line.group(1)
                        ip_addr = line.group(2)
                        netmask = line.group(3)
                        bcast_ip = line.group(4)
                        iptype = line.group(5)
                        if esx_ver != "6.0.0":
                            gateway = line.group(6)
                            dhcp = line.group(7)
                        else:
                            dhcp = line.group(6)
                    except AttributeError:
                        interface = "not updated"
                        ip_addr = "not updated"
                        netmask = "not updated"
                        bcast_ip = "not updated"
                        iptype = "not updated"
                        if esx_ver != "6.0.0":
                            gateway = "not updated"
                        dhcp = "not updated"
                    if "6.0.0" in esx_ver:
                        ip4_table += '</tr><td>%s'%interface +'<td>%s'%ip_addr \
                                    +'<td>%s'%netmask +'<td>%s'%bcast_ip \
                                    +'<td>%s'%iptype +'<td>%s'%dhcp
                    else:
                        ip4_table += '</tr><td>%s' % interface + '<td>%s' % ip_addr \
                                     + '<td>%s' % netmask + '<td>%s' % bcast_ip \
                                     + '<td>%s' % iptype + '<td>%s' % gateway + '<td>%s' % dhcp
                elif ip_type == "v6":
                    try:
                        line = re.search('(\w+)\s+(\w+\:\:\w+\:\w+\:\w+\:\w+)'
                                         '\s+(\w+)\s+(\w+)\s+(.*)',
                                          line)
                        interface = line.group(1)
                        address = line.group(2)
                        netmask = line.group(3)
                        iptype = line.group(4)
                        status = line.group(5)
                    except AttributeError:
                        interface = "not updated"
                        address = "not updated"
                        netmask = "not updated"
                        iptype = "not updated"
                        status = "not updated"
                    ip6_table += '</tr><td>%s' % interface + '<td>%s' % address \
                                 + '<td>%s' % netmask + '<td>%s' % iptype \
                                 + '<td>%s' % status

    ip4_table += '</table>'
    ip6_table += '</table>'
    output_file.write(ip4_table)
    output_file.write(ip6_table)
    route_table = '<table><table border="1">'
    output_file.write('<h1 style="font-size:18px;"> IPv4 Routing Table: <br></H1>')
    ipv4_route_cmd = "esxcli "+ server + " network ip route ipv4 list"
    ipv4_route_info = execute(ipv4_route_cmd)
    if ipv4_route_info == 1:
        output_file.write('Not Available')
        return
    ipv4_route_info = ipv4_route_info.split('\n')
    for hdr in ('Network', 'Netmask', 'Gateway', 'Interface', 'Source'):
        route_table += '<th>%s' % hdr + '</th>'

    for line in ipv4_route_info:
        if line != "" and not line.startswith("---") and not line.startswith("Network"):
            try:
                line = re.search('(.*)\s+(\d+\.\d+\.\d+\.\d+)\s+(\d+\.\d+\.\d+\.\d+)'
                                 '\s+(\w+)\s+(\w+)', line)
                network = line.group(1)
                netmask = line.group(2)
                gateway = line.group(3)
                interface = line.group(4)
                source = line.group(5)
            except AttributeError:
                network = "not updated"
                netmask = "not updated"
                gateway = "not updated"
                interface = "not updated"
                source = "not updated"
            route_table += '</tr><td>%s'%network +'<td>%s'%netmask +'<td>%s'%gateway +\
                           '<td>%s'%interface +'<td>%s'%source
    route_table += '</table>'
    output_file.write(route_table)
    if mode == 'esxi':
        dns_information(OUT_FILE)
    return 0

def ethernet_settings(output_file, sfvmk_adapter_list, server, mode, cli_vib):
    """Fetch ethernet setting of SF adapters"""
    output_file.write('<h1 id="Ethernet Settings"style="font-size:26px;"> \
                              Ethernet Settings: <br></H1>')
    for interface in sfvmk_adapter_list:
        output_file.write('<h1 style="font-size:16px;"> \
                                      %s : </H1>'%interface)
        nic_info_cmd = "esxcli " + server + " network nic get -n " + interface
        nic_info = execute(nic_info_cmd, mode)
        if nic_info == 1:
            output_file.write('Not Available')
            return
        collapse(output_file, nic_info, "Ethernet settings:")
        ring_info_cmd = "esxcli " + server + " network nic ring current get -n "\
                        + interface
        ring_info = execute(ring_info_cmd, mode)
        if ring_info == 1:
            output_file.write('Not Available')
            return
        collapse(output_file, ring_info,"Rx/Tx Ring Configuration:")
        if cli_vib:
            fec_cmd = "esxcli " + server + " sfvmk fec get -n " + interface
            fec_info = execute(fec_cmd, mode)
            if fec_info == 1:
                output_file.write('Not Available')
                return
            collapse(output_file, fec_info, "FEC Configuration:")

    return 0

def interface_statistics(output_file, sfvmk_adapter_list, server, mode):
    """Fetch SF interface statistic counters"""
    output_file.write('<h1 id="Interface Statistics"style="font-size:26px;"> \
                       Interface Statistics: <br></H1>')
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
        if stats_values == 1:
            output_file.write('Not Available')
            return
        for line in stats_values.split('\n'):
            if not line.startswith("NIC") and line != "":
                try:
                    line = re.search('(.*)\:\s+(.*)', line)
                    param_name = line.group(1)
                    param_value = line.group(2)
                except AttributeError:
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
    for key, values in stats_dict:
        table += '<th align=left>%s</th>' % key
        for value in values:
            table += '<td> %s </td>'% value
        table += '</tr>'
    table += '</table>'
    collapse(output_file, table, "Stats")
    # fetch the private stats for the sf adapters
    if mode == "esxi":
        interface_count = 1
        priv_table = '<table><th>Private Statistics:</th></tr><table border="1">'
        priv_table += '<th>Interface_name</th>'
        priv_stats_dict = OrderedDict()
        for interface in sfvmk_adapter_list:
            private_stats_cmd = "vsish -e cat /net/pNics/"+ interface+"/stats"
            private_stats_values = execute(private_stats_cmd)
            if private_stats_values == 1:
                priv_table = 'Private Statistics not available for: '+interface
                output_file.write(priv_table)
                return 1
            priv_table += '<td> %s </td>' % interface
            if interface_count == no_of_interfaces:
                priv_table += '</tr>'
            for line in private_stats_values.split('\n'):
                if not "device {" in line and line != "" and not "--" in line \
                   and not "}" in line:
                    try:
                        line = re.search('(.*)\:(.*)', line)
                        param_name = line.group(1)
                        param_value = line.group(2)
                    except AttributeError:
                        param_name = "not updated"
                        param_value = "not updated"
                    if interface_count == 1:
                        if param_name not in priv_stats_dict:
                            priv_stats_dict[param_name] = []
                            priv_stats_dict[param_name].append(param_value)
                    else:
                        priv_stats_dict[param_name].append(param_value)
            interface_count += 1
        priv_stats_dict = (priv_stats_dict.items())
        for key, values in priv_stats_dict:
            priv_table += '<th align=left>%s</th>' % key
            for value in values:
                priv_table += '<td> %s </td>' % value
            priv_table += '</tr>'
        priv_table += '</table>'
        collapse(output_file, priv_table, "Private Stats")
    return 0

def sf_module_file(output_file):
    """Fetch information of SF modules loaded"""
    lines = ('<p>')
    output_file.write('<h1 id="SF Module File Names"style="font-size:26px;">\
                      SF Module File Names : <br></H1>')
    module_cmd = "vmkload_mod -s sfvmk"
    module_info = execute(module_cmd)
    if module_info == 1:
        output_file.write('Not Available')
        return
    module_info = module_info.split('\n')
    for line in module_info:
        lines += '<small>%s</small><br>' % line
    output_file.write('%s</p>' % lines)
    return 0

def net_queue_status(output_file, sfvmk_adapter_list):
    """function to retrieve info on Netqueue status"""
    output_file.write('<h1 id="NetQueue Summary"style="font-size:26px;">\
                      NetQueue Summary: <br></H1>')
    for interface in sfvmk_adapter_list:
        q_count_cmd = "vsish -e ls /net/pNics/%s/txqueues/queues" % interface
        netq_stats = execute(q_count_cmd)
        if netq_stats == 1 or netq_stats == None:
            output_file.write('Not Available')
            return
        # get no. of tx queues available
        queue_count = []
        for line in netq_stats.splitlines():
            q = line.replace('/', '')
            queue_count.append(q)
        res = ""
        if queue_count:
            for qno in queue_count:
                res += '<h>%s ' % interface + 'TX Stats Q# %s</h>' % qno
                stats_cmd = "vsish -e cat /net/pNics/"+ interface \
                           +"/txqueues/queues/%s/stats" % qno
                netq_stats = execute(stats_cmd)
                if netq_stats == 1:
                    res += ('Not Available')
                    return
                res += '<p><PRE>%s</PRE></p>' % netq_stats
                res += '<h>%s ' % interface + 'TX Info Q# %s</h>' \
                                  % qno
                tx_info_cmd = "vsish -e cat /net/pNics/" + interface \
                           + "/txqueues/queues/%s/info" % qno
                tx_info = execute(tx_info_cmd)
                if tx_info == 1:
                    res += 'Not Available'
                    return
                res += '<p><PRE>%s</PRE></p>' % tx_info
                res += '<h>%s ' % interface + 'RX Info Q# %s</h>' % qno
                rx_info_cmd = "vsish -e cat /net/pNics/" + interface \
                           + "/rxqueues/queues/%s/info" % qno
                rx_info = execute(rx_info_cmd)
                if rx_info == 1:
                    res += 'Not Available'
                    return
                res += '<p><PRE>%s</PRE></p>' % rx_info
            collapse(output_file, res, "%s NetQ Stats" %interface)
    return 0

def get_geneve_info(output_file, server, mode):
    """function to fetch information related to GENEVE"""
    output_file.write('<h1 id="GENEVE"style="font-size:26px;">\
                          GENEVE: <br></H1>')
    # get GENEVE vib info:
    geneve_vib = execute("esxcli " + server + " software vib list |grep nsxcli")
    if geneve_vib == 1:
        return 0
    elif geneve_vib == None:
        output_file.write("INFO: No GENEVE VIBs installed")
        return 0
    #list the geneve related vibs:
    if mode == "vcli" or mode == "esxi":
        output_file.write('<h1"style="font-size:18px;"><b>Geneve VIBs:</b><br></H1>')
        words = ('<p>')
        for line in geneve_vib.splitlines():
            words += '<small>%s</small><br>' % line
        output_file.write('%s</p>' % words)
    # fetch geneve configuration:
    if mode == "esxi":
        output_file.write('<h1"style="font-size:18px;"><b>Geneve Configuration '
                          'Summary:</b><br></H1>')
        cmd = "net-vdl2 -l"
        geneve_info = execute(cmd)
        if geneve_info == 1:
            output_file.write('Not Available')
            return
        lines = ('<p>')
        if geneve_info:
           for l in geneve_info.splitlines():
               lines += '<small>%s</small><br>' %l
           output_file.write('%s</p>' % lines)
    return 0

def table_parser(output):
    """ Generic table parser for output tables
        returns a dict of headers and their values
    """
    v = {}
    hdr = True
    hdr_list = []
    if output == None:
       return 1
    for line in output.splitlines():
        if line.startswith("--"):
            continue
        else:
            val_list = []
            for h in re.split(r'\s{2}', line):
                if h:
                    if hdr:
                        hdr_list.append(h.lower().strip())
                    else:
                        val_list.append(h.strip())
            hdr = False
        for key, val in zip(hdr_list, val_list):
            v[key] = val
    return v

def get_vxlan_info(output_file, server, mode):
    """function to fetch information related to VXLAN"""
    output_file.write('<h1 id="VXLAN"style="font-size:26px;">\
                              VXLAN: <br></H1>')
    # Verify if any vxlan vibs are installed, only then proceed
    vxlan_vib = execute("esxcli " + server + " software vib list "
                        "|grep -E '(esx-vxlan|nsxv)'")
    if vxlan_vib == 1 or vxlan_vib == None:
        output_file.write("INFO: No VXLAN VIBs installed")
        return 0
    output_file.write('<h1"style="font-size:18px;"><b>VXLAN VIBs:</b><br></H1>')
    output_file.write('<p><PRE>%s</PRE></p>' % vxlan_vib)
    output_file.write('<h1"style="font-size:18px;"><b>VXLAN Configuration:'
                      '</b><br></H1>')
    vxlan_list_cmd = "esxcli " + server + " network vswitch dvs vmware vxlan list"
    vxlan_list = execute(vxlan_list_cmd, mode)
    if vxlan_list == 1:
        output_file.write('Not Available')
        return
    output_file.write('<p><PRE>%s</PRE></p>' % vxlan_list)
    # parse vds_name from vxlan_list
    vxlan_list_dict = table_parser(vxlan_list)
    try:
        vds_name = vxlan_list_dict["vds name"]
    except (TypeError, KeyError):
        output_file.write("INFO: No VXLAN Distributed switch found.")
        return 1
    # get VXlan stats level
    vxlan_stats_level_cmd = "esxcli " + server + " network vswitch dvs vmware \
                            vxlan config stats get"
    vxlan_stats_level = execute(vxlan_stats_level_cmd, mode)
    if vxlan_stats_level == 1:
        output_file.write('Not Available')
        return
    output_file.write('<p>VXLAN stats %s</p>' % vxlan_stats_level)
    # get VXlan stats
    output_file.write('<h1"style="font-size:18px;"><b>VXLAN Stats:</b><br></H1>')
    vxlan_stats_cmd = "esxcli " + server + " network vswitch dvs vmware vxlan \
                        stats list --vds-name " + vds_name
    vxlan_stats = execute(vxlan_stats_cmd, mode)
    if vxlan_stats == 1:
        output_file.write('Not Available')
        return
    output_file.write('<p><PRE>%s</PRE></p>' % vxlan_stats)
    # list the segment IDs
    seg_id_cmd = "esxcli " + server + " network vswitch dvs vmware vxlan network\
                  list --vds-name " + vds_name
    seg_id_list = execute(seg_id_cmd, mode)
    if seg_id_list == 1:
        output_file.write('Not Available')
        return
    # fetch vxlan id from segment id list
    seg_id_dict = table_parser(seg_id_list)
    try:
        vxlan_id = seg_id_dict["vxlan id"]
    except (TypeError, KeyError):
        output_file.write("INFO: No VXLAN found for Distributed Switch:%s"
                          %vds_name)
        return 1
    output_file.write('<h1"style="font-size:18px;"><b>VXLAN Segment Id list:'
                      '</b><br></H1>')
    output_file.write('<p><PRE>%s</PRE></p>' % seg_id_list)
    #fetch network statistics for segment-ids
    seg_id_nw_statistics_cmd = "esxcli " + server + " network vswitch dvs vmware \
        vxlan network stats list --vds-name " + vds_name + " --vxlan-id " + vxlan_id
    seg_id_nw_stats = execute(seg_id_nw_statistics_cmd, mode)
    if seg_id_nw_stats == 1:
        output_file.write('Not Available')
        return
    output_file.write('<h1"style="font-size:18px;"><b>VXLAN Segment Id N/W Stats:'
                      '</b><br></H1>')
    output_file.write('<p><PRE>%s</PRE></p>' % seg_id_nw_stats)
    #fetch n/w port list
    nw_port_list_cmd = "esxcli " + server + " network vswitch dvs vmware vxlan network\
                    port list --vds-name " + vds_name + " --vxlan-id " + vxlan_id
    nw_port_list = execute(nw_port_list_cmd, mode)
    if nw_port_list == 1:
        output_file.write('Not Available')
        return
    output_file.write('<h1"style="font-size:18px;"><b>VXLAN Port Id List:'
                      '</b><br></H1>')
    output_file.write('<p><PRE>%s</PRE></p>' % nw_port_list)
    # fetch VXLAN configuration details
    if mode == "esxi":
        output_file.write('<h1"style="font-size:18px;"><b>VXLAN Configuration:</b><br></H1>')
        net_vdl_cmd = "net-vdl2 -l"
        net_vdl_info = execute(net_vdl_cmd)
        if net_vdl_info == 1:
            output_file.write('Not Available')
            return
        output_file.write('<p><PRE>%s</PRE></p>' % net_vdl_info)
    return 0

def get_sriov_info(output_file, server, mode, adapters, sriov_list):
    """function to get sriov related information"""
    output_file.write('<h1 id="SRIOV"style="font-size:26px;">\
                                  SRIOV: <br></H1>')
    output_file.write('<h1"style="font-size:18px;"><b>SR-IOV enabled adapters:'
                      '</b><br></H1>')
    output_file.write('<p><PRE>%s</PRE></p>' % sriov_list)
    output_file.write('<h1"style="font-size:18px;"><b>SRIOV VF list for Solarflare Adapters:'
                      '</b><br></H1>')
    # get the SF uplinks where SRIOV is enabled:
    sriov_uplink_list = []
    for iface in adapters:
        vf_list_cmd = "esxcli " + server + " network sriovnic vf list -n " + iface
        vf_list = execute(vf_list_cmd)
        if "vf id" in vf_list.lower():
            output_file.write('<br><h1"style="font-size:5px;"><b>%s:'
                              '</b><br></H1>' % iface)
            output_file.write('<p><PRE>%s</PRE></p>' % vf_list)
            sriov_uplink_list.append(iface)
    # Get ESXi mode specific information:
    if mode == "esxi":
        output_file.write('<h1"style="font-size:18px;"><b>cat /etc/vmware/esx.conf for max_vfs:'
                          '</b><br></H1>')
        vfs_esx_conf = execute("cat /etc/vmware/esx.conf | grep max_vfs")
        output_file.write('<p><PRE>%s</PRE></p>' % vfs_esx_conf)

        # get vfs stats:
        output_file.write('<h1"style="font-size:18px;"><b>VF-Statistics:'
                          '</b><br></H1>')
        for iface in sriov_uplink_list:
            output_file.write('<br><h1"style="font-size:5px;"><b>%s:'
                              '</b><br></H1>' % iface)
            get_vfs = "vsish -e ls net/sriov/%s/vfs"%iface
            vf_nos = execute(get_vfs)
            vf_count = []
            for line in vf_nos.splitlines():
                vf = line.replace('/', '')
                vf_count.append(vf)
            for v in vf_count:
                vf_stats_cmd = "vsish -e get net/sriov/" + iface +"/vfs/" + v + "/stats"
                vf_stats = execute(vf_stats_cmd)
                if vf_stats == 1:
                    output_file.write('<br><h1"style="font-size:5px;"><b>%s :'
                                      '</b></H1>' % iface)
                    output_file.write("ERROR: %s failed to fetch stats<br>" % vf_stats_cmd)
                    continue
                else:
                    output_file.write('<br><h1"style="font-size:18px;"><b>%s' % iface +'-VF stats for for VF#%s:'
                                      '</b><br></H1>' % v)
                    collapse(output_file, vf_stats, "Stats")
        # get uplink hwCapabilities
        output_file.write('<br><h1"style="font-size:5px;"><b>vsish -e cat /net/pNics/<uplink>'
                          '/hwCapabilities/CAP_SRIOV:</b><br></H1>')
        for iface in sriov_uplink_list:
            hw_cap_cmd = "vsish -e cat /net/pNics/" + iface + "/hwCapabilities/CAP_SRIOV"
            hw_cap = execute(hw_cap_cmd)
            output_file.write('<br><h1"style="font-size:5px;"><b>%s:'  % iface + '%s'
                              '</b><br></H1>' %hw_cap)
    return 0

def file_properties(output_file, server, mode):
    """function to get file properties of SF drivers"""
    file_cmd = "esxcli "+ server + " software vib get"
    file_status = execute(file_cmd, mode)
    if file_status == 1:
        output_file.write('Not Available')
        return
    file_status = re.search('(.*)Name\:\ssfvmk(.*)Payloads\:\ssfvmk',\
                            file_status, re.DOTALL)
    sfvmk_info = file_status.group(2)
    table = '<table id="File Properties"style="font-size:26px;"><th>\
             File Properties: </th><table border="1">'
    for line in sfvmk_info.split('\n'):
        line_len = len(line.strip())
        if line_len != 0:
            try:
                line = re.search('(.*)\:(.*)', line)
                param_name = line.group(1)
                param_val = line.group(2)
            except AttributeError:
                param_name = "not updated"
                param_val = "not updated"
            table += '<tr><th align=left> %s' % param_name + '</th><td>\
                     %s </td><tr>' % param_val
    table += '</table>'
    output_file.write(table)
    return 0

def arp_cache(output_file, server, mode):
    """function to fetch arp information."""
    table = '<table id="ARP Cache"style="font-size:26px;"><th>ARP Cache:</th>\
            <table border="1">'
    for hdr in ('Neighbor', 'MAC Address', 'Vmknic', 'Expiry', 'State/Type'):
        table += '<th>%s' % hdr + '</th>'
    arp_cmd = "esxcli " + server + " network ip neighbor list"
    arp_info = execute(arp_cmd, mode)
    if arp_info == 1:
        output_file.write('Not Available')
        return
    for line in arp_info.split('\n'):
        if line != "" and not line.startswith("---") \
                      and not line.startswith("Neighbor"):
            try:
                line = re.search('(\d+\.\d+\.\d+\.\d+)\s+(\w+\:\w+\:\w+\:\w+\:\w+\:\w+)'
                                 '\s+(\w+)\s+(\w+\s\w+)\s+(.*)', line)
                neighbor = line.group(1)
                mac_addr = line.group(2)
                vmknic = line.group(3)
                expiry = line.group(4)
                state = line.group(5)
            except AttributeError:
                neighbor = "not updated"
                mac_addr = "not updated"
                vmknic = "not updated"
                expiry = "not updated"
                state = "not updated"
            table += '</tr><td>%s'%neighbor +'<td>%s'%mac_addr +'<td>%s'%vmknic \
                     +'<td>%s'% expiry +'<td>%s'%state
    table += '</table>'
    output_file.write(table)
    return 0

def virtual_machine_info(output_file, server, mode):
    """function to fetch Virtual Machine information on the host"""
    output_file.write('<h1 id="Virtual Machine"style="font-size:26px;"> \
                           Virtual Machine: <br></H1>')
    vm_cmd = "esxcli " + server + " network vm list"
    vm_info = execute(vm_cmd, mode)
    if vm_info == 1:
        output_file.write("INFO:No Active Virtual Machines found.")
        return 1
    output_file.write('<p><PRE>%s</PRE></p>' % vm_info)

    return 0

def portgroup_details(output_file, server, mode):
    """function to fetch Vswitch portgroup informations"""
    output_file.write('<h1 id="Portgroup Information"style="font-size:26px;"> \
                       Portgroup Information: <br></H1>')
    pg_cmd = "esxcli " + server + " network vswitch standard portgroup list"
    pg_info = execute(pg_cmd, mode)
    if pg_info == 1:
        output_file.write('Not Available')
        return
    output_file.write('<p><PRE>%s</PRE></p>' % pg_info)
    return 0

def hw_statistics(output_file, server, mode, adapter_list):
    """function to fetch Solarflare statistic counters"""
    hw_q_start = False
    direction = None
    failed_nic_stats = []
    no_of_interfaces = len(adapter_list)
    output_file.write('<h1 id="HW Statistics"style="font-size:26px;"> \
                        HW Statistics: <br></H1>')
    interface_count = 1
    sf_table = '<table><table border="1">'
    sf_table += '<th>Interface_name</th>'
    sf_stats_dict = defaultdict()
    for interface in adapter_list:
        sf_stats_cmd = 'esxcli ' + server + ' sfvmk stats  get -n '\
                       + interface
        sf_stats_values = execute(sf_stats_cmd, mode)
        if sf_stats_values == 1:
            failed_nic_stats.append(interface)
            continue
        elif sf_stats_values == None:
            output_file.write("ERROR: error fetching statistics")
            return
        sf_table += '<td> %s </td>' % interface
        if interface_count == no_of_interfaces:
            sf_table += '</tr>'
        for line in sf_stats_values.splitlines():
            if len(line):
                if line.startswith("  -- Per Hardware Queue Statistics"):
                    hw_q_start = True
                    continue
                if not hw_q_start:
                    lhs, rhs = line.split(':', 1)
                    if lhs not in sf_stats_dict:
                        sf_stats_dict[lhs] = []
                    sf_stats_dict[lhs.strip()].append(rhs.strip())
            if hw_q_start:
                if ":" in line and line not in sf_stats_dict:
                    direction = line
                else:
                    stats_parse = line.split()
                    num_stats = len(stats_parse) // 2
                    if num_stats*2 != len(stats_parse):
                        continue
                    for i in range(0, len(stats_parse), 2):
                        stats_param = direction + stats_parse[i]
                        stats_value = stats_parse[i+1]
                        if stats_param not in sf_stats_dict:
                            sf_stats_dict[stats_param] = []
                        sf_stats_dict[stats_param].append(stats_value)
        interface_count += 1
        hw_q_start = False
    stats_dict = sorted(sf_stats_dict.items())
    for key, values in stats_dict:
        sf_table += '<th align=left>%s</th>' % key
        for value in values:
            sf_table += '<td> %s </td>' % value
        sf_table += '</tr>'
    sf_table += '</table>'
    collapse(output_file, sf_table, "H/W Statistics")
    if failed_nic_stats:
        output_file.write("SF statistics not available for interfaces:%s"
                          %failed_nic_stats)
    return 0

def vswitch_details(output_file, server, mode):
    """function to fetch Vswitch portgroup informations"""
    output_file.write('<h1 id="vSwitch"style="font-size:26px;">\
                      vSwitch: <br></H1>')
    vswitch_cmd = "esxcli " + server + " network vswitch standard list"
    vswitch_info = execute(vswitch_cmd)
    if vswitch_info == 1:
        output_file.write('Not Available')
        return
    vswitch_info = vswitch_info.split('\n')
    lines = ('<p>')
    for line in vswitch_info:
        lines += '<small>%s</small><br>' % line
    output_file.write('%s</p>' % lines)
    output_file.write('<h1 style="font-size:26px;">Distributed vSwitch \
                      Information: <br></H1>')
    dvs_cmd = "esxcli " + server + " network vswitch dvs vmware list"
    dvs_info = execute(dvs_cmd, mode)
    if dvs_info == 1 or dvs_info is None:
        output_file.write("INFO:No Distributed vSwitches were found")
        return 1
    dvs_info = dvs_info.split('\n')
    lines = ('<p>')
    for line in dvs_info:
        lines += '<small>%s</small><br>' % line
    output_file.write('%s</p>' % lines)
    return 0

def lacp_details(output_file, server, mode):
    """function to fetch information about LACP"""
    output_file.write('<h1 id="LACP"style="font-size:26px;">\
                          LACP: <br></H1>')
    output_file.write('<h1"style="font-size:18px;"><b>LACP Status:</b><br></H1>')
    lacp_status_cmd = "esxcli " + server + " network vswitch dvs vmware lacp status get"
    lacp_status = execute(lacp_status_cmd, mode)
    if lacp_status == 1:
        output_file.write('Not Available')
        return
    if lacp_status.startswith("LACP is supported"):
        output_file.write("INFO: LACP is not configured on ESXi host")
        return 1
    output_file.write('<p><PRE>%s</PRE></p>' % lacp_status)
    lacp_config_cmd = "esxcli " + server + " network vswitch dvs vmware lacp config get"
    lacp_config = execute(lacp_config_cmd, mode)
    if lacp_config == 1:
        return
    output_file.write('<h1"style="font-size:18px;"><b>LACP Configuration:</b><br></H1>')
    output_file.write('<p><PRE>%s</PRE></p>'% lacp_config)
    output_file.write('<h1"style="font-size:18px;"><b>LACP Stats:</b><br></H1>')
    lacp_stats_cmd = "esxcli " + server + " network vswitch dvs vmware lacp stats get"
    lacp_stats = execute(lacp_stats_cmd, mode)
    if lacp_stats == 1:
        output_file.write('Not Available')
        return
    output_file.write('<p><PRE>%s</PRE></p>' % lacp_stats)
    return 0

def numa_information(output_file):
    """function to fetch the memory information."""
    output_file.write('<h1 id="NUMA Information"style="font-size:26px;">\
                      NUMA Information: <br></H1>')
    meminfo_cmd = "vsish -e cat /memory/memInfo"
    mem_info = execute(meminfo_cmd)
    if mem_info == 1:
        output_file.write('Not Available')
        return
    mem_info = mem_info.split('\n')
    lines = ('<p>')
    for line in mem_info:
        lines += '<small>%s</small><br>' % line
    output_file.write('%s</p>' % lines)
    return 0

def dns_information(output_file):
    """function to fetch the dns information."""
    output_file.write('<h1 style="font-size:26px;">Configuration file\
                       /etc/hosts: <br></H1>')
    dns_cmd = "cat /etc/hosts"
    dns_info = execute(dns_cmd)
    if dns_info == 1:
        output_file.write('Not Available')
        return
    dns_info = dns_info.split('\n')
    get_dns = "dnsdomainname"
    srvr_dns = execute(get_dns)
    if srvr_dns == 1:
        output_file.write('Not Available')
        return
    lines = ('<p>')
    for line in dns_info:
        if not line.startswith("#"):
            lines += '<small>%s</small><br>' % line
    output_file.write("DNS: "+srvr_dns)
    output_file.write('%s</p>' % lines)
    return 0

#- start here
if __name__ == "__main__":
    PARSER = optparse.OptionParser(usage="usage: %prog [options]")
    PARSER.add_option("-m", "--mode",
                      action="store",
                      dest="mode",
                      default="esxi",
                      help="vcli/esxi[default=esxi]")
    PARSER.add_option("-s", "--server",
                      action="store",
                      dest="server",
                      default="",
                      help="<server_name>[needed while using vcli only]")
    OPTIONS, ARGS = PARSER.parse_args()
    SERVER_NAME = OPTIONS.server
    CURRENT_MODE = OPTIONS.mode.lower()
    SFVMK_ADAPTERS = []
    SFVMK_TLPS = []
    ESX_VER = None
    CIM_VER = None
    SFVMK_VER = None
    CLI_VER = None
    SF_VER = {}
    CLI_VIB = False
    CURRENT_TIME = (execute('date +"%Y-%m-%d-%H-%M-%S"'))
    vcli_connectivity = None
    if CURRENT_TIME == 1:
        print("Error: Couldn't retrieve current time on the system")
        sys.exit()
    TXT = "sfout_" + CURRENT_TIME.strip('\n') + ".txt"
    TXT_FILE = open(TXT, 'w')
    OS_TYPE = execute("uname -o", CURRENT_MODE)
    if OS_TYPE == 1:
        print ("Error: Couldn't retrieve OS version")
        sys.exit()
    if CURRENT_MODE not in ['vcli', 'esxi']:
        print("\nOption Error: -m|--mode can be either 'vcli' or 'esxi'\n")
        sys.exit()
    # Check when mode=vcli then os type should not be ESXi.
    # Proceed only if vcli is run on vcli enabled Linux OS.
    if CURRENT_MODE == "vcli" and "esxi" in OS_TYPE.lower():
        print("\nOption Error: when --mode=vcli,OS type should be vcli "
              "enabled Linux\n")
        sys.exit()
    elif "esxi" not in OS_TYPE.lower() and CURRENT_MODE == "esxi":
        print("\nError: Cannot run mode=esxi on current host for Solarflare "
              "System Report generation")
        sys.exit()
    # Proceed only if vcli is specified with server option.
    elif CURRENT_MODE == "vcli" and not SERVER_NAME:
        print("\nError: while running mode=vcli, --server option should "
              "be specified")
        sys.exit()
    elif CURRENT_MODE == "esxi" and SERVER_NAME:
        print("\nError: while running mode=esxi, --server option is "
              "not required\n")
        sys.exit()
    # Proceed only if any SF adapters are found.
    if CURRENT_MODE == "vcli":
        SERVER_NAME = "--server " + SERVER_NAME
    GET_NIC_CMD = 'esxcli ' + SERVER_NAME + ' network nic list |grep sfvmk'
    SF_ADAPTERS = execute(GET_NIC_CMD, CURRENT_MODE)
    if SF_ADAPTERS == 1 or SF_ADAPTERS == None:
        if vcli_connectivity == "false":
           sys.exit()
        print("\nCAUTION: Either sfvmk driver is NOT loaded OR Solarflare "
              "NIC is NOT visible\n")
        sys.exit()
    # check if connection not established between vcli server and host:
    if not SF_ADAPTERS and CURRENT_MODE == 'vcli':
        print("\nERROR: Failed to establish connection with ",SERVER_NAME)
        sys.exit()
    if SF_ADAPTERS:
        for string in SF_ADAPTERS.split('\n'):
            if string != "":
                iface = re.search('(\w+)\s+(\w+\:\w+\:\w+\.\w+)\s(.*)', string)
                SFVMK_ADAPTERS.append(iface.group(1))
                SFVMK_TLPS.append(iface.group(2))
    # get ESX version
    ESX_VER_CMD = 'esxcli ' + SERVER_NAME + ' system version get'
    ESX_VER_GET = execute(ESX_VER_CMD, CURRENT_MODE)
    if ESX_VER_GET == 1 or ESX_VER_GET == None:
        print ("Error: Couldn't retrieve OS version")
        sys.exit()
    for LINE in ESX_VER_GET.splitlines():
        LHS, RHS = LINE.split(':', 1)
        if LHS.lower().strip(' ') == "version":
            ESX_VER = RHS.strip(' ')
    SF_VER['ESX Version'] = ESX_VER
    # get sfvmk driver version
    SFVMK_VIB_LIST = execute("esxcli  " + SERVER_NAME + " software vib list |grep 'sfvmk ' ")
    if SFVMK_VIB_LIST == 1:
        print ("Error: Couldn't retrieve sfvmk driver version")
        sys.exit()
    if SFVMK_VIB_LIST:
        for I in SFVMK_VIB_LIST.splitlines():
            J = re.split(r'\s+', I)
            SFVMK_VER = J[1] #sfvmk version
            SF_VER['Driver Version'] = SFVMK_VER
    # get esxcli_ext vib info:
    CLI_VIB_LIST = execute("esxcli " + SERVER_NAME + "  software vib list |grep sfvmkcli")
    if CLI_VIB_LIST == 1:
        print ("Error: Couldn't retrieve sfvmk cli version")
        sys.exit()
    if CLI_VIB_LIST:
        for k in CLI_VIB_LIST.splitlines():
            L = re.split(r'\s+', k)
            CLI_VER = L[1] #esxcli ext version
            SF_VER['CLI Version'] = CLI_VER
            CLI_VIB = True
    # get CIM vib info:
    CIM_VIB_LIST = execute("esxcli " + SERVER_NAME + " software vib list |grep solar |grep cim")
    if CIM_VIB_LIST == 1:
        print ("Error: Couldn't retrieve CIM version")
        sys.exit()
    if CIM_VIB_LIST:
        for m in CIM_VIB_LIST.splitlines():
            n = re.split(r'\s+', m)
            CIM_VER = n[1] # cim version
            SF_VER['CIM Version'] = CIM_VER
    # get SRIOV status
    SRIOV_LIST = execute("esxcli " + SERVER_NAME + " network sriovnic list")
    if "sfvmk" in SF_ADAPTERS:
        print("sfreport version: "+ SFREPORT_VERSION)
        print("Solarflare Adapters detected..")
        print("Please be patient.\n"
              "Solarflare system report generation is in progress....")

        HTML_FILE = 'sfreport-' + CURRENT_TIME.strip('\n') + '.html'
        OUT_FILE = open(HTML_FILE, 'w')
        # Call the feature specific funtions.
        file_header(OUT_FILE, CURRENT_TIME, CURRENT_MODE, SFREPORT_VERSION)
        OUT_FILE.write('<h1 style="font-size:26px;"> Report Index:<br></H1>')
        OUT_FILE.write('<a href="#System Summary">-> System Summary</a><br>')
        OUT_FILE.write('<a href="#Software Versions">-> Software Versions</a><br>')
        if CLI_VIB:
            OUT_FILE.write('<a href="#VPD Information">-> VPD Information</a><br'
                           '>')
            OUT_FILE.write('<a href="#Sensor Information">-> Sensor Information</a><br'
                           '>')
        OUT_FILE.write('<a href="#Driver Bindings">-> Driver Bindings</a><br>')
        OUT_FILE.write('<a href="#SFVMK Parameters">-> Sfvmk Parameters</a><br>\
                       ')
        OUT_FILE.write('<a href="#Ethernet Settings">-> Ethernet Settings\
                        </a><br>')
        OUT_FILE.write('<a href="#Network Configuration">-> Network\
                        Configuration  </a><br>')
        OUT_FILE.write('<a href="#SF PCI Devices">-> SF PCI Devices</a><br>')
        OUT_FILE.write('<a href="#File Properties">-> File Properties</a><br>')
        OUT_FILE.write('<a href="#ARP Cache">-> ARP Cache</a><br>')
        OUT_FILE.write('<a href="#Virtual Machine">-> Virtual Machine</a><br>')
        OUT_FILE.write('<a href="#vSwitch">-> vSwitch</a><br>')
        OUT_FILE.write('<a href="#LACP">-> LACP</a><br>')
        OUT_FILE.write('<a href="#Portgroup Information">-> Portgroup \
                        Information </a><br>')
        OUT_FILE.write('<a href="#GENEVE">-> GENEVE </a><br>')
        OUT_FILE.write('<a href="#VXLAN">-> VXLAN </a><br>')
        OUT_FILE.write('<a href="#Interface Statistics">-> Interface Statistics\
                        </a><br>')
        if CLI_VIB:
            OUT_FILE.write('<a href="#HW Statistics">-> HW Statistics\
                                </a><br>')
        if SRIOV_LIST:
            OUT_FILE.write('<a href="#SRIOV">-> SRIOV\
                                            </a><br>')
        if CURRENT_MODE == "esxi":
            # Following module needs to be imported here as vcli python version
            # doesn't have this module included [python-ver < 3]
            from collections import OrderedDict
            OUT_FILE.write('<a href="#NetQueue Summary">-> NetQueue Summary\
                            </a><br>')
            OUT_FILE.write('<a href="#SF Module File Names">-> SF Module File Names\
                            </a><br>')
            OUT_FILE.write('<a href="#Known Kernel Modules">->\
                           Known Kernel Modules </a><br>')
            OUT_FILE.write('<a href="#NUMA Information">-> NUMA Information</a><br>')
            OUT_FILE.write('<a href="#VMkernel Logs">-> VMkernel Logs</a><br>')
            if CIM_VIB_LIST:
               OUT_FILE.write('<a href="#CIM Logs">-> CIM Logs</a><br>')
            OUT_FILE.write('<a href="#PCI Configuration">-> PCI Configuration\
                            </a><br>')
        try:
            system_summary(OUT_FILE, SERVER_NAME, CURRENT_MODE)
        except:
            OUT_FILE.write("WARNING: system_summary failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            sw_versions(OUT_FILE, SERVER_NAME, SFVMK_ADAPTERS, CURRENT_MODE,
                    SF_VER, CLI_VIB)
        except:
            OUT_FILE.write("WARNING: sw_versions failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        if CLI_VIB:
            try:
                vpd_information(OUT_FILE, SERVER_NAME, CURRENT_MODE, SFVMK_ADAPTERS)
            except:
                OUT_FILE.write("WARNING: vpd_information failed due to following reasons:<br>")
                OUT_FILE.write("Traceback:%s" % traceback.format_exc())
            try:
                get_sensor_info(OUT_FILE, SERVER_NAME, CURRENT_MODE, SFVMK_ADAPTERS)
            except:
                OUT_FILE.write("WARNING: get_sensor_info failed due to following reasons:<br>")
                OUT_FILE.write("Traceback:%s" % traceback.format_exc())
        try:
            driver_binding(OUT_FILE, SERVER_NAME, CURRENT_MODE)
        except:
            OUT_FILE.write("WARNING: driver_binding failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            sfvmk_parameter_info(OUT_FILE, SERVER_NAME, SFVMK_ADAPTERS, CURRENT_MODE,
                             CLI_VIB)
        except:
            OUT_FILE.write("WARNING: sfvmk_parameter_info failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            ethernet_settings(OUT_FILE, SFVMK_ADAPTERS, SERVER_NAME, CURRENT_MODE, CLI_VIB)
        except:
            OUT_FILE.write("WARNING: ethernet_settings failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            network_configuration(OUT_FILE, SERVER_NAME, CURRENT_MODE, ESX_VER)
        except:
            OUT_FILE.write("WARNING: network_configuration failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            sf_pci_devices(OUT_FILE, SFVMK_TLPS, SERVER_NAME, CURRENT_MODE)
        except:
            OUT_FILE.write("WARNING: sf_pci_devices failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            file_properties(OUT_FILE, SERVER_NAME, CURRENT_MODE)
        except:
            OUT_FILE.write("WARNING: file_properties failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            arp_cache(OUT_FILE, SERVER_NAME, CURRENT_MODE)
        except:
            OUT_FILE.write("WARNING: arp_cache failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            virtual_machine_info(OUT_FILE, SERVER_NAME, CURRENT_MODE)
        except:
            OUT_FILE.write("WARNING: virtual_machine_info failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            vswitch_details(OUT_FILE, SERVER_NAME, CURRENT_MODE)
        except:
            OUT_FILE.write("WARNING: vswitch_details failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            lacp_details(OUT_FILE, SERVER_NAME, CURRENT_MODE)
        except:
            OUT_FILE.write("WARNING: lacp_details failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            portgroup_details(OUT_FILE, SERVER_NAME, CURRENT_MODE)
        except:
            OUT_FILE.write("WARNING: portgroup_details failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            get_geneve_info(OUT_FILE, SERVER_NAME, CURRENT_MODE)
        except:
            OUT_FILE.write("WARNING: get_geneve_info failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        try:
            get_vxlan_info(OUT_FILE, SERVER_NAME, CURRENT_MODE)
        except:
            OUT_FILE.write("WARNING: get_vxlan_info failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        if SRIOV_LIST:
            get_sriov_info(OUT_FILE, SERVER_NAME, CURRENT_MODE, SFVMK_ADAPTERS, SRIOV_LIST)
        try:
            interface_statistics(OUT_FILE, SFVMK_ADAPTERS, SERVER_NAME, CURRENT_MODE)
        except:
            OUT_FILE.write("WARNING: interface_statistics failed due to following reasons:<br>")
            OUT_FILE.write("Traceback:%s"%traceback.format_exc())
        if CLI_VIB:
            try:
                hw_statistics(OUT_FILE, SERVER_NAME, CURRENT_MODE, SFVMK_ADAPTERS)
            except:
                OUT_FILE.write("WARNING: hw_statistics failed due to following reasons:<br>")
                OUT_FILE.write("Traceback:%s" % traceback.format_exc())
        # run esxi specific functions under this check.
        if CURRENT_MODE == "esxi":
            try:
                net_queue_status(OUT_FILE, SFVMK_ADAPTERS)
            except:
                OUT_FILE.write("WARNING: net_queue_status failed due to following reasons:<br>")
                OUT_FILE.write("Traceback:%s" % traceback.format_exc())
            try:
                sf_module_file(OUT_FILE)
            except:
                OUT_FILE.write("WARNING: sf_module_file failed due to following reasons:<br>")
                OUT_FILE.write("Traceback:%s" % traceback.format_exc())
            try:
                sf_kernel_modules(OUT_FILE)
            except:
                OUT_FILE.write("WARNING: sf_kernel_modules failed due to following reasons:<br>")
                OUT_FILE.write("Traceback:%s" % traceback.format_exc())
            try:
                numa_information(OUT_FILE)
            except:
                OUT_FILE.write("WARNING: numa_information failed due to following reasons:<br>")
                OUT_FILE.write("Traceback:%s" % traceback.format_exc())
            try:
                vmkernel_logs(OUT_FILE)
            except:
                OUT_FILE.write("WARNING: vmkernel_logs failed due to following reasons:<br>")
                OUT_FILE.write("Traceback:%s" % traceback.format_exc())
            if CIM_VIB_LIST:
               try:
                   cim_logs(OUT_FILE)
               except:
                   OUT_FILE.write("WARNING: cim_logs failed due to following reasons:<br>")
                   OUT_FILE.write("Traceback:%s" % traceback.format_exc())
            try:
                pci_configuration_space(OUT_FILE)
            except:
                OUT_FILE.write("WARNING: pci_configuration_space failed due to following reasons:<br>")
                OUT_FILE.write("Traceback:%s" % traceback.format_exc())
        call_jscript(OUT_FILE)
        tarball = "sfreport_" + CURRENT_TIME.strip('\n') + ".tgz"
        tar_cmd = "tar cvf %s %s %s " %(tarball,HTML_FILE, TXT)
        try:
            tar_out = execute(tar_cmd, CURRENT_MODE)
            print('Generated output file: '+tarball)
        except:
            print("WARNING! Unable to create tarball")
            print("Please share following files:%s & %s" %(HTML_FILE, TXT))
    else:
        print("No SolarFlare Adapters were found")
        sys.exit()
