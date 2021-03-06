/*
 * Copyright (c) 2017-2020 Xilinx, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "sfvmk_driver.h"

/* Default mtu size*/
#define SFVMK_DEFAULT_MTU 1500

/* This is a workaround to take care of bug 84849. Enabling only phy test. */
#ifdef SFVMK_WORKAROUND_84849
  /* Number of supported offline diagnostics (PHY) test */
  #define SFVMK_SELF_TEST_COUNT       1
  #define SFVMK_SELFTEST_ALL          SFVMK_SELFTEST_PHY
#else
  /* Number of supported offline diagnostics (PHY,MEM & REG) test */
  #define SFVMK_SELF_TEST_COUNT       3
  #define SFVMK_SELFTEST_ALL          (SFVMK_SELFTEST_PHY | \
                                       SFVMK_SELFTEST_REG | \
                                       SFVMK_SELFTEST_MEM)
#endif

#define SFVMK_SELF_TEST_RESULT_LEN 32

/* Parser can only receive the first 256 bytes of data for a packet,
 * 32 bytes of that are metadata, so maximum of (256-32) bytes of actual
 * actual packet data can be used by Parser. The packet will be parsed as
 * Geneve if the Outer header + Geneve header + inner header <= 224 bytes
 */
#define SFVMK_GENEVE_MAX_HEADER_OFFSET  224

typedef enum sfvmk_selfTest_e {
  SFVMK_SELFTEST_PHY = 1 << 0,
  SFVMK_SELFTEST_REG = 1 << 1,
  SFVMK_SELFTEST_MEM = 1 << 2
} sfvmk_selfTest_t;

/* Wait time for NIC to come up with full functional mode on Reset */
#define SFVMK_STARTIO_ON_RESET_TIME_OUT_USEC    (100 * VMK_USEC_PER_MSEC)

static VMK_ReturnStatus sfvmk_registerIOCaps(sfvmk_adapter_t *pAdapter);
static void sfvmk_addUplinkFilter(sfvmk_adapter_t *pAdapter, vmk_uint32 qidVal,
                                  vmk_uint32 *pPairHwQid);
static VMK_ReturnStatus sfvmk_startIO(sfvmk_adapter_t *pAdapter);
static VMK_ReturnStatus sfvmk_quiesceIO(sfvmk_adapter_t *pAdapter);
static void sfvmk_uplinkResetHelper(vmk_AddrCookie data);
static VMK_ReturnStatus sfvmk_uplinkLinkStatusSet(vmk_AddrCookie cookie,
                                                  vmk_LinkStatus *pLinkStatus);

static vmk_uint32 sfvmk_getNumUplinkRxq(sfvmk_adapter_t *pAdapter);

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

/****************************************************************************
 *                Dynamic RSS Handler                                       *
 ****************************************************************************/
static VMK_ReturnStatus
sfvmk_rssParamsGet(vmk_AddrCookie,
                   vmk_UplinkQueueRSSParams *pParam);

static VMK_ReturnStatus
sfvmk_rssStateInit(vmk_AddrCookie,
                   vmk_UplinkQueueRSSHashKey *pRSSHashKey,
                   vmk_UplinkQueueRSSIndTable *pIndTable);

static VMK_ReturnStatus
sfvmk_rssIndTableUpdate(vmk_AddrCookie,
                        vmk_UplinkQueueRSSIndTable *pIndTable);

static const vmk_UplinkQueueRSSDynOps sfvmkRssDynOps = {
  .queueGetRSSParams = sfvmk_rssParamsGet,
  .queueInitRSSState = sfvmk_rssStateInit,
  .queueUpdateRSSIndTable = sfvmk_rssIndTableUpdate
};

/****************************************************************************
 *               vmk_UplinkOps Handlers                                     *
 ****************************************************************************/
static VMK_ReturnStatus sfvmk_uplinkTx(vmk_AddrCookie, vmk_PktList);
static VMK_ReturnStatus sfvmk_uplinkMTUSet(vmk_AddrCookie, vmk_uint32);
static VMK_ReturnStatus sfvmk_uplinkStateSet(vmk_AddrCookie, vmk_UplinkState);
static VMK_ReturnStatus sfvmk_uplinkStatsGet(vmk_AddrCookie, vmk_UplinkStats *);
static VMK_ReturnStatus sfvmk_uplinkAssociate(vmk_AddrCookie, vmk_Uplink);
static VMK_ReturnStatus sfvmk_uplinkDisassociate(vmk_AddrCookie);
static VMK_ReturnStatus sfvmk_uplinkCapEnable(vmk_AddrCookie, vmk_UplinkCap);
static VMK_ReturnStatus sfvmk_uplinkCapDisable(vmk_AddrCookie, vmk_UplinkCap);
static VMK_ReturnStatus sfvmk_uplinkStartIO(vmk_AddrCookie);
static VMK_ReturnStatus sfvmk_uplinkQuiesceIO(vmk_AddrCookie);
static VMK_ReturnStatus sfvmk_uplinkReset(vmk_AddrCookie);

static vmk_UplinkOps sfvmkUplinkOps = {
  .uplinkTx = sfvmk_uplinkTx,
  .uplinkReset = sfvmk_uplinkReset,
  .uplinkMTUSet = sfvmk_uplinkMTUSet,
  .uplinkStateSet = sfvmk_uplinkStateSet,
  .uplinkStatsGet = sfvmk_uplinkStatsGet,
  .uplinkStartIO = sfvmk_uplinkStartIO,
  .uplinkQuiesceIO = sfvmk_uplinkQuiesceIO,
  .uplinkCapEnable = sfvmk_uplinkCapEnable,
  .uplinkCapDisable = sfvmk_uplinkCapDisable,
  .uplinkAssociate = sfvmk_uplinkAssociate,
  .uplinkDisassociate = sfvmk_uplinkDisassociate,
};

/****************************************************************************
*                vmk_UplinkCableTypeOps Handler                             *
****************************************************************************/
static VMK_ReturnStatus sfvmk_getCableType(vmk_AddrCookie,
                                         vmk_UplinkCableType *);

static VMK_ReturnStatus sfvmk_getSupportedCableTypes(vmk_AddrCookie,
                                           vmk_UplinkCableType *);

static VMK_ReturnStatus sfvmk_setCableType(vmk_AddrCookie,
                                         vmk_UplinkCableType);

struct vmk_UplinkCableTypeOps sfvmkCableTypeOps = {
  .getCableType = sfvmk_getCableType,
  .getSupportedCableTypes = sfvmk_getSupportedCableTypes,
  .setCableType = sfvmk_setCableType,
};

/****************************************************************************
*                vmk_UplinkMessageLevelOps Handler                          *
****************************************************************************/
static VMK_ReturnStatus sfvmk_messageLevelGet(vmk_AddrCookie cookie,
                                              vmk_uint32 *pLevel);

static VMK_ReturnStatus sfvmk_messageLevelSet(vmk_AddrCookie cookie,
                                              vmk_uint32 level);

static const vmk_UplinkMessageLevelOps sfvmk_messageLevelOps = {
  .getMessageLevel = sfvmk_messageLevelGet,
  .setMessageLevel = sfvmk_messageLevelSet,
};

/****************************************************************************
*                vmk_UplinkQueueOps Handler                             *
****************************************************************************/
static VMK_ReturnStatus sfvmk_allocQueue(vmk_AddrCookie cookie,
                                         vmk_UplinkQueueType qType,
                                         vmk_UplinkQueueID *pQID,
                                         vmk_NetPoll *pNetpoll);

static VMK_ReturnStatus sfvmk_allocQueueWithAttr(vmk_AddrCookie cookie,
                                                 vmk_UplinkQueueType qType,
                                                 vmk_uint16 numAttr,
                                                 vmk_UplinkQueueAttr *pAttr,
                                                 vmk_UplinkQueueID *pQID,
                                                 vmk_NetPoll *pNetpoll);

static VMK_ReturnStatus sfvmk_freeQueue(vmk_AddrCookie cookie,
                                        vmk_UplinkQueueID qid);

static VMK_ReturnStatus sfvmk_quiesceQueue(vmk_AddrCookie cookie,
                                           vmk_UplinkQueueID qid);

static VMK_ReturnStatus sfvmk_startQueue(vmk_AddrCookie cookie,
                                         vmk_UplinkQueueID qid);

static VMK_ReturnStatus sfvmk_removeQueueFilter(vmk_AddrCookie cookie,
                                                vmk_UplinkQueueID qid,
                                                vmk_UplinkQueueFilterID fid);

static VMK_ReturnStatus sfvmk_applyQueueFilter(vmk_AddrCookie cookie,
                                               vmk_UplinkQueueID qid,
                                               vmk_UplinkQueueFilter *pFilter,
                                               vmk_UplinkQueueFilterID *pFID,
                                               vmk_uint32 *pPairHwQid);

static VMK_ReturnStatus sfvmk_getQueueStats(vmk_AddrCookie cookie,
                                            vmk_UplinkQueueID qid,
                                            vmk_UplinkStats *pStats);

static VMK_ReturnStatus sfvmk_toggleQueueFeature(vmk_AddrCookie cookie,
                                                 vmk_UplinkQueueID qid,
                                                 vmk_UplinkQueueFeature feature,
                                                 vmk_Bool setUnset);

static VMK_ReturnStatus sfvmk_setQueueTxPriority(vmk_AddrCookie cookie,
                                                 vmk_UplinkQueueID qid,
                                                 vmk_UplinkQueuePriority priority);

static VMK_ReturnStatus sfvmk_setQueueCoalesceParams(vmk_AddrCookie cookie,
                                                     vmk_UplinkQueueID qid,
                                                     vmk_UplinkCoalesceParams *pParams);

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
static vmk_UplinkMultiQueueOps sfvmkQueueOps = {
   .queueOps = {
      .queueAlloc             = sfvmk_allocQueue,
      .queueAllocWithAttr     = sfvmk_allocQueueWithAttr,
      .queueFree              = sfvmk_freeQueue,
      .queueQuiesce           = sfvmk_quiesceQueue,
      .queueStart             = sfvmk_startQueue,
      .queueApplyFilter       = sfvmk_applyQueueFilter,
      .queueRemoveFilter      = sfvmk_removeQueueFilter,
      .queueGetStats          = sfvmk_getQueueStats,
      .queueToggleFeature     = sfvmk_toggleQueueFeature,
      .queueSetPriority       = sfvmk_setQueueTxPriority,
      .queueSetCoalesceParams = sfvmk_setQueueCoalesceParams,
   },
   .queueSetCount = NULL,
};
#else
static vmk_UplinkQueueOps sfvmkQueueOps = {
   .queueAlloc             = sfvmk_allocQueue,
   .queueAllocWithAttr     = sfvmk_allocQueueWithAttr,
   .queueFree              = sfvmk_freeQueue,
   .queueQuiesce           = sfvmk_quiesceQueue,
   .queueStart             = sfvmk_startQueue,
   .queueApplyFilter       = sfvmk_applyQueueFilter,
   .queueRemoveFilter      = sfvmk_removeQueueFilter,
   .queueGetStats          = sfvmk_getQueueStats,
   .queueToggleFeature     = sfvmk_toggleQueueFeature,
   .queueSetPriority       = sfvmk_setQueueTxPriority,
   .queueSetCoalesceParams = sfvmk_setQueueCoalesceParams,
};
#endif

/****************************************************************************
*              vmk_UplinkRingParamsOps Handler                              *
****************************************************************************/
static VMK_ReturnStatus sfvmk_ringParamsGet(vmk_AddrCookie,
                                            vmk_UplinkRingParams *);
static VMK_ReturnStatus sfvmk_ringParamsSet(vmk_AddrCookie cookie,
                                            vmk_UplinkRingParams *);

const static vmk_UplinkRingParamsOps sfvmk_ringParamsOps = {
  .ringParamsGet = sfvmk_ringParamsGet,
  .ringParamsSet = sfvmk_ringParamsSet,
};

/****************************************************************************
 *               vmk_UplinkPrivStatsOps Handlers                            *
 ****************************************************************************/
static VMK_ReturnStatus sfvmk_privStatsLengthGet(vmk_AddrCookie cookie,
                                                  vmk_ByteCount *pLength);
static VMK_ReturnStatus sfvmk_privStatsGet(vmk_AddrCookie cookie,
                                            char *pStatBuf,
                                            vmk_ByteCount length);

const static vmk_UplinkPrivStatsOps sfvmkPrivStatsOps = {
   .privStatsLengthGet     = sfvmk_privStatsLengthGet,
   .privStatsGet           = sfvmk_privStatsGet,
};

/****************************************************************************
 *               vmk_UplinkCoalesceParamsOps Handlers                       *
 ****************************************************************************/
static VMK_ReturnStatus sfvmk_coalesceParamsGet(vmk_AddrCookie,
                                                vmk_UplinkCoalesceParams *);
static VMK_ReturnStatus sfvmk_coalesceParamsSet(vmk_AddrCookie,
                                                vmk_UplinkCoalesceParams *);

const static vmk_UplinkCoalesceParamsOps sfvmkCoalesceParamsOps = {
  .getParams = sfvmk_coalesceParamsGet,
  .setParams = sfvmk_coalesceParamsSet,
};

/****************************************************************************
 *               vmk_UplinkSelfTestOps Handlers                             *
 ****************************************************************************/
static VMK_ReturnStatus sfvmk_selfTestResultLenGet(vmk_AddrCookie cookie,
                                                   vmk_uint32  *pLen);
static VMK_ReturnStatus sfvmk_selfTestRun(vmk_AddrCookie cookie,
                                          vmk_Bool online,
                                          vmk_Bool *pPassed,
                                          vmk_UplinkSelfTestResult *pResultBuf,
                                          vmk_UplinkSelfTestString *pStringsBuf);

const static vmk_UplinkSelfTestOps sfvmk_selfTestOps = {
  .selfTestResultLenGet = sfvmk_selfTestResultLenGet,
  .selfTestRun          = sfvmk_selfTestRun,
};

/****************************************************************************
 *               vmk_UplinkEncapOffloadOps Handlers                         *
 ****************************************************************************/
static VMK_ReturnStatus sfvmk_vxlanPortUpdate(vmk_AddrCookie cookie,
                                              vmk_uint16 portNumNBO);
#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
static VMK_ReturnStatus sfvmk_genevePortUpdate(vmk_AddrCookie cookie,
                                              vmk_uint16 portNumNBO);
#endif
static VMK_ReturnStatus sfvmk_tunnelPortUpdate(vmk_AddrCookie cookie,
                                               vmk_uint16 portNumNBO,
                                               efx_tunnel_protocol_t encapType);

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 5, 0, 0)
static vmk_UplinkVXLANOffloadParams sfvmk_vxlanOffloadOps = {
   .vxlanPortUpdate = sfvmk_vxlanPortUpdate,
   .flags           = (VMK_UPLINK_VXLAN_FLAG_INNER_IPV4_CSO |
                       VMK_UPLINK_VXLAN_FLAG_INNER_IPV4_TSO |
                       VMK_UPLINK_VXLAN_FLAG_INNER_IPV6_CSO |
                       VMK_UPLINK_VXLAN_FLAG_INNER_IPV6_TSO |
                       VMK_UPLINK_VXLAN_FLAG_OUTER_UDP_CSO)
};
#else
static vmk_UplinkEncapOffloadOps sfvmk_vxlanOffloadOps = {

  .vxlanPortUpdate = sfvmk_vxlanPortUpdate,
};
#endif

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
static vmk_UplinkGeneveOffloadParams sfvmk_geneveOffloadOps = {
  .portUpdate      = sfvmk_genevePortUpdate,
  .maxHeaderOffset = SFVMK_GENEVE_MAX_HEADER_OFFSET,
#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 5, 0, 0)
  .flags           = (VMK_UPLINK_GENEVE_FLAG_INNER_IPV4_CSO |
                      VMK_UPLINK_GENEVE_FLAG_INNER_IPV4_TSO |
                      VMK_UPLINK_GENEVE_FLAG_INNER_IPV6_CSO |
                      VMK_UPLINK_GENEVE_FLAG_INNER_IPV6_TSO |
                      VMK_UPLINK_GENEVE_FLAG_OUTER_UDP_CSO)
#else
  .flags           = VMK_UPLINK_GENEVE_FLAG_OUTER_UDP_CSO,
#endif
};
#endif

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
/****************************************************************************
 *               vmk_UplinkAdvertisedModesOps Handlers                      *
 ****************************************************************************/
static VMK_ReturnStatus
sfvmk_advModesGet(vmk_AddrCookie driverData,
                  vmk_UplinkAdvertisedMode *pModes,
                  vmk_uint32 *pNumModes);

static VMK_ReturnStatus
sfvmk_advModesSet(vmk_AddrCookie driverData,
                  vmk_UplinkAdvertisedMode *pModes,
                  vmk_uint32 numModes);

const static vmk_UplinkAdvertisedModesOps sfvmk_advModesOps = {
  .getAdvertisedModes  = sfvmk_advModesGet,
  .setAdvertisedModes  = sfvmk_advModesSet,
};

#endif
/****************************************************************************
 *               vmk_UplinkPauseParamsOps Handlers                          *
 ****************************************************************************/
static VMK_ReturnStatus sfvmk_pauseParamGet(vmk_AddrCookie cookie,
                                            vmk_UplinkPauseParams  *pauseParams);
static VMK_ReturnStatus sfvmk_pauseParamSet(vmk_AddrCookie cookie,
                                            vmk_UplinkPauseParams  pauseParams);


static vmk_UplinkPauseParamsOps sfvmkPauseParamsOps = {
  .pauseParamsGet = sfvmk_pauseParamGet,
  .pauseParamsSet = sfvmk_pauseParamSet,
};

/*! \brief Associate the RSS netpoll to the uplink
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
**
** \return: VMK_OK on success or error code otherwise
**
*/
static VMK_ReturnStatus
sfvmk_associateRSSNetPoll(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_Name netpollName;
  vmk_uint32 qIndex;
  vmk_uint32 qStartIndex;
  vmk_uint32 qEndIndex;

  VMK_ASSERT_NOT_NULL(pAdapter);

  qStartIndex = sfvmk_getRSSQStartIndex(pAdapter);
  qEndIndex = qStartIndex + pAdapter->numRSSQs;

  VMK_ASSERT_NOT_NULL(pAdapter->ppEvq);

  for(qIndex = qStartIndex; qIndex < qEndIndex; qIndex++) {
    vmk_NameFormat(&netpollName, "rss-%d", qIndex);
    /* RSS netpoll are not associated to uplinkSharedQueueData,
     * it need to be registered with uplink explicitly */
    VMK_ASSERT_NOT_NULL(pAdapter->ppEvq[qIndex]);
    status = vmk_NetPollRegisterUplink(pAdapter->ppEvq[qIndex]->netPoll,
                                       pAdapter->uplink.handle, netpollName, VMK_FALSE);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_NetPollRegisterUplink(%u) Failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto failed;
    }
  }

  goto done;

failed:
  for (qIndex--; qIndex >= qStartIndex; qIndex--)
    vmk_NetPollUnregisterUplink(pAdapter->ppEvq[qIndex]->netPoll);

done:

  return status;
}

/*! \brief Disassociate the RSS netpolls from the uplink
 **
 ** \param[in]  pAdapter pointer to sfvmk_adapter_t
 **
 ** \return: VMK_OK on success or error code otherwise
 **
 */
static void
sfvmk_disassociateRssNetPoll(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 qIndex;
  vmk_uint32 qStartIndex;
  vmk_uint32 qEndIndex;

  VMK_ASSERT_NOT_NULL(pAdapter);

  qStartIndex = sfvmk_getRSSQStartIndex(pAdapter);
  qEndIndex = qStartIndex + pAdapter->numRSSQs;

  VMK_ASSERT_NOT_NULL(pAdapter->ppEvq);

  for(qIndex = qStartIndex; qIndex < qEndIndex; qIndex++) {
    VMK_ASSERT_NOT_NULL(pAdapter->ppEvq[qIndex]);
    vmk_NetPollUnregisterUplink(pAdapter->ppEvq[qIndex]->netPoll);
  }
}

/*! \brief  Uplink callback function to associate uplink device with driver and
**          driver register its cap with uplink device.
**
** \param[in]  cookie       vmk_AddrCookie
** \param[in]  uplinkHandle uplink device handle
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_uplinkAssociate(vmk_AddrCookie cookie, vmk_Uplink uplinkHandle)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if ((pAdapter == NULL) || (uplinkHandle == NULL)) {
    SFVMK_ERROR("Invalid argument(s)");
    status = VMK_FAILURE;
    goto done;
  }

  if (pAdapter->uplink.handle != NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Already associated with uplink");
    status = VMK_FAILURE;
    goto done;
  }

  pAdapter->uplink.handle = uplinkHandle;
  pAdapter->uplink.name = vmk_UplinkNameGet(uplinkHandle);

  vmk_SemaLock(&sfvmk_modInfo.lock);
  status = vmk_HashKeyInsert(sfvmk_modInfo.vmkdevHashTable,
                             pAdapter->uplink.name.string,
                             (vmk_HashValue) pAdapter);
  if (status != VMK_OK) {
    vmk_SemaUnlock(&sfvmk_modInfo.lock);
    SFVMK_ADAPTER_ERROR(pAdapter, "Hash Key Insertion failed, %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto done;
  }

  vmk_SemaUnlock(&sfvmk_modInfo.lock);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "%s associated",  pAdapter->uplink.name.string);

  status = sfvmk_registerIOCaps(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_registerIOCaps failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto done;
  }

  if(sfvmk_isRSSEnable(pAdapter)) {
    status = sfvmk_associateRSSNetPoll(pAdapter);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_associateRSSNetPoll Failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Uplink callback function to disassociate uplink device from driver.
**
** \param[in]  cookie     vmk_AddrCookie
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_uplinkDisassociate(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  sfvmk_MutexLock(pAdapter->lock);
  if (pAdapter->state != SFVMK_ADAPTER_STATE_REGISTERED) {
    sfvmk_MutexUnlock(pAdapter->lock);
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter is not quiesced yet");
    status = VMK_FAILURE;
    goto done;
  }
  sfvmk_MutexUnlock(pAdapter->lock);

  if (pAdapter->uplink.handle == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Not associated with uplink");
    status = VMK_FAILURE;
    goto done;
  }

  vmk_SemaLock(&sfvmk_modInfo.lock);
  status = vmk_HashKeyDelete(sfvmk_modInfo.vmkdevHashTable,
                             pAdapter->uplink.name.string,
                             NULL);
  if (status != VMK_OK) {
    vmk_SemaUnlock(&sfvmk_modInfo.lock);
    SFVMK_ADAPTER_ERROR(pAdapter, "%s: Failed to find node in vmkDevice "
                        "table status: %s", pAdapter->uplink.name.string,
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto done;
  }

  if(sfvmk_isRSSEnable(pAdapter)) {
    /* Disassociate Netpoll */
    sfvmk_disassociateRssNetPoll(pAdapter);
  }

  vmk_SemaUnlock(&sfvmk_modInfo.lock);

  pAdapter->uplink.handle = NULL;

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Uplink callback function to transmit pkt
**
** \param[in]  cookie     pointer to sfvmk_adapter_t
** \param[in]  pktList    List of packets to be transmitted
**
** \return: VMK_OK [success]
** \return: VMK_BUSY when queue busy
** \return: VMK_FAILURE [failure]
**
*/
static VMK_ReturnStatus
sfvmk_uplinkTx(vmk_AddrCookie cookie, vmk_PktList pktList)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_PktHandle *pkt;
  vmk_UplinkQueueID uplinkQid;
  vmk_uint32 qid = 0;
  vmk_uint32 txqStartIndex;
  vmk_int16 maxRxQueues;
  vmk_int16 maxTxQueues;
  vmk_Bool queueIdentified = VMK_FALSE;
  VMK_PKTLIST_ITER_STACK_DEF(iter);
  sfvmk_pktCompCtx_t compCtx = {
    .type = SFVMK_PKT_COMPLETION_OTHERS,
  };

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pAdapter);

  maxRxQueues = pAdapter->uplink.queueInfo.maxRxQueues;
  maxTxQueues = pAdapter->uplink.queueInfo.maxTxQueues;

  /* Retrieve the queue id from first packet */
  pkt = vmk_PktListGetFirstPkt(pktList);
  if (pkt == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "First packet pointer NULL");
    goto release_all_pkts;
  }

  uplinkQid = vmk_PktQueueIDGet(pkt);
  qid = vmk_UplinkQueueIDVal(uplinkQid);
  txqStartIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);

  if ((qid < txqStartIndex) || (qid >= (maxTxQueues + maxRxQueues))) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "Invalid QID %d, uplinkQID %p, txqs %d, rxqs %d",
                        qid, uplinkQid, maxTxQueues, maxRxQueues);

    goto release_all_pkts;
  }

  /* Cross over the rx queues in shared queue data structure */
  qid -= txqStartIndex;

  if ((pAdapter->ppTxq == NULL) || (pAdapter->ppTxq[qid] == NULL)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL ppTxq ptr, qid: %d", qid);
    goto release_all_pkts;
  }

  queueIdentified = VMK_TRUE;

  vmk_SpinlockLock(pAdapter->ppTxq[qid]->lock);

  if (pAdapter->ppTxq[qid]->state != SFVMK_TXQ_STATE_STARTED) {
    pAdapter->ppTxq[qid]->stats[SFVMK_TXQ_INVALID_QUEUE_STATE]++;
    vmk_SpinlockUnlock(pAdapter->ppTxq[qid]->lock);
    goto release_all_pkts;
  }

  for (vmk_PktListIterStart(iter, pktList); !vmk_PktListIterIsAtEnd(iter);) {
    vmk_PktListIterRemovePkt(iter, &pkt);

    if (pkt == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL pkt pointer");
      continue;
    }

    if (sfvmk_isTxqStopped(pAdapter, qid)) {
      vmk_SpinlockUnlock(pAdapter->ppTxq[qid]->lock);

      SFVMK_ADAPTER_DEBUG_IO(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_IO,
                             "Queue stopped, returning");
      vmk_PktListIterInsertPktBefore(iter, pkt);
      pAdapter->ppTxq[qid]->stats[SFVMK_TXQ_QUEUE_BUSY]++;
      status = VMK_BUSY;
      goto done;
    }

    status = sfvmk_transmitPkt(pAdapter->ppTxq[qid], pkt);
    if(status == VMK_BUSY) {
      vmk_SpinlockUnlock(pAdapter->ppTxq[qid]->lock);
      SFVMK_ADAPTER_DEBUG_IO(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_IO,
                             "Queue full, returning");
      vmk_PktListIterInsertPktBefore(iter, pkt);
      pAdapter->ppTxq[qid]->stats[SFVMK_TXQ_QUEUE_BUSY]++;
      goto done;
    } else if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_transmitPkt failed status %s",
                          vmk_StatusToString(status));
      sfvmk_pktRelease(pAdapter, &compCtx, pkt);
      vmk_AtomicInc64(&pAdapter->txDrops);
      pAdapter->ppTxq[qid]->stats[SFVMK_TXQ_DISCARD]++;
      continue;
    }

    pAdapter->ppTxq[qid]->stats[SFVMK_TXQ_PKTS]++;
  }

  vmk_SpinlockUnlock(pAdapter->ppTxq[qid]->lock);
  goto done;

release_all_pkts:
  for (vmk_PktListIterStart(iter, pktList); !vmk_PktListIterIsAtEnd(iter);) {
    vmk_PktListIterRemovePkt(iter, &pkt);
    sfvmk_pktRelease(pAdapter, &compCtx, pkt);
    vmk_AtomicInc64(&pAdapter->txDrops);
    if (queueIdentified)
      pAdapter->ppTxq[qid]->stats[SFVMK_TXQ_DISCARD]++;
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief uplink callback function to transmit pkts in panic state
**
** \param[in]  cookie     pointer to sfvmk_adapter_t
** \param[in]  pktList    list of packets to be transmitted
**
** \return: VMK_OK on success, error code otherwise.
**
*/
static VMK_ReturnStatus
sfvmk_panicTx(vmk_AddrCookie cookie, vmk_PktList pktList)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  vmk_UplinkSharedData *pSharedData = NULL;
  vmk_UplinkSharedQueueInfo *pQueueInfo = NULL;
  vmk_PktHandle *pPkt = NULL;
  VMK_PKTLIST_ITER_STACK_DEF(iter);
  sfvmk_pktCompCtx_t compCtx = {
    .type = SFVMK_PKT_COMPLETION_PANIC,
  };

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    for (vmk_PktListIterStart(iter, pktList); !vmk_PktListIterIsAtEnd(iter);) {
      vmk_PktListIterRemovePkt(iter, &pPkt);
      sfvmk_pktRelease(pAdapter, &compCtx, pPkt);
    }
    goto done;
  }

  pSharedData = &pAdapter->uplink.sharedData;
  pQueueInfo = pSharedData->queueInfo;
  pPkt = vmk_PktListGetFirstPkt(pktList);

  /* Set the default tx queue for pktList */
  vmk_PktQueueIDSet(pPkt, pQueueInfo->defaultTxQueueID);
  status = sfvmk_uplinkTx(cookie, pktList);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief uplink callback function to poll for rx pkts in panic state
**
** \param[in]   cookie      pointer to sfvmk_adapter_t
** \param[out]  pktList     list of packets received
**
** \return: VMK_OK always
**
*/
static VMK_ReturnStatus sfvmk_panicPoll(vmk_AddrCookie cookie,
                                        vmk_PktList pktList)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  vmk_uint32 qIndex;
  vmk_PktHandle *pPkt = NULL;
  VMK_PKTLIST_ITER_STACK_DEF(iter);
  sfvmk_pktCompCtx_t compCtx = {
    .type = SFVMK_PKT_COMPLETION_PANIC,
  };

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    for (vmk_PktListIterStart(iter, pktList); !vmk_PktListIterIsAtEnd(iter);) {
      vmk_PktListIterRemovePkt(iter, &pPkt);
      sfvmk_pktRelease(pAdapter, &compCtx, pPkt);
    }
    goto done;
  }

  for (qIndex = 0; qIndex < pAdapter->numEvqsAllocated; qIndex++) {
    sfvmk_evq_t *pEvq = pAdapter->ppEvq[qIndex];

    vmk_SpinlockLock(pEvq->lock);
    pEvq->panicPktList = pktList;
    vmk_SpinlockUnlock(pEvq->lock);

    sfvmk_evqPoll(pEvq, VMK_TRUE);
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return VMK_OK;
}

/*! \brief uplink callback function to get panic-time polling properties
**
** \param[in]   cookie       pointer to sfvmk_adapter_t
** \param[out]  panicInfo    panic-time polling properties of the device
**
** \return: VMK_OK always
**
*/
static VMK_ReturnStatus sfvmk_panicInfoGet(vmk_AddrCookie cookie,
                                           vmk_UplinkPanicInfo *panicInfo)
{
  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_UPLINK);

  /* Fill in data for sfvmk_panicPoll function */
  panicInfo->clientData = cookie;

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_UPLINK);
  return VMK_OK;
}

/*! \brief Uplink callback function to set MTU
**
** \param[in]  cookie  vmk_AddrCookie
** \param[in]  mtu     MTU
**
** \return: VMK_OK on success or error code otherwise
*/
static VMK_ReturnStatus
sfvmk_uplinkMTUSet(vmk_AddrCookie cookie, vmk_uint32 mtu)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pAdapter->uplink.sharedData.mtu == mtu) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                        "New MTU is same as the old: %u, nothing to do", mtu);
    status = VMK_OK;
    goto done;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "MTU update from %u to %u",
                      pAdapter->uplink.sharedData.mtu, mtu);

  /* Not checking MTU validity as it is done by vmkernel itself */

  sfvmk_MutexLock(pAdapter->lock);

  sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);
  pAdapter->uplink.sharedData.mtu = mtu;
  sfvmk_sharedAreaEndWrite(&pAdapter->uplink);

  /* Reset adapter to apply MTU changes if it is started, otherwise
   * MTU would be applied later when adapter is started */
  if (pAdapter->state == SFVMK_ADAPTER_STATE_STARTED) {
    status = sfvmk_quiesceIO(pAdapter);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_uplinkQuiesceIO failed status: %s",
                          vmk_StatusToString(status));
      status = VMK_FAILURE;
      goto failed_quiesce_io;
    }

    status = sfvmk_startIO(pAdapter);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_uplinkStartIO failed status: %s",
                          vmk_StatusToString(status));
      status = VMK_FAILURE;
      goto failed_start_io;
    }

    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                        "Adapter has been reset, MTU %u applied", mtu);
  }

  status = VMK_OK;

failed_quiesce_io:
failed_start_io:
  sfvmk_MutexUnlock(pAdapter->lock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Uplink callback function to set the link status.
**         success of this API enable vmkernel to call
**         startIO uplink callback.
**
** \param[in]  cookie    vmk_AddrCookie
** \param[in]  state     uplink state.
**
** \return: VMK_OK on success or error code otherwise
**
*/
static VMK_ReturnStatus
sfvmk_uplinkStateSet(vmk_AddrCookie cookie, vmk_UplinkState state)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  sfvmk_MutexLock(pAdapter->lock);

  if (pAdapter->state == SFVMK_ADAPTER_STATE_STARTED) {
    status = sfvmk_setMacFilter(pAdapter, state);
    if (status != VMK_OK) {
      sfvmk_MutexUnlock(pAdapter->lock);
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_setMacFilter(%d) failed status: %s",
                          state, vmk_StatusToString(status));
      goto done;
    }
  }

  sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);
  pAdapter->uplink.sharedData.state = state;
  sfvmk_sharedAreaEndWrite(&pAdapter->uplink);

  sfvmk_MutexUnlock(pAdapter->lock);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Uplink callback function to get the NIC stats
**
** \param[in]  cookie  pointer to vmk_AddrCookie
** \param[out] pNicStats    ptr to stats
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_uplinkStatsGet(vmk_AddrCookie cookie, vmk_UplinkStats *pNicStats)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  vmk_Memset(pNicStats, 0, sizeof(*pNicStats));

  sfvmk_MutexLock(pAdapter->lock);

  status = sfvmk_macStatsUpdate(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Stats update failed with error %s",
                        vmk_StatusToString(status));
    goto failed_stats_update;
  }

  pNicStats->rxPkts = pAdapter->adapterStats[EFX_MAC_RX_PKTS];
  pNicStats->txPkts = pAdapter->adapterStats[EFX_MAC_TX_PKTS];
  pNicStats->rxBytes = pAdapter->adapterStats[EFX_MAC_RX_OCTETS];
  pNicStats->txBytes = pAdapter->adapterStats[EFX_MAC_TX_OCTETS];

  pNicStats->rxErrors = pAdapter->adapterStats[EFX_MAC_RX_ERRORS];
  pNicStats->txErrors = pAdapter->adapterStats[EFX_MAC_TX_ERRORS];

  pNicStats->rxDrops = pAdapter->adapterStats[EFX_MAC_RX_DROP_EVENTS];
  pNicStats->txDrops = vmk_AtomicRead64(&pAdapter->txDrops);

  pNicStats->rxMulticastPkts = pAdapter->adapterStats[EFX_MAC_RX_MULTICST_PKTS];
  pNicStats->rxBroadcastPkts = pAdapter->adapterStats[EFX_MAC_RX_BRDCST_PKTS];
  pNicStats->txMulticastPkts = pAdapter->adapterStats[EFX_MAC_TX_MULTICST_PKTS];
  pNicStats->txBroadcastPkts = pAdapter->adapterStats[EFX_MAC_TX_BRDCST_PKTS];

  pNicStats->collisions = pAdapter->adapterStats[EFX_MAC_TX_SGL_COL_PKTS] +
                         pAdapter->adapterStats[EFX_MAC_TX_MULT_COL_PKTS] +
                         pAdapter->adapterStats[EFX_MAC_TX_EX_COL_PKTS] +
                         pAdapter->adapterStats[EFX_MAC_TX_LATE_COL_PKTS];

  pNicStats->rxLengthErrors = pAdapter->adapterStats[EFX_MAC_RX_JABBER_PKTS];

  pNicStats->rxOverflowErrors = pAdapter->adapterStats[EFX_MAC_RX_NODESC_DROP_CNT];
  pNicStats->rxCRCErrors = pAdapter->adapterStats[EFX_MAC_RX_FCS_ERRORS];
  pNicStats->rxFrameAlignErrors = pAdapter->adapterStats[EFX_MAC_RX_ALIGN_ERRORS];

  pNicStats->rxFifoErrors = pAdapter->adapterStats[EFX_MAC_PM_TRUNC_BB_OVERFLOW] +
	                    pAdapter->adapterStats[EFX_MAC_PM_DISCARD_BB_OVERFLOW] +
	                    pAdapter->adapterStats[EFX_MAC_PM_TRUNC_VFIFO_FULL] +
	                    pAdapter->adapterStats[EFX_MAC_PM_DISCARD_VFIFO_FULL];

  pNicStats->rxMissErrors = pAdapter->adapterStats[EFX_MAC_RXDP_DI_DROPPED_PKTS];

  status = VMK_OK;

failed_stats_update:
  sfvmk_MutexUnlock(pAdapter->lock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

#define SFVMK_PRIV_STATS_ENTRY_LEN  100
#define SFVMK_PRIV_STATS_BUFFER_SZ  ((EFX_MAC_NSTATS * SFVMK_PRIV_STATS_ENTRY_LEN) + \
                                     ((SFVMK_MAX_TXQ + SFVMK_MAX_RXQ) * \
                                      SFVMK_PRIV_STATS_ENTRY_LEN))

/*! \brief Fill the buffer with a Tx queue stats
**         lock is already taken.
**
** \param[in]  pAdapter      pointer to sfvmk_adapter_t
** \param[in]  qIndex        queue Index
** \param[in]  pStart        pointer to start position in stats buffer
** \param[in]  maxBytes      maximum number of bytes to output
** \param[out] pBytesCopied  pointer to number of bytes copied in stats buffer
**
** \return: VMK_OK [success]
**     Below error values are returned in case of failure,
**           VMK_LIMIT_EXCEEDED  If stats buffer overflowed
**           VMK_BAD_PARAM       If buffer is not valid.
**           VMK_FAILURE         Any other error
*/
static VMK_ReturnStatus
sfvmk_fillTxQueueStats(sfvmk_adapter_t *pAdapter,
                       vmk_uint16 qIndex,
                       char *pStart, vmk_ByteCount maxBytes,
                       vmk_ByteCount *pBytesCopied)
{
  char *pCurr;
  sfvmk_txq_t *pTxq;
  vmk_ByteCount bytesCopied = 0;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pBytesCopied != NULL)
    *pBytesCopied = 0;

  pTxq = pAdapter->ppTxq[qIndex];
  pCurr = pStart;

  status = vmk_StringFormat(pCurr, maxBytes,
                            &bytesCopied, "TxQ[%u]: %s %lu %s %lu %s %lu\n",
                            qIndex,
                            pSfvmkTxqStatsName[SFVMK_TXQ_PKTS],
                            pTxq->stats[SFVMK_TXQ_PKTS],
                            pSfvmkTxqStatsName[SFVMK_TXQ_BYTES],
                            pTxq->stats[SFVMK_TXQ_BYTES],
                            pSfvmkTxqStatsName[SFVMK_TXQ_DISCARD],
                            pTxq->stats[SFVMK_TXQ_DISCARD]);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_StringFormat failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  if (pBytesCopied != NULL)
   *pBytesCopied += bytesCopied;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief Fill the buffer with a Rx queue stats
**         lock is already taken.
**
** \param[in]  pAdapter      pointer to sfvmk_adapter_t
** \param[in]  qIndex        queue Index
** \param[in]  pStart        pointer to start position in stats buffer
** \param[in]  maxBytes      maximum number of bytes to output
** \param[out] pBytesCopied  pointer to number of bytes copied in stats buffer
**
** \return: VMK_OK [success]
**     Below error values are returned in case of failure,
**           VMK_LIMIT_EXCEEDED  If stats buffer overflowed
**           VMK_BAD_PARAM       If buffer is not valid.
**           VMK_FAILURE         Any other error
*/
static VMK_ReturnStatus
sfvmk_fillRxQueueStats(sfvmk_adapter_t *pAdapter,
                       vmk_uint16 qIndex,
                       char *pStart, vmk_ByteCount maxBytes,
                       vmk_ByteCount *pBytesCopied)
{
  char *pCurr;
  sfvmk_rxq_t *pRxq;
  vmk_ByteCount bytesCopied = 0;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pBytesCopied != NULL)
    *pBytesCopied = 0;

  pRxq = pAdapter->ppRxq[qIndex];
  pCurr = pStart;

  status = vmk_StringFormat(pCurr, maxBytes,
                            &bytesCopied, "RxQ[%u]: %s %lu %s %lu %s %lu\n",
                            qIndex,
                            pSfvmkRxqStatsName[SFVMK_RXQ_PKTS],
                            pRxq->stats[SFVMK_RXQ_PKTS],
                            pSfvmkRxqStatsName[SFVMK_RXQ_BYTES],
                            pRxq->stats[SFVMK_RXQ_BYTES],
                            pSfvmkRxqStatsName[SFVMK_RXQ_DISCARD],
                            pRxq->stats[SFVMK_RXQ_DISCARD]);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_StringFormat failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  if (pBytesCopied != NULL)
   *pBytesCopied += bytesCopied;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

#define SFVMK_UPDATE_STATS_INFO(curr, maxb, count, pCummCount) \
                                 do {                          \
                                   (curr)        += (count);   \
                                   (maxb)        -= (count);   \
                                   if((pCummCount) != NULL)    \
                                     (*(pCummCount)) += (count); \
                                 } while(VMK_FALSE)

/*! \brief Fill the buffer with per Rx/Tx queue stats
**         lock is already taken.
**
** \param[in]  pAdapter      pointer to sfvmk_adapter_t
** \param[in]  pStart        pointer to start position in stats buffer
** \param[in]  maxBytes      maximum number of bytes to output
** \param[out] pBytesCopied  pointer to number of bytes copied in stats buffer
**
** \return: VMK_OK [success]
**     Below error values are returned in case of failure,
**           VMK_LIMIT_EXCEEDED  If stats buffer overflowed
**           VMK_BAD_PARAM       If buffer is not valid.
**           VMK_FAILURE         Any other error
*/
static VMK_ReturnStatus
sfvmk_fillQueueStats(sfvmk_adapter_t *pAdapter,
                     char *pStart, vmk_ByteCount maxBytes,
                     vmk_ByteCount *pBytesCopied)
{
  char *pCurr;
  vmk_uint16 maxRxQueues;
  vmk_uint16 maxTxQueues;
  vmk_uint16 qIndex;
  vmk_ByteCount bytesCopied = 0;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pBytesCopied != NULL)
    *pBytesCopied = 0;

  maxRxQueues = sfvmk_getMaxRxHardwareQueues(pAdapter);
  maxTxQueues = sfvmk_getMaxTxHardwareQueues(pAdapter);

  pCurr = pStart;

  status = vmk_StringFormat(pCurr, maxBytes, &bytesCopied,
                            "  -- Per Hardware Queue Statistics\n");
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_StringFormat failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  SFVMK_UPDATE_STATS_INFO(pCurr, maxBytes, bytesCopied, pBytesCopied);
  bytesCopied = 0;

  for (qIndex = 0; qIndex < maxRxQueues; qIndex++) {
    status = sfvmk_fillRxQueueStats(pAdapter, qIndex, pCurr,
                                    maxBytes, &bytesCopied);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_fillRxQueueStats failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }

    SFVMK_UPDATE_STATS_INFO(pCurr, maxBytes, bytesCopied, pBytesCopied);
  }

  status = vmk_StringFormat(pCurr, maxBytes, &bytesCopied, "\n");
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_StringFormat failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  SFVMK_UPDATE_STATS_INFO(pCurr, maxBytes, bytesCopied, pBytesCopied);
  bytesCopied = 0;

  for (qIndex = 0; qIndex < maxTxQueues; qIndex++) {
    status = sfvmk_fillTxQueueStats(pAdapter, qIndex, pCurr,
                                    maxBytes, &bytesCopied);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_fillTxQueueStats failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }

    SFVMK_UPDATE_STATS_INFO(pCurr, maxBytes, bytesCopied, pBytesCopied);
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief Fill the buffer with private stats
**         lock is already taken.
**
** \param[in]  pAdapter      pointer to sfvmk_adapter_t
** \param[in]  pStart        pointer to start position in stats buffer
** \param[in]  maxBytes      maximum number of bytes to output
** \param[out] pBytesCopied  pointer to number of bytes copied in stats buffer
**
** \return: VMK_OK [success]
**     Below error values are returned in case of failure,
**           VMK_LIMIT_EXCEEDED  If stats buffer overflowed
**           VMK_BAD_PARAM       If buffer is not valid.
**           VMK_FAILURE         Any other error
*/
VMK_ReturnStatus
sfvmk_fillMacStats(sfvmk_adapter_t *pAdapter,
                   char *pStart, vmk_ByteCount maxBytes,
                   vmk_ByteCount *pBytesCopied)
{
  const char *pEntryName;
  char *pCurr;
  uint32_t id;
  vmk_ByteCount bytesCopied = 0;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pBytesCopied != NULL)
    *pBytesCopied = 0;

  status = sfvmk_macStatsUpdate(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Stats sync failed with error %s",
                        vmk_StatusToString(status));
    goto done;
  }

  pCurr = pStart;

  for (id = 0; id < EFX_MAC_NSTATS; id++) {
    pEntryName = efx_mac_stat_name(pAdapter->pNic, id);
    status = vmk_StringFormat(pCurr, maxBytes,
                              &bytesCopied, "%s: %lu\n", pEntryName,
                              pAdapter->adapterStats[id]);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_StringFormat failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }

    SFVMK_UPDATE_STATS_INFO(pCurr, maxBytes, bytesCopied, pBytesCopied);
  }

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief Handler used by vmkernel to get uplink private stats length
**
** \param[in]  cookie   pointer to sfvmk_adapter_t
** \param[out] pLength  length of the private stats in bytes
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus
sfvmk_privStatsLengthGet(vmk_AddrCookie cookie, vmk_ByteCount *pLength)
{

  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pLength == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL pLength ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  *pLength = SFVMK_PRIV_STATS_BUFFER_SZ;
   status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief uplink private statistics callback handler
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[out] pStatsBuf buffer to put device private stats
** \param[in]  length    length of stats buf in bytes
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus
sfvmk_privStatsGet(vmk_AddrCookie cookie,
                    char *pStatsBuf, vmk_ByteCount length)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  char *pCurr;
  vmk_ByteCount maxBytes = 0;;
  vmk_ByteCount bytesCopied = 0;
  vmk_ByteCount totalBytes = 0;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pStatsBuf == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Stats buffer is NULL");
    status = VMK_BAD_PARAM;
    goto done;
  }

  sfvmk_MutexLock(pAdapter->lock);
  maxBytes = SFVMK_PRIV_STATS_BUFFER_SZ;
  pCurr = pStatsBuf;

  status = vmk_StringFormat(pCurr, maxBytes,
                            &bytesCopied, "\n");
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_StringFormat failed status: %s",
                        vmk_StatusToString(status));
    goto failed_stats_update;
  }

  maxBytes -= bytesCopied;
  totalBytes += bytesCopied;
  pCurr += bytesCopied;

  status = sfvmk_fillMacStats(pAdapter, pCurr, maxBytes, &bytesCopied);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk MAC stats failed");
    goto failed_stats_update;
  }

  maxBytes -= bytesCopied;
  totalBytes += bytesCopied;
  pCurr += bytesCopied;

  status = sfvmk_fillQueueStats(pAdapter, pCurr, maxBytes, &bytesCopied);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Priv stats buffer fill failed with error %s",
                        vmk_StatusToString(status));
    goto failed_stats_update;
  }

  maxBytes -= bytesCopied;
  totalBytes += bytesCopied;
  pCurr += bytesCopied;

  status = VMK_OK;

failed_stats_update:
  sfvmk_MutexUnlock(pAdapter->lock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief Uplink callback function to enable cap
**
** \param[in]  cookie      vmk_AddrCookie
** \param[in]  uplinkCap   uplink capability to be enabled
**
* @return: VMK_OK always
**
*/
static VMK_ReturnStatus
sfvmk_uplinkCapEnable(vmk_AddrCookie cookie, vmk_UplinkCap uplinkCap)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK,
                                 "uplinkCap: %u", uplinkCap);

  /*
   * There is just one esxcli command for changing NIC CSO setting which
   * applies to both IPv4 and IPv6:
   * esxcli network nic cso set -e 1 -n <vmnicX> */
  if((VMK_UPLINK_CAP_IPV4_CSO == uplinkCap) ||
     (VMK_UPLINK_CAP_IPV6_CSO == uplinkCap)) {
    pAdapter->isRxCsumEnabled = VMK_TRUE;
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return VMK_OK;
}

/*! \brief uplink callback function to disable cap
**
** \param[in]  cookie     vmk_AddrCookie
** \param[in]  uplinkCap  uplink cap to be disabled
**
** \return: VMK_OK always
**
*/
static VMK_ReturnStatus
sfvmk_uplinkCapDisable(vmk_AddrCookie cookie, vmk_UplinkCap uplinkCap)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK,
                                 "uplinkCap: %u", uplinkCap);

  /*
   * There is just one esxcli command for changing NIC CSO setting which
   * applies to both IPv4 and IPv6:
   * esxcli network nic cso set -e 0 -n <vmnicX> */
  if((VMK_UPLINK_CAP_IPV4_CSO == uplinkCap) ||
     (VMK_UPLINK_CAP_IPV6_CSO == uplinkCap)) {
    pAdapter->isRxCsumEnabled = VMK_FALSE;
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return VMK_OK;
}

/*! \brief uplink callback function to reset the adapter.
**
** \param[in]  cookie  vmk_AddrCookie
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_uplinkReset(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_FAILURE;
    goto done;
  }

  sfvmk_MutexLock(pAdapter->lock);

  status = sfvmk_quiesceIO(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_uplinkQuiesceIO failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto failed_quiesce_io;
  }

  status = sfvmk_startIO(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_uplinkStartIO failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto failed_start_io;
  }

  status = VMK_OK;

failed_quiesce_io:
failed_start_io:
  sfvmk_MutexUnlock(pAdapter->lock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to get the cable type
**
** \param[in]   cookie      pointer to sfvmk_adapter_t
** \param[out]  pCableType  ptr to cable type
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus sfvmk_getCableType(vmk_AddrCookie cookie,
                                           vmk_UplinkCableType *pCableType)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;
  efx_phy_media_type_t mediumType;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  sfvmk_MutexLock(pAdapter->lock);
  mediumType = pAdapter->port.mediumType;
  sfvmk_MutexUnlock(pAdapter->lock);

  switch (mediumType) {
    case EFX_PHY_MEDIA_QSFP_PLUS:
      *pCableType = sfvmk_decodeQsfpCableType(pAdapter);
      break;
    case EFX_PHY_MEDIA_SFP_PLUS:
      *pCableType = sfvmk_decodeSfpCableType(pAdapter);
      break;
    default:
      *pCableType = VMK_UPLINK_CABLE_TYPE_OTHER;
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                          "Unknown media = %d", mediumType);
      break;
  }

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to set the cable type
**
** \param[in]   cookie     pointer to sfvmk_adapter_t
** \param[out]  cableType  cable type
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus sfvmk_setCableType(vmk_AddrCookie cookie,
                                           vmk_UplinkCableType cableType)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  SFVMK_ADAPTER_ERROR(pAdapter, "setCableType is not supported");

  status = VMK_NOT_SUPPORTED;

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to get all cable type supported by interface.
**
** \param[in]   cookie      pointer to sfvmk_adapter_t
** \param[out]  pCableType  ptr to cable type
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus sfvmk_getSupportedCableTypes(vmk_AddrCookie cookie,
                                                     vmk_UplinkCableType *pCableType)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  *pCableType = VMK_UPLINK_CABLE_TYPE_FIBRE | VMK_UPLINK_CABLE_TYPE_DA;
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to retrieve log level.
**
** \param[in]   cookie    pointer to sfvmk_adapter_t
** \param[out]  pLevel     pointer to log level
**
** \return: VMK_OK on success or error code otherwise
**
*/
static VMK_ReturnStatus
sfvmk_messageLevelGet(vmk_AddrCookie cookie, vmk_uint32 *pLevel)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  *pLevel = modParams.debugMask;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "Current debug mask: 0x%x", *pLevel);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief uplink callback function to set log level.
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[in]  level     log level
**
** \return: VMK_OK on success or error code otherwise
**
*/
static VMK_ReturnStatus
sfvmk_messageLevelSet(vmk_AddrCookie cookie, vmk_uint32 level)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "Setting debug mask to 0x%x", level);

  modParams.debugMask = level;
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief uplink callback function to set the link status
**
** \param[in]  cookie       pointer to sfvmk_adapter_t
** \param[out] pLinkStatus  pointer to link status
**
** \return: VMK_OK [success] error code [failure]
**          VMK_BAD_PARAM:     NULL pointer to sfvmk_adapter_t
**          VMK_FAILURE:       Other failure
**
*/
static VMK_ReturnStatus
sfvmk_uplinkLinkStatusSet(vmk_AddrCookie cookie,
                          vmk_LinkStatus *pLinkStatus)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto end;
  }

  sfvmk_MutexLock(pAdapter->lock);

  /* Handle Link down request */
  if (pLinkStatus->state == VMK_LINK_STATE_DOWN) {

    /* If Link State is already down, no action required */
    if (pAdapter->state != SFVMK_ADAPTER_STATE_STARTED) {
      SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                          "Take no action, Link is already down");
      status = VMK_OK;
      goto quiesceio_done;
    }

    /* Call Quiesce IO to bring the link down */
    status = sfvmk_quiesceIO(pAdapter);
    if (status != VMK_OK)
      SFVMK_ADAPTER_ERROR(pAdapter, "Link down failed with error %s",
                          vmk_StatusToString(status));

    goto quiesceio_done;
  }

  /* Handle Link up request */
  if (pAdapter->state != SFVMK_ADAPTER_STATE_STARTED) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                        "Bringing link up");
    status = sfvmk_startIO(pAdapter);
    if (status != VMK_OK)
      SFVMK_ADAPTER_ERROR(pAdapter, "Link up failed with error %s",
                          vmk_StatusToString(status));
    goto startio_done;
  }

  if (pAdapter->state == SFVMK_ADAPTER_STATE_STARTED) {
    /* Check if the request is for speed change.
     *
     * Note: This driver only support Medford and Medford+ boards. Half
     * duplex mode and speed less than 1000 Mbps is not supported.
     * Only 1000Mbps Full duplex and onwards is supported by driver */
    status = sfvmk_phyLinkSpeedSet(pAdapter, pLinkStatus->speed);
    if (status != VMK_OK)
      SFVMK_ADAPTER_ERROR(pAdapter, "Link speed set failed with error %s",
                          vmk_StatusToString(status));
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "device Link up");
quiesceio_done:
startio_done:
  sfvmk_MutexUnlock(pAdapter->lock);

end:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief function to register all driver cap with uplink device.
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus sfvmk_registerIOCaps(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  const efx_nic_cfg_t *pNicCfg = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  /* Driver supports scatter-gather transmit */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_SG_TX, NULL);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "CAP_SG_TX register failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Driver supports scatter-gather entries spanning multiple pages */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_MULTI_PAGE_SG, NULL);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,"CAP_MULTI_PAGE_SG register failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Driver supports getting and setting cable type */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_CABLE_TYPE, &sfvmkCableTypeOps);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,"vmk_UplinkCapRegister failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Register capability for Netqueue support */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_MULTI_QUEUE,
                                 &sfvmkQueueOps);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,"VMK_UPLINK_CAP_MULTI_QUEUE failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Register capability for RX/TX ring params configuration */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_RING_PARAMS,
                                 (vmk_UplinkRingParamsOps *)
                                 &sfvmk_ringParamsOps);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "VMK_UPLINK_CAP_RING_PARAMS register failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Register capability to control logging level */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_MESSAGE_LEVEL,
                                 (vmk_UplinkMessageLevelOps *)
                                 &sfvmk_messageLevelOps);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "VMK_UPLINK_CAP_MESSAGE_LEVEL register failed status: %s",
                        vmk_StatusToString(status));
  }

  /* Register capability for changing link status and speed */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_LINK_STATUS_SET,
                                 &sfvmk_uplinkLinkStatusSet);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "VMK_UPLINK_CAP_LINK_STATUS_SET register failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Register capability for getting private stats */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_PRIV_STATS,
                                 (vmk_UplinkPrivStatsOps *)
                                 &sfvmkPrivStatsOps);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "VMK_UPLINK_CAP_PRIV_STATS register failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Register capability for modifying packet headers on Tx */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_MOD_TX_HDRS, NULL);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,"VMK_UPLINK_CAP_MOD_TX_HDRS failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL NIC configuration ptr");
    goto done;
  }

  /* Register capability for VLAN TX Offload support (tag insertion)
   * Note: only claim VMK_UPLINK_CAP_VLAN_TX_INSERT when hardware has this
   * capability */
  if (pNicCfg->enc_hw_tx_insert_vlan_enabled) {
    status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                   VMK_UPLINK_CAP_VLAN_TX_INSERT, NULL);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,"VMK_UPLINK_CAP_VLAN_TX_INSERT failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }
  }
  else {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_INFO,
                        "VMK_UPLINK_CAP_VLAN_TX_INSERT: not supported by hw");
  }

  /* Register capability coalesce param configuration */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_COALESCE_PARAMS,
                                 (vmk_UplinkCoalesceParamsOps *)
                                 &sfvmkCoalesceParamsOps);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "VMK_UPLINK_CAP_COALESCE_PARAMS register failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Register capability for self test/offline diagnostics  */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_SELF_TEST,
                                 (vmk_UplinkSelfTestOps *)
                                 &sfvmk_selfTestOps);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "VMK_UPLINK_CAP_SELF_TEST register failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Register capability for IPv4 TCP and UDP checksum offload */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_IPV4_CSO, NULL);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,"VMK_UPLINK_CAP_IPV4_CSO failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Register capability for IPv6 TCP and UDP checksum offload */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_IPV6_CSO, NULL);
  if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
    SFVMK_ADAPTER_ERROR(pAdapter,"VMK_UPLINK_CAP_IPV6_CSO failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  /* Register TSO capability if supported by HW */
  if (pAdapter->isTsoFwAssisted) {
    status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                   VMK_UPLINK_CAP_IPV4_TSO, NULL);
    if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
      SFVMK_ADAPTER_ERROR(pAdapter,"VMK_UPLINK_CAP_IPV4_TSO failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }

    status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                   VMK_UPLINK_CAP_IPV6_TSO, NULL);
    if ((status != VMK_OK) && (status != VMK_IS_DISABLED)) {
      SFVMK_ADAPTER_ERROR(pAdapter,"VMK_UPLINK_CAP_IPV6_TSO failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }

    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_INFO,
                        "IPV4_TSO and IPV6_TSO registered");
  } else {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_INFO,
                        "TSO capability not registered: not supported by hw");
  }

  /* Register network dump capability */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_NETWORK_DUMP, &sfvmkNetDumpOps);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,"VMK_UPLINK_CAP_NETWORK_DUMP failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
  /* Register advertise modes capability */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_ADVERTISE_MODES,
                                 (vmk_UplinkAdvertisedModesOps *)&sfvmk_advModesOps);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,"VMK_UPLINK_CAP_ADVERTISE_MODES failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }
#endif

  /* Register dynamic RSS capability */
  if (sfvmk_isRSSEnable(pAdapter)) {
    status = vmk_UplinkQueueRegisterFeatureOps(pAdapter->uplink.handle,
                                               VMK_UPLINK_QUEUE_FEAT_RSS_DYN,
                                               (void *)&sfvmkRssDynOps);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_UplinkQueueRegisterFeatureOps for Dynamic RSS failed status: %s",
                          vmk_StatusToString(status));
    }
  } else {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_INFO,
                        "Dynamic RSS capability not registered");
  }

  /* Register capability for vxlan offload */
  if (pAdapter->isTunnelEncapSupported & SFVMK_VXLAN_OFFLOAD) {
    status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 5, 0, 0)
                                   VMK_UPLINK_CAP_VXLAN_OFFLOAD,
#else
                                   VMK_UPLINK_CAP_ENCAP_OFFLOAD,
#endif
                                   &sfvmk_vxlanOffloadOps);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "VMK_UPLINK_CAP_ENCAP_OFFLOAD register failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }
  } else {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_INFO,
                        "VXLAN Offload capability not registered : "
                        "not supported by hw or feature is disabled");
  }

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
  /* Register capability for geneve offload */
  if (pAdapter->isTunnelEncapSupported & SFVMK_GENEVE_OFFLOAD) {
    status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                   VMK_UPLINK_CAP_GENEVE_OFFLOAD,
                                   &sfvmk_geneveOffloadOps);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "VMK_UPLINK_GENEVE_OFFLOAD register failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }
  } else {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_INFO,
                        "Geneve Offload capability not registered : "
                        "not supported by hw or feature is disabled");
  }
#endif

  /* Register capability for pause param configuration */
  status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                 VMK_UPLINK_CAP_PAUSE_PARAMS,
                                 (vmk_UplinkPauseParamsOps *)
                                 &sfvmkPauseParamsOps);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "VMK_UPLINK_CAP_PAUSE_PARAMS register failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

#ifdef SFVMK_SUPPORT_SRIOV
  /* Register capability for SR-IOV if FW supports EVB and
   * VFs have been enabled for this adapter */
  if (pAdapter->numVfsEnabled && pNicCfg->enc_datapath_cap_evb) {
    status = vmk_UplinkCapRegister(pAdapter->uplink.handle,
                                   VMK_UPLINK_CAP_SRIOV, NULL);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "VMK_UPLINK_CAP_SRIOV register failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                        "SR-IOV capability registered successfully");
  } else {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_INFO,
                        "SR-IOV capability not registered : "
                        "EVB %ssupported by FW, VFs enabled [%u]",
                        pNicCfg->enc_datapath_cap_evb ? "" : "not ",
                        pAdapter->numVfsEnabled);
  }
#endif

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}



/*! \brief function to update the queue state.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  qState   queue state STOPPED or STARTED
** \param[in]  qIndex   index in to shared queueData structure
**
** \return:    Nothing
**
*/
void sfvmk_updateQueueStatus(sfvmk_adapter_t *pAdapter,
                             vmk_UplinkQueueState qState,
                             vmk_uint32 qIndex)
{
  vmk_UplinkSharedQueueData *queueData;
  sfvmk_txqStats_t idx = SFVMK_TXQ_QUEUE_BLOCKED;
  sfvmk_uplink_t *pUplink = &pAdapter->uplink;
  vmk_uint32 txqStartIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  txqStartIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);
  queueData = sfvmk_getUplinkTxSharedQueueData(&pAdapter->uplink);

  if (queueData[qIndex].state != qState) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                        "qIndex %u, state %u to %u", qIndex,
                        queueData[qIndex].state, qState);

    sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);
    if (queueData[qIndex].flags &
        (VMK_UPLINK_QUEUE_FLAG_IN_USE | VMK_UPLINK_QUEUE_FLAG_DEFAULT)) {
      queueData[qIndex].state = qState;
      if (qState == VMK_UPLINK_QUEUE_STATE_STOPPED) {
        pUplink->queueInfo.activeTxQueues--;
        vmk_BitVectorClear(pUplink->queueInfo.activeQueues,
                           qIndex + txqStartIndex);
      } else {
        pUplink->queueInfo.activeTxQueues++;
        vmk_BitVectorSet(pUplink->queueInfo.activeQueues,
                         qIndex + txqStartIndex);
      }
    }
    sfvmk_sharedAreaEndWrite(&pAdapter->uplink);

    if (queueData[qIndex].flags &
        (VMK_UPLINK_QUEUE_FLAG_IN_USE | VMK_UPLINK_QUEUE_FLAG_DEFAULT)) {
      if (qState == VMK_UPLINK_QUEUE_STATE_STOPPED) {
        vmk_UplinkQueueStop(pAdapter->uplink.handle, queueData[qIndex].qid);
        idx = SFVMK_TXQ_QUEUE_BLOCKED;
      }
      else {
        vmk_UplinkQueueStart(pAdapter->uplink.handle, queueData[qIndex].qid);
        idx = SFVMK_TXQ_QUEUE_UNBLOCKED;
      }
      pAdapter->ppTxq[qIndex]->stats[idx]++;
    }
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

/*! \brief uplink callback function to get ring params
**
** \param[in]  cookie   pointer to sfvmk_adapter_t
** \param[out] params   pointer to vmk_UplinkRingParams
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_ringParamsGet(vmk_AddrCookie cookie,
                    vmk_UplinkRingParams *params)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;
  const efx_nic_cfg_t *pNicCfg;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_FAILURE;
    goto done;
  }

  if (params == NULL) {
    SFVMK_ERROR("NULL vmk_UplinkRingParams ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_cfg_get failed");
    status = VMK_FAILURE;
    goto done;
  }

  /* rxMaxPending and txMaxPending denotes max number of
   * RX/TX descs supported by device */
  params->txMaxPending = pNicCfg->enc_txq_max_ndescs;
  params->rxMaxPending = EFX_RXQ_MAXNDESCS;

  /* rxPending and txPending denotes currently configured
   * RX/TX descs values */
  params->txPending = pAdapter->numTxqBuffDesc;
  params->rxPending = pAdapter->numRxqBuffDesc;

  /* There are no dedicated rings for mini/jumbo frames so
   * these entries are not supported */
  params->rxMiniMaxPending = 0;
  params->rxJumboMaxPending = 0;
  params->rxMiniPending = 0;
  params->rxJumboPending = 0;

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to set RX & TX ring params
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[in]  params    pointer to vmk_UplinkRingParams
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_ringParamsSet(vmk_AddrCookie cookie,
                    vmk_UplinkRingParams *params)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;
  const efx_nic_cfg_t *pNicCfg;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_FAILURE;
    goto done;
  }

  if (params == NULL) {
    SFVMK_ERROR("NULL vmk_UplinkRingParams ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_cfg_get failed");
    status = VMK_FAILURE;
    goto done;
  }

  if ((params->rxPending < EFX_RXQ_MINNDESCS) ||
      (params->rxPending > EFX_RXQ_MAXNDESCS) ||
      (!ISP2(params->rxPending))) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Unsupported RX ring param :%d", params->rxPending);
    status = VMK_BAD_PARAM;
    goto done;
  }

  if ((params->txPending < EFX_TXQ_MINNDESCS) ||
      (params->txPending > pNicCfg->enc_txq_max_ndescs) ||
      (!ISP2(params->txPending))) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Unsupported TX ring param :%d", params->txPending);
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Nothing to be done if requested ring params values
   * are same as configured value */
  if ((params->txPending == pAdapter->numTxqBuffDesc) &&
      (params->rxPending == pAdapter->numRxqBuffDesc)) {
    status = VMK_OK;
    goto done;
  }

  sfvmk_MutexLock(pAdapter->lock);

  status = sfvmk_quiesceIO(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_uplinkQuiesceIO failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto failed_quiesce_io;
  }

  /* Configure requested RX & TX queue buffer descs */
  pAdapter->numTxqBuffDesc = params->txPending;
  pAdapter->numRxqBuffDesc = params->rxPending;

  status = sfvmk_startIO(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_uplinkStartIO failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto failed_start_io;
  }

  status = VMK_OK;

failed_quiesce_io:
failed_start_io:
  sfvmk_MutexUnlock(pAdapter->lock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Set the driver limit in fw.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_setDrvLimits(sfvmk_adapter_t *pAdapter)
{
  efx_drv_limits_t limits;

  vmk_Memset(&limits, 0, sizeof(limits));

  /* The resource limits configured here use the allotted resource values
   * obtained when the NIC was probed. Request fixed sizes here to ensure
   * that the allocated resources do not change across an MC reset.
   */
  limits.edl_min_evq_count = limits.edl_max_evq_count = pAdapter->numEvqsAllotted;
  limits.edl_min_txq_count = limits.edl_max_txq_count = pAdapter->numTxqsAllotted;
  limits.edl_min_rxq_count = limits.edl_max_rxq_count = pAdapter->numRxqsAllotted;

  return efx_nic_set_drv_limits(pAdapter->pNic, &limits);
}

/*! \brief uplink callback function to start the IO operations.
**
** \param[in]  cookie  pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_uplinkStartIO(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *) cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;
  VMK_ReturnStatus rc = VMK_FAILURE;
  unsigned int attempt;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  sfvmk_MutexLock(pAdapter->lock);

  for (attempt = 0; attempt < 3; ++attempt) {
    status = sfvmk_startIO(pAdapter);
    if (status == VMK_OK) {
      goto start_io_done;
    }

    /* Sleep for 100 milliseconds */
    rc = sfvmk_worldSleep(SFVMK_STARTIO_ON_RESET_TIME_OUT_USEC);
    if (rc != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_WorldSleep failed status: %s",
                          vmk_StatusToString(rc));
      /* World is dying */
      break;
    }
    sfvmk_mcdiReset(pAdapter);
  }

  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Uplink Start IO failed with status: %s",
                        vmk_StatusToString(status));
  }

start_io_done:
  sfvmk_MutexUnlock(pAdapter->lock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief start device IO operations. Assumes Adapter
**         lock is already taken.
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] or if pAdapter
**                 is already in the start state
** \return: Error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_startIO(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pAdapter);

  if (pAdapter->state == SFVMK_ADAPTER_STATE_STARTED) {
    /* This should not normally happen so recording it as an error instead of log */
    status = VMK_OK;
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter IO is already started");
    goto done;
  }

#ifdef SFVMK_SUPPORT_SRIOV
  /* Setup EVB switching */
  if (pAdapter->evbState == SFVMK_EVB_STATE_UNINITIALIZED) {
    status = sfvmk_evbSwitchInit(pAdapter);
    if ((status != VMK_OK) && (status != VMK_BAD_PARAM)) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_evbSwitchInit failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }

    status = sfvmk_proxyAuthInit(pAdapter);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "sfvmk_proxyAuthInit failed status: %s",
                          vmk_StatusToString(status));
      goto proxy_auth_init_failed;
    }
  }
#endif

  /* Set required resource limits */
  status = sfvmk_setDrvLimits(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_setDrvLimits failed status: %s",
                        vmk_StatusToString(status));
    goto failed_set_drv_limits;
  }

  status = efx_nic_init(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_init failed status: %s",
                        vmk_StatusToString(status));
    goto failed_nic_init;
  }

  status = sfvmk_intrStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_intrStart failed status: %s",
                        vmk_StatusToString(status));
    goto failed_intr_start;
  }

  status = sfvmk_evStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_evStart failed status: %s",
                        vmk_StatusToString(status));
    goto failed_ev_start;
  }

  status = sfvmk_portStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_portStart failed status: %s",
                        vmk_StatusToString(status));
    goto failed_port_start;
  }

  status = sfvmk_rxStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_rxStart failed status: %s",
                        vmk_StatusToString(status));
    goto failed_rx_start;
  }

  status = sfvmk_txStart(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_txStart failed status: %s",
                        vmk_StatusToString(status));
    goto failed_tx_start;
  }

  status = sfvmk_allocFilterDBHash(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_allocFilterDBHash failed status: %s",
                        vmk_StatusToString(status));
    goto failed_filter_db_init;
  }

  if (pAdapter->isTunnelEncapSupported) {
    /* Tunnel reconfigure triggers reset, this flag
     * is being used to prevent resets in a loop */
    if (pAdapter->startIOTunnelReCfgReqd == VMK_TRUE) {
      status = efx_tunnel_reconfigure(pAdapter->pNic);
      /* Tunnel_reconfigure returns VMK_RETRY to indicate impending reboot */
      if ((status != VMK_OK) && (status != VMK_RETRY)) {
        SFVMK_ADAPTER_ERROR(pAdapter, "efx_tunnel_reconfigure failed status: %s",
                            vmk_StatusToString(status));
        goto failed_tunnel_reconfig;
      }
      pAdapter->startIOTunnelReCfgReqd = VMK_FALSE;
    }
  }

  pAdapter->state = SFVMK_ADAPTER_STATE_STARTED;

  /* Send startIO completion event to any world waiting for this event */
  if (pAdapter->startIO_compl_event) {
    vmk_WorldWakeup((vmk_WorldEventID) pAdapter->startIO_compl_event);
  }
  status = VMK_OK;
  goto done;

failed_tunnel_reconfig:
  sfvmk_freeFilterDBHash(pAdapter);

failed_filter_db_init:
  sfvmk_txStop(pAdapter);

failed_tx_start:
  sfvmk_rxStop(pAdapter);

failed_rx_start:
  sfvmk_portStop(pAdapter);

failed_port_start:
  sfvmk_evStop(pAdapter);

failed_ev_start:
  sfvmk_intrStop(pAdapter);

failed_intr_start:
  efx_nic_fini(pAdapter->pNic);

failed_nic_init:
failed_set_drv_limits:
#ifdef SFVMK_SUPPORT_SRIOV
  sfvmk_proxyAuthFini(pAdapter);

proxy_auth_init_failed:
  sfvmk_evbSwitchFini(pAdapter);
#endif

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to  Quiesce IO operations.
**
** \param[in]  cookie  pointer to vmk_AddrCookie
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_uplinkQuiesceIO(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    return VMK_BAD_PARAM;
  }

  sfvmk_MutexLock(pAdapter->lock);
  status = sfvmk_quiesceIO(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Uplink Quiesce IO failed with status: %s",
                        vmk_StatusToString(status));
  }

  sfvmk_MutexUnlock(pAdapter->lock);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief stop device IO operations. Assumes Adapter
**         lock is already taken.
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_quiesceIO(sfvmk_adapter_t *pAdapter)
{
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pAdapter);

  if (pAdapter->state != SFVMK_ADAPTER_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter IO is not yet started");
    goto clean_up_evb;
  }

  pAdapter->state = SFVMK_ADAPTER_STATE_REGISTERED;

  /* Called sfvmk_macLinkUpdate directly insted of using helper as
   * link should be down before proceeding further. */
  pAdapter->port.linkMode = EFX_LINK_DOWN;
  sfvmk_macLinkUpdate(pAdapter);

  sfvmk_freeFilterDBHash(pAdapter);

  sfvmk_txStop(pAdapter);

  sfvmk_rxStop(pAdapter);

  sfvmk_portStop(pAdapter);

  sfvmk_evStop(pAdapter);

  status = sfvmk_intrStop(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_intrStop failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  efx_nic_fini(pAdapter->pNic);

clean_up_evb:
#ifdef SFVMK_SUPPORT_SRIOV
  /* clean-up EVB switch */
  if (pAdapter->evbState == SFVMK_EVB_STATE_STOPPING) {
    sfvmk_proxyAuthFini(pAdapter);

    status = sfvmk_evbSwitchFini(pAdapter);
    if ((status != VMK_OK) && (status != VMK_BAD_PARAM)) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_evbSwitchFini failed status: %s",
                          vmk_StatusToString(status));
      /* Do not return error to allow uplinkResetHelper proceed with startIO */
    }
  }
#endif

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Update supported link modes for reporting to VMK interface
**
** \param[in]  pAdapter  pointer to sfvmk_adapter_t
** \param[in]  efxPhyCap PHY capability to get, either of
**                       EFX_PHY_CAP_DEFAULT or EFX_PHY_CAP_CURRENT
**
** \return: void
*/
void
sfvmk_updateSupportedCap(sfvmk_adapter_t *pAdapter, vmk_uint8 efxPhyCap)
{
  vmk_uint32 *pCount = NULL;
  vmk_UplinkSupportedMode *pSupportedModes = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  if (efxPhyCap == EFX_PHY_CAP_PERM) {
    pSupportedModes = pAdapter->uplink.supportedModes;
    pCount = &pAdapter->uplink.numSupportedModes;
#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
  } else if (efxPhyCap == EFX_PHY_CAP_CURRENT) {
    pSupportedModes = pAdapter->uplink.advertisedModes;
    pCount = &pAdapter->uplink.numAdvertisedModes;
#endif
  } else {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid efxPhyCap [%u] value", efxPhyCap);
    goto done;
  }

  sfvmk_getPhyAdvCaps(pAdapter, efxPhyCap, pSupportedModes, pCount);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

/****************************************************************************
 * Utility function to design TX/RX queue layout                            *
 ****************************************************************************/
#define SFVMK_DEFAULT_UPLINK_RXQ        SFVMK_UPLINK_RXQ_START_INDEX

/*! \brief Get number of uplink TXQs
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: Number of uplink TXQs
*/
static inline vmk_uint32
sfvmk_getNumUplinkTxq(sfvmk_adapter_t *pAdapter)
{
  return pAdapter->uplink.queueInfo.maxTxQueues;
}

/*! \brief Get number of uplink RXQs
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: Number of uplink RXQs
*/
static inline vmk_uint32
sfvmk_getNumUplinkRxq(sfvmk_adapter_t *pAdapter)
{
  return pAdapter->uplink.queueInfo.maxRxQueues;
}

/*! \brief Get number of shared uplink queues
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: Number of shared uplink queues
*/
static inline vmk_uint32
sfvmk_getNumUplinkQueues(sfvmk_adapter_t *pAdapter)
{
  return (sfvmk_getNumUplinkRxq(pAdapter) + sfvmk_getNumUplinkTxq(pAdapter));
}

/*! \brief Check if an uplink queue is already in use
**
** \param[in]  pUplink  pointer to uplink structure
** \param[in]  qIndex   uplink queue index
**
** \return: VMK_TRUE    If it is free
** \return: VMK_FALSE   Otherwise
*/
static inline vmk_Bool
sfvmk_isQueueFree(sfvmk_uplink_t *pUplink, vmk_uint32 qIndex)
{
  return ((pUplink->queueInfo.queueData[qIndex].flags & VMK_UPLINK_QUEUE_FLAG_IN_USE) == 0);
}

/*! \brief Get the start index of uplink RXQs in vmk_UplinkSharedQueueData array
**
** \param[in]  pUplink  pointer to uplink structure
**
** \return: index from where RXQs is starting in vmk_UplinkSharedQueueData array
*/
static inline vmk_uint32
sfvmk_getUplinkRxqStartIndex(sfvmk_uplink_t *pUplink)
{
  return SFVMK_UPLINK_RXQ_START_INDEX;
}

/*! \brief Check if given uplink RXQ is a default RXQ
**
** \param[in]  pUplink  pointer to uplink structure
** \param[in]  qIndex   RXQ index
**
** \return: VMK_TRUE    If it is a default RXQ
** \return: VMK_FALSE   otherwise
*/
static inline vmk_Bool
sfvmk_isDefaultUplinkRxq(sfvmk_uplink_t *pUplink, vmk_uint32 qIndex)
{
  return (qIndex == SFVMK_DEFAULT_UPLINK_RXQ);
}

/*! \brief Check if given uplink TXQ is a default TXQ
**
** \param[in]  pUplink  pointer to uplink structure
** \param[in]  qIndex   Queue index
**
** \return: VMK_TRUE    If it is a default TXQ
** \return: VMK_FALSE   otherwise
*/
static inline vmk_Bool
sfvmk_isDefaultUplinkTxq(sfvmk_uplink_t *pUplink, vmk_uint32 qIndex)
{
  return (qIndex == pUplink->queueInfo.maxRxQueues);
}

/*! \brief Check if given uplinkQ is in start state.
**
** \param[in]  pUplink  pointer to uplink structure
** \param[in]  qIndex   Q index
**
** \return: VMK_TRUE    If Q is in start state.
** \return: VMK_FALSE   otherwise
*/
static inline vmk_Bool
sfvmk_isUplinkQStarted(sfvmk_uplink_t *pUplink, vmk_uint32 qIndex)
{
  return (pUplink->queueInfo.queueData[qIndex].state ==
          VMK_UPLINK_QUEUE_STATE_STARTED);
}

/*! \brief Check if given uplink RXQ is a valid Q.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  qIndex   RXQ index
**
** \return: VMK_TRUE    If RXQ is a valid Q.
** \return: VMK_FALSE   otherwise
*/
static inline vmk_Bool
sfvmk_isValidRXQ(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  vmk_uint32 rxqStartIndex = sfvmk_getUplinkRxqStartIndex(&pAdapter->uplink);

  return ((qIndex < rxqStartIndex + pAdapter->uplink.queueInfo.maxRxQueues) &&
          (qIndex >= rxqStartIndex));
}

/*! \brief Check if given uplink TXQ is a valid Q.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  qIndex   TXQ index
**
** \return: VMK_TRUE    If TXQ is a valid Q.
** \return: VMK_FALSE   otherwise
*/
static inline vmk_Bool
sfvmk_isValidTXQ(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  vmk_uint32 txqStartIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);

  return ((qIndex >= txqStartIndex) &&
          (qIndex < txqStartIndex + sfvmk_getNumUplinkTxq(pAdapter)));
}

/*! \brief  The poll thread calls this callback function for polling packets.
**
** \param[in]  cookie    pointer to sfvmk_evq_t
** \param[in]  budget    Maximum number of packets to be
**                       processed in each invocation
**
** \return: VMK_TRUE if Completion is pending
**          VMK_FALSE if No pending completion
*/
static vmk_Bool
sfvmk_netPollCB(vmk_AddrCookie cookie, vmk_uint32 budget)
{
  vmk_Bool pendCompletion = VMK_FALSE;
  sfvmk_evq_t *pEvq = (sfvmk_evq_t *)cookie.ptr;

  SFVMK_DEBUG_FUNC_ENTRY(SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pEvq);
  pEvq->rxBudget = budget;
  pEvq->txBudget = SFVMK_NETPOLL_TX_BUDGET;

  if (sfvmk_evqPoll(pEvq, VMK_FALSE) == VMK_OK) {
    if ((pEvq->rxDone >= pEvq->rxBudget) ||
        (pEvq->txDone >= pEvq->txBudget))
      pendCompletion = VMK_TRUE;
  }

  SFVMK_DEBUG_FUNC_EXIT(SFVMK_DEBUG_UPLINK);
  return pendCompletion;
}

VMK_ReturnStatus
sfvmk_createNetPollForRSSQs(sfvmk_adapter_t *pAdapter, vmk_ServiceAcctID serviceID)
{
  vmk_uint32 qStartIndex;
  vmk_uint32 qEndIndex;
  vmk_uint32 qIndex;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pAdapter);

  qStartIndex = sfvmk_getRSSQStartIndex(pAdapter);
  qEndIndex = qStartIndex + pAdapter->numRSSQs;

  VMK_ASSERT_NOT_NULL(pAdapter->ppEvq);

  /* Create NetQueue for RSS Queues */
  for(qIndex = qStartIndex; qIndex < qEndIndex; qIndex++) {
    vmk_NetPollProperties pollProp;

    VMK_ASSERT_NOT_NULL(pAdapter->ppEvq[qIndex]);

    /* Poll properties for creating netPoll */
    pollProp.poll = sfvmk_netPollCB;
    pollProp.priv.ptr = pAdapter->ppEvq[qIndex];
    pollProp.deliveryCallback = NULL;
    pollProp.features = VMK_NETPOLL_NONE;

    status = vmk_NetPollCreate(&pollProp, serviceID, vmk_ModuleCurrentID,
                               &pAdapter->ppEvq[qIndex]->netPoll);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_NetPollCreate(%u) failed status: %s",
                          qIndex, vmk_StatusToString(status));
      goto failed;
    }
  }

  status = VMK_OK;
  goto done;

failed:
  for (qIndex--; qIndex >= qStartIndex; qIndex--)
    vmk_NetPollDestroy(pAdapter->ppEvq[qIndex]->netPoll);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

void
sfvmk_destroyNetPollForRSSQs(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 qStartIndex;
  vmk_uint32 qEndIndex;
  vmk_uint32 qIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pAdapter);

  qStartIndex = sfvmk_getRSSQStartIndex(pAdapter);
  qEndIndex = qStartIndex + pAdapter->numRSSQs;

  VMK_ASSERT_NOT_NULL(pAdapter->ppEvq);

  for(qIndex = qStartIndex; qIndex < qEndIndex; qIndex++) {
    VMK_ASSERT_NOT_NULL(pAdapter->ppEvq[qIndex]);
    vmk_NetPollDestroy(pAdapter->ppEvq[qIndex]->netPoll);
  }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

/*! \brief  Create uplink RXQ
**
** \param[in]   pAdapter pointer to sfvmk_adapter_t
** \param[out]  pQid     ptr to uplink Q ID
** \param[out]  pNetPoll ptr to netpoll registered with this Q
** \param[in]   feat     uplink queue feature
**
** \return:  VMK_BAD_PARAM   Input arguments are not valid
** \return:  VMK_OK          Able to create uplink queue
*/
static VMK_ReturnStatus
sfvmk_createUplinkRxq(sfvmk_adapter_t *pAdapter,
                      vmk_UplinkQueueID *pQid,
                      vmk_NetPoll *pNetPoll,
                      vmk_UplinkQueueFeature feat)
{
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_uint32 queueIndex;
  vmk_uint32 flags;
  vmk_uint32 queueStartIndex;
  vmk_uint32 queueEndIndex;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  pQueueInfo = &pAdapter->uplink.queueInfo;

  queueStartIndex = sfvmk_getUplinkRxqStartIndex(&pAdapter->uplink);
  queueEndIndex = queueStartIndex + sfvmk_getNumUplinkRxq(pAdapter) - 1;

  pQueueData = &pQueueInfo->queueData[0];

  if (sfvmk_getNumUplinkQueues(pAdapter) < queueEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", queueEndIndex);
    status = VMK_FAILURE;
    goto done;
  }

  /* Check if queue is free */
  for (queueIndex = queueStartIndex; queueIndex <= queueEndIndex; queueIndex++)
    if (sfvmk_isQueueFree(&pAdapter->uplink, queueIndex))
      break;

  if (queueIndex > queueEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "All Qs are in use");
    status = VMK_FAILURE;
    goto done;
  }

  if (feat & (VMK_UPLINK_QUEUE_FEAT_RSS |
              VMK_UPLINK_QUEUE_FEAT_RSS_DYN)) {

    if (!sfvmk_isRSSEnable(pAdapter)) {
      SFVMK_ADAPTER_ERROR(pAdapter, "RSSq allocation requested but "
                          "RSS is not enabled");
      status = VMK_FAILURE;
      goto done;
    }

    vmk_uint32 hwQueueIndex = sfvmk_getRSSQStartIndex(pAdapter);

    sfvmk_MutexLock(pAdapter->lock);
    efx_mac_filter_default_rxq_clear(pAdapter->pNic);

    VMK_ASSERT_NOT_NULL(pAdapter->ppRxq);
    VMK_ASSERT_NOT_NULL(pAdapter->ppRxq[queueIndex]);

    status = efx_mac_filter_default_rxq_set(pAdapter->pNic,
                                            pAdapter->ppRxq[hwQueueIndex]->pCommonRxq,
                                            VMK_TRUE);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_filter_default_rxq_set failed status: %s",
                          vmk_StatusToString(status));
      sfvmk_MutexUnlock(pAdapter->lock);
      goto done;
    }
    sfvmk_MutexUnlock(pAdapter->lock);

    pAdapter->uplink.rssUplinkQueue = queueIndex;
  }

  /* Make uplink queue */
  flags = VMK_UPLINK_QUEUE_FLAG_IN_USE;
  vmk_UplinkQueueMkRxQueueID(pQid, queueIndex, queueIndex);
  if (pNetPoll != NULL)
    *pNetPoll = pQueueData[queueIndex].poll;

  if (sfvmk_isDefaultUplinkRxq(&pAdapter->uplink, queueIndex))
    flags |= VMK_UPLINK_QUEUE_FLAG_DEFAULT;

  /* Setup flags and qid for the allocated queue */
  sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);
  pQueueData[queueIndex].flags &= ~(VMK_UPLINK_QUEUE_FLAG_DEFAULT |
                               VMK_UPLINK_QUEUE_FLAG_IN_USE);
  pQueueData[queueIndex].flags |= flags;
  pQueueData[queueIndex].qid = *pQid;
  sfvmk_sharedAreaEndWrite(&pAdapter->uplink);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Create uplink TXQ
**
** \param[in]   pAdapter pointer to sfvmk_adapter_t
** \param[out]  pQid     ptr to uplink Q ID
**
** \return:  VMK_BAD_PARAM   Input arguments are not valid
** \return:  VMK_OK          Able to create uplink queue
*/
static VMK_ReturnStatus
sfvmk_createUplinkTxq(sfvmk_adapter_t *pAdapter,
                      vmk_UplinkQueueID *pQid)
{
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_uint32 queueIndex;
  vmk_uint32 flags;
  vmk_uint32 queueStartIndex;
  vmk_uint32 queueEndIndex;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  pQueueInfo = &pAdapter->uplink.queueInfo;

  queueStartIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);
  queueEndIndex = queueStartIndex + pAdapter->uplink.queueInfo.maxTxQueues - 1;

  pQueueData = &pQueueInfo->queueData[0];

  if (sfvmk_getNumUplinkQueues(pAdapter) < queueEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", queueEndIndex);
    status = VMK_FAILURE;
    goto done;
  }

  /* Check if queue is free */
  for (queueIndex = queueStartIndex; queueIndex <= queueEndIndex; queueIndex++)
    if (sfvmk_isQueueFree(&pAdapter->uplink, queueIndex))
      break;

  if (queueIndex > queueEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "All Qs are in use");
    status = VMK_FAILURE;
    goto done;
  }

  /* Make uplink queue */
  flags = VMK_UPLINK_QUEUE_FLAG_IN_USE;
  vmk_UplinkQueueMkTxQueueID(pQid, queueIndex, queueIndex);

  if (sfvmk_isDefaultUplinkTxq(&pAdapter->uplink, queueIndex))
    flags |= VMK_UPLINK_QUEUE_FLAG_DEFAULT;

  /* Setup flags and qid for the allocated queue */
  sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);
  pQueueData[queueIndex].flags &= ~(VMK_UPLINK_QUEUE_FLAG_DEFAULT |
                               VMK_UPLINK_QUEUE_FLAG_IN_USE);
  pQueueData[queueIndex].flags |= flags;
  pQueueData[queueIndex].qid = *pQid;
  sfvmk_sharedAreaEndWrite(&pAdapter->uplink);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Update vmk_UplinkSharedQueueData for all uplink RXQs
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  serviceID   service to which work has to be charged
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_rxqDataInit(sfvmk_adapter_t *pAdapter, vmk_ServiceAcctID serviceID)
{
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_NetPollProperties pollProp = {0};
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_evq_t *pEvq = NULL;
  vmk_uint32 queueIndex;
  vmk_uint32 queueStartIndex;
  vmk_uint32 queueEndIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Populate qInfo information */
  pQueueInfo = &pAdapter->uplink.queueInfo;

  queueStartIndex = sfvmk_getUplinkRxqStartIndex(&pAdapter->uplink);
  queueEndIndex = queueStartIndex + sfvmk_getNumUplinkRxq(pAdapter) - 1;

  if (sfvmk_getNumUplinkQueues(pAdapter) < queueEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", queueEndIndex);
    status = VMK_FAILURE;
    goto done;
  }

  for (queueIndex = queueStartIndex; queueIndex <= queueEndIndex; queueIndex++) {
    pQueueData = &pQueueInfo->queueData[queueIndex];
    if (pQueueData == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL shared queue data ptr");
      status = VMK_FAILURE;
      goto failed_valid_queue_data;
    }

    pQueueData->type =  VMK_UPLINK_QUEUE_TYPE_RX;
    pQueueData->state = VMK_UPLINK_QUEUE_STATE_STOPPED;
    pQueueData->flags = VMK_UPLINK_QUEUE_FLAG_UNUSED;
    pQueueData->dmaEngine = pAdapter->dmaEngine;
    pQueueData->activeFilters = 0;
    pQueueData->activeFeatures = VMK_UPLINK_QUEUE_FEAT_NONE;

    if (sfvmk_isDefaultUplinkRxq(&pAdapter->uplink, queueIndex)) {
      pQueueData->maxFilters = SFVMK_MAX_FILTER;
      pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_NONE;
    } else {
      pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_PAIR |
                                      VMK_UPLINK_QUEUE_FEAT_DYNAMIC;

      if (sfvmk_isRSSEnable(pAdapter))
        pQueueData->supportedFeatures |= VMK_UPLINK_QUEUE_FEAT_RSS_DYN;

      pQueueData->maxFilters = SFVMK_MAX_FILTER / pQueueInfo->maxRxQueues;
    }

    pEvq = pAdapter->ppEvq[queueIndex];
    if (pEvq == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL event queue ptr");
      status = VMK_BAD_PARAM;
      goto done;
    }

    /* Poll properties for creating netpoll*/
    pollProp.poll = sfvmk_netPollCB;
    pollProp.features = VMK_NETPOLL_NONE;
    pollProp.priv.ptr = pEvq;
    pollProp.deliveryCallback = NULL;

    status = vmk_NetPollCreate(&pollProp, serviceID, vmk_ModuleCurrentID,
                               &pEvq->netPoll);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_NetPollCreate(Q[%u]) failed status: %s",
                          queueIndex, vmk_StatusToString(status));
      goto failed_net_poll_create;
    }
    pQueueData->poll = pEvq->netPoll;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "Successful initialization of uplink RXQ %u", queueIndex);

  status = VMK_OK;
  goto done;

failed_net_poll_create:
failed_valid_queue_data:
  while(queueIndex > queueStartIndex) {
    queueIndex--;
    vmk_NetPollDestroy(pAdapter->ppEvq[queueIndex]->netPoll);
    pAdapter->ppEvq[queueIndex]->netPoll = NULL;
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Update vmk_UplinkSharedQueueData for all uplink TXQs
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_txqDataInit(sfvmk_adapter_t *pAdapter)
{
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 queueIndex;
  vmk_uint32 queueStartIndex;
  vmk_uint32 queueEndIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Populate qInfo information */
  pQueueInfo = &pAdapter->uplink.queueInfo;

  queueStartIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);
  queueEndIndex = queueStartIndex + sfvmk_getNumUplinkTxq(pAdapter) - 1;

  if (sfvmk_getNumUplinkQueues(pAdapter) < queueEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", queueEndIndex);
    status = VMK_FAILURE;
    goto done;
  }

  for (queueIndex = queueStartIndex; queueIndex <= queueEndIndex; queueIndex++) {
    pQueueData = &pQueueInfo->queueData[queueIndex];
    if (pQueueData == NULL) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NULL shared queue data ptr");
      status = VMK_FAILURE;
      goto done;
    }

    pQueueData->type =  VMK_UPLINK_QUEUE_TYPE_TX;
    pQueueData->state = VMK_UPLINK_QUEUE_STATE_STOPPED;
    pQueueData->flags = VMK_UPLINK_QUEUE_FLAG_UNUSED;
    pQueueData->dmaEngine = pAdapter->dmaEngine;
    pQueueData->activeFeatures = VMK_UPLINK_QUEUE_FEAT_NONE;
    pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_PAIR;

    /* Check if it is a default queue */
    if (sfvmk_isDefaultUplinkTxq(&pAdapter->uplink, queueIndex)) {
      pQueueData->supportedFeatures = VMK_UPLINK_QUEUE_FEAT_NONE;
    }

  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "Successful initialization of uplink TXQ %u", queueIndex);
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Deallocate resource acquired by all uplink RXQs
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t
**
** \return: void
**
*/
static void
sfvmk_rxqDataFini(sfvmk_adapter_t *pAdapter)
{
  vmk_uint32 qIndex;
  vmk_uint32 qStartIndex;
  vmk_uint32 qEndIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  qStartIndex = sfvmk_getUplinkRxqStartIndex(&pAdapter->uplink);
  qEndIndex = qStartIndex + sfvmk_getNumUplinkRxq(pAdapter);

  if (pAdapter->numEvqsAllocated < qEndIndex) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue index %u", qEndIndex);
    goto done;
  }

  for (qIndex = qStartIndex; qIndex < qEndIndex; qIndex++) {
    if (pAdapter->ppEvq[qIndex] != NULL) {
      if (pAdapter->ppEvq[qIndex]->netPoll != NULL)
        vmk_NetPollDestroy(pAdapter->ppEvq[qIndex]->netPoll);
    }
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

/*! \brief Initialize shared queue info which will be used by uplink device.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_sharedQueueInfoInit(sfvmk_adapter_t *pAdapter)
{
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_ServiceAcctID serviceID;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 numSharedQueue;
  vmk_uint32 queueDataArraySize;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Populate shared queue information */
  pQueueInfo = &pAdapter->uplink.queueInfo;
  pQueueInfo->supportedQueueTypes = VMK_UPLINK_QUEUE_TYPE_TX |
                                    VMK_UPLINK_QUEUE_TYPE_RX;

  /* Populate shared filter class with MAC and VLANMAC filter */
  pQueueInfo->supportedRxQueueFilterClasses =
                              VMK_UPLINK_QUEUE_FILTER_CLASS_MAC_ONLY |
                              VMK_UPLINK_QUEUE_FILTER_CLASS_VLANMAC;

  if (pAdapter->isTunnelEncapSupported & SFVMK_VXLAN_OFFLOAD) {
    pQueueInfo->supportedRxQueueFilterClasses |=
                              VMK_UPLINK_QUEUE_FILTER_CLASS_VXLAN;
  }

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
  if (pAdapter->isTunnelEncapSupported & SFVMK_GENEVE_OFFLOAD) {
    pQueueInfo->supportedRxQueueFilterClasses |=
                              VMK_UPLINK_QUEUE_FILTER_CLASS_GENEVE;
  }
#endif

  /* Update max TX and RX queue
   * For base RSSQ one uplink Q is added
   * HWQs are sum of netQs + one uplink Q for RSS and num of RSSQs
   */
  if (sfvmk_isRSSEnable(pAdapter)) {
    pQueueInfo->maxTxQueues =  pAdapter->numNetQs + 1;
    pQueueInfo->maxRxQueues =  pAdapter->numNetQs + 1;
  } else {
    pQueueInfo->maxTxQueues =  pAdapter->numNetQs;
    pQueueInfo->maxRxQueues =  pAdapter->numNetQs;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "maxTxQs: %d maxRxQs: %d ", pQueueInfo->maxTxQueues,
                      pQueueInfo->maxRxQueues);

  numSharedQueue = pQueueInfo->maxTxQueues + pQueueInfo->maxRxQueues;
  queueDataArraySize = numSharedQueue * sizeof(vmk_UplinkSharedQueueData);

  pQueueInfo->queueData = vmk_HeapAlloc(sfvmk_modInfo.heapID, queueDataArraySize);
  if (pQueueInfo->queueData == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HeapAlloc failed");
    status = VMK_NO_MEMORY;
    goto done;
  }
  vmk_Memset(pQueueInfo->queueData, 0, queueDataArraySize);

  pQueueInfo->activeRxQueues = 0;
  pQueueInfo->activeTxQueues = 0;

  pQueueInfo->activeQueues = vmk_BitVectorAlloc(sfvmk_modInfo.heapID,
						numSharedQueue);
  if (!pQueueInfo->activeQueues) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_BitVectorAlloc failed");
    goto failed_bitvector_alloc;
  }

  vmk_BitVectorZap(pQueueInfo->activeQueues);

  status = vmk_ServiceGetID(VMK_SERVICE_ACCT_NAME_NET, &serviceID);
  if (status != VMK_OK) {
    goto failed_get_service_id;
  }

  /* Update shared RXQData */
  status = sfvmk_rxqDataInit(pAdapter, serviceID);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_rxqDataInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_rxqdata_init;
  }

  if (sfvmk_isRSSEnable(pAdapter)) {
    status = sfvmk_createNetPollForRSSQs(pAdapter, serviceID);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createNetPollForRSSQs failed status: %s",
                          vmk_StatusToString(status));
      sfvmk_disableRSS(pAdapter);
      goto failed_create_netpoll_rss;
    }
  }

  /* Update shared TXQData */
  status = sfvmk_txqDataInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_txqDataInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_txqdata_init;
  }

  /* Create default RX uplink queue */
  status = sfvmk_createUplinkRxq(pAdapter, &pQueueInfo->defaultRxQueueID, NULL, 0);
  if (pQueueInfo->queueData == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createUplinkRxq failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_uplink_rxq;
  }

  /* Create default TX uplink queue */
  status = sfvmk_createUplinkTxq(pAdapter, &pQueueInfo->defaultTxQueueID);
  if (pQueueInfo->queueData == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createUplinkTxq failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_uplink_txq;
  }

  goto done;

failed_create_uplink_txq:
failed_create_uplink_rxq:
failed_txqdata_init:
  if (sfvmk_isRSSEnable(pAdapter))
    sfvmk_destroyNetPollForRSSQs(pAdapter);

failed_create_netpoll_rss:
  sfvmk_rxqDataFini(pAdapter);

failed_rxqdata_init:
failed_get_service_id:
  vmk_BitVectorFree(sfvmk_modInfo.heapID, pQueueInfo->activeQueues);
failed_bitvector_alloc:
  vmk_HeapFree(sfvmk_modInfo.heapID, pQueueInfo->queueData);
  pQueueInfo->queueData = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Deallocate resource acquired by the shared queue
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
**
*/
static void
sfvmk_sharedQueueInfoFini(sfvmk_adapter_t *pAdapter)
{
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  /* Deallocate queueData for RXQ */
  sfvmk_rxqDataFini(pAdapter);

  if (sfvmk_isRSSEnable(pAdapter))
    sfvmk_destroyNetPollForRSSQs(pAdapter);

  if (pAdapter->uplink.queueInfo.activeQueues) {
    vmk_BitVectorFree(sfvmk_modInfo.heapID,
                      pAdapter->uplink.queueInfo.activeQueues);
  }

  vmk_HeapFree(sfvmk_modInfo.heapID, pAdapter->uplink.queueInfo.queueData);
  pAdapter->uplink.queueInfo.queueData = NULL;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

/*! \brief  allocate the resource required for uplink device and initalize all
**          required information.
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_uplinkDataInit(sfvmk_adapter_t * pAdapter)
{
  vmk_UplinkSharedData *pSharedData = NULL;
  vmk_UplinkRegData *pRegData = NULL;
  VMK_ReturnStatus status = VMK_BAD_PARAM;
  const efx_nic_cfg_t *pNicCfg;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pAdapter->pNic == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL NIC ptr");
    status = VMK_FAILURE;
    goto done;
  }

  /* Create lock to serialize multiple writers's access to protected sharedData area */
  status = sfvmk_createLock(pAdapter, "uplinkLock", SFVMK_SPINLOCK_RANK_UPLINK_LOCK,
                            &pAdapter->uplink.shareDataLock);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createLock failed status: %s",
                        vmk_StatusToString(status));
    goto failed_create_lock;
  }

  sfvmk_updateSupportedCap(pAdapter, EFX_PHY_CAP_PERM);

  pAdapter->uplink.rssUplinkQueue = pAdapter->defRxqIndex;

  /* Initialize shared data area */
  pSharedData = &pAdapter->uplink.sharedData;
  vmk_VersionedAtomicInit(&pSharedData->lock);

  pSharedData->supportedModes = pAdapter->uplink.supportedModes;
  pSharedData->supportedModesArraySz = pAdapter->uplink.numSupportedModes;

  pSharedData->link.state = VMK_LINK_STATE_DOWN;
  pSharedData->link.speed = 0;
  pSharedData->link.duplex = VMK_LINK_DUPLEX_FULL;

  pSharedData->flags = 0;
  pSharedData->state = VMK_UPLINK_STATE_ENABLED;

  pSharedData->mtu = SFVMK_DEFAULT_MTU;

  pSharedData->queueInfo = &pAdapter->uplink.queueInfo;

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_cfg_get failed");
    status = VMK_FAILURE;
    goto failed_nic_cfg_get;
  }

  vmk_Memcpy(pSharedData->macAddr, pNicCfg->enc_mac_addr, VMK_ETH_ADDR_LENGTH);
  vmk_Memcpy(pSharedData->hwMacAddr, pNicCfg->enc_mac_addr,VMK_ETH_ADDR_LENGTH);

  status = sfvmk_sharedQueueInfoInit(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_sharedQueueInfoInit failed status: %s",
                        vmk_StatusToString(status));
    goto failed_init_shared_qdata;
  }

  pRegData = &pAdapter->uplink.regData;
  pRegData->ops = sfvmkUplinkOps;
  pRegData->moduleID = vmk_ModuleCurrentID;
  pRegData->apiRevision = VMKAPI_REVISION;
  pRegData->sharedData = pSharedData;
  pRegData->driverData.ptr = pAdapter;

  status = VMK_OK;
  goto done;

failed_init_shared_qdata:
failed_nic_cfg_get:
  sfvmk_destroyLock(pAdapter->uplink.shareDataLock);

failed_create_lock:
done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Dealloocate resources acquired by uplink device
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
**
*/
void
sfvmk_uplinkDataFini(sfvmk_adapter_t * pAdapter)
{

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  sfvmk_sharedQueueInfoFini(pAdapter);
  sfvmk_destroyLock(pAdapter->uplink.shareDataLock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

/*! \brief  callback to allocate netqueue queue
**
** \param[in]  cookie   pointer to vmk_AddrCookie/sfvmk_adapter_t.
** \param[in]  qType    Queue Type (Rx/Tx).
** \param[out] pQID     pointer to newly created uplink queue.
** \param[out] pNetPoll pointer to net poll on top of the allocated queue if any.
**
** \return: VMK_OK on success or error code otherwise.
*/
static VMK_ReturnStatus
sfvmk_allocQueue(vmk_AddrCookie cookie, vmk_UplinkQueueType qType,
                 vmk_UplinkQueueID *pQID, vmk_NetPoll *pNetPoll)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if ((pQID == NULL) || (pAdapter == NULL)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid param(s)");
    goto done;
  }

  switch(qType) {
    case VMK_UPLINK_QUEUE_TYPE_RX:
      if (pNetPoll == NULL) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Null net poll ptr");
        goto done;
      }
      status = sfvmk_createUplinkRxq(pAdapter, pQID, pNetPoll, 0);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createUplinkRxq failed status: %s",
                            vmk_StatusToString(status));
      }
      break;
    case VMK_UPLINK_QUEUE_TYPE_TX:
      status = sfvmk_createUplinkTxq(pAdapter, pQID);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createUplinkTxq failed status: %s",
                            vmk_StatusToString(status));
      }
      break;
    default:
     status = VMK_BAD_PARAM;
     SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue type");
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "Allocated uplinkQ(%u)", vmk_UplinkQueueIDVal(*pQID));

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  callback to allocate netqueue queue with attribute
**
** \param[in]  cookie   pointer to vmk_AddrCookie/sfvmk_adapter_t.
** \param[in]  qType    Queue Type (Rx/Tx).
** \param[in]  numAttr  Number of attributes.
** \param[in]  pAttr    Queue attributes.
** \param[out] pQID     pointer to newly created uplink queue.
** \param[out] pNetPoll pointer to net poll on top of the allocated queue if any.
**
** \return: VMK_OK on success or error code otherwise.
*/
static VMK_ReturnStatus
sfvmk_allocQueueWithAttr(vmk_AddrCookie cookie, vmk_UplinkQueueType qType,
                         vmk_uint16 numAttr, vmk_UplinkQueueAttr *pAttr,
                         vmk_UplinkQueueID *pQID, vmk_NetPoll *pNetPoll)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_UplinkQueueFeature feat = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if ((pQID == NULL) || (pAdapter == NULL)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid param(s)");
    goto done;
  }

  if (pAttr) {
    switch (pAttr->type) {
      case VMK_UPLINK_QUEUE_ATTR_FEAT:
        feat = pAttr->args.features;
        if (pNetPoll == NULL) {
          SFVMK_ADAPTER_ERROR(pAdapter, "Null net poll ptr");
          goto done;
        }

        if ((feat & (VMK_UPLINK_QUEUE_FEAT_RSS |
                     VMK_UPLINK_QUEUE_FEAT_RSS_DYN |
                     VMK_UPLINK_QUEUE_FEAT_PAIR)) || (!feat)) {
          if (vmk_UplinkQueueIDType(*pQID) == VMK_UPLINK_QUEUE_TYPE_TX) {
            SFVMK_ADAPTER_ERROR(pAdapter, "RSS valid only for RXQ");
            break;
          }
          status = sfvmk_createUplinkRxq(pAdapter, pQID, pNetPoll, feat);
          if (status != VMK_OK) {
            SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_createUplinkRxq failed status: %s",
                                vmk_StatusToString(status));
          }
        }
        break;
      default:
        status = VMK_BAD_PARAM;
        SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue attribute");
    }
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                        "Allocated uplinkQ(%u) with attr", vmk_UplinkQueueIDVal(*pQID));
  } else {
    status = sfvmk_allocQueue(cookie, qType, pQID, pNetPoll);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_allocQueue failed status: %s",
                          vmk_StatusToString(status));
    }
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  callback to free queue
**
** \param[in]  cookie   pointer to vmk_AddrCookie/sfvmk_adapter_t.
** \param[in]  qid      ID of already created queue.
**
** \return: VMK_OK on success or error code otherwise
*/
static VMK_ReturnStatus
sfvmk_freeQueue(vmk_AddrCookie cookie,
                vmk_UplinkQueueID qid)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_UplinkQueueType qType = vmk_UplinkQueueIDType(qid);
  vmk_uint32 qIndex = vmk_UplinkQueueIDVal(qid);
  vmk_Bool validQ = VMK_FALSE;
  sfvmk_uplink_t *pUplink;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  pUplink = &pAdapter->uplink;

  if ( qType == VMK_UPLINK_QUEUE_TYPE_RX)
    validQ = sfvmk_isValidRXQ(pAdapter, qIndex);
  else if (qType == VMK_UPLINK_QUEUE_TYPE_TX)
    validQ = sfvmk_isValidTXQ(pAdapter, qIndex);

  if (validQ) {
    sfvmk_sharedAreaBeginWrite(pUplink);
    pUplink->queueInfo.queueData[qIndex].flags &= ~(VMK_UPLINK_QUEUE_FLAG_IN_USE |
                                                    VMK_UPLINK_QUEUE_FLAG_DEFAULT);
    sfvmk_sharedAreaEndWrite(pUplink);
    status = VMK_OK;
  } else {
    status = VMK_BAD_PARAM;
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue(%u)", qIndex);
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  return status;
}

/*! \brief  quiesce uplink RXQ
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t.
** \param[in]  qIndex     RXQ index.
**
** \return: VMK_OK on success or error code otherwise.
*/
static VMK_ReturnStatus
sfvmk_quiesceUplinkRxq(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_uplink_t *pUplink;
  sfvmk_rxq_t *pRxq = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  VMK_ASSERT_NOT_NULL(pAdapter);

  pUplink = &pAdapter->uplink;

  if (!sfvmk_isValidRXQ(pAdapter, qIndex)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid RXQ(%u)", qIndex);
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (!sfvmk_isUplinkQStarted(pUplink, qIndex)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RXQ(%u) is not yet started", qIndex);
    status = VMK_FAILURE;
    goto done;
  }

  if (sfvmk_isQueueFree(pUplink, qIndex)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RXQ(%u) is not in use", qIndex);
    status = VMK_FAILURE;
    goto done;
  }

  sfvmk_MutexLock(pAdapter->lock);
  if ((qIndex == pAdapter->uplink.rssUplinkQueue) &&
      (pAdapter->rssInit) &&
      (pAdapter->state == SFVMK_ADAPTER_STATE_STARTED)) {

    efx_mac_filter_default_rxq_clear(pAdapter->pNic);

    VMK_ASSERT_NOT_NULL(pAdapter->ppRxq);

    pRxq = pAdapter->ppRxq[pAdapter->defRxqIndex];
    VMK_ASSERT_NOT_NULL(pRxq);
    status = efx_mac_filter_default_rxq_set(pAdapter->pNic,
                                            pRxq->pCommonRxq,
                                            VMK_FALSE);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_filter_default_rxq_set failed status: %s",
                          vmk_StatusToString(status));
    }
    pAdapter->rssInit = VMK_FALSE;
    /* Uplink RSS queue will never be on default RxQ Index */
    pAdapter->uplink.rssUplinkQueue = pAdapter->defRxqIndex;
  }
  sfvmk_MutexUnlock(pAdapter->lock);

  sfvmk_sharedAreaBeginWrite(pUplink);
  pUplink->queueInfo.queueData[qIndex].state = VMK_UPLINK_QUEUE_STATE_STOPPED;
  pUplink->queueInfo.activeRxQueues--;
  vmk_BitVectorClear(pUplink->queueInfo.activeQueues, qIndex);
  sfvmk_sharedAreaEndWrite(pUplink);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  return status;
}

/*! \brief  quiesce uplink TXQ
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t.
** \param[in]  qIndex     index in to ppTxq array.
**
** \return: VMK_OK on success or error code otherwise.
*/
static VMK_ReturnStatus
sfvmk_quiesceUplinkTxq(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_uplink_t *pUplink;
  sfvmk_txq_t *pTxq = NULL;
  vmk_uint32 txqStartIndex;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  VMK_ASSERT_NOT_NULL(pAdapter);

  pUplink = &pAdapter->uplink;

  if (!sfvmk_isValidTXQ(pAdapter, qIndex)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid TXQ(%u)", qIndex);
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (sfvmk_isQueueFree(pUplink, qIndex)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "TXQ(%u) is not in use", qIndex);
    status = VMK_FAILURE;
    goto done;
  }

  if (!sfvmk_isUplinkQStarted(pUplink, qIndex)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "TXQ(%u) is already stopped", qIndex);
    status = VMK_OK;
    goto done;
  }

  txqStartIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);
  pTxq = pAdapter->ppTxq[qIndex - txqStartIndex];

  vmk_SpinlockLock(pTxq->lock);
  sfvmk_updateQueueStatus(pAdapter, VMK_UPLINK_QUEUE_STATE_STOPPED, pTxq->index);
  vmk_SpinlockUnlock(pTxq->lock);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  return status;
}

/*! \brief  callback to quiesce uplink queue
**
** \param[in]  cookie   pointer to vmk_AddrCookie/sfvmk_adapter_t.
** \param[in]  qid      ID of already created queue.
**
** \return: VMK_OK [success] error code [failure]
*/
static VMK_ReturnStatus
sfvmk_quiesceQueue(vmk_AddrCookie cookie,
                   vmk_UplinkQueueID qid)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_UplinkQueueType qType = vmk_UplinkQueueIDType(qid);
  vmk_uint32 qIndex = vmk_UplinkQueueIDVal(qid);

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  switch (qType) {
    case VMK_UPLINK_QUEUE_TYPE_RX:
      status = sfvmk_quiesceUplinkRxq(pAdapter, qIndex);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_quiesceUplinkRxq(%u) failed status: %s",
                            qIndex, vmk_StatusToString(status));
      }
      break;
    case VMK_UPLINK_QUEUE_TYPE_TX:
      status = sfvmk_quiesceUplinkTxq(pAdapter, qIndex);
      if ((status != VMK_OK)  && (status != VMK_EALREADY)){
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_quiesceUplinkTxq(%u) failed status: %s",
                            qIndex, vmk_StatusToString(status));
      }
      break;
    default:
      status = VMK_BAD_PARAM;
      SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue type");
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  return status;
}

/*! \brief  start uplink RXQ
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t.
** \param[in]  qIndex     RXQ index.
**
** \return: VMK_OK on success or error code otherwise.
*/
static VMK_ReturnStatus
sfvmk_startUplinkRxq(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_uplink_t *pUplink;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  VMK_ASSERT_NOT_NULL(pAdapter);

  pUplink = &pAdapter->uplink;

  if (!sfvmk_isValidRXQ(pAdapter, qIndex)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid RXQ(%u)", qIndex);
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (sfvmk_isUplinkQStarted(pUplink, qIndex)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RXQ(%u) already started", qIndex);
    status = VMK_FAILURE;
    goto done;
  }

  if (sfvmk_isQueueFree(pUplink, qIndex)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RXQ(%u) is not in use", qIndex);
    status = VMK_FAILURE;
    goto done;
  }

  sfvmk_sharedAreaBeginWrite(pUplink);
  pUplink->queueInfo.queueData[qIndex].state = VMK_UPLINK_QUEUE_STATE_STARTED;
  pUplink->queueInfo.activeRxQueues++;
  vmk_BitVectorSet(pUplink->queueInfo.activeQueues, qIndex);
  sfvmk_sharedAreaEndWrite(pUplink);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  return status;
}

/*! \brief  start uplink TXQ
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t.
** \param[in]  qIndex     index in to ppTxq array.
**
** \return: VMK_OK on success or error code otherwise.
*/
static VMK_ReturnStatus
sfvmk_startUplinkTxq(sfvmk_adapter_t *pAdapter, vmk_uint32 qIndex)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_uplink_t *pUplink;
  vmk_uint32 txqStartIndex;
  sfvmk_txq_t *pTxq = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  VMK_ASSERT_NOT_NULL(pAdapter);

  pUplink = &pAdapter->uplink;

  if (!sfvmk_isValidTXQ(pAdapter, qIndex)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid TXQ(%u)", qIndex);
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (sfvmk_isQueueFree(pUplink, qIndex)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "TXQ(%u) is not in use", qIndex);
    status = VMK_FAILURE;
    goto done;
  }

  if (sfvmk_isUplinkQStarted(pUplink, qIndex)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "TXQ(%u) already started", qIndex);
    status = VMK_OK;
    goto done;
  }

  txqStartIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);
  pTxq = pAdapter->ppTxq[qIndex - txqStartIndex];

  vmk_SpinlockLock(pTxq->lock);
  sfvmk_updateQueueStatus(pAdapter, VMK_UPLINK_QUEUE_STATE_STARTED, pTxq->index);
  vmk_SpinlockUnlock(pTxq->lock);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  return status;
}

/*! \brief  callback to start uplink queue
**
** \param[in]  cookie   pointer to vmk_AddrCookie/sfvmk_adapter_t.
** \param[in]  qid      ID of already created queue.
**
** \return: VMK_OK on success or error code otherwise.
*/
static VMK_ReturnStatus
sfvmk_startQueue(vmk_AddrCookie cookie,
                 vmk_UplinkQueueID qid)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_NOT_SUPPORTED;
  vmk_UplinkQueueType qType = vmk_UplinkQueueIDType(qid);
  vmk_uint32 qIndex = vmk_UplinkQueueIDVal(qid);

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    goto done;
  }

  switch (qType) {
    case VMK_UPLINK_QUEUE_TYPE_RX:
      status = sfvmk_startUplinkRxq(pAdapter, qIndex);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_startUplinkRxq(%u) failed status: %s",
                            qIndex, vmk_StatusToString(status));
      }
      break;
    case VMK_UPLINK_QUEUE_TYPE_TX:
      status = sfvmk_startUplinkTxq(pAdapter, qIndex);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_startUplinkTxq(%u) failed status: %s",
                            qIndex, vmk_StatusToString(status));
      }
      break;
    default:
      status = VMK_BAD_PARAM;
      SFVMK_ADAPTER_ERROR(pAdapter, "Invalid queue type");
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK, "qIndex(%u)", qIndex);

  return status;
}

/*! \brief  callback to remove netqueue queue filter
**
** \param[in]  cookie   pointer to vmk_AddrCookie/sfvmk_adapter_t.
** \param[in]  qid      ID of already created queue.
** \param[in]  fid      ID of already created filter.
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_removeQueueFilter(vmk_AddrCookie cookie,
                         vmk_UplinkQueueID qid,
                         vmk_UplinkQueueFilterID fid)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  vmk_uint32 qidVal = vmk_UplinkQueueIDVal(qid);
  vmk_uint32 filterKey = vmk_UplinkQueueFilterIDVal(fid);
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_UplinkSharedQueueData *pQueueData;
  sfvmk_filterDBEntry_t *pFdbEntry;
  VMK_ReturnStatus status = VMK_OK;

  if (!pAdapter) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter pointer is NULL");
    status = VMK_FAILURE;
    goto end;
  }

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);
  sfvmk_MutexLock(pAdapter->lock);

  /* Nothing to do if adapter is down already and filters were all gone */
  if (pAdapter->state != SFVMK_ADAPTER_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter not started, Ignoring filterKey:%u", filterKey);
    goto done;
  }

  if (qidVal == 0) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                        "Queue ID value is 0, Ignoring filterKey:%u", filterKey);
    goto done;
  }

  pQueueInfo = &pAdapter->uplink.queueInfo;
  pQueueData = &pQueueInfo->queueData[0];

  if (pQueueData[qidVal].activeFilters == 0) {
    SFVMK_ADAPTER_ERROR(pAdapter, "qData[%u].activeFilters = %u (max %u), Ignoring",
                        qidVal, pQueueData[qidVal].activeFilters,
                        pQueueData[qidVal].maxFilters);
    goto done;
  }

  pFdbEntry = sfvmk_removeFilterRule(pAdapter, filterKey);
  if (!pFdbEntry) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Filter not found for filterKey: %u", filterKey);
    goto done;
  }

  status = vmk_HashKeyDelete(pAdapter->filterDBHashTable,
                             (vmk_HashKey)(vmk_uint64)filterKey,
                             (vmk_HashValue *)&pFdbEntry);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Hash key (%u) delete failed, status: %s",
                        filterKey, vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto done;
  }

  sfvmk_removeUplinkFilter(pAdapter, qidVal);
  sfvmk_freeFilterRule(pAdapter, pFdbEntry);

done:
  sfvmk_MutexUnlock(pAdapter->lock);
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
end:
  return status;
}

/*! \brief  callback to apply netqueue queue filter
**
** \param[in]  cookie     pointer to vmk_AddrCookie/sfvmk_adapter_t.
** \param[in]  qid        ID of already created queue.
** \param[in]  pFilter    New queue filter to be applied.
** \param[out] pFId       New Filter ID.
** \param[out] pPairHwQid Potential paired tx queue hardware index.
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_applyQueueFilter(vmk_AddrCookie cookie,
                        vmk_UplinkQueueID qid,
                        vmk_UplinkQueueFilter *pFilter,
                        vmk_UplinkQueueFilterID *pFId,
                        vmk_uint32 *pPairHwQid)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  sfvmk_filterDBEntry_t *pFdbEntry = NULL;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_uint32 qidVal = vmk_UplinkQueueIDVal(qid);
  vmk_uint32 hwQid = 0;
  vmk_uint32 filterKey;
  efx_filter_flags_t hwFilterFlag = 0;

  VMK_ReturnStatus status = VMK_OK;

  if (!pAdapter) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter pointer is NULL");
    status = VMK_FAILURE;
    goto end;
  }

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  sfvmk_MutexLock(pAdapter->lock);

  if (qidVal == 0) {
    /* Do not commit filters to default queue */
    goto done;
  }

  VMK_ASSERT(pAdapter->filterDBHashTable != VMK_INVALID_HASH_HANDLE);

  pQueueInfo = &pAdapter->uplink.queueInfo;
  pQueueData = &pQueueInfo->queueData[0];

  if (pQueueData[qidVal].activeFilters >= pQueueData[qidVal].maxFilters) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Reached max filter count for QID %u\n", qidVal);
    status = VMK_FAILURE;
    goto done;
  }

  pFdbEntry = sfvmk_allocFilterRule(pAdapter);
  if (!pFdbEntry) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Memory allocation for filter entry failed");
    status = VMK_FAILURE;
    goto done;
  }

  filterKey = sfvmk_generateFilterKey(pAdapter);

  /* Find corresponding hardware qid
   * if Uplink Q corresponds to RSS, get the base RSS HW Q
   * else it is one to one mapping
   * TX HW Q is always at same index as uplink q
   * Therefore for RSS, TX and RX hardware queues are at diff index
   * Note: when rss q is not allocated, rssUplinkQueue = 0,
   *       the code assumes that the filters will never be
   *       created on 0th uplink Q
   */
  if (qidVal == pAdapter->uplink.rssUplinkQueue) {
    hwQid = sfvmk_getRSSQStartIndex(pAdapter);
    hwFilterFlag = EFX_FILTER_FLAG_RX_RSS;
  } else {
    hwQid = qidVal;
  }

  status = sfvmk_prepareFilterRule(pAdapter, pFilter, pFdbEntry,
                                   filterKey, hwQid, hwFilterFlag);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Failed to prepare filter rule with error %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto free_filter_rule;
  }

  status = sfvmk_insertFilterRule(pAdapter, pFdbEntry);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Failed in Hw filter rule creation, %s\n",
                        vmk_StatusToString(status));
    goto free_filter_rule;
  }

  status = vmk_HashKeyInsert(pAdapter->filterDBHashTable,
                             (vmk_HashKey)(vmk_uint64)filterKey,
                             (vmk_HashValue) pFdbEntry);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Hash Key Insertion failed, %s\n",
                        vmk_StatusToString(status));
    goto free_filter_rule;
  }

  vmk_UplinkQueueMkFilterID(pFId, filterKey);
  sfvmk_addUplinkFilter(pAdapter, qidVal, pPairHwQid);
  goto done;

free_filter_rule:
  sfvmk_freeFilterRule(pAdapter, pFdbEntry);
done:
  sfvmk_MutexUnlock(pAdapter->lock);
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
end:
  return status;
}

/*! \brief  callback to get queue stats
**
** \param[in]  cookie   pointer to vmk_AddrCookie/sfvmk_adapter_t.
** \param[in]  qid      ID of already created queue.
** \param[out] pStats   Uplink queue stats
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_getQueueStats(vmk_AddrCookie cookie,
                    vmk_UplinkQueueID qid,
                    vmk_UplinkStats *pStats)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_NOT_SUPPORTED;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);
  /* This API is not invoked in ESXi 6.5 */
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  callback to toggle queue feature
**
** \param[in]  cookie   pointer to vmk_AddrCookie/sfvmk_adapter_t.
** \param[in]  qid      ID of already created queue.
** \param[in]  feature  Queue feature.
** \param[in]  setUnset Set or remove queue feature
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_toggleQueueFeature(vmk_AddrCookie cookie,
                         vmk_UplinkQueueID qid,
                         vmk_UplinkQueueFeature feature,
                         vmk_Bool setUnset)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_NOT_SUPPORTED;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);
  /* Not Supported */
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  callback to set netqueue queue priority
**
** \param[in]  cookie    pointer to vmk_AddrCookie/sfvmk_adapter_t.
** \param[in]  qid       ID of already created queue.
** \param[in]  priority  Queue priority
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_setQueueTxPriority(vmk_AddrCookie cookie,
                         vmk_UplinkQueueID qid,
                         vmk_UplinkQueuePriority priority)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_NOT_SUPPORTED;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);
  /* Not Supported */
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  callback to allocate netqueue queue
**
** \param[in]  cookie   pointer to vmk_AddrCookie/sfvmk_adapter_t.
** \param[in]  qid      ID of already created queue.
** \param[in]  pParams  Queue Coalesce parameters.
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_setQueueCoalesceParams(vmk_AddrCookie cookie,
                             vmk_UplinkQueueID qid,
                             vmk_UplinkCoalesceParams *pParams)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_NOT_SUPPORTED;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);
  /* Not Supported */
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief add uplink filter
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t
** \param[in]  qidVal     Queue ID value
** \param[out] pPairHwQid Tx Queue ID pair
**
** \return: void
**
*/
static void
sfvmk_addUplinkFilter(sfvmk_adapter_t *pAdapter,
                      vmk_uint32 qidVal,
                      vmk_uint32 *pPairHwQid)
{
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_uint32 txQueueStartIndex;

  if (!pAdapter) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter pointer is NULL");
    return;
  }

  if (qidVal == 0) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                        "Queue ID value is 0, no filter applied on default queue");
    return;
  }

  pQueueInfo = &pAdapter->uplink.queueInfo;
  pQueueData = &pQueueInfo->queueData[0];

  sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);
  txQueueStartIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);
  *pPairHwQid = qidVal + txQueueStartIndex;
  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "Rx queue ID %d paired with TX queue ID %d",
                       qidVal, *pPairHwQid);

  pQueueData[qidVal].activeFeatures |= VMK_UPLINK_QUEUE_FEAT_PAIR;
  pQueueData[qidVal].activeFilters++;

  pQueueData[txQueueStartIndex + qidVal].activeFeatures |= VMK_UPLINK_QUEUE_FEAT_PAIR;
  sfvmk_sharedAreaEndWrite(&pAdapter->uplink);
}

/*! \brief remove uplink filter
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
** \param[in]  qidVal   Uplink queue ID value
**
** \return: void
**
*/
void
sfvmk_removeUplinkFilter(sfvmk_adapter_t *pAdapter,
                         vmk_uint32 qidVal)
{
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_uint32 txQueueStartIndex;

  if (!pAdapter) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter pointer is NULL");
    return;
  }

  if (qidVal == 0) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                        "Queue ID value is 0, no filter applied on default queue");
    return;
  }

  pQueueInfo = &pAdapter->uplink.queueInfo;
  pQueueData = &pQueueInfo->queueData[0];

  sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);
  pQueueData[qidVal].activeFilters--;

  /* Clear _FEAT_PAIR for RX queue */
  pQueueData[qidVal].activeFeatures &= ~VMK_UPLINK_QUEUE_FEAT_PAIR;

  /* Clear _FEAT_PAIR for TX queue */
  txQueueStartIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);
  pQueueData[txQueueStartIndex + qidVal].activeFeatures &= ~VMK_UPLINK_QUEUE_FEAT_PAIR;
  sfvmk_sharedAreaEndWrite(&pAdapter->uplink);
}

/*! \brief Helper callback to reset NIC.
**
** \param[in]  cookie  pointer to sfvmk_adapter_t
**
** \return: None
**
*/
static void
sfvmk_uplinkResetHelper(vmk_AddrCookie cookie)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status;
  unsigned int attempt;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pAdapter);

  sfvmk_MutexLock(pAdapter->lock);

#ifdef SFVMK_SUPPORT_SRIOV
  /*
   * evbState is per adapter but it forms the basis for decision of
   * proxyAuthInit and proxyAuthFini as well. In case, primary has no
   * VFs enabled, evbState will remain UNINITIALIZED and hence will
   * not be set to STOPPING here if checked for STARTED. So, set evbState
   * to STOPPING unconditionally to take care of such case.
   */
  pAdapter->evbState = SFVMK_EVB_STATE_STOPPING;
#endif

  status = sfvmk_quiesceIO(pAdapter);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_quiesceIO failed with error %s",
                        vmk_StatusToString(status));
    goto end;
  }

  sfvmk_mcdiReset(pAdapter);

  status = efx_nic_reset(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_nic_reset failed with error %s",
                        vmk_StatusToString(status));
    goto end;
  }

  status = efx_nic_set_workaround_bug26807(pAdapter->pNic);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "efx_nic_set_workaround_bug26807 failed with error %s",
                        vmk_StatusToString(status));
    goto end;
  }

  for (attempt = 0; attempt < 3; ++attempt) {
    status = sfvmk_startIO(pAdapter);
    if (status == VMK_OK) {
      /* The reset could be scheduled after firmware update,
       * therefore update the new firmware version in adpater
       * shared data info */
      sfvmk_updateDrvInfo(pAdapter);
      goto end;
    }

    /* Sleep for 100 milliseconds */
    status = sfvmk_worldSleep(SFVMK_STARTIO_ON_MC_REBOOT_TIME_OUT_MSEC);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_WorldSleep failed status: %s",
                          vmk_StatusToString(status));
      /* World is dying */
      break;
    }
  }

  SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_startIO failed");

end:
  sfvmk_MutexUnlock(pAdapter->lock);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
}

/*! \brief Fuction to submit driver reset request.
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_scheduleReset(sfvmk_adapter_t *pAdapter)
{
  vmk_HelperRequestProps props;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter->state == SFVMK_ADAPTER_STATE_UNINITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter is not yet initilized");
    goto done;
  }
  vmk_HelperRequestPropsInit(&props);

  /* Create a request and submit */
  props.requestMayBlock = VMK_FALSE;
  props.tag = (vmk_AddrCookie)NULL;
  props.cancelFunc = NULL;
  props.worldToBill = VMK_INVALID_WORLD_ID;
  status = vmk_HelperSubmitRequest(pAdapter->helper,
                                   sfvmk_uplinkResetHelper,
                                   (vmk_AddrCookie *)pAdapter,
                                   &props);
  if (status != VMK_OK) {
     SFVMK_ADAPTER_ERROR(pAdapter, "Failed to submit reset request to "
                         "helper world queue with error %s",
                         vmk_StatusToString(status));
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief function to set requested intr moderation settings.
**
** \param[in] pAdapter    pointer to sfvmk_adapter_t
** \param[in] moderation  Interrupt moderation value
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_configIntrModeration(sfvmk_adapter_t *pAdapter,
                           vmk_uint32 moderation)
{
  vmk_uint32 qIndex=0;
  const efx_nic_cfg_t *pNicCfg;
  VMK_ReturnStatus status = VMK_FAILURE;

  VMK_ASSERT_NOT_NULL(pAdapter);

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL) {
    status = VMK_FAILURE;
    goto done;
  }

  /* Parameter Validation */
  if (moderation > pNicCfg->enc_evq_timer_max_us) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid moderation value(%u)", moderation);
    status = VMK_BAD_PARAM;
    goto done;
  }

  sfvmk_MutexLock(pAdapter->lock);

  /* Firmware doesn't support different moderation settings for
   * different queues, so apply same config to all queues */
  for (qIndex=0; qIndex < pAdapter->numEvqsAllocated; qIndex++) {
    status = sfvmk_evqModerate(pAdapter, qIndex, moderation);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_evqModerate failed status: %s",
                          vmk_StatusToString(status));
      sfvmk_MutexUnlock(pAdapter->lock);
      goto done;
    }
  }

  pAdapter->intrModeration = moderation;

  sfvmk_MutexUnlock(pAdapter->lock);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "Configured static interupt moderation to %d (us)",
                      moderation);

done:
  return status;
}

/*! \brief function to copy the current coalesce params in shared queue area.
**
** \param[in] pAdapter  pointer to sfvmk_adapter_t
** \param[in] pParams   pointer to vmk_UplinkCoalesceParams
**
** \return: void
**
*/
void
sfvmk_configQueueDataCoalescParams(sfvmk_adapter_t *pAdapter,
                                   vmk_UplinkCoalesceParams *pParams)
{
  vmk_UplinkSharedQueueData *pQueueData;
  vmk_UplinkSharedQueueInfo *pQueueInfo;
  vmk_uint32 qIndex = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pParams);

  /* Once gloabl coalesce params are set, set to every TX/RX queues */
  sfvmk_sharedAreaBeginWrite(&pAdapter->uplink);

  pQueueInfo = &pAdapter->uplink.queueInfo;
  pQueueData = &pQueueInfo->queueData[0];

  /* Configure RX queue data */
  qIndex = sfvmk_getUplinkRxqStartIndex(&pAdapter->uplink);
  for (qIndex=0; qIndex < pQueueInfo->maxRxQueues; qIndex++) {
    vmk_Memcpy(&pQueueData[qIndex].coalesceParams, pParams, sizeof(*pParams));
  }

  /* Configure TX queue data */
  qIndex = sfvmk_getUplinkTxqStartIndex(&pAdapter->uplink);
  for (qIndex=0; qIndex < pQueueInfo->maxTxQueues; qIndex++) {
    vmk_Memcpy(&pQueueData[qIndex].coalesceParams, pParams, sizeof(*pParams));
  }

  sfvmk_sharedAreaEndWrite(&pAdapter->uplink);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return;
}

/*! \brief uplink callback function to get current coalesce params.
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[out] pParams   pointer to vmk_UplinkCoalesceParams
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus
sfvmk_coalesceParamsGet(vmk_AddrCookie cookie,
                        vmk_UplinkCoalesceParams *pParams)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_BAD_PARAM;
    goto done;
  }

  if (pParams == NULL) {
    status = VMK_BAD_PARAM;
    goto done;
  }

  vmk_Memset(pParams, 0, sizeof(vmk_UplinkCoalesceParams));

  /* Firmware doesn't support different moderation settings for
   * different rx and tx event types. Only txUsecs parameter is being
   * used for both rx & tx queue interrupt moderation conifguration */
  sfvmk_MutexLock(pAdapter->lock);
  pParams->txUsecs = pParams->rxUsecs = pAdapter->intrModeration;
  sfvmk_MutexUnlock(pAdapter->lock);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to set requested coalesce params.
**
** \param[in] cookie    pointer to sfvmk_adapter_t
** \param[in] pParams   pointer to vmk_UplinkCoalesceParams
**
** \return: VMK_OK or error code
**
*/
static VMK_ReturnStatus
sfvmk_coalesceParamsSet(vmk_AddrCookie cookie,
                        vmk_UplinkCoalesceParams *pParams)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter ptr");
    status = VMK_FAILURE;
    goto done;
  }

  if (pParams == NULL) {
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Firmware doesn't support different moderation settings for
   * different (rx/tx) event types. Only txUsecs would be considered
   * and rxUsecs value would be ignored */
  if (!(pParams->txUsecs)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid txUsecs value(%d)", pParams->txUsecs);
    status = VMK_BAD_PARAM;
    goto done;
  }

  /* Update interrupt moderation settings for both RX & TX queues */
  status = sfvmk_configIntrModeration(pAdapter, pParams->txUsecs);
  if (status != VMK_OK)
    goto done;

  /* Update RX param with the same value as TX params,
   * as FW supports same moderation setting for both */
  pParams->rxUsecs = pParams->txUsecs;

  /* Update shared queue data with the latest interrupt moderation values */
  sfvmk_configQueueDataCoalescParams(pAdapter, pParams);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Function to execute bist tests
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  bistMode    Bist mode of type efx_bist_type_t
** \param[in]  pTestString Name of the test
** \param[out] pResult     test result of type efx_bist_result_e
** \param[out] pStringBuf  buffer to store self test status
**
** \return: VMK_OK - When test executed successfully else error code
**
*/
static VMK_ReturnStatus
sfvmk_executeBistTest(sfvmk_adapter_t *pAdapter,
		      efx_bist_type_t bistMode,
		      char *pTestString,
		      efx_bist_result_t *pResult,
		      char *pStringBuf)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  unsigned long values[EFX_BIST_NVALUES];
  vmk_uint8 count = 0;
  vmk_Bool bistStarted = VMK_FALSE;

  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pTestString);
  VMK_ASSERT_NOT_NULL(pResult);
  VMK_ASSERT_NOT_NULL(pStringBuf);

  status = efx_bist_start(pAdapter->pNic, bistMode);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_bist_start failed status: %s",
                        vmk_StatusToString(status));
    *pResult = EFX_BIST_RESULT_FAILED;
    status = VMK_FAILURE;
    goto done;
  }
  bistStarted = VMK_TRUE;

  do {
    /* Delay for 1 ms */
    status = sfvmk_worldSleep(1000);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_worldSleep failed status: %s",
                          vmk_StatusToString(status));
      *pResult = EFX_BIST_RESULT_FAILED;
      goto done;
    }

    status = efx_bist_poll(pAdapter->pNic, bistMode,
                           pResult, NULL, values,
                           sizeof (values) / sizeof (values[0]));

    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_bist_poll failed status: %s",
                          vmk_StatusToString(status));
      *pResult = EFX_BIST_RESULT_FAILED;
      goto done;
    }

    if (*pResult != EFX_BIST_RESULT_RUNNING) {
      /* test executed successfully so returning VMK_OK
       * irrespective of the outcome of the test
       * outcome of test will be reflected in *pResult
       */
      status = VMK_OK;
      goto done;
    }
  } while (++count < 100);

  status = VMK_TIMEOUT;

done:
  if (bistStarted)
    efx_bist_stop(pAdapter->pNic, bistMode);

  /* Update string buffer */
  switch (*pResult) {
    case EFX_BIST_RESULT_PASSED:
      vmk_StringFormat(pStringBuf, SFVMK_SELF_TEST_RESULT_LEN,
                       NULL, "%s : PASSED", pTestString);
      break;
    case EFX_BIST_RESULT_FAILED:
      vmk_StringFormat(pStringBuf, SFVMK_SELF_TEST_RESULT_LEN,
                       NULL, "%s : FAILED", pTestString);
      break;
    default:
      vmk_StringFormat(pStringBuf, SFVMK_SELF_TEST_RESULT_LEN,
                       NULL, "%s : %s", pTestString,
                       (status == VMK_TIMEOUT) ? "TIMEOUT" : "UNKNOWN");
    }

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to get selfTest length.
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[out] pLen      pointer to length of self test result
**                       in vmk_UplinkSelfTestResult or vmk_UplinkSelfTestString
**
** \return: VMK_OK [success] error code [failure]
**
*/
static VMK_ReturnStatus sfvmk_selfTestResultLenGet(vmk_AddrCookie cookie,
                                                   vmk_uint32  *pLen)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_FAILURE;
    goto done;
  }

  if (pLen == NULL) {
    SFVMK_ERROR("NULL length ptr");
    status = VMK_FAILURE;
    goto done;
  }

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  *pLen = SFVMK_SELF_TEST_COUNT;

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to configure tunnel UDP port number.
**
** \param[in]  cookie       pointer to sfvmk_adapter_t
** \param[in]  portNumNBO   Encap UDP port number in network byte order
** \param[in]  tunnelType   pointer to efx_tunnel_protocol_t
**
** \return: VMK_OK on success or error code
**
*/
static VMK_ReturnStatus
sfvmk_tunnelPortUpdate(vmk_AddrCookie cookie,
                      vmk_uint16 portNumNBO,
                      efx_tunnel_protocol_t tunnelType)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  vmk_uint16 *pTunnelUdpPortNum = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_FAILURE;
    goto done;
  }
  sfvmk_MutexLock(pAdapter->lock);

  if (tunnelType == EFX_TUNNEL_PROTOCOL_VXLAN)
    pTunnelUdpPortNum = &(pAdapter->vxlanUdpPort);
  else {
    status = VMK_NOT_SUPPORTED;
    goto failed_tunnel_type_unsupported;
  }

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "sfvmk_tunnelPortUpdate: Tunnel Type: %d"
                      "New tunnel port num :%u Current tunnel port num : %u",
                       tunnelType, vmk_BE16ToCPU(portNumNBO), *pTunnelUdpPortNum);

  if (*pTunnelUdpPortNum == vmk_BE16ToCPU(portNumNBO)) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "No change in tunnel UDP port number (new:%u) (old:%u)",
                        vmk_BE16ToCPU(portNumNBO), *pTunnelUdpPortNum);
    goto tunnel_port_exist;
  }

  if (*pTunnelUdpPortNum != 0) {
    /* Remove Existing port number before updating new one */
    status = efx_tunnel_config_udp_remove(pAdapter->pNic,
                                          *pTunnelUdpPortNum,
                                          tunnelType);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "efx_tunnel_config_udp_remove failed status: %s",
                          vmk_StatusToString(status));
      goto failed_tunnel_port_remove;
    }
  }

  status = efx_tunnel_config_udp_add(pAdapter->pNic,
	                             vmk_BE16ToCPU(portNumNBO),
                                     tunnelType);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "efx_tunnel_config_udp_add (%u) failed status: %s",
                        vmk_BE16ToCPU(portNumNBO), vmk_StatusToString(status));
    goto failed_tunnel_port_cfg;
  }

  /* Reconfigure tunnel */
  status = efx_tunnel_reconfigure(pAdapter->pNic);
  /* Tunnel_reconfigure returns VMK_RETRY to indicate impending reboot */
  if ((status != VMK_OK) && (status != VMK_RETRY)) {
      SFVMK_ADAPTER_ERROR(pAdapter, "efx_tunnel_reconfigure (%u) failed status: %s",
                          vmk_BE16ToCPU(portNumNBO), vmk_StatusToString(status));

    /* Remove added port before exit */
    efx_tunnel_config_udp_remove(pAdapter->pNic,
                                 vmk_BE16ToCPU(portNumNBO),
                                 tunnelType);

    /* Reset tunnel port number from adapter data structure as well */
    *pTunnelUdpPortNum = 0;

    goto failed_tunnel_recfg;
  }

  *pTunnelUdpPortNum = vmk_BE16ToCPU(portNumNBO);

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_UPLINK, SFVMK_LOG_LEVEL_DBG,
                      "sfvmk_tunnelPortUpdate: Updated tunnel port number :%u",
                      *pTunnelUdpPortNum);

  pAdapter->startIOTunnelReCfgReqd = VMK_FALSE;

tunnel_port_exist:
  status = VMK_OK;

failed_tunnel_type_unsupported:
failed_tunnel_port_remove:
failed_tunnel_port_cfg:
failed_tunnel_recfg:
  sfvmk_MutexUnlock(pAdapter->lock);

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to configure VXLAN UDP port number.
**
** \param[in]  cookie       pointer to sfvmk_adapter_t
** \param[in]  portNumNBO   VXLAN UDP port number in network byte order
**
** \return: VMK_OK on success or error code
**
*/
static VMK_ReturnStatus
sfvmk_vxlanPortUpdate(vmk_AddrCookie cookie, vmk_uint16 portNumNBO)
{
  return sfvmk_tunnelPortUpdate(cookie, portNumNBO, EFX_TUNNEL_PROTOCOL_VXLAN);
}

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
/*! \brief uplink callback function to configure Geneve UDP port number.
**
** \param[in]  cookie       pointer to sfvmk_adapter_t
** \param[in]  portNumNBO   Geneve UDP port number in network byte order
**
** \return: VMK_OK on success or error code
**
*/
static VMK_ReturnStatus
sfvmk_genevePortUpdate(vmk_AddrCookie cookie, vmk_uint16 portNumNBO)
{
  return sfvmk_tunnelPortUpdate(cookie, portNumNBO, EFX_TUNNEL_PROTOCOL_GENEVE);
}
#endif

/*! \brief uplink callback function to run self tests
**
** \param[in]  cookie      pointer to sfvmk_adapter_t
** \param[in]  online      if TRUE, perform online tests only else offline test
** \param[out] pPassed     self test result which would be TRUE
**                         only if all tests are passed
** \param[out] pResultBuf  buffer to store self test result
** \param[out] pStringsBuf buffer to store self test string
**
** \return: VMK_OK        When any one test is executed successfully
** \return: VMK_FAILURE   If test execution of all test cases fail
**
*/
static VMK_ReturnStatus sfvmk_selfTestRun(vmk_AddrCookie cookie,
                                          vmk_Bool online,
                                          vmk_Bool *pPassed,
                                          vmk_UplinkSelfTestResult *pResultBuf,
                                          vmk_UplinkSelfTestString *pStringsBuf)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  efx_bist_result_t result = EFX_BIST_RESULT_FAILED;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 testExecFail = 0;
  vmk_uint32 testPass = 0;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (online == VMK_TRUE) {
    status = VMK_NOT_SUPPORTED;
    goto done;
  }

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_FAILURE;
    goto done;
  }

  if ((pPassed == NULL) || (pResultBuf == NULL) || (pStringsBuf == NULL)) {
    SFVMK_ERROR("NULL params ptr");
    status = VMK_FAILURE;
    goto done;
  }

  /* PHY Test : EFX_BIST_TYPE_PHY_NORMAL */
  status = sfvmk_executeBistTest(pAdapter,
                                 EFX_BIST_TYPE_PHY_NORMAL,
                                 "Phy Test", &result,
                                 pStringsBuf[0]);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "Phy Self Test execution failed status: %s",
                        vmk_StatusToString(status));
    testExecFail |= SFVMK_SELFTEST_PHY;
  }

  if (result == EFX_BIST_RESULT_PASSED)
    testPass |= SFVMK_SELFTEST_PHY;

#ifndef SFVMK_WORKAROUND_84849
  /* Enter bist offline mode. This is a fw mode which puts the NIC
   * into a state where memory BIST tests can be run
   * A reboot is required to exit this mode. */
  status = efx_bist_enable_offline(pAdapter->pNic);
  if ((status != VMK_OK)) {
    SFVMK_ADAPTER_ERROR(pAdapter,
                        "Bist Offline enable failed with err %s",
                        vmk_StatusToString(status));

    /* Skip memory & reg tests as they can be executed in offline mode only */
    vmk_StringCopy(pStringsBuf[1], "Register Test : FAILED",
                   SFVMK_SELF_TEST_RESULT_LEN);
    vmk_StringCopy(pStringsBuf[2], "Memory Test : FAILED",
                   SFVMK_SELF_TEST_RESULT_LEN);

    testExecFail |= SFVMK_SELFTEST_REG;
    testExecFail |= SFVMK_SELFTEST_MEM;
  } else {
    /* Reg Test : EFX_BIST_TYPE_REG */
    status = sfvmk_executeBistTest(pAdapter,
                                   EFX_BIST_TYPE_REG,
                                   "Register Test", &result,
                                   pStringsBuf[1]);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "Register Test execution failed status: %s",
                          vmk_StatusToString(status));
      testExecFail |= SFVMK_SELFTEST_REG;
    }
    if (result == EFX_BIST_RESULT_PASSED)
      testPass |= SFVMK_SELFTEST_REG;

    /* Mem Test : EFX_BIST_TYPE_MC_MEM */
    status = sfvmk_executeBistTest(pAdapter,
                                   EFX_BIST_TYPE_MC_MEM,
                                   "Memory Test", &result,
                                   pStringsBuf[2]);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "Memory Test execution failed status: %s",
                          vmk_StatusToString(status));
      testExecFail |= SFVMK_SELFTEST_MEM;
    }
    if (result == EFX_BIST_RESULT_PASSED)
      testPass |= SFVMK_SELFTEST_MEM;

    /* Apply MC reset to exit offline mode */
    efx_mcdi_reboot(pAdapter->pNic);

    /* Wait for completion of startIO */
    vmk_WorldWait((vmk_WorldEventID) &pAdapter->startIO_compl_event,
                  VMK_LOCK_INVALID,
                  SFVMK_MC_REBOOT_TIME_OUT_MSEC,
                  "sfvmk_mc_reboot_wait");

    status = VMK_OK;
  }
#endif

  /* Callback fails if none of the test could be executed
   * Result is pass if all tests are passed */
  status = (testExecFail == SFVMK_SELFTEST_ALL) ? VMK_FAILURE : VMK_OK;
  *pPassed = (testPass == SFVMK_SELFTEST_ALL) ? VMK_TRUE : VMK_FALSE;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Uplink callback function to get the RSS params.
**          (numRSSPools, number of RSS Qs, indirection and hash key size)
**
** \param[in]  cookie     vmk_AddrCookie
** \param[out] pParams    vmk_UplinkQueueRSSParams which have RSS info.
**
** \return: VMK_OK on success or error code otherwise
*/
static VMK_ReturnStatus
sfvmk_rssParamsGet(vmk_AddrCookie cookie,
                   vmk_UplinkQueueRSSParams *pParams)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pParams);

  if (!sfvmk_isRSSEnable(pAdapter)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RSS is not enabled");
    goto done;
  }

  /* Vmkernel supports only 1 RSS Pool Currently */
  pParams->numRSSPools = 1;
  pParams->numRSSQueuesPerPool = pAdapter->numRSSQs;

  /* Length of the RSS hash key in bytes */
  pParams->rssHashKeySize = SFVMK_RSS_HASH_KEY_SIZE;

  /* Size of the RSS indirection table */
  pParams->rssIndTableSize = EFX_RSS_TBL_SIZE;

  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Uplink callback function to init RSS configuration with the
**          given hash key and indirectional table.
**
** \param[in]  cookie         vmk_AddrCookie
** \param[in]  pRssHashKey    pointer to vmk_UplinkQueueRSSHashKey
** \param[in]  pRssIndTable   pointer to vmk_UplinkQueueRSSIndTable
**
** \return: VMK_OK on success or error code otherwise
*/
static VMK_ReturnStatus
sfvmk_rssStateInit(vmk_AddrCookie cookie,
                   vmk_UplinkQueueRSSHashKey *pRssHashKey,
                   vmk_UplinkQueueRSSIndTable *pRssIndTable)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 tableSize, i;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pRssHashKey);
  VMK_ASSERT_NOT_NULL(pRssIndTable);

  sfvmk_MutexLock(pAdapter->lock);

  if (!sfvmk_isRSSEnable(pAdapter)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RSS is not enabled");
    goto done;
  }

  if (pRssHashKey->keySize > SFVMK_RSS_HASH_KEY_SIZE) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid hash key size(%u)",
                        pRssHashKey->keySize);
    goto done;
  }

  tableSize = MIN(EFX_RSS_TBL_SIZE, pRssIndTable->tableSize);
  VMK_ASSERT_GT(tableSize, 0);

  for (i = 0; i < tableSize; i++)
    pAdapter->rssIndTable[i] = pRssIndTable->table[i];

  /* Configure RSS */
  status = sfvmk_configRSS(pAdapter,
                           (vmk_uint8 *)&pRssHashKey->key,
                           pRssHashKey->keySize,
                           pAdapter->rssIndTable,
                           tableSize);
  if (status) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_configRss Failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  memcpy(pAdapter->rssHashKey, &pRssHashKey->key, pRssHashKey->keySize);
  pAdapter->rssHashKeySize = pRssHashKey->keySize;
  pAdapter->rssInit = VMK_TRUE;

done:
  sfvmk_MutexUnlock(pAdapter->lock);

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief  Uplink callback function to reconfigure indirectional table
**          for RSS.
**
** \param[in]  cookie         vmk_AddrCookie
** \param[in]  pRssIndTable   pointer to vmk_UplinkQueueRSSIndTable
**
** \return: VMK_OK on success or error code otherwise
*/
static VMK_ReturnStatus
sfvmk_rssIndTableUpdate(vmk_AddrCookie cookie,
                        vmk_UplinkQueueRSSIndTable *pRssIndTable)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 tableSize, i;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pRssIndTable);

  sfvmk_MutexLock(pAdapter->lock);

  if (!sfvmk_isRSSEnable(pAdapter)) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RSS is not enabled");
    goto done;
  }

  if (!pAdapter->rssInit) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RSS is not yet Initialized");
    goto done;
  }

  tableSize = MIN(EFX_RSS_TBL_SIZE, pRssIndTable->tableSize);
  VMK_ASSERT_GT(tableSize, 0);

  for (i = 0; i < tableSize; i++)
    pAdapter->rssIndTable[i] = pRssIndTable->table[i];

  /* Configure RSS */
  status = sfvmk_configRSS(pAdapter,
                           pAdapter->rssHashKey,
                           pAdapter->rssHashKeySize,
                           pAdapter->rssIndTable,
                           tableSize);
  if (status) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_configRss Failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

done:
  sfvmk_MutexUnlock(pAdapter->lock);

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
/*! \brief uplink callback function to get advertised modes
**
** \param[in]        cookie     pointer to sfvmk_adapter_t
** \param[out]       pModes     advertised modes being advertised by driver
** \param[in, out]   pNumModes  number of advertised modes
**
** \return: VMK_OK on success, VMK_FAILURE otherwise
**
*/
static VMK_ReturnStatus
sfvmk_advModesGet(vmk_AddrCookie cookie,
                  vmk_UplinkAdvertisedMode *pModes,
                  vmk_uint32 *pNumModes)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  vmk_uint32 num;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    goto done;
  }

  /* update the current snapshot of advertised capabilities */
  sfvmk_updateSupportedCap(pAdapter, EFX_PHY_CAP_CURRENT);

  num = MIN(*pNumModes, pAdapter->uplink.numAdvertisedModes);
  VMK_ASSERT_GT(num, 0);
  vmk_Memcpy(pModes, pAdapter->uplink.advertisedModes,
             num * sizeof(vmk_UplinkAdvertisedMode));
  *pNumModes = num;
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief uplink callback function to set advertised modes
**
** \param[in]   cookie    pointer to sfvmk_adapter_t
** \param[in]   pModes    advertised modes to be set
** \param[in]   numModes  number of advertised modes
**
** \return: VMK_NOT_SUPPORTED
**
*/
static VMK_ReturnStatus
sfvmk_advModesSet(vmk_AddrCookie cookie,
                  vmk_UplinkAdvertisedMode *pModes,
                  vmk_uint32 numModes)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return VMK_NOT_SUPPORTED;
}
#endif
/*! \brief uplink callback function to retrieve pause params.
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[out] pParams   pointer to vmk_UplinkPauseParams
**
** \return: VMK_OK on success, VMK_FAILURE otherwise
**
*/
static VMK_ReturnStatus
sfvmk_pauseParamGet(vmk_AddrCookie cookie,
                    vmk_UplinkPauseParams *pParams)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  vmk_uint32 fcntlWanted = VMK_FALSE;
  vmk_uint32 fcntlLink   = VMK_FALSE;
  vmk_uint32 mask        = VMK_FALSE;
  sfvmk_port_t *pPort = NULL;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_FAILURE;
    goto done;
  }

  pPort = &pAdapter->port;

  sfvmk_MutexLock(pAdapter->lock);

  if (pAdapter->state != SFVMK_ADAPTER_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter IO is not yet started");
    status = VMK_FAILURE;
    goto failed_adapter_state;
  }

  efx_mac_fcntl_get(pAdapter->pNic, &fcntlWanted, &fcntlLink);

  pParams->txPauseEnabled = (fcntlWanted & EFX_FCNTL_GENERATE) ?
                            VMK_TRUE : VMK_FALSE;
  pParams->rxPauseEnabled = (fcntlWanted & EFX_FCNTL_RESPOND) ?
                            VMK_TRUE : VMK_FALSE;

  /* No separate control is available for pause autoneg, it would
   * be always auto negotiated if link (10 Gbps and higher) is enabled */
  pParams->autoNegotiate = VMK_TRUE;

  efx_phy_adv_cap_get(pAdapter->pNic, EFX_PHY_CAP_CURRENT, &mask);

  pParams->localDeviceAdvertise = (mask & (1 << EFX_PHY_CAP_ASYM)) ?
                                  VMK_UPLINK_FLOW_CTRL_ASYM_PAUSE :
                                  VMK_UPLINK_FLOW_CTRL_PAUSE;

  efx_phy_lp_cap_get(pAdapter->pNic, &mask);

  pParams->linkPartnerAdvertise = (mask & (1 << EFX_PHY_CAP_ASYM)) ?
                                 VMK_UPLINK_FLOW_CTRL_ASYM_PAUSE :
                                 VMK_UPLINK_FLOW_CTRL_PAUSE;

  status = VMK_OK;

failed_adapter_state:
  sfvmk_MutexUnlock(pAdapter->lock);
done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief uplink callback function to set requested pause params.
**
** \param[in]  cookie    pointer to sfvmk_adapter_t
** \param[in]  pParams   Pause parameters to set
**
** \return: VMK_OK on success, VMK_FAILURE otherwise
**
*/
static VMK_ReturnStatus
sfvmk_pauseParamSet(vmk_AddrCookie cookie, vmk_UplinkPauseParams pParams)
{
  sfvmk_adapter_t *pAdapter = (sfvmk_adapter_t *)cookie.ptr;
  vmk_uint32 fcntl = VMK_FALSE;
  vmk_uint32 fcntlWanted = VMK_FALSE;
  VMK_ReturnStatus status = VMK_FAILURE;
  sfvmk_port_t *pPort = NULL;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pAdapter == NULL) {
    SFVMK_ERROR("NULL adapter ptr");
    status = VMK_FAILURE;
    goto done;
  }

  pPort = &pAdapter->port;

  sfvmk_MutexLock(pAdapter->lock);

  if (pAdapter->state != SFVMK_ADAPTER_STATE_STARTED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Adapter IO is not yet started");
    status = VMK_FAILURE;
    goto failed_adapter_state;
  }

  efx_mac_fcntl_get(pAdapter->pNic, &fcntlWanted, &fcntl);

  if (pParams.txPauseEnabled == VMK_TRUE)
    fcntl |=  EFX_FCNTL_GENERATE;
  else
    fcntl &= ~EFX_FCNTL_GENERATE;

  if (pParams.rxPauseEnabled == VMK_TRUE)
    fcntl |= EFX_FCNTL_RESPOND;
  else
    fcntl &= ~EFX_FCNTL_RESPOND;

  status = efx_mac_fcntl_set(pAdapter->pNic, fcntl, pParams.autoNegotiate);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "efx_mac_fcntl_set failed status: %s",
                        vmk_StatusToString(status));
    status = VMK_FAILURE;
    goto failed_mac_fcntl_set;
  }

  pPort->fcRequested = fcntl;

  status = VMK_OK;

failed_adapter_state:
failed_mac_fcntl_set:
  sfvmk_MutexUnlock(pAdapter->lock);
done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);

  return status;
}

/*! \brief Fill the buffer with a Rx/Tx queue stats
**         lock is already taken.
**
** \param[in]  pAdapter      pointer to sfvmk_adapter_t
** \param[in]  qIndex        queue Index
** \param[in]  pStart        pointer to start position in stats buffer
** \param[in]  maxBytes      maximum number of bytes to output
** \param[in]  isRxQueue     VMK_TRUE if called for a rx queue
** \param[out] pBytesCopied  pointer to number of bytes copied in stats buffer
**
** \return: VMK_OK [success]
**     Below error values are returned in case of failure,
**           VMK_LIMIT_EXCEEDED  If stats buffer overflowed
**           VMK_BAD_PARAM       If buffer is not valid.
**           VMK_FAILURE         Any other error
*/
static VMK_ReturnStatus
sfvmk_fillAllQueueStats(sfvmk_adapter_t *pAdapter,
                        vmk_uint16 qIndex,
                        char *pStart, vmk_ByteCount maxBytes,
                        vmk_Bool isRxQueue, vmk_ByteCount *pBytesCopied)
{
  char *pCurr;
  char **ppStatsName = NULL;
  vmk_uint64 *pStatsVal = NULL;
  vmk_ByteCount bytesCopied = 0;
  vmk_uint32 i, statsCounts = 0;
  vmk_uint32 maxStats;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pBytesCopied != NULL)
    *pBytesCopied = 0;

  pCurr = pStart;

  if (isRxQueue) {
    pStatsVal = (vmk_uint64 *)&pAdapter->ppRxq[qIndex]->stats;
    ppStatsName = (char **)pSfvmkRxqStatsName;
    maxStats = SFVMK_RXQ_MAX_STATS;
  } else {
    pStatsVal = (vmk_uint64 *)&pAdapter->ppTxq[qIndex]->stats;
    ppStatsName = (char **)pSfvmkTxqStatsName;
    maxStats = SFVMK_TXQ_MAX_STATS;
  }

  status = vmk_StringFormat(pCurr, maxBytes, &bytesCopied,
                            isRxQueue ?  "RxQ[%u]:\n" : "TxQ[%u]:\n",
                            qIndex);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_StringFormat failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  SFVMK_UPDATE_STATS_INFO(pCurr, maxBytes, bytesCopied, pBytesCopied);
  bytesCopied = 0;

  for (i = 0; i < maxStats; i++ ) {
    status = vmk_StringFormat(pCurr, maxBytes,
                              &bytesCopied, " %-30s %-22lu",
                              ppStatsName[i],
                              pStatsVal[i]);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_StringFormat failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }

    SFVMK_UPDATE_STATS_INFO(pCurr, maxBytes, bytesCopied, pBytesCopied);
    bytesCopied = 0;
    statsCounts++;

    if ((statsCounts <= 1) && (i != (maxStats - 1)))
      continue;

    status = vmk_StringFormat(pCurr, maxBytes, &bytesCopied, "\n");
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "vmk_StringFormat failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }

    SFVMK_UPDATE_STATS_INFO(pCurr, maxBytes, bytesCopied, pBytesCopied);
    bytesCopied = 0;
    statsCounts = 0;
  }

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief Fill the buffer with per Rx/Tx queue stats
**
** \param[in]  pAdapter      pointer to sfvmk_adapter_t
** \param[in]  pStart        pointer to start position in stats buffer
** \param[in]  maxBytes      maximum number of bytes to output
** \param[out] pBytesCopied  pointer to number of bytes copied in stats buffer
**
** \return: VMK_OK [success]
**     Below error values are returned in case of failure,
**           VMK_LIMIT_EXCEEDED  If stats buffer overflowed
**           VMK_BAD_PARAM       If buffer is not valid.
**           VMK_FAILURE         Any other error
*/
VMK_ReturnStatus
sfvmk_requestQueueStats(sfvmk_adapter_t *pAdapter,
                        char *pStart, vmk_ByteCount maxBytes,
                        vmk_ByteCount *pBytesCopied)
{
  char *pCurr = NULL;
  vmk_uint16 maxRxQueues;
  vmk_uint16 maxTxQueues;
  vmk_uint16 qIndex;
  vmk_ByteCount bytesCopied = 0;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  sfvmk_MutexLock(pAdapter->lock);

  if (pStart == NULL) {
    status = VMK_BAD_PARAM;
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid parameter: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  if (pBytesCopied != NULL)
    *pBytesCopied = 0;

  pCurr = pStart;
  maxRxQueues = sfvmk_getMaxRxHardwareQueues(pAdapter);
  maxTxQueues = sfvmk_getMaxTxHardwareQueues(pAdapter);

  status = vmk_StringFormat(pCurr, maxBytes,
                            &bytesCopied, "  -- Per Hardware Queue Statistics\n");
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_StringFormat failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  SFVMK_UPDATE_STATS_INFO(pCurr, maxBytes, bytesCopied, pBytesCopied);

  for (qIndex = 0; qIndex < maxRxQueues; qIndex++) {
    status = sfvmk_fillAllQueueStats(pAdapter, qIndex, pCurr,
                                     maxBytes, VMK_TRUE, &bytesCopied);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_fillRxQueueStats failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }

    SFVMK_UPDATE_STATS_INFO(pCurr, maxBytes, bytesCopied, pBytesCopied);
  }

  bytesCopied = 0;
  status = vmk_StringFormat(pCurr, maxBytes, &bytesCopied, "\n");
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_StringFormat failed status: %s",
                        vmk_StatusToString(status));
    goto done;
  }

  SFVMK_UPDATE_STATS_INFO(pCurr, maxBytes, bytesCopied, pBytesCopied);

  for (qIndex = 0; qIndex < maxTxQueues; qIndex++) {
    status = sfvmk_fillAllQueueStats(pAdapter, qIndex ,pCurr,
                                     maxBytes, VMK_FALSE, &bytesCopied);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_fillTxQueueStats failed status: %s",
                          vmk_StatusToString(status));
      goto done;
    }

    SFVMK_UPDATE_STATS_INFO(pCurr, maxBytes, bytesCopied, pBytesCopied);
  }

  status = VMK_OK;

done:
  sfvmk_MutexUnlock(pAdapter->lock);
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

/*! \brief Fill the buffer with MAC stats
**
** \param[in]  pAdapter      pointer to sfvmk_adapter_t
** \param[in]  pStart        pointer to start position in stats buffer
** \param[in]  maxBytes      maximum number of bytes to output
** \param[out] pBytesCopied  pointer to number of bytes copied in stats buffer
**
** \return: VMK_OK [success]
**     Below error values are returned in case of failure,
**           VMK_LIMIT_EXCEEDED  If stats buffer overflowed
**           VMK_BAD_PARAM       If buffer is not valid.
**           VMK_FAILURE         Any other error
*/
VMK_ReturnStatus
sfvmk_requestMACStats(sfvmk_adapter_t *pAdapter,
                      char *pStart, vmk_ByteCount maxBytes,
                      vmk_ByteCount *pBytesCopied)
{
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_UPLINK);

  if (pStart == NULL) {
    status = VMK_BAD_PARAM;
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid parameter: %s",
                        vmk_StatusToString(status));
    return status;
  }

  if (pBytesCopied != NULL)
    *pBytesCopied = 0;

  sfvmk_MutexLock(pAdapter->lock);
  status = sfvmk_fillMacStats(pAdapter, pStart, maxBytes, pBytesCopied);
  sfvmk_MutexUnlock(pAdapter->lock);

  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_UPLINK);
  return status;
}

