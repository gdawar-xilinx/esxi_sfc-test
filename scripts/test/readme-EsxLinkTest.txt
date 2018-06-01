#Prerequisite(On running windows):Make sure you install following packages on windows machine to be used further for script execution:

pip install pycrypto
pip install paramiko
pip install pywin32

Use 2 VM's each on a particular ESX host.One pointing to vmnic4 and other pointing to vmnic5.

PortIndependence as well as LinkTest can be validated using this script.

Usage: EsxLinkTest.py [options]
==========================================
Options:
  -h, --help            show this help message and exit
  -m TARGET_MACHINES, --machines=TARGET_MACHINES
                        Mention 2 target machines<DUTHOST,AUXHost>
  -v VIRTUAL_MACHINES, --virtualmachines=VIRTUAL_MACHINES
                        Mention VM's Management IP's <DUT_VM,AUX_VM>
  -e VIRTUAL_MACHINE_ENDPOINTS, --endpoints=VIRTUAL_MACHINE_ENDPOINTS
                        Mention VM's test interface IP's
                        <DUT_VM_TEST_IP,AUX_VM_TEST_IP>
  -n VMNICS, --vmnics=VMNICS
                        Mention ESX uplink interfaces
                        <DUT_VMNIC1,AUX_VMNIC1,(DUT_VMNIC2,AUX_VMNIC2)>
  -t TEST_TYPE, --testType=TEST_TYPE
                        Mention Test Type i.e Linktest,PortIndependence
  -l DURATION, --Duration=DURATION
                        Mention duration for a particular testType


Before executing script have a look at the disruptive events you want to enable:
================================================================================
Current List:
disruptive_events = { 'DRIVER_RELOAD':DRIVER_RELOAD_CMD, 'DIAGNOSTICS': DIAGNOSTICS_CMD,'LINK_DOWN': LINK_DOWN_CMD,'LINK_UP': LINK_UP_CMD}

Complete List:
#disruptive_events = {'MC_REBOOT':MC_REBOOT_CMD,'LINK_DOWN':LINK_DOWN_CMD,'LINK_UP':LINK_UP_CMD,'DRIVER_UNLOAD':DRIVER_UNLOAD_CMD,'DRIVER_LOAD':DRIVER_LOAD_CMD,'VxLAN_DRIVER_RELOAD':VxLAN_DRIVER_RELOAD_CMD, 'RSS_DRIVER_RELOAD':RSS_DRIVER_RELOAD_CMD, 'NETQUEUE_DRIVER_RELOAD':NETQUEUE_DRIVER_RELOAD_CMD}

Sample Command - To execute LinkTest :
======================================
EsxLinkTest_new.py -m ndr730g.nd.solarflarecom.com,ndr730h.nd.solarflarecom.com -v 10.40.128.181,10.40.128.59 -e 192.168.3.1,192.168.3.11 -n vmnic4,vmnic4 -t Linktest -l 21600



Sample Command - To execute PortIndependence :
==============================================
EsxLinkTest.py -m ndr730o.nd.solarflarecom.com,ndr730f.nd.solarflarecom.com -v 10.40.160.134,10.40.160.161,10.40.160.176,10.40.160.180 -e 172.16.1.1,172.16.1.11,192.168.6.183,192.168.6.191 -n vmnic4,vmnic4,vmnic5,vmnic5 -t PortIndependence -l 300


NOTE: BY default silentFlag='True' for PortIndependence tests signifying that Netperf throughput results will be monitored for this link only while no Netperf test is in progress and only normal/disruptive events are getting triggered on other link.
Changing silentFlag='False' for PortIndependence test will additionally enable Netperf thread on both links. 