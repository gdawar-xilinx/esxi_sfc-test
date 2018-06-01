# coding: utf-8
#Importing modules
import paramiko
import sys
import time
from optparse import OptionParser
import thread
import re
import random
import os
import datetime
from random import randint
import webbrowser
import datetime
from webbrowser import open_new_tab
import win32com.client as win32
from collections import OrderedDict
#Prerequisite(On running windows):
#pip install pycrypto
#pip install paramiko
#pip install pywin32


parser = OptionParser()
parser.add_option("-m", "--machines", dest="Target_Machines",help="Mention 2 target machines<DUTHOST,AUXHost>")
parser.add_option("-v", "--virtualmachines", dest="Virtual_Machines",help="Mention VM's Management IP's <DUT_VM,AUX_VM>")
parser.add_option("-e", "--endpoints", dest="Virtual_Machine_Endpoints",help="Mention VM's test interface IP's <DUT_VM_TEST_IP,AUX_VM_TEST_IP>")
parser.add_option("-n", "--vmnics", dest="VMNICS",help="Mention ESX uplink interfaces <DUT_VMNIC1,AUX_VMNIC1,(DUT_VMNIC2,AUX_VMNIC2)>")
parser.add_option("-t", "--testType", dest="Test_Type",help="Mention Test Type i.e Linktest,PortIndependence")
#parser.add_option("-i", "--Iterations", dest="Iterations",help="Mention no. of Iterations for a particular testType")
parser.add_option("-l", "--Duration", dest="Duration",help="Mention duration for a particular testType")
(options, args) = parser.parse_args()

#print "Options are",options
#print "Arguments are",args
print "Connected Machines:",options.Target_Machines


if len(options.Target_Machines.split(',')) != 2:
  parser.error("Wrong number of host machines-should be 2")
else:
  DUT_HOST=options.Target_Machines.strip().split(',')[0]
  AUX_HOST=options.Target_Machines.strip().split(',')[1]

if len(options.Virtual_Machines.split(',')) > 4:
  parser.error("Number of virtual machines-should be either 2 or 4")
elif len(options.Virtual_Machines.split(',')) == 4 :
  #print "******SplitVMMgmtIP:Check PASS******\n"
  DUT_VM_MGMT=options.Virtual_Machines.strip().split(',')[0]
  AUX_VM_MGMT=options.Virtual_Machines.strip().split(',')[1]
  DUT_VM2_MGMT=options.Virtual_Machines.strip().split(',')[2]
  AUX_VM2_MGMT=options.Virtual_Machines.strip().split(',')[3]
else:
  DUT_VM_MGMT=options.Virtual_Machines.strip().split(',')[0]
  AUX_VM_MGMT=options.Virtual_Machines.strip().split(',')[1]

if len(options.VMNICS.split(',')) > 4:
  parser.error("Number of vmnics-should be either 2 or 4")
else:
 vmnics = options.VMNICS.strip().split(',')

if len(options.Virtual_Machine_Endpoints.split(',')) == 4:
  #print "******SplitVMTestIP:Check PASS******\n"
  DUT_VM_TEST=options.Virtual_Machine_Endpoints.strip().split(',')[0]
  AUX_VM_TEST=options.Virtual_Machine_Endpoints.strip().split(',')[1]
  DUT_VM2_TEST=options.Virtual_Machine_Endpoints.strip().split(',')[2]
  AUX_VM2_TEST=options.Virtual_Machine_Endpoints.strip().split(',')[3]
elif len(options.Virtual_Machine_Endpoints.split(',')) == 2:
  DUT_VM_TEST = options.Virtual_Machine_Endpoints.strip().split(',')[0]
  AUX_VM_TEST = options.Virtual_Machine_Endpoints.strip().split(',')[1]
else:
  parser.error("Wrong number of VM machines test interface-should be 2 or 4 ")

if options.Test_Type not in ('Linktest','PortIndependence'):
  parser.error('Invalid Test_Type specified')
else:
  testType=options.Test_Type

if options.Duration is None:
  DURATION = 300
elif int(options.Duration) < 300:
  print "Test Duration set to 300 secs"
  DURATION = 100
else:
  DURATION=int(options.Duration)

if options.Test_Type=='Linktest':
  LinkTest_Throughput = {}
elif options.Test_Type=='PortIndependence':
  PortIndependence_Throughput = {}

print "Test Type:",options.Test_Type
#HOST = "ndr730b.nd.solarflarecom.com"
#HOST1 = "10.40.128.12"
#HOST2 = "10.40.128.13"
#DUT_VM_TEST = "172.16.0.12"
#AUX_VM_TEST = "172.16.0.13"
USER = "root"
PASS = "news@ben"
#ITERATION = 1

dir = os.getcwd()
date = datetime.datetime.now()
now = date.strftime("%Y-%m-%d_%H-%M")
print now
logFile= os.path.join(dir,testType+"_TestResults_"+now+".txt")
htmlLogFile=os.path.join(dir,testType+"_TestResult_"+now+".html")

def wrapHTML(htmlLogFile,body,prepend=0):


    if prepend==1:
      f = open(htmlLogFile, 'r+')
      readcontent = f.read()
      f.seek(0,0)
      wrapper = "<html>\n"
      wrapper += "<body style=\"background-color:lemonchiffon;\">\n"

      if re.search( r'NETPERF RESULTS SUMMARY', body):
        wrapper += "<h1></h1>\n"
        wrapper +="<p style=\"font-size:22px;color:darkred\">%s</p>\n"
      elif re.search( r'---', body):
        wrapper += "<h1></h1>\n"
        wrapper +="<p style=\"font-size:22px;color:darkred\">%s</p>\n"
      elif re.search( r'RELEASE INFO', body,re.I):
        wrapper += "<h1></h1>\n"
        wrapper +="<p style=\"font-size:22px;color:darkred;\">%s</p>\n"
      elif re.search( r'TEST RESULTS:', body):
        wrapper += "<h1 style=\"color:darkred;\">%s</h1>\n"
        wrapper += "<p></p>\n"
      else:
        wrapper += "<h1></h1>\n"
        wrapper +="<p style=\"color:blue;\">%s</p>\n"
      wrapper +="</body>\n"
      wrapper +="</html>\n"
      whole = wrapper % (body)
      f.write(whole)
      f.write(readcontent)
    else:
      f = open(htmlLogFile, 'a+')
      wrapper = "<html>\n"
      wrapper += "<body style=\"background-color:lemonchiffon;\">\n"
      wrapper += "<h1></h1>\n"
      if re.search( r'Throughput:', body):
        wrapper +="<p style=\"font-size:22px;color:Navy\">%s:%s</p>\n"
      elif re.search( r'fail', body,re.I):
        wrapper +="<p style=\"font-size:25px;color:red;\">%s:%s</p>\n"
      else:
        wrapper +="<p style=\"color:blue;\">%s:%s</p>\n"
      wrapper +="</body>\n"
      wrapper +="</html>\n"
      if re.search( r'\=\=|\+\+', body):
        whole = wrapper % (re.search( r'\=\=|\+\+',body).group(0),body)
      else:
        whole = wrapper % (time.ctime(time.time()),body)
      f.write(whole)
    f.close()

#A function that logins and execute commands
def connect_machines(testType,vmnics):


  DUT_ESX_CONNECT=paramiko.SSHClient()
  #Add missing client key
  DUT_ESX_CONNECT.set_missing_host_key_policy(paramiko.AutoAddPolicy())
  #connect to switch
  DUT_ESX_CONNECT.connect(DUT_HOST,username=USER,password=PASS,timeout=30)
  print "SSH connection to %s established" %DUT_HOST

  AUX_ESX_CONNECT = paramiko.SSHClient()
  # Add missing client key
  AUX_ESX_CONNECT.set_missing_host_key_policy(paramiko.AutoAddPolicy())
  # connect to switch
  AUX_ESX_CONNECT.connect(AUX_HOST, username=USER, password=PASS,timeout=30)
  print "SSH connection to %s established" % AUX_HOST

  PASS2='news@ben'
  silentFlag='True'
  if testType=='Linktest' or (testType=='PortIndependence' and silentFlag=='False'):
    #print "%%%%%%% LinkTestPath1 %%%%%%%%%%\n"
    DUT_VM_CONNECT=paramiko.SSHClient()
    #Add missing client key
    DUT_VM_CONNECT.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    #connect to switch
    DUT_VM_CONNECT.connect(DUT_VM_MGMT,username=USER,password=PASS2,timeout=30)
    print "SSH connection to %s established" %DUT_VM_MGMT

    AUX_VM_CONNECT = paramiko.SSHClient()
    # Add missing client key
    AUX_VM_CONNECT.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    # connect to switch
    AUX_VM_CONNECT.connect(AUX_VM_MGMT, username=USER, password=PASS2,timeout=30)
    print "SSH connection to %s established" % AUX_VM_MGMT

  if len(vmnics)==4:
    DUT_VM2_CONNECT = paramiko.SSHClient()
    # Add missing client key
    DUT_VM2_CONNECT.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    # connect to switch
    DUT_VM2_CONNECT.connect(DUT_VM2_MGMT, username=USER, password=PASS2, timeout=100)
    print "SSH connection to %s established" % DUT_VM2_MGMT

    AUX_VM2_CONNECT=paramiko.SSHClient()
    #Add missing client key
    AUX_VM2_CONNECT.set_missing_host_key_policy(paramiko.AutoAddPolicy())
    #connect to switch
    AUX_VM2_CONNECT.connect(AUX_VM2_MGMT,username=USER,password=PASS2,timeout=100)
    print "SSH connection to %s established" %AUX_VM2_MGMT
    usr_input=raw_input('Type \'YES\' after placing netserver[DUT VMs:'+DUT_VM_MGMT+','+DUT_VM2_MGMT+']'+' and netperf[AUX VMs:'+AUX_VM_MGMT+','+AUX_VM2_MGMT+']'+' in root directory and cmdlient(/tmp directory): '+ '\n' )
    if str(usr_input).strip().lower()!='yes':
      print "Aborting Test"
      return 0
    if testType=='PortIndependence' and silentFlag=='False':
      test_initialize(DUT_HOST,AUX_HOST,DUT_ESX_CONNECT,AUX_ESX_CONNECT,DUT_VM_TEST,AUX_VM_TEST,DUT_VM_CONNECT,AUX_VM_CONNECT,testType,vmnics,DUT_VM2_TEST,AUX_VM2_TEST,DUT_VM2_CONNECT,AUX_VM2_CONNECT,silentFlag)
    elif testType=='PortIndependence' and silentFlag=='True':
      #print "*******Check1:PASS*********\n"
      test_initialize(DUT_HOST,AUX_HOST,DUT_ESX_CONNECT,AUX_ESX_CONNECT,DUT_VM2_TEST, AUX_VM2_TEST, DUT_VM2_CONNECT,AUX_VM2_CONNECT, testType,vmnics,silentFlag)

  elif len(vmnics)==2:
    #print "%%%%%%% LinkTestPath2 %%%%%%%%%%\n"
    usr_input=raw_input('Type '+'\'yes\''+' after placing netserver[DUT VM:'+DUT_VM_MGMT+']'+' & netperf[AUX VM:'+AUX_VM_MGMT+']'+' in root directory: '+ '\n' )
    if str(usr_input).strip().lower()!='yes':
      print "Aborting Test"
      return 0

    test_initialize(DUT_HOST,AUX_HOST,DUT_ESX_CONNECT,AUX_ESX_CONNECT,DUT_VM_TEST, AUX_VM_TEST, DUT_VM_CONNECT,AUX_VM_CONNECT, testType,vmnics,silentFlag)



def test_initialize(DUT_HOST,AUX_HOST,DUT_ESX_CONNECT,AUX_ESX_CONNECT,DUT_VM_TEST,AUX_VM_TEST,DUT_VM_CONNECT,AUX_VM_CONNECT,testType,vmnics,DUT_VM2_TEST='None',AUX_VM2_TEST='None',DUT_VM2_CONNECT='None',AUX_VM2_CONNECT='None',silent='True'):
  #ping_cmd='ping '+HOST2+' -c 1'+'\n'
  print "Command:", sys.argv[0],str(sys.argv)
  wrapHTML(htmlLogFile,'Command:' + ' '.join(sys.argv))
  ITERATION=0
  fh = open(logFile, 'a+')
  data = fh.write(time.ctime(time.time())+' ######## STARTING TESTS FOR ' + testType +'########'+ '\n')
  data = fh.write(time.ctime(time.time())+' ######## STARTING NETPERF & PING INITIALLY ' + testType +'########'+ '\n')
  fh.close()

  #TRIGGER BIDIRECTIONAL NETPERF INITIALLY - SHOULD RESUME AFTER DISRUPTIVE EVENTS
  if testType=='Linktest'or (testType=='PortIndependence' and silent=='False'):
    #print "%%%%%%% LinkTestPath3 %%%%%%%%%%\n"
    print "Triggering NETPERF AND PING FOR 1st time"
    netperf_command_execution(DUT_VM_TEST, AUX_VM_TEST, DUT_VM_CONNECT, AUX_VM_CONNECT, '1', DURATION - 15, '1',ITERATION)
    ping_command_execution(DUT_VM_TEST, AUX_VM_TEST, DUT_VM_CONNECT, AUX_VM_CONNECT, DURATION-15, ITERATION)

  ITERATION=1
  T0= time.clock()
  while time.clock()-T0 < DURATION:
    print "###### REMAINING TIME :"+str(DURATION-time.clock())+"###### \n"
    fh = open(logFile, 'a+')
    data = fh.write('\n'+'*********************** ITERATION-' + str(ITERATION) + ' FOR ' + testType + '***********************' + '\n')
    fh.close()
    wrapHTML(htmlLogFile,'*********************** ITERATION-' + str(ITERATION) + ' FOR ' + testType + '***********************')

    if (testType=='Linktest'):
      #print "%%%%%%% LinkTestPath4 %%%%%%%%%%\n"
      test_execution_trigger(DUT_ESX_CONNECT,AUX_ESX_CONNECT,DUT_VM_TEST,AUX_VM_TEST,DUT_VM_CONNECT,AUX_VM_CONNECT,ITERATION,vmnics)
      ITERATION=ITERATION+1
    elif testType == 'PortIndependence' and silent=='False':
      test_execution_trigger(DUT_ESX_CONNECT, AUX_ESX_CONNECT,DUT_VM_TEST, AUX_VM_TEST,DUT_VM_CONNECT, AUX_VM_CONNECT,ITERATION,vmnics,DUT_VM2_TEST,AUX_VM2_TEST,DUT_VM2_CONNECT,AUX_VM2_CONNECT,silent)
      ITERATION=ITERATION+1
    elif testType == 'PortIndependence' and silent=='True':
      #print "%%%%%%% LinkTestPath44 %%%%%%%%%%\n"
      #print "*******Check2:PASS*********\n"
      test_execution_trigger(DUT_ESX_CONNECT, AUX_ESX_CONNECT,DUT_VM_TEST, AUX_VM_TEST,DUT_VM_CONNECT, AUX_VM_CONNECT,ITERATION,vmnics)
      ITERATION=ITERATION+1

  wait_exit=20
  time.sleep(wait_exit)
  fh = open(logFile, 'a+')
  data = fh.write('\n'+time.ctime(time.time())+' *********************** COMPLETED TESTS FOR ' + testType +'***********************'+ '\n'+'\n')
  fh.close()
  default_config(DUT_VM_TEST, AUX_VM_TEST, DUT_HOST, AUX_HOST, DUT_ESX_CONNECT, AUX_ESX_CONNECT, DUT_VM_CONNECT,AUX_VM_CONNECT, testType, vmnics)
  wrapHTML(htmlLogFile, ' *********************** COMPLETED TESTS FOR ' + testType + '***********************')
  wrapHTML(htmlLogFile, '--------------------------------------------- E X E C U T I O N - D E T A I L S -----------------------------------------',prepend=1)
  #wrapHTML(htmlLogFile,LinkTest_Throughput)
  print' ############################ NETPERF RESULTS SUMMARY ############################'
  if testType == 'Linktest':
    print 'LinkTest_Throughput >>',LinkTest_Throughput
  elif testType == 'PortIndependence':
    print 'PortIndependence_Throughput >>', PortIndependence_Throughput

  if testType == 'Linktest':
    for key, value in sorted(LinkTest_Throughput.items(), key=lambda pair: pair[0], reverse=True):
      wrapHTML(htmlLogFile,'ITERATION-'+str(key)+'-> '+value,prepend=1)
  elif testType == 'PortIndependence':
    for key, value in sorted(PortIndependence_Throughput.items(), key=lambda pair: pair[0], reverse=True):
      wrapHTML(htmlLogFile,'ITERATION-'+str(key)+'-> '+value,prepend=1)
  wrapHTML(htmlLogFile, ' ############################### NETPERF RESULTS SUMMARY ###############################',prepend=1)

  build_command='esxcli system version get | grep Build'
  print "Yogesh:build_command", build_command
  stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command(build_command)
  time.sleep(1)
  build = stdout.read()
  wrapHTML(htmlLogFile, ' ESX '+build, prepend=1)

  fw_version_command ='esxcli network nic get -n '+vmnics[0]+' | grep Firmware'
  #print "Yogesh:fw_version_command",fw_version_command
  stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command(fw_version_command)
  time.sleep(1)
  FW_Version=stdout.read()
  FW_Version=FW_Version.strip()
  FW_Version=re.sub(r'rx0 tx0','', FW_Version)
  wrapHTML(htmlLogFile,FW_Version.strip(), prepend=1)

  driver_version_command ='esxcli network nic get -n '+vmnics[0]+' | grep Version | grep -v Firmware'
  #print "Yogesh:driver_version_command",driver_version_command
  stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command(driver_version_command)
  time.sleep(1)
  Driver_Version=stdout.read()
  wrapHTML(htmlLogFile,'Driver '+Driver_Version.strip(), prepend=1)


  wrapHTML(htmlLogFile, '##################################### RELEASE INFO ####################################',prepend=1)
  wrapHTML(htmlLogFile, testType.upper()+" TEST RESULTS:", prepend=1)
  if testType == 'PortIndependence' and silent=='True':
    disconnect_machines(DUT_ESX_CONNECT,AUX_ESX_CONNECT, DUT_VM_CONNECT, AUX_VM_CONNECT,testType)
  elif testType == 'PortIndependence' and silent=='False':
    disconnect_machines(DUT_ESX_CONNECT,AUX_ESX_CONNECT, DUT_VM_CONNECT, AUX_VM_CONNECT,testType,DUT_VM2_CONNECT, AUX_VM2_CONNECT)
  elif testType == 'Linktest':
    disconnect_machines(DUT_ESX_CONNECT, AUX_ESX_CONNECT, DUT_VM_CONNECT, AUX_VM_CONNECT, testType)
  webbrowser.open_new_tab(htmlLogFile)
  subject= testType+'Results:'+now
  mailbody='Please refer attachment for Test Results.'+'\n'+'Regards,'+'\n'+'Automation Team'
  attachment1=htmlLogFile
  attachment2=logFile
  send_mail(subject,mailbody,attachment1,attachment2=0)


def test_execution_trigger(DUT_ESX_CONNECT,AUX_ESX_CONNECT,DUT_VM_TEST,AUX_VM_TEST,DUT_VM_CONNECT,AUX_VM_CONNECT,ITERATION,vmnics,DUT_VM2_TEST='None',AUX_VM2_TEST='None',DUT_VM2_CONNECT='None',AUX_VM2_CONNECT='None',silent='True'):
  # Create two threads as follows
  #link_down_cmd = "./cmdclient -s -c ' setlink 0x03c0 1 0 10G; q' 'ioctl=eth4'"
  #link_up_cmd = "./cmdclient -s -c ' setlink 0x03c0 0 0 10G; q' 'ioctl=eth4'"
  link_down_cmd = "esxcli network nic down -n vmnic4"
  link_up_cmd = "esxcli network nic up -n vmnic4"
  link_verify_cmd="esxcli network nic list"

  #DISRUPTIVE EVENTS COMMANDS
  MC_REBOOT_CMD ="./tmp/cmdclient -d medf -q -s -c \'reboot;q\' \'ioctl=vmnic5\'"
  #MC_BOARDCFG_CMD ="./tmp/cmdclient -d medf -s -c 'boardcfg;q' 'ioctl=vmnic5'"
  LINK_DOWN_CMD = 'esxcli network nic down -n '+vmnics[0]
  LINK_UP_CMD = 'esxcli network nic up -n '+vmnics[0]
  DRIVER_UNLOAD_CMD = "vmkload_mod -u sfvmk"
  DRIVER_RELOAD_CMD = "vmkload_mod -u sfvmk;vmkload_mod sfvmk;kill -SIGIO $(pidof vmkdevmgr)"
  TOSS=['0','1']
  random.shuffle(TOSS)
  VxLAN_DRIVER_RELOAD_CMD = "vmkload_mod -u sfvmk;vmkload_mod sfvmk vxlanOffload="+str(TOSS[0])+";kill -SIGIO $(pidof vmkdevmgr)"
  RSS_DRIVER_RELOAD_CMD = "vmkload_mod -u sfvmk;vmkload_mod sfvmk rssQCount="+str(TOSS[0])+";kill -SIGIO $(pidof vmkdevmgr)"
  NETQUEUE_DRIVER_RELOAD_CMD = "vmkload_mod -u sfvmk;vmkload_mod sfvmk netQCount="+str(TOSS[0])+";kill -SIGIO $(pidof vmkdevmgr)"
  DIAGNOSTICS_CMD = "esxcli network nic selftest run -n " +vmnics[0]

  #Edit_Normal_Events
  if testType=='Linktest':
    #Complete list of disruptive_events in very next line
    #disruptive_events = {'MC_REBOOT':MC_REBOOT_CMD,'LINK_DOWN':LINK_DOWN_CMD,'LINK_UP':LINK_UP_CMD,'DRIVER_UNLOAD':DRIVER_UNLOAD_CMD,'DRIVER_LOAD':DRIVER_LOAD_CMD}
    #disruptive_events = {'MC_REBOOT': MC_REBOOT_CMD, 'LINK_DOWN': LINK_DOWN_CMD, 'LINK_UP': LINK_UP_CMD}
    #disruptive_events = OrderedDict()
    disruptive_events = { 'DRIVER_RELOAD':DRIVER_RELOAD_CMD, 'DIAGNOSTICS': DIAGNOSTICS_CMD,'LINK_DOWN': LINK_DOWN_CMD, 'LINK_UP': LINK_UP_CMD}
    ##disruptive_events = { 'VxLAN_DRIVER_RELOAD':VxLAN_DRIVER_RELOAD_CMD, 'RSS_DRIVER_RELOAD':RSS_DRIVER_RELOAD_CMD, 'NETQUEUE_DRIVER_RELOAD':NETQUEUE_DRIVER_RELOAD_CMD, 'DIAGNOSTICS': DIAGNOSTICS_CMD,'LINK_DOWN': LINK_DOWN_CMD, 'LINK_UP': LINK_UP_CMD}
    #disruptive_events = {}
    normal_events = ['TSO_DISABLE','TSO_ENABLE','MTU','Interrupt_Moderation','CSO_ENABLE','CSO_DISABLE','TX_RX_RING']
    #normal_events = {}
  elif testType=='PortIndependence':
    disruptive_events = {'LINK_DOWN': LINK_DOWN_CMD, 'LINK_UP': LINK_UP_CMD}
    #disruptive_events = {}
    normal_events = ['TSO_DISABLE','TSO_ENABLE','MTU','Interrupt_Moderation','CSO_ENABLE','CSO_DISABLE']
    #normal_events = {}


  # Trigger Disruptive Events
  for key, command in sorted(disruptive_events.iteritems(), key=lambda (k, v): (k, v)):
    wrapHTML(htmlLogFile,"+++++++++++++++D I S R U P T I V E__E V E N T__S T A R T+++++++++++++++")
    fh = open(logFile, 'a+')
    fh.write('\n'+'------- DISRUPTIVE EVENT-' + key + ' Command ' + command + '-------' + '\n')
    fh.close()
    wrapHTML(htmlLogFile,'DISRUPTIVE EVENT-'+key)
    stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command(command)
    ##event_output = stdout.read()
    #print "DISRUPTIVE_EVENT OUTPUT:",key,event_output
    print "DISRUPTIVE_EVENT:", key
    wrapHTML(htmlLogFile,"+++++++++++++++D I S R U P T I V E__E V E N T__E N D+++++++++++++++")
    time.sleep(30)



  '''
  # Trigger Disruptive Events
  for key, command in sorted(disruptive_events.items(), key=lambda x: random.random()):
    wrapHTML(htmlLogFile,"+++++++++++++++D I S R U P T I V E__E V E N T__S T A R T+++++++++++++++")
    fh = open(logFile, 'a+')
    data = fh.write('\n'+'------- DISRUPTIVE EVENT-' + key + ' Command ' + command + '-------' + '\n')
    fh.close()
    wrapHTML(htmlLogFile,'DISRUPTIVE EVENT-'+key)
    stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command(command)
    ##event_output = stdout.read()
    #print "DISRUPTIVE_EVENT OUTPUT:",key,event_output
    print "DISRUPTIVE_EVENT:", key
    wrapHTML(htmlLogFile,"+++++++++++++++D I S R U P T I V E__E V E N T__E N D+++++++++++++++")
    time.sleep(30)
  '''

  if testType=='Linktest'or (testType=='PortIndependence' and silent=='False'):
    #print "%%%%%%% LinkTestPath5 %%%%%%%%%%\n"
    #print "DUT_VM_TEST:>>",DUT_VM_TEST+"<<<"
    #print "DUT_VM_CONNECT:>>",str(DUT_VM_CONNECT)+"<<<"
    #print "AUX_VM_TEST:>>",AUX_VM_TEST+"<<<"
    #print "AUX_VM_CONNECT:>>",str(AUX_VM_CONNECT)+"<<<"
    print "******* NETPERF-P ITERATION-"+str(ITERATION)+"**************\n"
    netperf_duration = '180'
    fh = open(logFile, 'a+')
    fh.write('\n'+'------- TRIGGERING PRIMARY VM NETPERF,ITERATION-'+str(ITERATION)+' BEFORE NORMAL EVENTS -----------' + '\n'+'\n')
    fh.close()
    wrapHTML(htmlLogFile,'TRIGGERING PRIMARY VM NETPERF,ITERATION-'+str(ITERATION)+' BEFORE NORMAL EVENTS')
    netperf_command_execution(DUT_VM_TEST,AUX_VM_TEST,DUT_VM_CONNECT,AUX_VM_CONNECT,'0',netperf_duration,'0',ITERATION)
    ping_command_execution(DUT_VM_TEST, AUX_VM_TEST, DUT_VM_CONNECT, AUX_VM_CONNECT,5,ITERATION)
    #Fetch DUT_VM and AUX_VM interface name e.g ens224
    DUT_VM_InterfaceName_Command = 'ifconfig | awk \'/' + DUT_VM_TEST + '/ {print $1}\' RS=\"\n\n\"'
    stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(DUT_VM_InterfaceName_Command)
    time.sleep(1)
    DUT_VM_INTERFACE_NAME = re.sub(':', '', stdout.read())
    DUT_VM_INTERFACE_NAME = DUT_VM_INTERFACE_NAME.strip()
    print "DUT_VM_INTERFACE_NAME",">>"+DUT_VM_INTERFACE_NAME+"<<"


    AUX_VM_InterfaceName_Command = 'ifconfig | awk \'/' + AUX_VM_TEST + '/ {print $1}\' RS=\"\n\n\"'
    stdin, stdout, stderr = AUX_VM_CONNECT.exec_command(AUX_VM_InterfaceName_Command)
    time.sleep(1)
    AUX_VM_INTERFACE_NAME = re.sub(':', '', stdout.read())
    AUX_VM_INTERFACE_NAME = AUX_VM_INTERFACE_NAME.strip()
    print "AUX_VM_INTERFACE_NAME", ">>"+AUX_VM_INTERFACE_NAME+"<<"

  if testType=='PortIndependence':
    #Trigger netperf on a different vmnic in unidirection
    time.sleep(10)
    print "******* NETPERF-PortIndependence ITERATION-"+str(ITERATION)+"**************\n"
    netperf_duration='180'
    fh = open(logFile, 'a+')
    data = fh.write('\n'+'------- TRIGGERING PortIndependence NETPERF ITERATION-'+str(ITERATION)+' BEFORE NORMAL EVENTS -----------' + '\n'+'\n')
    fh.close()
    wrapHTML(htmlLogFile,'TRIGGERING PortIndependence NETPERF ITERATION-'+str(ITERATION)+' BEFORE NORMAL EVENTS')
    if silent=='True':
      #print "*******Check3:PASS*********\n"
      netperf_command_execution(DUT_VM_TEST,AUX_VM_TEST,DUT_VM_CONNECT,AUX_VM_CONNECT,'0',netperf_duration,'0',ITERATION)
      ping_command_execution(DUT_VM_TEST, AUX_VM_TEST, DUT_VM_CONNECT, AUX_VM_CONNECT, 5, ITERATION)
    elif silent=='False':
      netperf_command_execution(DUT_VM2_TEST,AUX_VM2_TEST,DUT_VM2_CONNECT,AUX_VM2_CONNECT,'0',netperf_duration,'0',ITERATION)
      ping_command_execution(DUT_VM2_TEST, AUX_VM2_TEST, DUT_VM2_CONNECT, AUX_VM2_CONNECT, 5, ITERATION)




  random.shuffle(normal_events)

  for event in normal_events:
    wrapHTML(htmlLogFile,"++++++++++++++++++++N O R M A L__E V E N T__S T A R T++++++++++++++++++++")
    fh = open(logFile, 'a+')
    data = fh.write('\n'+'------- NORMAL EVENT-' + event +'-------' + '\n')
    fh.close()
    print "NORMAL_EVENT:",event
    if event=='MTU':
      MTU_VALUES = [1500,8192,9000]
      random.shuffle(MTU_VALUES)
      wrapHTML(htmlLogFile, "NORMAL_EVENT:"+event+"="+str(MTU_VALUES[0]))
    elif event == 'Interrupt_Moderation':
      #IMOD_VALUES = [30]  # To check consistency of Netperf Throughput uncomment this option
      IMOD_VALUES = [33, 60, 120, 1]
      random.shuffle(IMOD_VALUES)
      wrapHTML(htmlLogFile, "NORMAL_EVENT:" + event + "=" + str(IMOD_VALUES[0]))
    elif event == 'TX_RX_RING':
      #IMOD_VALUES = [30]  # To check consistency of Netperf Throughput uncomment this option
      RX_RING_VALUES = [512, 1024, 2048, 4096]
      TX_RING_VALUES = [512, 1024, 2048]
      random.shuffle(RX_RING_VALUES)
      random.shuffle(TX_RING_VALUES)
      wrapHTML(htmlLogFile, "NORMAL_EVENT:" + event + "=" + str(TX_RING_VALUES[0])+','+str(RX_RING_VALUES[0]))
    else:
      wrapHTML(htmlLogFile,"NORMAL_EVENT:"+event)
    wrapHTML(htmlLogFile,"++++++++++++++++++++++N O R M A L__E V E N T__E N D++++++++++++++++++++++")


    #Trigger Normal Events
    normal_event_delay=10
    if event=='TSO_DISABLE':
      print "TSO_DISABLE on vmnic:",vmnics[0]
      time.sleep(normal_event_delay)
      stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command('esxcli network nic tso set -e 0 -n '+vmnics[0])
      stdin, stdout, stderr = AUX_ESX_CONNECT.exec_command('esxcli network nic tso set -e 0 -n '+vmnics[1])
      time.sleep(1)
      if testType=='Linktest'or (testType=='PortIndependence' and silent=='False'):
        TSO_DISABLE_AUX_VM = 'ethtool -K ' +AUX_VM_INTERFACE_NAME+'tso off'
        stdin, stdout, stderr = AUX_VM_CONNECT.exec_command(TSO_DISABLE_AUX_VM)
        time.sleep(1)
        TSO_DISABLE_DUT_VM = 'ethtool -K '+DUT_VM_INTERFACE_NAME+'tso off'
        stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(TSO_DISABLE_DUT_VM)
        time.sleep(1)

    elif event=='TSO_ENABLE':
      time.sleep(normal_event_delay)
      stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command('esxcli network nic tso set -e 1 -n'+vmnics[0])
      stdin, stdout, stderr = AUX_ESX_CONNECT.exec_command('esxcli network nic tso set -e 1 -n '+vmnics[1])
      time.sleep(1)
      if testType=='Linktest'or (testType=='PortIndependence' and silent=='False'):
        TSO_ENABLE_AUX_VM = 'ethtool -K ' +AUX_VM_INTERFACE_NAME+'tso on'
        stdin, stdout, stderr = AUX_VM_CONNECT.exec_command(TSO_ENABLE_AUX_VM)
        time.sleep(1)
        TSO_ENABLE_DUT_VM = 'ethtool -K '+DUT_VM_INTERFACE_NAME+'tso on'
        stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(TSO_ENABLE_DUT_VM)
        time.sleep(1)

    elif event=='CSO_DISABLE':
      time.sleep(normal_event_delay)
      stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command('esxcli network nic cso set -e 0 -n '+vmnics[0])
      stdin, stdout, stderr = AUX_ESX_CONNECT.exec_command('esxcli network nic cso set -e 0 -n '+vmnics[1])
      time.sleep(1)
      if testType=='Linktest'or (testType=='PortIndependence' and silent=='False'):
        CSO_DISABLE_AUX_VM = 'ethtool -K '+AUX_VM_INTERFACE_NAME+'rx off tx off'
        stdin, stdout, stderr = AUX_VM_CONNECT.exec_command(CSO_DISABLE_AUX_VM)
        time.sleep(1)
        CSO_DISABLE_DUT_VM = 'ethtool -K '+DUT_VM_INTERFACE_NAME+'rx off tx off'
        stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(CSO_DISABLE_DUT_VM)
        time.sleep(1)
    elif event=='CSO_ENABLE':
      time.sleep(normal_event_delay)
      stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command('esxcli network nic cso set -e 1 -n '+vmnics[0])
      stdin, stdout, stderr = AUX_ESX_CONNECT.exec_command('esxcli network nic tso set -e 1 -n '+vmnics[1])
      time.sleep(1)
      if testType=='Linktest'or (testType=='PortIndependence' and silent=='False'):
        CSO_ENABLE_AUX_VM = 'ethtool -K '+AUX_VM_INTERFACE_NAME+'rx on tx on'
        stdin, stdout, stderr = AUX_VM_CONNECT.exec_command(CSO_ENABLE_AUX_VM)
        time.sleep(1)
        CSO_ENABLE_DUT_VM = 'ethtool -K '+DUT_VM_INTERFACE_NAME+'rx on tx on'
        stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(CSO_ENABLE_DUT_VM)
        time.sleep(1)
    elif event=='TX_RX_RING':
      time.sleep(normal_event_delay)
      stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command('esxcli network nic ring current set  -r '+str(RX_RING_VALUES[0])+' -t '+str(TX_RING_VALUES[0])+' -n '+vmnics[0])
      stdin, stdout, stderr = AUX_ESX_CONNECT.exec_command('esxcli network nic ring current set  -r '+str(RX_RING_VALUES[0])+' -t '+str(TX_RING_VALUES[0])+' -n '+vmnics[1])
      time.sleep(1)
      if testType=='Linktest'or (testType=='PortIndependence' and silent=='False'):
        #CSO_ENABLE_AUX_VM = 'ethtool -K '+AUX_VM_INTERFACE_NAME+'rx on tx on'
        #stdin, stdout, stderr = AUX_VM_CONNECT.exec_command(CSO_ENABLE_AUX_VM)
        #time.sleep(1)
        #CSO_ENABLE_DUT_VM = 'ethtool -K '+DUT_VM_INTERFACE_NAME+'rx on tx on'
        #stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(CSO_ENABLE_DUT_VM)
        time.sleep(1)
    elif event=='MTU':
      #MTU_VALUES=[1500] #To check consistency of Netperf Throughput uncomment this option
      #MTU_VALUES = [1500, 9000]
      #random.shuffle(MTU_VALUES)
      #interface_name_command= 'ifconfig -a'

      #interface_name_command= 'ifconfig -a'
      #stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command('vsish -e set /net/pNics/'+vmnics[0]+'/mtu '+MTU_VALUES[0])

      if testType=='Linktest'or (testType=='PortIndependence' and silent=='False'):
        MTU_DUT_VM = 'ifconfig '+DUT_VM_INTERFACE_NAME+' mtu '+str(MTU_VALUES[0])
        MTU_AUX_VM = 'ifconfig ' +AUX_VM_INTERFACE_NAME+' mtu '+str(MTU_VALUES[0])
        print "+MTU_VM Command:",MTU_DUT_VM
        stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(MTU_DUT_VM)
        time.sleep(1)
        stdin, stdout, stderr = AUX_VM_CONNECT.exec_command(MTU_AUX_VM)
        time.sleep(1)
        fh = open(logFile, 'a+')
        fh.write('\n'+'------- MTU_DUT_VM_Command-' + MTU_DUT_VM + '-------' + '\n')
        fh.write('\n'+'------- MTU_AUX_VM_Command-' + MTU_AUX_VM + '-------' + '\n')
        fh.close()

      MTU_DUT_ESX = 'vsish -e set /net/pNics/'+vmnics[0]+'/mtu '+str(MTU_VALUES[0])
      MTU_AUX_ESX = 'vsish -e set /net/pNics/' + vmnics[1] + '/mtu ' + str(MTU_VALUES[0])
      time.sleep(normal_event_delay)
      print "MTU_ESX Command:",MTU_DUT_ESX
      stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command(MTU_DUT_ESX)
      time.sleep(1)

      stdin, stdout, stderr = AUX_ESX_CONNECT.exec_command(MTU_AUX_ESX)
      time.sleep(1)

      fh = open(logFile, 'a+')
      fh.write('\n'+'------- MTU_DUT_ESX_Command-' + MTU_DUT_ESX + '-------' + '\n')
      fh.write('\n'+'------- MTU_AUX_ESX_Command-' + MTU_AUX_ESX + '-------' + '\n')
      fh.close()
    elif event=='Interrupt_Moderation':
      #IMOD_VALUES=[30] #To check consistency of Netperf Throughput uncomment this option
      #IMOD_VALUES = [33, 60, 120, 1]
      #random.shuffle(IMOD_VALUES)
      IMOD_DUT_ESX = 'esxcli network nic coalesce set --tx-usecs '+str(IMOD_VALUES[0])+' -n '+vmnics[0]
      IMOD_AUX_ESX = 'esxcli network nic coalesce set --tx-usecs ' + str(IMOD_VALUES[0]) + ' -n ' + vmnics[1]
      time.sleep(normal_event_delay)
      print "IMOD_ESX Command:",IMOD_DUT_ESX
      stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command(IMOD_DUT_ESX)
      stdin, stdout, stderr = AUX_ESX_CONNECT.exec_command(IMOD_AUX_ESX)
      time.sleep(1)
      fh = open(logFile, 'a+')
      data = fh.write('\n'+'------- IMOD_ESX_Command-' + IMOD_DUT_ESX + '-------' + '\n')
      fh.close()
  print "Zzzzzzzzzz..."+str(int(netperf_duration)+10)+" secs"
  time.sleep(int(netperf_duration)+10)
  wrapHTML(htmlLogFile,"+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++")

def netperf_command_execution(DUTVM_TEST,AUXVM_TEST,DUTVM_CONNECT,AUXVM_CONNECT,kill_process,netperf_duration,bidirectional,ITERATION):
  print "Inside netperf_command_execution.ITERATION-"+str(ITERATION)+' WITH DURATION '+str(netperf_duration)
  print "DUT: netperf_command_execution on %s" % (DUTVM_TEST)
  print "AUX: netperf_command_execution on %s" % (AUXVM_TEST)
  netperf_stream="TCP_STREAM"
  #Fetch netserver process_id's
  netperf_server_grep="ps -ef | grep netserver"
  netperf_server_DUTVM_TEST = netperf_server_grep
  stdin, stdout, stderr = DUTVM_CONNECT.exec_command(netperf_server_DUTVM_TEST)
  time.sleep(1)
  netperf_process_DUTVM_TEST = stdout.read().split('\n')
  for netserver in  netperf_process_DUTVM_TEST:
    print "DUT_NetServer:",netserver
    fh = open(logFile, 'a+')
    fh.write('\n' + 'DUT_NetServer:' + netserver + '\n')
    fh.close()

  netperf_server_AUXVM_TEST = netperf_server_grep
  stdin, stdout, stderr = AUXVM_CONNECT.exec_command(netperf_server_AUXVM_TEST)
  time.sleep(1)
  netperf_process_AUXVM_TEST = stdout.read().split('\n')
  for netserver in  netperf_process_AUXVM_TEST:
    print "AUX_NetServer:",netserver
    fh = open(logFile, 'a+')
    fh.write('\n' + 'AUX_NetServer:' + netserver + '\n')
    fh.close()

  if kill_process=='1':
    print "********KILLING ALL NETSERVERS****************\n"
    for i in range(0, len(netperf_process_DUTVM_TEST)):
      if re.search(r'root.*?([\d]+).*netserver.*-p', netperf_process_DUTVM_TEST[i]):
        process_id_DUTVM_TEST = re.search(r'root.*?([\d]+).*netserver.*-p', netperf_process_DUTVM_TEST[i]).group(1)
        netperf_server_kill_DUTVM_TEST = "kill -9 " + process_id_DUTVM_TEST
        stdin, stdout, stderr = DUTVM_CONNECT.exec_command(netperf_server_kill_DUTVM_TEST)
    for i in range(0,len(netperf_process_AUXVM_TEST)):
      if re.search(r'root.*?([\d]+).*netserver.*-p',netperf_process_AUXVM_TEST[i]):
        process_id_AUXVM_TEST=re.search(r'root.*?([\d]+).*netserver.*-p',netperf_process_AUXVM_TEST[i]).group(1)
        netperf_server_kill_AUXVM_TEST = "kill -9 "+process_id_AUXVM_TEST
        stdin, stdout, stderr = AUXVM_CONNECT.exec_command(netperf_server_kill_AUXVM_TEST)
  elif len(netperf_process_DUTVM_TEST)>2 and len(netperf_process_AUXVM_TEST)>2 and ITERATION>1:
    print "********KILLING NETSERVERS EXCLUSIVELY****************\n"
    fh = open(logFile, 'a+')
    fh.write('\n' + '********KILLING DUT NETSERVERS EXCLUSIVELY****************'+str(len(netperf_process_DUTVM_TEST))+'\n')
    fh.write('\n' + 'DUT EXISTING NETSERVERS:' + str(netperf_process_DUTVM_TEST) + '\n')
    fh.close()
    for i in range(len(netperf_process_DUTVM_TEST)-1, 1, -1):
      if re.search(r'root.*?([\d]+).*netserver.*-p', netperf_process_DUTVM_TEST[i]):
        fh = open(logFile, 'a+')
        fh.write('\n'+'DUT Exclusive Kill:' + netperf_process_DUTVM_TEST[i] +'\n')
        fh.close()
        print "DUT Exclusive Kill:",netperf_process_DUTVM_TEST[i]
        process_id_DUTVM_TEST = re.search(r'root.*?([\d]+).*netserver.*-p', netperf_process_DUTVM_TEST[i]).group(1)
        netperf_server_kill_DUTVM_TEST = "kill -9 " + process_id_DUTVM_TEST
        stdin, stdout, stderr = DUTVM_CONNECT.exec_command(netperf_server_kill_DUTVM_TEST)
        break
      else:
        print "No DUT Netserver associated >>>>>>>>>>>:",netperf_process_DUTVM_TEST[i]
    fh = open(logFile, 'a+')
    fh.write('\n' + '********KILLING AUX NETSERVERS EXCLUSIVELY****************'+str(len(netperf_process_AUXVM_TEST))+'\n')
    fh.write('\n' + 'AUX EXISTING NETSERVERS:' + str(netperf_process_AUXVM_TEST) + '\n')
    fh.close()
    for i in range(len(netperf_process_AUXVM_TEST)-1, 1, -1):
      if re.search(r'root.*?([\d]+).*netserver.*-p', netperf_process_AUXVM_TEST[i]):
        fh = open(logFile, 'a+')
        fh.write('\n'+'AUX Exclusive Kill:' + netperf_process_AUXVM_TEST[i] +'\n')
        fh.close()
        print "AUX Exclusive Kill:",netperf_process_AUXVM_TEST[i]
        process_id_AUXVM_TEST = re.search(r'root.*?([\d]+).*netserver.*-p', netperf_process_AUXVM_TEST[i]).group(1)
        netperf_server_kill_AUXVM_TEST = "kill -9 " + process_id_AUXVM_TEST
        stdin, stdout, stderr = AUXVM_CONNECT.exec_command(netperf_server_kill_AUXVM_TEST)
        break
      else:
        print "No AUX Netserver associated >>>>>>>>>>>:",netperf_process_AUXVM_TEST[i]



  print "****Triggering Netperf in parallel with following commands:*******\n"
  netserver1_port=randint(12900,14000)
  netserver_start_DUTVM_TEST=r"netserver -L "+DUTVM_TEST+" -p "+str(netserver1_port)+" –D"
  netperf_startTest_AUXVM_TEST=r"netperf -H "+DUTVM_TEST+" -l "+str(netperf_duration)+" -t "+netperf_stream+" -p "+str(netserver1_port)+" -- -m 64000 -M 64000 -L "+AUXVM_TEST+",inet"
  stdin, stdout, stderr = DUTVM_CONNECT.exec_command(netserver_start_DUTVM_TEST)
  time.sleep(1)
  thread.start_new_thread(parallel_cmds, ("NetPerfThread-" + str(int(ITERATION)) + '.' + str(2), AUXVM_CONNECT,netperf_startTest_AUXVM_TEST,ITERATION))


  fh = open(logFile, 'a+')
  fh.write('\n'+"TRIGGERED DUT NETSERVER ITERATION-"+str(ITERATION)+":"+netserver_start_DUTVM_TEST+'\n')
  wrapHTML(htmlLogFile,"TRIGGERED DUT NETSERVER ITERATION-"+str(ITERATION)+":"+netserver_start_DUTVM_TEST)
  fh.write('\n'+"TRIGGERED AUX NETPERF ITERATION-"+str(ITERATION)+":"+netperf_startTest_AUXVM_TEST+'\n')
  wrapHTML(htmlLogFile,"TRIGGERED AUX NETPERF ITERATION-"+str(ITERATION)+":"+netperf_startTest_AUXVM_TEST)
  fh.close()
  time.sleep(2)
  #print "YOGESH_DUT: %s on %s with handle %s " % (netserver_start_DUTVM_TEST,DUTVM_TEST,DUTVM_CONNECT)
  #print "YOGESH_AUX:%s on %s with handle %s"% (netperf_startTest_AUXVM_TEST,AUXVM_TEST,AUXVM_CONNECT)

  if bidirectional=='1':
    netserver2_port=randint(14001,15000)
    netperf_startTest_DUTVM_TEST=r"netperf -H "+AUXVM_TEST+" -l "+str(netperf_duration)+" -t "+netperf_stream+" -p "+str(netserver2_port)+" -- -m 64000 -M 64000 -L "+DUTVM_TEST+",inet"
    netserver_start_AUXVM_TEST=r"netserver -L "+AUXVM_TEST+" -p "+str(netserver2_port)+" –D"
    #start Netperf servers
    stdin, stdout, stderr = AUXVM_CONNECT.exec_command(netserver_start_AUXVM_TEST)
    time.sleep(1)
    thread.start_new_thread(parallel_cmds, ("NetPerfThread-" + str(int(ITERATION)) + '.' + str(1), DUTVM_CONNECT,netperf_startTest_DUTVM_TEST,ITERATION))
    fh = open(logFile, 'a+')
    fh.write('\n'+"TRIGGERED BIDIRECTIONAL AUX NETSERVER ITERATION-"+str(ITERATION)+":"+netserver_start_AUXVM_TEST+'\n')
    fh.write('\n'+"TRIGGERED BIDIRECTIONAL DUT NETPERF ITERATION-"+str(ITERATION)+":"+netperf_startTest_DUTVM_TEST+'\n')
    fh.close()
    wrapHTML(htmlLogFile,"TRIGGERED BIDIRECTIONAL AUX NETSERVER ITERATION-"+str(ITERATION)+":"+netserver_start_AUXVM_TEST)
    wrapHTML(htmlLogFile,"TRIGGERED BIDIRECTIONAL DUT NETPERF ITERATION-"+str(ITERATION)+":"+netperf_startTest_DUTVM_TEST)
    #print "YOGESH:",netserver_start_AUXVM_TEST
    #print "YOGESH:",netperf_startTest_DUTVM_TEST
    time.sleep(1)

'''  stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command(link_verify_cmd)
  ethtool_output =stdout.read()
  if ethtool_parser(ethtool_output,'Link detected')=='yes':
    ping_command_execution(DUT_VM_TEST, AUX_VM_TEST, DUT_VM_CONNECT, AUX_VM_CONNECT, i)
    print "Performing link down:"
    #link_down_cmd="./cmdclient -s -c ' setlink 0x03c0 1 0 10G; q' 'ioctl=eth4'"
    stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(link_down_cmd)
    time.sleep(1)
    output_link_down=stdout.read()
    print "Link down output:",output_link_down
    link_down_verify_cmd_DUT_VM_TEST = link_verify_cmd
    stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(link_down_verify_cmd_DUT_VM_TEST)
    ethtool_output_DUT_VM_TEST = stdout.read()
    link_down_verify_cmd_AUX_VM_TEST = link_verify_cmd
    stdin, stdout, stderr = AUX_VM_CONNECT.exec_command(link_down_verify_cmd_AUX_VM_TEST)
    ethtool_output_AUX_VM_TEST = stdout.read()
    if (ethtool_parser(ethtool_output_DUT_VM_TEST, 'Link detected') == 'no') and (ethtool_parser(ethtool_output_AUX_VM_TEST, 'Link detected') == 'no'):
      fh = open(logFile, 'a+')
      fh.write('PASS: LinkDownTest-Link detected as DOWN for ' + AUX_VM_TEST + ' once ' + DUT_VM_TEST +' interface goes down' + '\n')
      fh.close()
      ping_cmd = 'ping ' + AUX_VM_TEST + ' -c 4' + '\n'
      time.sleep(6)
      stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(ping_cmd)
      ping_cmd_output = stdout.read()
      ping_request_count = int(re.search('ping.*-c ([\d]+)',ping_cmd).group(1))
      if ping_cmd_output.count('Request timed out')==4 or ('100% packet loss' in ping_cmd_output):
        print "Capturing ethtool_output, 'Link detected'=no test results in a file"
        fh = open(logFile, 'a+')
        fh.write('PASS: LinkDownTest-Ping failed for ' + AUX_VM_TEST + ' Echo Request:' + str(ping_request_count) + ' Echo Reply:0'+ '\n')
        fh.close()
        #link_up_cmd = "./cmdclient -s -c ' setlink 0x03c0 0 0 10G; q' 'ioctl=eth4'"
        stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(link_up_cmd)
        time.sleep(1)
        output_link_up = stdout.read()
        print "Performing Link up again:",output_link_up
        link_up_verify_cmd = "ethtool eth4"
        stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(link_up_verify_cmd)
        ethtool_output = stdout.read()
        if ethtool_parser(ethtool_output, 'Link detected') == 'yes':
          print "Capturing ethtool_output, 'Link detected'=YES test results in a file"
          fh = open(logFile, 'a+')
          fh.write('After LinkUp: Capturing ping test results between ' + DUT_VM_TEST +' and '+AUX_VM_TEST + ' :' + '\n')
          fh.close()
          ping_command_execution(DUT_VM_TEST,AUX_VM_TEST,DUT_VM_CONNECT,AUX_VM_CONNECT)
        else:
          print "Link fails to revive after turned on"
      else:
        print "After LinkDown,Request timed out=",ping_cmd_output.count('Request timed out')
        print "Ping command output after LinkDownTest",ping_cmd_output
'''

def ping_command_execution(DUT_VM_TEST,AUX_VM_TEST,DUT_VM_CONNECT,AUX_VM_CONNECT,COUNT,ITERATION):
 ping_cmd1 = 'ping ' + AUX_VM_TEST + ' -i 1 -c '+str(COUNT)+'\n'
 ping_cmd2 = 'ping ' + DUT_VM_TEST + ' -i 1 -c '+str(COUNT)+'\n'
 print "1st ping cmd from DUT VM: %s" %(ping_cmd1)
 thread.start_new_thread(parallel_cmds_ping, ("PingThread-"+str(ITERATION)+'.'+str(1),DUT_VM_CONNECT,ping_cmd1,ITERATION))
 print "2nd ping cmd from AUX VM: %s"% (ping_cmd2)
 thread.start_new_thread(parallel_cmds_ping, ("PingThread-"+str(ITERATION)+'.'+str(2), AUX_VM_CONNECT,ping_cmd2,ITERATION))
 time.sleep(2)

def parallel_cmds(threadName,client_handle,cmd,ITERATION):
  #print "Inside parallel_cmds for Netperf",cmd
  #print client_handle
  stdin, stdout, stderr = client_handle.exec_command(cmd)
  output= stdout.read()
  ##  print output
  #print "%s: %s" % (threadName,time.ctime(time.time()))
  #print "Netperf Output while in parallel_cmds:"
  #time.sleep(15)
  parse_output(cmd,output,testType,threadName,ITERATION)

def parallel_cmds_ping(threadName,client_handle,cmd,ITERATION):
  #print client_handle
  stdin, stdout, stderr = client_handle.exec_command(cmd)
  output= stdout.read()
  ##  print output
  #print "%s: %s %s %s" % (threadName,time.ctime(time.time()),cmd,client_handle)
  print "%s: %s %s " % (threadName, time.ctime(time.time()), cmd)
  #time.sleep(15)
  parse_output_ping(cmd,output,testType,threadName,ITERATION)

def parse_output_ping(cmd,output,testType,threadName,ITERATION):
  #print 'Inside parse_output_ping function command used',cmd
  return parse_ping(cmd,output,testType,threadName,ITERATION)


def parse_output(cmd,output,testType,threadName,ITERATION):
  #print 'Inside parse_output Netperf command used',cmd
  #parse_ping(cmd,output,testType,threadName)
  return parse_Netperf(cmd,output,testType,threadName,ITERATION)


def parse_Netperf(cmd,output,testType,threadName,ITERATION):
  print "***** ITERATION-"+str(ITERATION)+"Netperf Output just before parsing********\n:"
  print "ITERATION-"+str(ITERATION)+" Netperf Command executed JBP:",cmd
  print "ITERATION-"+str(ITERATION)+" Netperf Result JBP:>>", output,"<<"

  if re.search(r'could not establish the control connection',output):
    print "NETPERF CONNECTION FAILURE \n"
    Throughput = 'NA'
    if testType == 'Linktest':
      print "Populating LinkTest_Throughput with key,NA", ITERATION, Throughput + " Gbps"
      LinkTest_Throughput[ITERATION] = Throughput
    elif testType == 'PortIndependence':
      if ITERATION in PortIndependence_Throughput.keys():
        PortIndependence_Throughput[str(ITERATION)+'-PortIndependence'] = Throughput
      else:
        PortIndependence_Throughput[ITERATION] = Throughput
  elif output.strip()!='':
  #print "ITERATION-"+str(ITERATION)+" Netperf Result:After split", output.split("\n")
    temp=output.split("\n")[-2].split(" ")
    for i in range(len(temp)-1,-1,-1):
      try:
          if temp[i]!='':
            Throughput=float(temp[i])
            Throughput = str(round((Throughput / 1000), 1))
            if testType == 'Linktest':
              if ITERATION == 0 and (ITERATION in LinkTest_Throughput.keys()):
                LinkTest_Throughput[str(ITERATION) + '.2'] = Throughput
                print "ITR0.2:Populating LinkTest_Throughput with key,val", str(ITERATION), Throughput + " Gbps"
              else:
                print "Populating LinkTest_Throughput with key,val", ITERATION, Throughput + " Gbps"
                LinkTest_Throughput[ITERATION] = Throughput
            elif testType == 'PortIndependence':
              if ITERATION == 0:
                if str(ITERATION) + '.1' in PortIndependence_Throughput.keys():
                  PortIndependence_Throughput[str(ITERATION) + '.2'] = Throughput
                  print "ITR0.2Populating Throughput with key,val", str(ITERATION) + '.2', Throughput + " Gbps"
                else:
                  print "ITR0.1:Populating Throughput with key,val", str(ITERATION) + '.1', Throughput + " Gbps"
                  PortIndependence_Throughput[str(ITERATION) + '.1'] = Throughput
              if ITERATION != 0:
                if ITERATION in PortIndependence_Throughput.keys():
                  PortIndependence_Throughput[str(ITERATION) + '-PortIndependence'] = Throughput
                else:
                  PortIndependence_Throughput[ITERATION] = Throughput
            break
      except ValueError,e:
          print "error",e,"on throughput line",i
          Throughput = 1



    fh = open(logFile, 'a+')
    fh.write('*********************Netperf Test Results for Thread :'+threadName+"*********************"+'\n'+cmd+'\n')
    #fh.write(output.split("\n")[0] + '\n')
    fh.write(output + '\n')
    fh.write('Throughput: ' +Throughput+" Gbps"+ '\n'+'\n')
    fh.close()
    print "Final Throughput:",Throughput
    wrapHTML(htmlLogFile,"===========================================================================")
    wrapHTML(htmlLogFile,testType+'-Netperf Test Results for Thread :'+threadName)
    wrapHTML(htmlLogFile,output)
    wrapHTML(htmlLogFile,'Throughput: '+Throughput+" Gbps")
    wrapHTML(htmlLogFile,"===========================================================================")





def parse_ping(cmd,output,testType,threadName,ITERATION):
  print "def parse_ping thread  output",threadName,output
  #wrapHTML(htmlLogFile,"===========================================================================")
  #wrapHTML(htmlLogFile,output)
  #wrapHTML(htmlLogFile,"===========================================================================")

  if re.search('ping.*-c ([\d]+)',cmd):
    #logFile='TestResults.txt'
    #print "Inside parse_ping function: Regexp Matched"
    expected_count=int(re.search('ping.*-c ([\d]+)',cmd).group(1))
    actual_count=output.count('bytes from')
    print "Actual=%d and Expected=%d for thread %s and command:%s" % (actual_count,expected_count,threadName,cmd)
    if re.search(r'(([\d]+\.){3,3}[\d]+)',cmd):
      target_ip=re.search(r'(([\d]+\.){3,3}[\d]+)',cmd).group(1)
      print "Target IP is %s" % target_ip
    else:
      print "Fail to fetch target_ip from command:",cmd
      wrapHTML(htmlLogFile,"Fail to fetch target_ip from command:"+cmd)
    if actual_count==expected_count:
      fh = open(logFile, 'a+')
      fh.write("-------"+threadName+' PASS: Ping successful for '+target_ip+' Echo Request:'+str(expected_count)+' Echo Reply:'+str(actual_count)+"-------"+'\n')
      fh.close()
      wrapHTML(htmlLogFile,threadName+' PASS: Ping successful for '+target_ip+' Echo Request:'+str(expected_count)+' Echo Reply:'+str(actual_count))
      return 'PASS'
    elif actual_count>0:
      print "Ping Count doesn't match EXACTLY"
      fh = open(logFile, 'a+')
      fh.write(threadName+' PARTIAL_FAIL: Ping PARTIALLY successful for ' +target_ip+' Echo Request:'+str(expected_count)+' Echo Reply:'+str(actual_count)+ '\n')
      fh.close()
      wrapHTML(htmlLogFile,threadName+' PARTIAL_FAIL: Ping PARTIALLY successful for ' +target_ip+' Echo Request:'+str(expected_count)+' Echo Reply:'+str(actual_count))
      return 'PARTIAL_FAIL'
    else:
      print "Ping FAILED completely"
      fh = open(logFile, 'a+')
      fh.write(threadName+' FAIL: Ping FAILED for ' +target_ip+' Echo Request:'+str(expected_count)+' Echo Reply:'+str(actual_count)+ '\n')
      fh.close()
      wrapHTML(htmlLogFile,threadName+' FAIL: Ping FAILED for ' +target_ip+' Echo Request:'+str(expected_count)+' Echo Reply:'+str(actual_count))
      return 'FAIL'
  else:
    print "Unable to parse regexp in PING"
    wrapHTML(htmlLogFile,"Unable to parse regexp in PING")
    return 'UNKNOWN_ERROR'

def ethtool_parser(ethtool_output,param):
  ethtool_params=ethtool_output.split('\n')
  #print len(ethtool_params),ethtool_params
  for i in range(0,len(ethtool_params)):
    if re.search(param+r'\:(.*)',ethtool_params[i],re.I):
      param_value=re.search(param+r'\:(.*)',ethtool_params[i]).group(1).strip()
      print param,"Found at index",i,"with value",param_value
      break
  return param_value


def default_config(DUT_VM_TEST, AUX_VM_TEST, DUT_HOST, AUX_HOST, DUT_ESX_CONNECT, AUX_ESX_CONNECT, DUT_VM_CONNECT,AUX_VM_CONNECT, testType, vmnics):
  # Fetch DUT_VM and AUX_VM interface name e.g ens224
  wrapHTML(htmlLogFile, 'Bringing ESX hosts->'+DUT_HOST+','+AUX_HOST+' to default state')
  DUT_VM_InterfaceName_Command = 'ifconfig | awk \'/' + DUT_VM_TEST + '/ {print $1}\' RS=\"\n\n\"'
  stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(DUT_VM_InterfaceName_Command)
  time.sleep(1)
  DUT_VM_INTERFACE_NAME = re.sub(':', '', stdout.read())
  DUT_VM_INTERFACE_NAME = DUT_VM_INTERFACE_NAME.strip()

  print "DUT_VM_INTERFACE_NAME", DUT_VM_INTERFACE_NAME

  AUX_VM_InterfaceName_Command = 'ifconfig | awk \'/' + AUX_VM_TEST + '/ {print $1}\' RS=\"\n\n\"'
  stdin, stdout, stderr = AUX_VM_CONNECT.exec_command(AUX_VM_InterfaceName_Command)
  time.sleep(1)
  AUX_VM_INTERFACE_NAME = re.sub(':', '', stdout.read())
  AUX_VM_INTERFACE_NAME = AUX_VM_INTERFACE_NAME.strip()
  print "AUX_VM_INTERFACE_NAME", AUX_VM_INTERFACE_NAME

  stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command('esxcli network nic ring current set  -r 1024 -t 1024  -n '+vmnics[0])
  time.sleep(1)
  stdin, stdout, stderr = AUX_ESX_CONNECT.exec_command('esxcli network nic ring current set  -r 1024 -t 1024  -n '+vmnics[1])
  time.sleep(1)
  stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command('esxcli network nic tso set -e 1 -n' + vmnics[0])
  time.sleep(1)
  TSO_ENABLE_DUT_VM = 'ethtool -K ' + DUT_VM_INTERFACE_NAME + 'tso on'
  stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(TSO_ENABLE_DUT_VM)
  time.sleep(1)
  stdin, stdout, stderr = AUX_ESX_CONNECT.exec_command('esxcli network nic tso set -e 1 -n ' + vmnics[1])
  time.sleep(1)
  TSO_ENABLE_AUX_VM = 'ethtool -K ' + AUX_VM_INTERFACE_NAME + 'tso on'
  stdin, stdout, stderr = AUX_VM_CONNECT.exec_command(TSO_ENABLE_AUX_VM)
  time.sleep(1)

  stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command('esxcli network nic cso set -e 1 -n ' + vmnics[0])
  time.sleep(1)
  CSO_ENABLE_DUT_VM = 'ethtool -K ' + DUT_VM_INTERFACE_NAME + 'rx on tx on'
  stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(CSO_ENABLE_DUT_VM)
  time.sleep(1)
  stdin, stdout, stderr = AUX_ESX_CONNECT.exec_command('esxcli network nic tso set -e 1 -n ' + vmnics[1])
  time.sleep(1)
  CSO_ENABLE_AUX_VM = 'ethtool -K ' + AUX_VM_INTERFACE_NAME + 'rx on tx on'
  stdin, stdout, stderr = AUX_VM_CONNECT.exec_command(CSO_ENABLE_AUX_VM)
  time.sleep(1)

  MTU_DUT_VM = 'ifconfig ' + DUT_VM_INTERFACE_NAME + ' mtu 1500'
  MTU_AUX_VM = 'ifconfig ' + AUX_VM_INTERFACE_NAME + ' mtu 1500'
  MTU_DUT_ESX = 'vsish -e set /net/pNics/' + vmnics[0] + '/mtu 1500'
  MTU_AUX_ESX = 'vsish -e set /net/pNics/' + vmnics[1] + '/mtu 1500'
  stdin, stdout, stderr = DUT_ESX_CONNECT.exec_command(MTU_DUT_ESX)
  stdin, stdout, stderr = DUT_VM_CONNECT.exec_command(MTU_DUT_VM)
  stdin, stdout, stderr = AUX_ESX_CONNECT.exec_command(MTU_AUX_ESX)
  stdin, stdout, stderr = AUX_VM_CONNECT.exec_command(MTU_AUX_VM)

  IMOD_DUT_ESX = 'esxcli network nic coalesce set --tx-usecs 33 -n ' + vmnics[0]
  IMOD_AUX_ESX = 'esxcli network nic coalesce set --tx-usecs 33 -n ' + vmnics[1]

def send_mail(subject,mailbody,attachment1,attachment2=0):
    outlook = win32.Dispatch('outlook.application')
    olFormatHTML = 2
    olFormatPlain = 1
    olFormatRichText = 3
    olFormatUnspecified = 0
    olMailItem = 0x0

    newMail = outlook.CreateItem(olMailItem)
    newMail.Subject = subject
    newMail.BodyFormat = olFormatHTML    #or olFormatRichText or olFormatPlain
    newMail.HTMLBody = mailbody
    newMail.To = "ydabas@solarflare.com"
    mail_attachment1 = attachment1
    newMail.Attachments.Add(mail_attachment1)
    if attachment2 != 0:
      mail_attachment2 = attachment2
      newMail.Attachments.Add(mail_attachment2)
    #newMail.display()
    # or just use this instead of .display() if you want to send immediately
    newMail.Send()

def disconnect_machines(DUT_ESX_CONNECT,AUX_ESX_CONNECT, DUT_VM_CONNECT, AUX_VM_CONNECT,testType,DUT_VM2_CONNECT='None', AUX_VM2_CONNECT='None'):

    DUT_ESX_CONNECT.close()
    AUX_ESX_CONNECT.close()
    DUT_VM_CONNECT.close()
    AUX_VM_CONNECT.close()
    if DUT_VM2_CONNECT!='None' and AUX_VM2_CONNECT!='None':
      DUT_VM2_CONNECT.close()
      AUX_VM2_CONNECT.close()
    print "Logged out of devices"
    wrapHTML(htmlLogFile,"Logged out of devices")
#for x in xrange(ITERATION):
#  fn()
#  print "%s Iteration/s completed" %(x+1)
#  print "********"
#  time.sleep(2) #sleep for 5 seconds

if __name__ == '__main__':
  connect_machines(testType,vmnics)
