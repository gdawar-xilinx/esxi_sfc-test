/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"
#include "sfvmk_tx.h"
#include "sfvmk_rx.h"
#include "sfvmk_ev.h"
#include "sfvmk_utils.h"
#include "sfvmk_uplink.h"
VMK_ReturnStatus
sfvmk_IntrAck(void *clientData, vmk_IntrCookie intrCookie)
{

  return VMK_OK;
}


static void
sfvmk_IntrMessage(void *arg, vmk_IntrCookie intrCookie)
{

	sfvmk_evq *evq;
	sfvmk_adapter *adapter;
	efx_nic_t *enp;
	struct sfvmk_intr *intr;
	unsigned int index;
	boolean_t fatal;

	evq = (struct sfvmk_evq *)arg;
	adapter = evq->adapter;
	enp = adapter->enp;
	intr = &adapter->intr;
	index = evq->index;

	//VMK_ASSERT(intr != NULL);
	//VMK_ASSERT(intr->type == EFX_INTR_MESSAGE);

        //vmk_LogMessage("got MSIX interuupt\n");

	if ((intr->state != SFVMK_INTR_STARTED))
		return;

	(void)efx_intr_status_message(enp, index, &fatal);

	if (fatal) {
		(void)efx_intr_disable(enp);
		(void)efx_intr_fatal(enp);
		return;
	}

  vmk_NetPollActivate(evq->netPoll);

  }




VMK_ReturnStatus
sfvmk_IntrStart(sfvmk_adapter *adapter ,vmk_IntrHandler handler , vmk_IntrAcknowledge ack )
{

	struct sfvmk_intr *intr;
	VMK_ReturnStatus status;

	intr = &adapter->intr;
	VMK_ASSERT(intr->state == SFVMK_INTR_INITIALIZED);

	/* Initialize common code interrupt bits. */
	(void)efx_intr_init(adapter->enp, intr->type, NULL);

	/* Register all the interrupts */
	SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 3, "sfvmk register interrupts");

	status = sfvmk_SetupInterrupts(adapter, handler, ack);
	if (status != VMK_OK) {
		vmk_WarningMessage("%s sfvmk driver: Failed to register interrupts",
												adapter->pciDeviceName.string);
		status = VMK_FAILURE;
		goto sfvmk_intr_registration_fail;
	}

	intr->state = SFVMK_INTR_STARTED;

	/* Enable interrupts at the NIC */
	efx_intr_enable(adapter->enp);

	return status ;


sfvmk_intr_registration_fail:
	/* Tear down common code interrupt bits. */
	efx_intr_fini(adapter->enp);

	intr->state = SFVMK_INTR_INITIALIZED;

	return status;
}




static int
sfvmk_SetDrvLimits( sfvmk_adapter *adapter)
{
  efx_drv_limits_t limits;

  vmk_Memset(&limits, 0, sizeof(limits));

  /* Limits are strict since take into account initial estimation */
  limits.edl_min_evq_count = limits.edl_max_evq_count =
      adapter->intr.numIntrAlloc;
  limits.edl_min_txq_count = limits.edl_max_txq_count =
      adapter->intr.numIntrAlloc + SFVMK_TXQ_NTYPES - 1;
  limits.edl_min_rxq_count = limits.edl_max_rxq_count =
      adapter->intr.numIntrAlloc;

  return (efx_nic_set_drv_limits(adapter->enp, &limits));
}

vmk_Bool
sfvmk_netPollCB(void *evq, vmk_uint32 budget)
{

    vmk_LogMessage("*************************************************\n");
    vmk_LogMessage("calling net poll callback \n");
    vmk_LogMessage("*************************************************\n");
    sfvmk_ev_qpoll(evq);
    return 0 ;
}
/****************************************************************************
*                vmk_UplinkNetDumpOps Handler                              *
****************************************************************************/
static VMK_ReturnStatus sfvmk_getCableType(vmk_AddrCookie cookie,
                                                    vmk_UplinkCableType *cableType);

static VMK_ReturnStatus sfvmk_getSupportedCableTypes(vmk_AddrCookie cookie,
                                                                    vmk_UplinkCableType *ct);

static VMK_ReturnStatus sfvmk_setCableType(vmk_AddrCookie cookie,
                                                    vmk_UplinkCableType cableType);

struct vmk_UplinkCableTypeOps sfvmkCableTypeOps = {
  .getCableType = sfvmk_getCableType,
  .getSupportedCableTypes = sfvmk_getSupportedCableTypes,
  .setCableType = sfvmk_setCableType,
};

/****************************************************************************
*                vmk_UplinkMessageLevelOps Handler                         *
****************************************************************************/
static VMK_ReturnStatus
sfvmk_messageLevelGet(vmk_AddrCookie cookie, vmk_uint32 *level);

static VMK_ReturnStatus
sfvmk_messageLevelSet(vmk_AddrCookie cookie, vmk_uint32 level);

static vmk_UplinkMessageLevelOps sfvmkMessageLevelOps = {
  .getMessageLevel = sfvmk_messageLevelGet,
  .setMessageLevel = sfvmk_messageLevelSet,
};

/****************************************************************************/

/****************************************************************************
*              vmk_UplinkTransceiverTypeOps Handler                         *
****************************************************************************/
static VMK_ReturnStatus
sfvmk_getTransceiverType(vmk_AddrCookie cookie,
                                      vmk_UplinkTransceiverType *type);

static VMK_ReturnStatus
sfvmk_setTransceiverType(vmk_AddrCookie cookie,
                                      vmk_UplinkTransceiverType type);

struct vmk_UplinkTransceiverTypeOps  sfvmkTransceiverTypeOps = {
  .getTransceiverType = sfvmk_getTransceiverType,
  .setTransceiverType = sfvmk_setTransceiverType,
};
/****************************************************************************/

/****************************************************************************
*              vmk_UplinkRingParamsOps Handler                             *
****************************************************************************/
static VMK_ReturnStatus
sfvmk_ringParamsGet(vmk_AddrCookie cookie,
                              vmk_UplinkRingParams *params);

static VMK_ReturnStatus
sfvmk_ringParamsSet(vmk_AddrCookie cookie,
                              vmk_UplinkRingParams *params);

struct vmk_UplinkRingParamsOps sfvmkRingParamsOps = {
  .ringParamsGet = sfvmk_ringParamsGet,
  .ringParamsSet = sfvmk_ringParamsSet,
};
/****************************************************************************/

/****************************************************************************
*              vmk_UplinkPhyAddressOps Handler                             *
****************************************************************************/
static VMK_ReturnStatus
sfvmk_getPhyAddress(vmk_AddrCookie cookie,
                              vmk_uint8* phyAddr);

static VMK_ReturnStatus
sfvmk_setPhyAddress(vmk_AddrCookie cookie,
                              vmk_uint8 phyAddr);

struct vmk_UplinkPhyAddressOps sfvmkPhyAddressOps = {
  .getPhyAddress = sfvmk_getPhyAddress,
  .setPhyAddress = sfvmk_setPhyAddress,
};
/****************************************************************************/

static VMK_ReturnStatus
sfvmk_linkStatusSet(vmk_AddrCookie cookie, vmk_LinkStatus *linkStatus)
{
  vmk_LogMessage("calling sfvmk_linkStatusSet\n");
  return VMK_OK;
}

/****************************************************************************
 *               vmk_UplinkCoalesceParamsOps Handlers                       *
 ****************************************************************************/
static VMK_ReturnStatus sfvmk_coalesceParamsGet(vmk_AddrCookie,
                                                 vmk_UplinkCoalesceParams *);
static VMK_ReturnStatus sfvmk_coalesceParamsSet(vmk_AddrCookie,
                                                 vmk_UplinkCoalesceParams *);

static vmk_UplinkCoalesceParamsOps sfvmkCoalesceParamsOps = {
   .getParams = sfvmk_coalesceParamsGet,
   .setParams = sfvmk_coalesceParamsSet,
};


 static VMK_ReturnStatus
 sfvmk_coalesceParamsSet(vmk_AddrCookie cookie,
													vmk_UplinkCoalesceParams *params)
 {
   vmk_LogMessage( "calling sfvmk_coalesceParamsSet\n");
   return VMK_NOT_SUPPORTED;

}


static VMK_ReturnStatus
sfvmk_coalesceParamsGet(vmk_AddrCookie cookie,
			vmk_UplinkCoalesceParams *params)
{
   vmk_LogMessage( "calling sfvmk_coalesceParamsGet\n");
   return VMK_NOT_SUPPORTED;

}


/****************************************************************************
 *                vmk_UplinkNetDumpOps Handler                              *
 ****************************************************************************/

static VMK_ReturnStatus sfvmk_panicTx(vmk_AddrCookie cookie,
                                       vmk_PktList pktList);
static VMK_ReturnStatus sfvmk_panicPoll(vmk_AddrCookie cookie,
                                         vmk_PktList pktList);
static VMK_ReturnStatus sfvmk_panicInfoGet(vmk_AddrCookie cookie,
                                            vmk_UplinkPanicInfo *panicInfo);

static vmk_UplinkNetDumpOps sfvmkNetDumpOps = {
   .panicTx = sfvmk_panicTx,
   .panicPoll = sfvmk_panicPoll,
   .panicInfoGet = sfvmk_panicInfoGet,
};
static VMK_ReturnStatus sfvmk_panicTx(vmk_AddrCookie cookie,
                                       vmk_PktList pktList)

{
   vmk_LogMessage( "calling sfvmk_panicTx\n");
    return VMK_NOT_SUPPORTED;
}
static VMK_ReturnStatus sfvmk_panicPoll(vmk_AddrCookie cookie,
                                         vmk_PktList pktList)


{
   vmk_LogMessage( "calling sfvmk_panicPoll\n");
    return VMK_NOT_SUPPORTED;
}

static VMK_ReturnStatus sfvmk_panicInfoGet(vmk_AddrCookie cookie,
                                            vmk_UplinkPanicInfo *panicInfo)

	{
   vmk_LogMessage( "calling sfvmk_panicPoll\n");
 SFVMK_DBG((sfvmk_adapter *)cookie.ptr, SFVMK_DBG_UPLINK, 3,
                  "Driver panicInfoGet");

   /* Provide the intrCookie corresponding to the default Rx Queue */
   //panicInfo->clientData = cookie;
   //panicInfo->intrCookie = ((sfvmk_adapter *) cookie.ptr)->intr.intrCookies[0];
   return VMK_OK;
	}
static VMK_ReturnStatus sfvmk_uplinkTx(vmk_AddrCookie, vmk_PktList);
static VMK_ReturnStatus sfvmk_uplinkMTUSet(vmk_AddrCookie, vmk_uint32);
static VMK_ReturnStatus sfvmk_uplinkStateSet(vmk_AddrCookie, vmk_UplinkState);
static VMK_ReturnStatus sfvmk_uplinkStatsGet(vmk_AddrCookie, vmk_UplinkStats *);
static VMK_ReturnStatus sfvmk_uplinkAssociate(vmk_AddrCookie, vmk_Uplink);
static VMK_ReturnStatus sfvmk_uplinkDisassociate(vmk_AddrCookie);
static VMK_ReturnStatus sfvmk_uplinkCapsRegister(vmk_AddrCookie);
static VMK_ReturnStatus sfvmk_uplinkCapEnable(vmk_AddrCookie, vmk_UplinkCap);
static VMK_ReturnStatus sfvmk_uplinkCapDisable(vmk_AddrCookie, vmk_UplinkCap);
static VMK_ReturnStatus sfvmk_uplinkStartIO(vmk_AddrCookie);
static VMK_ReturnStatus sfvmk_uplinkQuiesceIO(vmk_AddrCookie);
static VMK_ReturnStatus sfvmk_uplinkReset(vmk_AddrCookie);


static vmk_UplinkOps sfvmkUplinkOps = {
   .uplinkTx = sfvmk_uplinkTx,
   .uplinkMTUSet = sfvmk_uplinkMTUSet,
   .uplinkStateSet = sfvmk_uplinkStateSet,
   .uplinkStatsGet = sfvmk_uplinkStatsGet,
   .uplinkAssociate = sfvmk_uplinkAssociate,
   .uplinkDisassociate = sfvmk_uplinkDisassociate,
   .uplinkCapEnable = sfvmk_uplinkCapEnable,
   .uplinkCapDisable = sfvmk_uplinkCapDisable,
   .uplinkStartIO = sfvmk_uplinkStartIO,
   .uplinkQuiesceIO = sfvmk_uplinkQuiesceIO,
   .uplinkReset = sfvmk_uplinkReset,
};


/*
***********************************************************************
* sfvmk_getCableType
*
*      Handler used by vmkernel to get an uplink's cable type.
*
*      param[in]  cookie      struct holding driverData
*      param[out] cableType   cable type for this uplink.
*
* Results:
*      retval: VMK_OK       Cable type is returned successfully.
*              VMK_FAILURE  Otherwise
* Side effects:
*      None
*
***********************************************************************
*/

static VMK_ReturnStatus
sfvmk_getCableType(vmk_AddrCookie cookie,
                            vmk_UplinkCableType *cableType)
{
  sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

  SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 3, "Entry ...");

  *cableType = adapter->phy.cable_type;

  return VMK_OK;
}



/*
***********************************************************************
* sfvmk_getSupportedCableTypes
*
*      Handler used by vmkernel to get an uplink supported cable types.
*
*      param[in]  cookie      struct holding driverData
*      param[out] cableType   supported cable types for this uplink.
*:
* Results:
*      retval: VMK_OK       Cable types is returned successfully.
*              VMK_FAILURE  Otherwise
*
* Side effects:
*      None
*
***********************************************************************
*/

static VMK_ReturnStatus
sfvmk_getSupportedCableTypes(vmk_AddrCookie cookie,
                                            vmk_UplinkCableType *cableType)
{
  sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

  SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 3, "Entry ... ");

  *cableType = adapter->phy.cable_type;

  return VMK_OK;
}



/*
***********************************************************************
* sfvmk_setCableType
*
*      Handler used by vmkernel to set an uplink's cable type.
*
*      param[in]  cookie      struct holding driverData
*      param[in]  cableType   cable type to set.
*
* Results:
*      retval: VMK_OK       Cable type is set successfully.
*              VMK_FAILURE  Otherwise
*
* Side effects:
*      None
*
***********************************************************************
*/

static VMK_ReturnStatus
sfvmk_setCableType(vmk_AddrCookie cookie,
                            vmk_UplinkCableType cableType)
{
  sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

  SFVMK_ERR(adapter, "setCableType is not supported");

  return VMK_NOT_SUPPORTED;
}
static VMK_ReturnStatus sfvmk_uplinkTx(vmk_AddrCookie var1, vmk_PktList var2)
{
        vmk_LogMessage(" calling  sfvmk_uplinkTx ");
	return VMK_OK;
}

static VMK_ReturnStatus sfvmk_uplinkMTUSet(vmk_AddrCookie var1, vmk_uint32 var2)
	{

        vmk_LogMessage(" calling  sfvmk_uplinkMTUSet ");
		return VMK_OK;
	}

static VMK_ReturnStatus sfvmk_uplinkStateSet(vmk_AddrCookie var1 ,
                                              vmk_UplinkState var2)
	{

        vmk_LogMessage(" calling  sfvmk_uplinkMTUSet ");
		return VMK_OK;
	}

static VMK_ReturnStatus sfvmk_uplinkStatsGet(vmk_AddrCookie var1,
                                              vmk_UplinkStats *var2 )
	{

        vmk_LogMessage(" calling  sfvmk_uplinkMTUGet ");
		return VMK_OK;
	}



static VMK_ReturnStatus sfvmk_uplinkCapEnable(vmk_AddrCookie var1, vmk_UplinkCap var2)

{
        
        vmk_LogMessage(" calling sfvmk_uplinkCapEnable ");
	return VMK_OK;
}

static VMK_ReturnStatus sfvmk_uplinkCapDisable(vmk_AddrCookie var1 ,
                                                vmk_UplinkCap var2)

{

        vmk_LogMessage(" calling  sfvmk_uplinkCapDisable ");
	return VMK_OK;
}



 void
 sfvmk_updateCableType(sfvmk_adapter *adapter)
 {
    struct sfvmk_PhyInfo *phy = &adapter->phy;


    efx_phy_media_type_get(adapter->enp, &phy->interface_type);

    switch (phy->interface_type) {
       case EFX_PHY_MEDIA_BASE_T:
          phy->cable_type = VMK_UPLINK_CABLE_TYPE_TP;
          break;

       case EFX_PHY_MEDIA_SFP_PLUS:
       case EFX_PHY_MEDIA_QSFP_PLUS:
       case EFX_PHY_MEDIA_XFP:
          phy->cable_type = VMK_UPLINK_CABLE_TYPE_FIBRE;
          break;

       default:
          phy->cable_type = VMK_UPLINK_CABLE_TYPE_OTHER;
    }

    return;
 }


static VMK_ReturnStatus
sfvmk_registerIOCaps(sfvmk_adapter * adapter)
{
   VMK_ReturnStatus status;

   status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_SG_TX, NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter, "SG_TX cap register failed with error 0x%x",status);
      VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
                                  VMK_UPLINK_CAP_MULTI_PAGE_SG, NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,"MULTI_PAGE_SG cap register failed with error 0x%x",
                  status);
      VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
	 																VMK_UPLINK_CAP_IPV4_CSO, NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,"IPv4_CSO cap register failed with error 0x%x",
                     status);
      VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
	 																VMK_UPLINK_CAP_IPV4_TSO, NULL);
   if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
      SFVMK_ERR(adapter, "IPv4_TSO cap register failed with error 0x%x",
                     status);
      VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_IPV6_CSO, NULL);
   if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
      SFVMK_ERR(adapter, "IPv6_CSO cap register failed with error 0x%x",
                     status);
      VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
	 																	VMK_UPLINK_CAP_IPV6_TSO, NULL);
   if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
      SFVMK_ERR(adapter,"IPv6_TSO cap register failed with error 0x%x",
                     status);
      VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
	 																VMK_UPLINK_CAP_VLAN_TX_INSERT, NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter, "VLAN_TX_INSERT cap register failed with error 0x%x",
                status);
      VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
	 																VMK_UPLINK_CAP_VLAN_RX_STRIP,	NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,"VLAN_RX_STRIP cap register failed with error 0x%x",
                 status);
      VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,VMK_UPLINK_CAP_COALESCE_PARAMS, &sfvmkCoalesceParamsOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter, "COALESCE_PARAMS cap register failed with error 0x%x",
                status);
      VMK_ASSERT(0);
   }
#if 0
   if (adapter->msix_enabled) {
      status = vmk_UplinkCapRegister(adapter->uplink,
                                     VMK_UPLINK_CAP_MULTI_QUEUE,&elxnetQueueOps);
      if (status != VMK_OK) {
         SFVMK_ERR(adapter,
                        "MULTI_QUEUE cap register failed with error 0x%x",
                        status);
         VMK_ASSERT(0);
      }
   }
	 else {
      SFVMK_ERR(adapter, "Skipping register of VMK_UPLINK_CAP_"
                     "MULTI_QUEUE as device does not have msix enabled");
   }
#endif
#if 0
#if ESX_VERSION_NUMBER >= ESX_VERSION(2015)
   if (ELXNET_IS_RSS_ENABLED(adapter)) {
      status = vmk_UplinkQueueRegisterFeatureOps(adapter->uplink,
                                                VMK_UPLINK_QUEUE_FEAT_RSS_DYN,
                                                &elxnetUplinkQueueRssDynOps);
      if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
         SFVMK_ERR(adapter,
                        "RSS Queue Feature register failed for %s"
                        "with error 0x%x",
                        vmk_NameToString(&adapter->uplinkName), status);
         VMK_ASSERT(0);
      }
   }
#endif




   /* Register private stats capability */
   status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_PRIV_STATS,
                                  &elxnetPrivStatsOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "PRIV_STATS cap register failed "
                     "with error 0x%x", status);
      VMK_ASSERT(0);
   }
#endif
   /* Link Status Set capability */

   status = vmk_UplinkCapRegister(adapter->uplink,
   																VMK_UPLINK_CAP_LINK_STATUS_SET,
                                  &sfvmk_linkStatusSet);
   VMK_ASSERT(status == VMK_OK);

#if 0
   /* Register  WOL Capability */
   if (ELXNET_IS_WOL_SUPPORTED(adapter)) {
      status = vmk_UplinkCapRegister(adapter->uplink,
                                     VMK_UPLINK_CAP_WAKE_ON_LAN,
                                     &elxnetLinkWolOps);
      if (status != VMK_OK) {
         SFVMK_ERR(adapter,
                    "WOL cap register failed with error 0x%x", status);
	     VMK_ASSERT(0);
      }
   }
	 else {
      SFVMK_DBG(adapter, ELXNET_DBG_DRIVER, 0, "Does not support WOL");

   }
	 #endif

    /* Register  NETWORK_DUMP Capability */
   status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_NETWORK_DUMP,
                                  &sfvmkNetDumpOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "NetDump cap register failed with error 0x%x", status);
      VMK_ASSERT(0);
   }
#if 0
   if (adapter->num_vfs_enabled) {
      status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_SRIOV,
                                     NULL);
      if (status != VMK_OK) {
         SFVMK_ERR(adapter,
                        "SRIOV cap register failed with error 0x%x", status);
         VMK_ASSERT(0);
      }
   }

#endif

#if 0
   if (ELXNET_IS_VXLAN_OFFLOAD_ENABLED(adapter)) {
      SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 2, "Register vxlan offload");

      status = vmk_UplinkCapRegister(adapter->uplink,
                                     VMK_UPLINK_CAP_ENCAP_OFFLOAD,
                                     &elxnetEncapOffloadOps);
      if (status != VMK_OK) {
         SFVMK_ERR(adapter,
                        "VXLAN OFFLOAD cap register failed with error 0x%x",
                        status);
         VMK_ASSERT(0);
      }
   }

#endif
   status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_MOD_TX_HDRS,
                                  NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Modify Tx Hdrs cap register failed with error 0x%x",
                     status);
      VMK_ASSERT(0);
   }
#if 0
   status = vmk_UplinkCapRegister(adapter->uplink,
                                  VMK_UPLINK_CAP_PAUSE_PARAMS,
                                  NULL);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Flow Control cap register failed with error 0x%x",
                     status);
      VMK_ASSERT(0);
   }

#endif
   status = vmk_UplinkCapRegister(adapter->uplink, VMK_UPLINK_CAP_CABLE_TYPE,
                                  &sfvmkCableTypeOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Cable Type cap register failed "
                     "with error 0x%x", status);
      VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
                                  VMK_UPLINK_CAP_MESSAGE_LEVEL,
                                  &sfvmkMessageLevelOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Message Level cap register failed "
                     "with error 0x%x", status);
      VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
                                  VMK_UPLINK_CAP_TRANSCEIVER_TYPE,
                                  &sfvmkTransceiverTypeOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Transceiver Type cap register failed "
                     " with error 0x%x", status);
      VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
                                  VMK_UPLINK_CAP_RING_PARAMS,
                                  &sfvmkRingParamsOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Ring Parameters cap register failed "
                     " with error 0x%x", status);
      VMK_ASSERT(0);
   }

   status = vmk_UplinkCapRegister(adapter->uplink,
                                  VMK_UPLINK_CAP_PHY_ADDRESS,
                                  &sfvmkPhyAddressOps);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Phy Address cap register failed "
                     " with error 0x%x", status);
      VMK_ASSERT(0);
   }

   return (status);
}


/*
 ***********************************************************************
 *
 * sfvmk_uplinkCapsRegister
 *
 *      Function to register uplink device capabilities such as TSO and CSO.
 *
 *      param[in] cookie     Struct holding driverData
 *
 * Results:
 *      retval: VMK_OK       Register uplink caps with vmkernel
 *                           succeeded
 *              VMK_FAILURE  Register uplink caps failed
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_uplinkCapsRegister(vmk_AddrCookie cookie)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;
   VMK_ReturnStatus status = VMK_OK;

   vmk_LogMessage("calling sfvmk_uplinkCapsRegister \n");
   VMK_ASSERT(adapter->uplink != NULL);

   status = sfvmk_registerIOCaps(adapter);

   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 2, "driverData: %p", adapter);

   return status;
}


/*
 ***********************************************************************
 *
 * sfvmk_uplinkAssociate
 *
 *      Callback invoked by vmkernel to notify uplink is associated with
 *      device
 *
 *      param[in] cookie     Struct holding driverData
 *      param[in] uplink     Handle to uplink oject
 *
 * Results:
 *      retval: VMK_OK       Device backup uplink object succeeded
 *              VMK_FAILURE  Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */
static VMK_ReturnStatus
sfvmk_uplinkAssociate(vmk_AddrCookie cookie, vmk_Uplink uplink)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;
   vmk_LogMessage("calling sfvmk_uplinkAssociate \n");
   /* Backup uplink object in elxnet device data */
   VMK_ASSERT(uplink != NULL);
   VMK_ASSERT(adapter->uplink == NULL);

   adapter->uplink = uplink;
   adapter->uplinkName = vmk_UplinkNameGet(adapter->uplink);

   return sfvmk_uplinkCapsRegister(cookie);
}



/*
 ***********************************************************************
 *
 * sfvmk_uplinkDisassociate
 *
 *      Callback invoked by vmkernel to notify uplink is disassociated
 *      from device
 *
 *      param[in] cookie     Struct holding driverData
 *
 * Results:
 *      retval: VMK_OK       Device forget uplink object succeeded
 *              VMK_FAILURE  Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_uplinkDisassociate(vmk_AddrCookie cookie)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   /* forget uplink object in elxnet device data */
   VMK_ASSERT(adapter->uplink != NULL);

    adapter->uplink = NULL;

   return VMK_OK;
}
/*
 ***********************************************************************
 * sfvmk_messageLevelGet
 *
 *      Handler used by vmkernel to get an uplink's message level.
 *
 *      param[in]  cookie      struct holding driverData
 *      param[out] level       Pointer to be filled in with the messsage
 *                             level for this uplink.
 *
 * Results:
 *      retval: VMK_OK       Cable type is set successfully.
 *              VMK_FAILURE  Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_messageLevelGet(vmk_AddrCookie cookie, vmk_uint32 *level)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   VMK_ASSERT(adapter != NULL);

   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 2, "debugMask: 0x%x",
                  adapter->debugMask);

   *level = adapter->debugMask;

   return VMK_OK;
}



/*
 ***********************************************************************
 * sfvmk_messageLevelGet
 *
 *      Handler used by vmkernel to set an uplink's message level.
 *
 *      param[in]  cookie      struct holding driverData
 *      param[in]  level       message level to set.
 *
 * Results:
 *      retval: VMK_OK       Cable type is set successfully.
 *              VMK_FAILURE  Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_messageLevelSet(vmk_AddrCookie cookie, vmk_uint32 level)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   VMK_ASSERT(adapter != NULL);

   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 2,
                  "Seting debugMask to 0x%x (Current mask: 0x%x)",
                  level, adapter->debugMask);

   adapter->debugMask = level;
   return VMK_OK;
}



/*
 ************************************************************************
 * sfvmk_getTransceiverType
 *
 *   Handler used by vmkernel to get an uplink's transceiver type.
 *
 *   param[in]  cookie             struct holding driverData
 *   param[out]  transceiverType   transceiver type to get for this uplink.
 *
 * Results:
 *      retval: VMK_OK       Transceiver type is returned successfully.
 *              VMK_FAILURE  Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_getTransceiverType(vmk_AddrCookie cookie,
                          vmk_UplinkTransceiverType *transceiverType)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   VMK_ASSERT(transceiverType != NULL);
   VMK_ASSERT(adapter != NULL);

   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 3, "Entry ... ");

   return VMK_OK;
}



/*
 ***********************************************************************
 * sfvmk_setTransceiverType
 *
 *      Handler used by vmkernel to set an uplink's transciever type.
 *
 *      param[in]  cookie            struct holding driverData
 *      param[in]  transceiverType   transceiver type to set.
 *
 * Results:
 *      retval: VMK_OK            transceiver type is set successfully.
 *              VMK_NOT_SUPPORTED set tansceiver type is not supported.
 *              VMK_FAILURE       Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_setTransceiverType(vmk_AddrCookie cookie,
                          vmk_UplinkTransceiverType transceiverType)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   VMK_ASSERT(adapter != NULL);

   SFVMK_ERR(adapter, "setTransceiverType is not supported");

   return VMK_NOT_SUPPORTED;
}



/*
 ***********************************************************************
 * sfvmk_ringParamsGet
 *
 *      Handler used by vmkernel to get an uplink's RX & TX ring size.
 *
 *      param[in]  cookie            struct holding driverData
 *      param[out]  params           RX & TX ring size to get for this uplink
 *
 * Results:
 *      retval: VMK_OK            ring size get is success.
 *              VMK_FAILURE       Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_ringParamsGet(vmk_AddrCookie cookie,
                     vmk_UplinkRingParams *params)
{

   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   VMK_ASSERT(adapter != NULL);

   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 3, "Entry ... ");

   /*
    * txMaxPending & rxMaxPending are maximum num of entries
    * supported by Hardware
    *
    * txPending & rxPending are currently configured
    * num of entries
    *
    */
   params->txPending = params->txMaxPending = SFVMK_RX_SCALE_MAX;
   params->rxMaxPending = params->rxPending = SFVMK_RX_SCALE_MAX;

   /*
    * Ignoring Mini & Jumbo params, since firmware don't support
    */
   params->rxMiniMaxPending = 0;
   params->rxJumboMaxPending = 0;
   params->rxMiniPending = 0;
   params->rxJumboPending = 0;

   return VMK_OK;
}



/*
 ***********************************************************************
 * sfvmk_ringParamsSet
 *
 *      Handler used by vmkernel to set an uplink's RX/TX ring size.
 *
 *      param[in]  cookie            struct holding driverData
 *      param[in]  params            RX/TX ring size to set
 *
 * Results:
 *      retval: VMK_OK            ring size is set successfully.
 *              VMK_NOT_SUPPORTED set RX/TX ring size is not supported.
 *              VMK_FAILURE       Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_ringParamsSet(vmk_AddrCookie cookie,
                     vmk_UplinkRingParams *params)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   VMK_ASSERT(adapter != NULL);

   SFVMK_ERR(adapter, "RingParamsSet is not supported");

   return VMK_NOT_SUPPORTED;
}



/*
 ***********************************************************************
 * sfvmk_getPhyAddress
 *
 *      Handler used by vmkernel to get an uplink's PHY address.
 *
 *      param[in]  cookie            struct holding driverData
 *      param[out] phyAddr           PHY address for this uplink.
 *
 * Results:
 *      retval: VMK_OK            PHY address get is success.
 *              VMK_FAILURE       Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_getPhyAddress(vmk_AddrCookie cookie,
                     vmk_uint8 *phyAddr)
{

   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   VMK_ASSERT(adapter != NULL);

   SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 3, "Entry ... ");

  // *phyAddr = (vmk_uint8) adapter->port_num;

   return VMK_OK;
}



/*
 ***********************************************************************
 * sfvmk_setPhyAddress
 *
 * Handler used by vmkernel to set an uplink's PHY address.
 *
 *
 *      param[in]  cookie            struct holding driverData
 *      param[in]  phyAddr           PHY address to set.
 *
 * Results:
 *      retval: VMK_OK            PHY address set is success.
 *              VMK_NOT_SUPPORTED set PHY address is not supported.
 *              VMK_FAILURE       Otherwise
 *
 * Side effects:
 *      None
 *
 ***********************************************************************
 */

static VMK_ReturnStatus
sfvmk_setPhyAddress(vmk_AddrCookie cookie,
                     vmk_uint8 phyAddr)
{
   sfvmk_adapter *adapter = (sfvmk_adapter *)cookie.ptr;

   VMK_ASSERT(adapter != NULL);

   SFVMK_ERR(adapter, "PHY address set is not supported");

   return VMK_NOT_SUPPORTED;
}

VMK_ReturnStatus
sfvmk_DestroyUplinkData(sfvmk_adapter *pAdapter)
{
   vmk_int16 i;
   sfvmk_evq *pEVQ;

    for (i = 0; i < pAdapter->queueInfo.maxRxQueues; i++) {
      pEVQ = pAdapter->evq[i];
      vmk_NetPollDestroy(pEVQ->netPoll);
   }

   vmk_SpinlockDestroy(pAdapter->sharedDataWriterLock);

   return (VMK_OK);
}





static VMK_ReturnStatus
sfvmk_allocTxQueue(sfvmk_adapter *adapter,
                    vmk_UplinkQueueID *qid)
{
   vmk_UplinkSharedQueueData *qData;
   struct sfvmk_txq *txQueue;
   vmk_uint32 txqStartIndex, i ,  flags, qidVal;


    txqStartIndex = SFVMK_GET_TX_SHARED_QUEUE_START_INDEX(adapter)
    qData = &adapter->queueData[txqStartIndex];

   for (i=0; i<  adapter->queueInfo.maxTxQueues; i++) {
      if ((qData[i].flags & VMK_UPLINK_QUEUE_FLAG_IN_USE) == 0) {
         break;
      }
   }


   // Make Tx Queue ID


   qidVal =  i + txqStartIndex;
   vmk_LogMessage("qindex = %d qidVal = %d\n" , i , qidVal);
   vmk_UplinkQueueMkTxQueueID(qid, qidVal, qidVal);

   // Setup flags and qid for the allocated Tx queue
   SFVMK_SHARED_AREA_BEGIN_WRITE(adapter);
   txQueue = adapter->txq[i];//sfvmk_GetTxQueue(adapter, *qid);


	 flags = VMK_UPLINK_QUEUE_FLAG_IN_USE;
	 if (txQueue == adapter->txq[SFVMK_DEFAULT_TX_QUEUE_INDEX])
         {
   flags |= VMK_UPLINK_QUEUE_FLAG_DEFAULT;
          vmk_LogMessage("default tx queue is formed\n");
         }

	 qData[i].flags &= ~(VMK_UPLINK_QUEUE_FLAG_DEFAULT |
      								VMK_UPLINK_QUEUE_FLAG_IN_USE);
   qData[i].flags |= flags;
   qData[i].qid = *qid;
   SFVMK_SHARED_AREA_END_WRITE(adapter);

   return VMK_OK;
}

static VMK_ReturnStatus
sfvmk_allocRxQueue(sfvmk_adapter *driver,
                    vmk_UplinkQueueFeature feat,
                    vmk_UplinkQueueID *qid,
                    vmk_NetPoll *netpoll)
{
   vmk_UplinkSharedQueueData *qData;
   sfvmk_rxq *rxQueue;
   vmk_uint32 qIndex, flags, qidVal;

   qData = &driver->queueData[SFVMK_RX_SHARED_QUEUE_START_INDEX];

   {
      /* When looking for Non-RSS queues, Search for free
         queue index among the net queues. */
      for (qIndex=0; qIndex < SFVMK_RSS_START_INDEX(driver); qIndex++) {
         if ((qData[qIndex].flags & VMK_UPLINK_QUEUE_FLAG_IN_USE) == 0) {
            break;
         }
      }
      if (qIndex >= SFVMK_RSS_START_INDEX(driver)) {
         return VMK_FAILURE;
      }
   }


   vmk_LogMessage("qindex = %d\n", qIndex);
   // Make Rx QID
   qidVal =  qIndex + SFVMK_RX_SHARED_QUEUE_START_INDEX;
   vmk_LogMessage("qidVal = %d\n", qidVal);
   vmk_UplinkQueueMkRxQueueID(qid, qidVal, qidVal);
   *netpoll = qData[qIndex].poll;

   // Setup flags and qid for the allocated Rx queue
   SFVMK_SHARED_AREA_BEGIN_WRITE(driver);

   rxQueue = driver->rxq[qIndex];//sfvmk_GetRxQueue(driver, *qid);
   flags = VMK_UPLINK_QUEUE_FLAG_IN_USE;
   if (rxQueue == driver->rxq[SFVMK_DEFAULT_RX_QUEUE_INDEX])
   {
     flags |= VMK_UPLINK_QUEUE_FLAG_DEFAULT;
     vmk_LogMessage("default rx queue is formed\n");
   }
   qData[qIndex].flags &= ~(VMK_UPLINK_QUEUE_FLAG_DEFAULT |
                       VMK_UPLINK_QUEUE_FLAG_IN_USE);
   qData[qIndex].flags |= flags;
   qData[qIndex].qid = *qid;
   SFVMK_SHARED_AREA_END_WRITE(driver);

   SFVMK_DBG(driver, SFVMK_DBG_QUEUE, 2,
                  "sfvmk_allocRxQueue %u alloced", qidVal);

   return VMK_OK;
}



VMK_ReturnStatus
sfvmk_CreateUplinkData(sfvmk_adapter * adapter)
{
   vmk_UplinkSharedData *sharedData;
   vmk_UplinkRegData *regData;
   vmk_UplinkSharedQueueInfo *queueInfo;
   vmk_UplinkSharedQueueData *queueData;
   vmk_ServiceAcctID serviceID;
   vmk_SpinlockCreateProps spinLockProps;
   vmk_NetPoll netpoll;
   VMK_ReturnStatus status;
   vmk_int16 i;
   sfvmk_txq *txQueue;
   sfvmk_rxq *rxQueue;

   SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 2,"sfvmk_CreateUplinkData Entered");


   //praveen will do it later on
   //sfvmk_cmdUpdatePhyInfo(adapter);
   //sfvmk_updateSpeedFromPhy(adapter);


   const efx_nic_cfg_t *encp = efx_nic_cfg_get(adapter->enp);

   sfvmk_updateCableType(adapter);
   spinLockProps.moduleID = vmk_ModuleCurrentID;
   spinLockProps.heapID = sfvmk_ModInfo.heapID;
   spinLockProps.type = VMK_SPINLOCK;
   spinLockProps.domain = sfvmk_ModInfo.lockDomain;
   spinLockProps.rank = 1;
   vmk_NameCopy(&spinLockProps.name, &sfvmk_ModInfo.driverName);

   status = vmk_SpinlockCreate(&spinLockProps,
                               &adapter->sharedDataWriterLock);
   if (status != VMK_OK) {
      SFVMK_ERR(adapter,
                     "Failed to create writer lock for shared data area");
                     vmk_LogMessage("Failed to create writer lock for shared data area");
      goto sfvmk_create_data_lock_fail;
   }

//sfvmk_shared_data_init:

   /* Initialize shared data area */
   sharedData = &adapter->sharedData;
   adapter->supportedModes[0].speed = 1000;
   adapter->supportedModes[1].speed = 10000;

   adapter->supportedModes[0].duplex = VMK_LINK_DUPLEX_FULL;
   adapter->supportedModes[1].duplex = VMK_LINK_DUPLEX_FULL;

   adapter->supportedModesArraySz =2 ;

   vmk_VersionedAtomicInit(&sharedData->lock); 

   sharedData->supportedModes = adapter->supportedModes;
   sharedData->supportedModesArraySz = adapter->supportedModesArraySz;

   sharedData->flags = 0;
   sharedData->state = VMK_UPLINK_STATE_ENABLED;
   sharedData->link.state = VMK_LINK_STATE_DOWN;
   sharedData->link.speed = 0;
   sharedData->link.duplex = VMK_LINK_DUPLEX_HALF;

   sharedData->mtu = adapter->mtu = 1500;

   vmk_Memcpy(adapter->sharedData.macAddr, encp->enc_mac_addr, VMK_ETH_ADDR_LENGTH);
   vmk_Memcpy(adapter->sharedData.hwMacAddr, encp->enc_mac_addr,VMK_ETH_ADDR_LENGTH);

   vmk_LogMessage("value of interrupt linit is %d\n",encp->enc_intr_limit);
   /* populate queue info */
   sharedData->queueInfo = queueInfo = &adapter->queueInfo;
   queueInfo->supportedQueueTypes = VMK_UPLINK_QUEUE_TYPE_TX |
      					VMK_UPLINK_QUEUE_TYPE_RX;
   queueInfo->supportedRxQueueFilterClasses =
       VMK_UPLINK_QUEUE_FILTER_CLASS_MAC_ONLY;
     queueInfo->maxTxQueues =  adapter->txq_count;
      queueInfo->maxRxQueues = 1;//adapter->rxq_count;
      
   SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 2, "maxTxQs: %d maxRxQs: %d ",
                  queueInfo->maxTxQueues, queueInfo->maxRxQueues);

   vmk_LogMessage("max tx queue = %d max rx queue %d\n", queueInfo->maxTxQueues, queueInfo->maxRxQueues);
   queueInfo->activeRxQueues = 0;
   queueInfo->activeTxQueues = 0;

   status = vmk_ServiceGetID(VMK_SERVICE_ACCT_NAME_NET, &serviceID);
   VMK_ASSERT(status == VMK_OK);

   queueInfo->queueData = adapter->queueData;

   /* populate RX queues */
   for (i = 0; i < queueInfo->maxRxQueues; i++) {
      vmk_NetPollProperties pollProp;
        vmk_NetPoll             netPoll;
      queueData = &adapter->queueData[i];
      queueData->flags = VMK_UPLINK_QUEUE_FLAG_UNUSED;
      queueData->type =  VMK_UPLINK_QUEUE_TYPE_RX;
      queueData->state = VMK_UPLINK_QUEUE_STATE_STOPPED;
      if (i == 0) {
         queueData->supportedFeatures = 0;
      }
			else {
         queueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_PAIR ;
				//	|VMK_UPLINK_QUEUE_FEAT_DYNAMIC;


      }
      queueData->activeFeatures = 0;

      queueData->dmaEngine = adapter->vmkDmaEngine;

      queueData->activeFilters = 0;

      queueData->maxFilters =0;
      pollProp.poll = sfvmk_netPollCB;
      pollProp.priv.ptr = adapter->evq[i];
      pollProp.deliveryCallback = NULL;
      pollProp.features = VMK_NETPOLL_NONE;
      status = vmk_NetPollCreate(&pollProp, serviceID, vmk_ModuleCurrentID,
                                 &netPoll);
      if (status != VMK_OK) {
         for (i--; i >= 0; i--) {
            vmk_NetPollDestroy(adapter->evq[i]->netPoll);
         }
         goto sfvmk_create_data_fail;
      }

      queueData->poll = adapter->evq[i]->netPoll = netPoll;

//sfvmk_update_nicPoll:
      adapter->evq[i]->vector = adapter->intr.intrCookies[i];

      SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 3, "RXq=%d, poll=0x%p, "
                     "flag=0x%x", i, queueData->poll, queueData->flags);
     vmk_LogMessage("RXq=%d, poll=0x%p, "
                     "flag=0x%x", i, queueData->poll, queueData->flags);
      /* TBD: queueData->coalesceParams: Add support when AIC is enabled */

   }

   /* Populate TX queues */
   for (i = queueInfo->maxRxQueues;
        i < (queueInfo->maxRxQueues + queueInfo->maxTxQueues); i++) {
      queueData = &adapter->queueData[i];
      queueData->flags = VMK_UPLINK_QUEUE_FLAG_UNUSED;
      queueData->type = VMK_UPLINK_QUEUE_TYPE_TX;
      queueData->state = VMK_UPLINK_QUEUE_STATE_STOPPED;
      if (i == queueInfo->maxRxQueues) {
         queueData->supportedFeatures = 0;
      } else {
         queueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_PAIR;
      }
      queueData->activeFeatures =  0;
      queueData->dmaEngine = adapter->vmkDmaEngine;
      queueData->priority = VMK_VLAN_PRIORITY_MINIMUM;
      SFVMK_DBG(adapter, SFVMK_DBG_DRIVER, 3, "txQ=%d,flag=0x%x",
                     i, queueData->flags);
      vmk_LogMessage("txQ=%d,flag=0x%x",
                     i, queueData->flags);
      /* TBD: queueData->coalesceParams: Add support when AIC is enabled */
   }

   /*
    * Allocate default RX and TX queues, but don't activate them;
    * activation will be done in UplinkStartIO callback.
    */
   queueInfo->defaultRxQueueID = 0;
   status = sfvmk_allocRxQueue(adapter, 0, &queueInfo->defaultRxQueueID,
                                &netpoll);
   VMK_ASSERT(status == VMK_OK);
   rxQueue = sfvmk_GetRxQueue(adapter, queueInfo->defaultRxQueueID);
   VMK_ASSERT(sfvmk_isDefaultRxQueue(adapter, rxQueue));

   queueInfo->defaultTxQueueID = 0;
   status = sfvmk_allocTxQueue(adapter, &queueInfo->defaultTxQueueID);
   VMK_ASSERT(status == VMK_OK);
   txQueue = sfvmk_GetTxQueue(adapter, queueInfo->defaultTxQueueID);
   VMK_ASSERT(sfvmk_isDefaultTxQueue(adapter, txQueue));

   regData = &adapter->regData;
   regData->apiRevision = VMKAPI_REVISION;
   regData->moduleID = vmk_ModuleCurrentID;
   regData->ops = sfvmkUplinkOps;
   regData->sharedData = sharedData;
   regData->driverData.ptr = adapter;

   return (VMK_OK);

sfvmk_create_data_fail:
   vmk_SpinlockDestroy(adapter->sharedDataWriterLock);
sfvmk_create_data_lock_fail:
   return (status);
}
static VMK_ReturnStatus sfvmk_uplinkStartIO(void * driverData)

{
  VMK_ReturnStatus status ; 
  sfvmk_adapter *adapter = (sfvmk_adapter *) driverData;

  int rc;

  VMK_ASSERT(adapter != NULL);

  adapter->uplinkName = vmk_UplinkNameGet(adapter->uplink);
  SFVMK_DBG(adapter, SFVMK_DBG_UPLINK, 0, "Received Uplink Start I/O");

  vmk_LogMessage( "calling sfvmk_uplinkStatrtIO\n");

#if 0

  if (adapter->init_state == SFVMK_STARTED)
    return (0);

  if (adapter->init_state != SFVMK_REGISTERED) {
    rc = EINVAL;
    goto fail;
  }
#endif
  /* Set required resource limits */
  if ((rc = sfvmk_SetDrvLimits(adapter)) != 0)
    goto sfvmk_fail;

  efx_nic_fini(adapter->enp);
  if ((rc = efx_nic_init(adapter->enp)) != 0)
    goto sfvmk_fail;
  /* Start processing interrupts. */
  status = sfvmk_IntrStart(adapter, sfvmk_IntrMessage, sfvmk_IntrAck);
  if (status != VMK_OK)
	goto sfvmk_intr_start_fail;

  status = sfvmk_EVStart(adapter);
  if (status != VMK_OK)
	goto sfvmk_ev_start_fail;


   status = sfvmk_PortStart(adapter);
  if (status != VMK_OK)
	goto sfvmk_port_start_fail;


   status = sfvmk_RXStart(adapter);
  if (status != VMK_OK)
	goto sfvmk_rx_start_fail;

  status =  sfvmk_TXStart(adapter);
  if (status != VMK_OK)
	goto sfvmk_tx_start_fail;


  vmk_LogMessage( "calling sfvmk_uplinkStatrtIO\n");
	return VMK_OK;

sfvmk_tx_start_fail:
  sfvmk_RXStop(adapter);
sfvmk_rx_start_fail:
  sfvmk_PortStop(adapter);
sfvmk_port_start_fail:
  sfvmk_EVStop(adapter);
sfvmk_ev_start_fail:
   /* intr clean up should have come here */
  //sfvmk_EvStop(adapter);
sfvmk_intr_start_fail:
  efx_nic_fini(adapter->enp);
sfvmk_fail:
    return VMK_FAILURE;

}

static VMK_ReturnStatus sfvmk_uplinkQuiesceIO(void * driverData)

{

         sfvmk_adapter *adapter = (sfvmk_adapter *) driverData;


        /* Stop the transmitter. */
        sfvmk_TXStop(adapter);

        /* Stop the receiver. */
        sfvmk_RXStop(adapter);

        /* Stop the port. */
        sfvmk_PortStop(adapter);

        /* Stop processing events. */
        sfvmk_EVStop(adapter);

        /* Stop processing interrupts. */
        sfvmk_IntrStop(adapter);

        efx_nic_fini(adapter->enp);


        vmk_LogMessage(" calling sfvmk_uplinkQuiesceIO");
        return VMK_OK;

}

static VMK_ReturnStatus sfvmk_uplinkReset(vmk_AddrCookie var1)

{


        vmk_LogMessage(" calling sfvmk_uplinkReset");
	return VMK_OK;
}

