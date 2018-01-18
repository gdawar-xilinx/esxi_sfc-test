/*
 * Copyright (c) 2017, Solarflare Communications Inc.
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

/*! \brief Routine to get VPD information
 **
 ** \param[in]  pAdapter       Pointer to sfvmk_adapter_t
 ** \param[out] pVpdData       VPD payload
 ** \param[in]  maxPayloadSize Maximum size of pVpdData
 ** \param[in]  vpdTag         VPD Tag info
 ** \param[in]  vpdKeyword     VPD keyword info
 ** \param[out] pVpdLen        Length of data copied into pVpdData
 **
 ** \return: VMK_OK <success> error code <failure>
 **     Below error values values will be returned in
 **     case of failure,
 **     VMK_NO_MEMORY:      Memory allocation failed
 **     VMK_NO_SPACE:       Input buffer overflowed
 **     VMK_FAILURE:        Any other failure
 **
 */
VMK_ReturnStatus
sfvmk_vpdGetInfo(sfvmk_adapter_t *pAdapter, vmk_uint8 *pVpdData,
                 vmk_uint32 maxPayloadSize, vmk_uint8 vpdTag,
                 vmk_uint16 vpdKeyword, vmk_uint8 *pVpdLen)
{
  efx_nic_t        *pNic = pAdapter->pNic;
  void             *pVpdBuf;
  efx_vpd_value_t   vpd;
  size_t            size;
  VMK_ReturnStatus  status = VMK_FAILURE;

  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pVpdData);
  VMK_ASSERT_NOT_NULL(pVpdLen);

  if ((status = efx_vpd_size(pNic, &size)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Get VPD size failed with error %s",
                        vmk_StatusToString(status));
    goto end;
  }

  pVpdBuf = sfvmk_MemAlloc(size);
  if (!pVpdBuf) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_MemAlloc failed for pVpdBuf");
    status = VMK_NO_MEMORY;
    goto end;
  }

  if ((status = efx_vpd_read(pNic, pVpdBuf, size)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Get VPD read failed with error %s",
                        vmk_StatusToString(status));
    goto vpd_ops_failed;
  }

  if ((status = efx_vpd_verify(pNic, pVpdBuf, size)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Get VPD verify failed with error %s",
                        vmk_StatusToString(status));
    goto vpd_ops_failed;
  }

  memset(&vpd, 0, sizeof(vpd));
  vpd.evv_tag = vpdTag;
  vpd.evv_keyword = vpdKeyword;

  if ((status = efx_vpd_get(pNic, pVpdBuf, size, &vpd)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Get VPD get failed with error %s",
                        vmk_StatusToString(status));
    goto vpd_ops_failed;
  }

  if (vpd.evv_length > maxPayloadSize) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Get VPD, Exceeding user buffer sapce");
    status = VMK_NO_SPACE;
    goto vpd_ops_failed;
  }

  *pVpdLen = vpd.evv_length;

  memcpy(pVpdData, &vpd.evv_value[0], vpd.evv_length);

  status = VMK_OK;

vpd_ops_failed:
  sfvmk_MemFree(pVpdBuf);

end:
  return status;
}

/*! \brief Routine to set VPD information
 **
 ** \param[in]  pAdapter       Pointer to sfvmk_adapter_t
 ** \param[in]  pVpdData       VPD payload
 ** \param[in]  vpdTag         VPD Tag info
 ** \param[in]  vpdKeyword     VPD keyword info
 ** \param[in]  vpdLen         Length of data in pVpdData
 **
 ** \return: VMK_OK <success> error code <failure>
 **     Below error values values will be returned in
 **     case of failure,
 **     VMK_NO_MEMORY:      Memory allocation failed
 **     VMK_FAILURE:        Any other failure
 **
 */
VMK_ReturnStatus
sfvmk_vpdSetInfo(sfvmk_adapter_t *pAdapter, vmk_uint8 *pVpdData,
                 vmk_uint8 vpdTag, vmk_uint16 vpdKeyword,
                 vmk_uint8 vpdLen)
{
  efx_nic_t       *pNic = pAdapter->pNic;
  void            *pVpdBuf;
  efx_vpd_value_t  vpd;
  size_t           size;
  VMK_ReturnStatus status = VMK_FAILURE;

  VMK_ASSERT_NOT_NULL(pAdapter);
  VMK_ASSERT_NOT_NULL(pVpdData);

  if (vpdLen > (sizeof(vpd.evv_value))) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Set VPD: Invalid buffer size");
    status = VMK_FAILURE;
    goto end;
  }

  if ((status = efx_vpd_size(pNic, &size)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Get VPD size failed with error %s",
                        vmk_StatusToString(status));
    goto end;
  }

  pVpdBuf = sfvmk_MemAlloc(size);
  if (!pVpdBuf) {
    SFVMK_ADAPTER_ERROR(pAdapter, "sfvmk_MemAlloc failed for pVpdBuf");
    status = VMK_NO_MEMORY;
    goto end;
  }

  if ((status = efx_vpd_read(pNic, pVpdBuf, size)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Set VPD read failed with error %s",
                        vmk_StatusToString(status));
    goto vpd_ops_failed;
  }

  if ((status = efx_vpd_verify(pNic, pVpdBuf, size)) != VMK_OK) {
    if ((status = efx_vpd_reinit(pNic, pVpdBuf, size)) != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "Set VPD reinit failed with error %s",
                          vmk_StatusToString(status));
      goto vpd_ops_failed;
    }

    if ((status = efx_vpd_verify(pNic, pVpdBuf, size)) != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "Set VPD verify failed with error %s",
                          vmk_StatusToString(status));
      goto vpd_ops_failed;
    }
  }

  memset(&vpd, 0, sizeof(vpd));
  vpd.evv_tag = vpdTag;
  vpd.evv_keyword = vpdKeyword;
  vpd.evv_length = vpdLen;

  memcpy(&vpd.evv_value[0], pVpdData, vpdLen);

  if ((status = efx_vpd_set(pNic, pVpdBuf, size, &vpd)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Set VPD set failed with error %s",
                        vmk_StatusToString(status));
    goto vpd_ops_failed;
  }

  if ((status = efx_vpd_verify(pNic, pVpdBuf, size)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Set VPD verify failed with error %s",
                        vmk_StatusToString(status));
    goto vpd_ops_failed;
  }

  /* Write the VPD back to the hardware */
  if ((status = efx_vpd_write(pNic, pVpdBuf, size)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Set VPD write failed with error %s",
                        vmk_StatusToString(status));
    goto vpd_ops_failed;
  }

  status = VMK_OK;

vpd_ops_failed:
  sfvmk_MemFree(pVpdBuf);

end:
  return status;
}

