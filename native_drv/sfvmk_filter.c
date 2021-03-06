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

/* Data structure to retrieve value from a
 * key-value pairs on a given hash table. */
typedef struct sfvmk_filterDBIterCtx_s {
  vmk_Bool                foundFilterEntry;
  sfvmk_filterDBEntry_t   *pFdbEntry;
} sfvmk_filterDBIterCtx_t;

typedef struct sfvmk_filterEncapEntry_s {
  uint16_t               etherType;
  uint32_t               innerFrameMatch;
} sfvmk_filterEncapEntry_t;

#define SFVMK_ENCAP_FILTER_ENTRY(ipv)                      \
  {EFX_ETHER_TYPE_##ipv,                                   \
   EFX_FILTER_INNER_FRAME_MATCH_OTHER}

static const sfvmk_filterEncapEntry_t sfvmk_filterEncapList[] = {
  SFVMK_ENCAP_FILTER_ENTRY(IPV4),
  SFVMK_ENCAP_FILTER_ENTRY(IPV6)
};

/*! \brief  Allocate a filter DB entry
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t
**
** \return: pointer to filter DB entry if success, NULL if failed
**
*/
sfvmk_filterDBEntry_t *
sfvmk_allocFilterRule(sfvmk_adapter_t *pAdapter)
{
  sfvmk_filterDBEntry_t *pFdbEntry;

  pFdbEntry = vmk_HeapAlloc(sfvmk_modInfo.heapID, sizeof(*pFdbEntry));
  if (pFdbEntry == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "vmk_HeapAlloc Failed for pFdbEntry");
    return NULL;
  }

  vmk_Memset(pFdbEntry, 0, sizeof(*pFdbEntry));

  return pFdbEntry;
}

/*! \brief  Free a filter DB entry
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t
** \param[in]  pFdbEntry  pointer to sfvmk_filterDBEntry_t
**
** \return: void
**
*/
void
sfvmk_freeFilterRule(sfvmk_adapter_t *pAdapter,
                     sfvmk_filterDBEntry_t *pFdbEntry)
{
  if (pFdbEntry == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Invalid pointer to pFdbEntry");
    return;
  }

  vmk_HeapFree(sfvmk_modInfo.heapID, pFdbEntry);
  pFdbEntry = NULL;
}

/*! \brief  Generate a filter Key
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t
**
** \return: filter Key
**
*/
vmk_uint32
sfvmk_generateFilterKey(sfvmk_adapter_t *pAdapter)
{
  pAdapter->filterKey += 1;
  return pAdapter->filterKey;
}

/*! \brief  Prepare a VLAN MAC filter rule
**
** \param[in]      pAdapter   pointer to sfvmk_adapter_t
** \param[in]      pRxq       pointer to HW Rx Q for which filter is prepared
** \param[in]      mac        MAC address
** \param[in]      vlanID     vlan ID
** \param[in]      flags      filter flags
** \param[in,out]  pFdbEntry  pointer to filter DB entry
**
** \return: VMK_OK if success, error number if failed
**
*/
static VMK_ReturnStatus
sfvmk_prepareVMACFilterRule(sfvmk_adapter_t *pAdapter,
                            struct sfvmk_rxq_s *pRxq,
                            vmk_EthAddress mac,
                            vmk_VlanID vlanID,
                            efx_filter_flags_t flags,
                            sfvmk_filterDBEntry_t *pFdbEntry)
{
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_FILTER, SFVMK_LOG_LEVEL_DBG,
                      "Create HWFilter %p: VLAN ID %u"
                      " %02x:%02x:%02x:%02x:%02x:%02x",
                      pFdbEntry, vlanID,
                      mac[0], mac[1], mac[2],
                      mac[3], mac[4], mac[5]);

  efx_filter_spec_init_rx(&pFdbEntry->spec[0], EFX_FILTER_PRI_MANUAL, flags, pRxq->pCommonRxq);

  status = efx_filter_spec_set_eth_local(&pFdbEntry->spec[0],
                                         vlanID,
                                         mac);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Prepare VLAN/MAC HW filter "
                        "failed with error code %s",
                        vmk_StatusToString(status));
    return status;
  }
  pFdbEntry->numHwFilter = 1;

  return status;
}

/*! \brief  Prepare a tunnel filter rule
 **
 ** \param[in]      pAdapter          pointer to sfvmk_adapter_t
 ** \param[in]      pRxq              pointer to HW Rx Q for which filter is prepared
 ** \param[in]      encapType         pointer to efx_tunnel_protocol_t
 ** \param[in]      innerMAC          inner mac address
 ** \param[in]      outerMAC          outer mac address
 ** \param[in]      vni_or_vsid       vni for vxlan/geneve or vsid of nvgre
 ** \param[in]      flags             filter flags
 ** \param[in,out]  pFdbEntry         pointer to filter DB entry
 **
 ** \return: VMK_OK if success, error number if failed
 **
 */
static VMK_ReturnStatus
sfvmk_prepareTunnelFilterRule(sfvmk_adapter_t *pAdapter,
                             struct sfvmk_rxq_s *pRxq,
                             efx_tunnel_protocol_t encapType,
                             vmk_EthAddress innerMAC,
                             vmk_EthAddress outerMAC,
                             vmk_uint32 vni_or_vsid,
                             efx_filter_flags_t flags,
                             sfvmk_filterDBEntry_t *pFdbEntry)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint8 vni[EFX_VNI_OR_VSID_LEN];
  const efx_nic_cfg_t *pNicCfg = NULL;
  vmk_uint32 i, hwFilterCount = 0;

  pNicCfg = efx_nic_cfg_get(pAdapter->pNic);
  if (pNicCfg == NULL)
    goto end;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_FILTER, SFVMK_LOG_LEVEL_DBG,
                      "Create Encap Filter %p: VNI ID %u, encapType %u"
                      " %02x:%02x:%02x:%02x:%02x:%02x"
                      " %02x:%02x:%02x:%02x:%02x:%02x",
                      pFdbEntry, vni_or_vsid, encapType,
                      innerMAC[0], innerMAC[1],
                      innerMAC[2], innerMAC[3],
                      innerMAC[4], innerMAC[5],
                      outerMAC[0], outerMAC[1],
                      outerMAC[2], outerMAC[3],
                      outerMAC[4], outerMAC[5]);

  /* VNI in big endian format */
  vni[0] = (vni_or_vsid >> 16) & 0xFF;
  vni[1] = (vni_or_vsid >> 8) & 0xFF;
  vni[2] = vni_or_vsid & 0xFF;

  EFX_STATIC_ASSERT(SFMK_MAX_HWF_PER_UPF >= EFX_ARRAY_SIZE(sfvmk_filterEncapList));

  for (i = 0; i < EFX_ARRAY_SIZE(sfvmk_filterEncapList); i++) {

    const sfvmk_filterEncapEntry_t *pEncapFilter = &sfvmk_filterEncapList[i];

    if (pEncapFilter->etherType == EFX_ETHER_TYPE_IPV6) {
      if (!(pNicCfg->enc_features & EFX_FEATURE_IPV6))
        continue;
    }

    efx_filter_spec_init_rx(&pFdbEntry->spec[i],
                            EFX_FILTER_PRI_MANUAL,
                            flags, pRxq->pCommonRxq);

    if (encapType == EFX_TUNNEL_PROTOCOL_VXLAN) {
      status = efx_filter_spec_set_vxlan(&pFdbEntry->spec[i],
                                         vni,
                                         innerMAC,
                                         outerMAC);
    }
#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
    else if (encapType == EFX_TUNNEL_PROTOCOL_GENEVE) {
      status = efx_filter_spec_set_geneve(&pFdbEntry->spec[i],
                                          vni,
                                          innerMAC,
                                          outerMAC);
    }
#endif
    else
      status = VMK_NOT_SUPPORTED;

    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "Prepare Encap HW filter for %d entry for encap_type %u"
                          "failed with error code %s",
                          i, encapType, vmk_StatusToString(status));
      goto end;
    }

    efx_filter_spec_set_ether_type(&pFdbEntry->spec[i], pEncapFilter->etherType);
    status = efx_filter_spec_set_encap_type(&pFdbEntry->spec[i],
                                            encapType,
                                            pEncapFilter->innerFrameMatch);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "Prepare Encap HW filter set encapType for %d entry for encap_type %u"
                          "failed with error code %s",
                          i, encapType, vmk_StatusToString(status));
      goto end;
    }

    hwFilterCount++;
  }

  pFdbEntry->numHwFilter = hwFilterCount;
  status = VMK_OK;

end:
  return status;
}

/*! \brief  Prepare a hardware filter rule
**
** \param[in]      pAdapter   pointer to sfvmk_adapter_t
** \param[in]      pFilter    pointer to uplink filter info
** \param[in,out]  pFdbEntry  pointer to filter DB entry
** \param[in]      filterKey  filter key value
** \param[in]      qidVal     queue ID value
** \param[in]      flags      filter flags
**
** \return: VMK_OK if success, error number if failed
**
*/
VMK_ReturnStatus
sfvmk_prepareFilterRule(sfvmk_adapter_t *pAdapter,
                       vmk_UplinkQueueFilter *pFilter,
                       sfvmk_filterDBEntry_t *pFdbEntry,
                       vmk_uint32 filterKey, vmk_uint32 qidVal,
                       efx_filter_flags_t flags)
{
  struct sfvmk_rxq_s *pRxq;
  VMK_ReturnStatus status = VMK_FAILURE;

  if (!pAdapter->ppRxq) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RX queues not intialized");
    goto done;
  }

  pRxq = pAdapter->ppRxq[qidVal];
  if (!pRxq) {
    SFVMK_ADAPTER_ERROR(pAdapter, "RX queue Id %u not intialized", qidVal);
    goto done;
  }

  pFdbEntry->class = pFilter->class;
  pFdbEntry->qID = qidVal;
  pFdbEntry->key = filterKey;

  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_FILTER, SFVMK_LOG_LEVEL_DBG,
                      "HWFilter %p: Class %d, Filter ID %u, Queue ID %u",
                      pFdbEntry, pFdbEntry->class, filterKey, qidVal);

  switch (pFdbEntry->class) {
    case VMK_UPLINK_QUEUE_FILTER_CLASS_MAC_ONLY:
      status = sfvmk_prepareVMACFilterRule(pAdapter, pRxq,
                                           pFilter->macFilterInfo->mac,
                                           EFX_FILTER_SPEC_VID_UNSPEC,
                                           flags, pFdbEntry);
      break;

    case VMK_UPLINK_QUEUE_FILTER_CLASS_VLANMAC:
      status = sfvmk_prepareVMACFilterRule(pAdapter, pRxq,
                                           pFilter->vlanMacFilterInfo->mac,
                                           pFilter->vlanMacFilterInfo->vlanID,
                                           flags, pFdbEntry);
      break;

    case VMK_UPLINK_QUEUE_FILTER_CLASS_VLAN_ONLY:
      SFVMK_ADAPTER_ERROR(pAdapter, "Filter class %d not supported",
                          pFdbEntry->class);
      status = VMK_NOT_SUPPORTED;
      break;

    case VMK_UPLINK_QUEUE_FILTER_CLASS_VXLAN:
      if (pAdapter->isTunnelEncapSupported & SFVMK_VXLAN_OFFLOAD) {
        status = sfvmk_prepareTunnelFilterRule(pAdapter, pRxq,
                                              EFX_TUNNEL_PROTOCOL_VXLAN,
                                              pFilter->vxlanFilterInfo->innerMAC,
                                              pFilter->vxlanFilterInfo->outerMAC,
                                              pFilter->vxlanFilterInfo->vxlanID,
                                              flags, pFdbEntry);
      }
      else {
        SFVMK_ADAPTER_ERROR(pAdapter, "Filter class %d not enabled",
                            pFdbEntry->class);
        status = VMK_NOT_SUPPORTED;
      }

      break;

#if VMKAPI_REVISION >= VMK_REVISION_FROM_NUMBERS(2, 4, 0, 0)
    case VMK_UPLINK_QUEUE_FILTER_CLASS_GENEVE:
      if (pAdapter->isTunnelEncapSupported & SFVMK_GENEVE_OFFLOAD) {
        status = sfvmk_prepareTunnelFilterRule(pAdapter, pRxq,
                                               EFX_TUNNEL_PROTOCOL_GENEVE,
                                               pFilter->geneveFilterInfo->innerMAC,
                                               pFilter->geneveFilterInfo->outerMAC,
                                               pFilter->geneveFilterInfo->vni,
                                               flags, pFdbEntry);
      }
      else {
        SFVMK_ADAPTER_ERROR(pAdapter, "Filter class %d not enabled",
                            pFdbEntry->class);
        status = VMK_NOT_SUPPORTED;
      }

      break;
#endif

    default:
      SFVMK_ADAPTER_ERROR(pAdapter, "Filter class %d not supported",
                          pFdbEntry->class);
      status = VMK_NOT_SUPPORTED;
  }

done:
  return status;
}

/*! \brief Iterator used to iterate and match
**         the key-value pairs on a given hash table.
**
** \param[in]     htbl   Hash handle.
** \param[in]     key    Hash key.
** \param[in]     value  Hash entry value stored at key
** \param[in,out] data   pointer to sfvmk_filterDBIterCtx_t,
**                       if a match is found then
**                       foundFilterEntry field is set to true.
**
** \return: Key iterator commands.
**
*/
static vmk_HashKeyIteratorCmd
sfvmk_matchFilterDBHashIter(vmk_HashTable htbl,
                            vmk_HashKey key, vmk_HashValue value,
                            vmk_AddrCookie data)
{
  sfvmk_filterDBIterCtx_t *pIterCtx = data.ptr;
  sfvmk_filterDBEntry_t *pMatchFdbEntry = pIterCtx->pFdbEntry;
  sfvmk_filterDBEntry_t *pStoredFdbEntry = (sfvmk_filterDBEntry_t *)value;
  vmk_HashKeyIteratorCmd returnCode = VMK_HASH_KEY_ITER_CMD_CONTINUE;
  vmk_uint32 i;

  if (!pStoredFdbEntry || !pMatchFdbEntry) {
    returnCode = VMK_HASH_KEY_ITER_CMD_STOP;
    goto done;
  }

  if (pStoredFdbEntry->class != pMatchFdbEntry->class) {
    returnCode = VMK_HASH_KEY_ITER_CMD_CONTINUE;
    goto done;
  }

  if (pStoredFdbEntry->numHwFilter != pMatchFdbEntry->numHwFilter) {
      returnCode = VMK_HASH_KEY_ITER_CMD_CONTINUE;
      goto done;
  }

  for (i = 0; i < pStoredFdbEntry->numHwFilter; i++) {
    if (memcmp(&pStoredFdbEntry->spec[i],
               &pMatchFdbEntry->spec[i], sizeof(pStoredFdbEntry->spec[0]))) {
      returnCode = VMK_HASH_KEY_ITER_CMD_CONTINUE;
      goto done;
    }
  }

  returnCode = VMK_HASH_KEY_ITER_CMD_STOP;
  pIterCtx->foundFilterEntry = VMK_TRUE;

done:
  return returnCode;
}

/*! \brief  search in filter data base and find match
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t
** \param[in]  pFdbEntry  pointer to filter entry to compare
**
** \return: VMK_TRUE if found match, VMK_FLASE if
**          filter match not found.
**
*/
static vmk_Bool
sfvmk_matchFilterRule(sfvmk_adapter_t *pAdapter, sfvmk_filterDBEntry_t *pFdbEntry)
{
  sfvmk_filterDBIterCtx_t iterCtx = {VMK_FALSE, pFdbEntry};
  VMK_ReturnStatus status;

  if (vmk_HashIsEmpty(pAdapter->filterDBHashTable)) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_FILTER, SFVMK_LOG_LEVEL_DBG,
                        "Filter hash database table is empty");
    return VMK_FALSE;
  }

  status = vmk_HashKeyIterate(pAdapter->filterDBHashTable,
                              sfvmk_matchFilterDBHashIter, &iterCtx);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Iterator failed with error code %s",
                        vmk_StatusToString(status));
    return VMK_FALSE;
  }

  if (iterCtx.foundFilterEntry) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_FILTER, SFVMK_LOG_LEVEL_DBG,
                        "Found a match for filter");
    return VMK_TRUE;
  }

  return VMK_FALSE;
}

/*! \brief  insert a filter rule
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t
** \param[in]  pFdbEntry  pointer to filter DB entry
**
** \return: VMK_OK if success, error number if failed
**
*/
VMK_ReturnStatus
sfvmk_insertFilterRule(sfvmk_adapter_t *pAdapter,
                       sfvmk_filterDBEntry_t *pFdbEntry)
{
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 i;

  if (sfvmk_matchFilterRule(pAdapter, pFdbEntry) == VMK_TRUE) {
    status = VMK_EXISTS;
    goto done;
  }

  for (i = 0; i < pFdbEntry->numHwFilter; i++) {
    status = efx_filter_insert(pAdapter->pNic, &pFdbEntry->spec[i]);
    if ((status != VMK_OK) && (status != VMK_EXISTS)) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "Insert of HW filter failed for entry %d with error code %s",
                          i, vmk_StatusToString(status));
      status = VMK_FAILURE;
      goto failed_insert;
    }
  }

  status = VMK_OK;
  SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_FILTER, SFVMK_LOG_LEVEL_DBG,
                      "Hw filter inserted successfully");
  goto done;

failed_insert:
  while (i--)
    efx_filter_remove(pAdapter->pNic, &pFdbEntry->spec[i]);

done:
  return status;
}

/*! \brief  find and remove filter rule for given filterKey
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
** \param[in]  filterKey   filter key of already created filter.
**
** \return: pointer to pFdbEntry if success, NULL if failed
**
*/
sfvmk_filterDBEntry_t *
sfvmk_removeFilterRule(sfvmk_adapter_t *pAdapter, vmk_uint32 filterKey)
{
  sfvmk_filterDBEntry_t *pFdbEntry;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 i;

  if (vmk_HashIsEmpty(pAdapter->filterDBHashTable)) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_FILTER, SFVMK_LOG_LEVEL_DBG,
                        "Filter hash database table is empty");
    return NULL;
  }

  status = vmk_HashKeyFind(pAdapter->filterDBHashTable,
                           (vmk_HashKey)(vmk_uint64)filterKey,
                           (vmk_HashValue *)&pFdbEntry);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_DEBUG(pAdapter, SFVMK_DEBUG_FILTER, SFVMK_LOG_LEVEL_DBG,
                        "vmk_HashKeyFind failed with error code %s",
                        vmk_StatusToString(status));
    return NULL;
  }

  if (!pFdbEntry) {
    SFVMK_ADAPTER_ERROR(pAdapter, "pFdbEntry is NULL for filter ID %d,"
                        "This is unexpected", filterKey);
    return NULL;
  }

  for (i = 0; i < pFdbEntry->numHwFilter; i++) {
    status = efx_filter_remove(pAdapter->pNic, &pFdbEntry->spec[i]);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter,
                          "Remove HW filter failed for entry %d with error code %s",
                          i, vmk_StatusToString(status));
      return NULL;
    }
  }

  return pFdbEntry;
}

/*! \brief Iterator used to pop a filter DB entry from hash
**         table and fill the filter DB Iterator context
**         structure entry.
**
** \param[in]     htbl   Hash handle.
** \param[in]     key    Hash key.
** \param[in]     value  Hash entry vlaue stored at key
** \param[in,out] data   pointer to sfvmk_filterDBIterCtx_t
**
** \return: Key iterator stop command.
**
*/
static vmk_HashKeyIteratorCmd
sfvmk_getFilterDBHashIter(vmk_HashTable htbl, vmk_HashKey key,
                          vmk_HashValue value, vmk_AddrCookie data)
{
  sfvmk_filterDBIterCtx_t *pIterCtx = data.ptr;
  sfvmk_filterDBEntry_t *pFdbEntry = value;

  if (!pIterCtx || !pFdbEntry)
   return VMK_HASH_KEY_ITER_CMD_STOP;

  pIterCtx->pFdbEntry = pFdbEntry;

  return VMK_HASH_KEY_ITER_CMD_STOP;
}

/*! \brief  clear all filters in the filter database hash
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
**
*/
static void
sfvmk_clearAllFilterRules(sfvmk_adapter_t *pAdapter)
{
  sfvmk_filterDBIterCtx_t iterCtx = {VMK_FALSE, NULL};
  sfvmk_filterDBEntry_t *pFdbEntry;
  VMK_ReturnStatus status = VMK_FAILURE;
  vmk_uint32 i;
  vmk_uint32 qID;

  while (!vmk_HashIsEmpty(pAdapter->filterDBHashTable)) {

    /* Note: The filter remove operation could be implemented
     * within iterator itself where iterator can also takes care
     * of deleting the entries from Hash. But it seems iterator
     * do not expect any blocking call while the iterator engine
     * is in progress. Therefore below logic has been implemented
     * to pop one entry at time using iterator, remove the filter
     * and delete the filter hash key in a loop */
    status = vmk_HashKeyIterate(pAdapter->filterDBHashTable,
                                sfvmk_getFilterDBHashIter, &iterCtx);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "Iterator failed with error code %s",
                          vmk_StatusToString(status));
      break;
    }

    if (!iterCtx.pFdbEntry)
      continue;

    pFdbEntry = iterCtx.pFdbEntry;
    iterCtx.pFdbEntry = NULL;

    for (i = 0; i < pFdbEntry->numHwFilter; i++) {
      status = efx_filter_remove(pAdapter->pNic, &pFdbEntry->spec[i]);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter,
                            "Remove HW filter failed for entry %d with error code %s",
                            i, vmk_StatusToString(status));
      }
    }

    /* There is one to one mapping between RX HWQs index and uplinkQ index
     * except for RSSQs. */
    if (pFdbEntry->qID == sfvmk_getRSSQStartIndex(pAdapter)) {
      qID = pAdapter->uplink.rssUplinkQueue;
    } else {
      qID = pFdbEntry->qID;
    }

    sfvmk_removeUplinkFilter(pAdapter, qID);
    status = vmk_HashKeyDelete(pAdapter->filterDBHashTable,
                               (vmk_HashKey)(vmk_uint64)pFdbEntry->key,
                               NULL);
    if (status != VMK_OK)
      SFVMK_ADAPTER_ERROR(pAdapter, "Delete key entry failed with error %s",
                          vmk_StatusToString(status));

    sfvmk_freeFilterRule(pAdapter, pFdbEntry);
  }

}

/*! \brief  allocate the filter hash table
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_allocFilterDBHash(sfvmk_adapter_t *pAdapter)
{
  vmk_HashProperties hashProps;
  VMK_ReturnStatus status;

  hashProps.moduleID  = vmk_ModuleCurrentID;
  hashProps.heapID    = sfvmk_modInfo.heapID;
  hashProps.keyType   = VMK_HASH_KEY_TYPE_INT;
  hashProps.keyFlags  = VMK_HASH_KEY_FLAGS_NONE;
  hashProps.keySize   = 0;
  hashProps.nbEntries = SFVMK_MAX_FILTER;
  hashProps.acquire   = NULL;
  hashProps.release   = NULL;

  status = vmk_HashAlloc(&hashProps, &pAdapter->filterDBHashTable);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Filter DB hash alloc failed with error code %s",
                        vmk_StatusToString(status));
    return status;
  }

  pAdapter->filterKey = 0;
  return VMK_OK;
}

/*! \brief  free the filter hash table
**
** \param[in]  pAdapter pointer to sfvmk_adapter_t
**
** \return: void
**
*/
void
sfvmk_freeFilterDBHash(sfvmk_adapter_t *pAdapter)
{
   sfvmk_clearAllFilterRules(pAdapter);
   vmk_HashDeleteAll(pAdapter->filterDBHashTable);
   if (vmk_HashIsEmpty(pAdapter->filterDBHashTable)) {
      vmk_HashRelease(pAdapter->filterDBHashTable);
      pAdapter->filterDBHashTable = VMK_INVALID_HASH_HANDLE;
   }
}
