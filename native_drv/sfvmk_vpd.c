/*
 * Copyright (c) 2017, Solarflare Communications Inc.
 * All rights reserved.
 *  
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "sfvmk_driver.h"
#include "sfvmk_vpd.h"


/*! \brief Routine to get VPD information
 **
 ** \param[in]  pAdapter       Adapter pointer to sfvmk_adapter_t
 ** \param[out] vpdData        VPD payload
 ** \param[in]  maxPayloadSize Maximum size of vpdData
 ** \param[in]  vpdTag         VPD Tag info
 ** \param[in]  vpdKeyword     VPD keyword info
 ** \param[out] vpdLen         Length of data copied into vpdData
 **
 ** \return: VMK_OK <success> error code <failure>
 **
 */
int
sfvmk_vpdGetInfo(sfvmk_adapter_t *pAdapter, vmk_uint8 *vpdData,
                 vmk_uint32 maxPayloadSize, vmk_uint8 vpdTag,
                 vmk_uint16 vpdKeyword, vmk_uint8 *vpdLen)
{
  efx_nic_t        *pNic = pAdapter->pNic;
  void             *pVpdBuf;
  efx_vpd_value_t   vpd;
  size_t            size;
  int               rc;

  memset(&vpd, 0, sizeof(vpd));

  if ((rc = efx_vpd_size(pNic, &size)) != VMK_OK) {
    SFVMK_ERR(pAdapter, "Get VPD size failed with error %s",
              vmk_StatusToString(rc));
    goto end;
  }

  pVpdBuf = sfvmk_MemAlloc(size);
  if (!pVpdBuf) {
    rc = VMK_NO_MEMORY;
    goto end;
  }

  if ((rc = efx_vpd_read(pNic, pVpdBuf, size)) != VMK_OK) {
    SFVMK_ERR(pAdapter, "Get VPD read failed with error %s",
              vmk_StatusToString(rc));
    goto vpd_ops_failed;
  }

  if ((rc = efx_vpd_verify(pNic, pVpdBuf, size)) != VMK_OK) {
    SFVMK_ERR(pAdapter, "Get VPD varify failed with error %s",
              vmk_StatusToString(rc));
    goto vpd_ops_failed;
  }

  vpd.evv_tag = vpdTag;
  vpd.evv_keyword = vpdKeyword;

  if ((rc = efx_vpd_get(pNic, pVpdBuf, size, &vpd)) != VMK_OK) {
    SFVMK_ERR(pAdapter, "Get VPD get failed with error %s",
              vmk_StatusToString(rc));
    goto vpd_ops_failed;
  }

  if (vpd.evv_length > maxPayloadSize) {
    SFVMK_ERR(pAdapter, "Get VPD, Exceeding user buffer sapce");
    rc = VMK_NO_SPACE;
    goto vpd_ops_failed;
  }

  *vpdLen = vpd.evv_length;

  memcpy(vpdData, &vpd.evv_value[0], vpd.evv_length);

  sfvmk_MemFree(pVpdBuf);
  return VMK_OK;

vpd_ops_failed:
  sfvmk_MemFree(pVpdBuf);
end:
  return rc;
}

/*! \brief Routine to Set VPD information
 **
 ** \param[in]  pAdapter       Adapter pointer to sfvmk_adapter_t
 ** \param[in]  vpdData        VPD payload
 ** \param[in]  vpdTag         VPD Tag info
 ** \param[in]  vpdKeyword     VPD keyword info
 ** \param[in]  vpdLen         Length of data in vpdData
 **
 ** \return: VMK_OK <success> error code <failure>
 **
 */
int
sfvmk_vpdSetInfo(sfvmk_adapter_t *pAdapter, vmk_uint8 *vpdData,
                 vmk_uint8 vpdTag, vmk_uint16 vpdKeyword,
                 vmk_uint8 vpdLen)
{
  efx_nic_t       *pNic = pAdapter->pNic;
  void            *pVpdBuf;
  efx_vpd_value_t  vpd;
  size_t           size;
  int              rc;

  memset(&vpd, 0, sizeof(vpd));

  if (vpdLen > (sizeof(vpd.evv_value))) {
    SFVMK_ERR(pAdapter, "Set VPD,Invalid buffer size");
    rc = VMK_FAILURE;
    goto end;
  }

  /* restriction on writable tags is in efx_vpd_hunk_set() */
  if ((rc = efx_vpd_size(pNic, &size)) != VMK_OK) {
    SFVMK_ERR(pAdapter, "Get VPD size failed with error %s",
              vmk_StatusToString(rc));
    goto end;
  }

  pVpdBuf = sfvmk_MemAlloc(size);
  if (!pVpdBuf) {
    rc = VMK_NO_MEMORY;
    goto end;
  }

  if ((rc = efx_vpd_read(pNic, pVpdBuf, size)) != VMK_OK) {
    SFVMK_ERR(pAdapter, "Set VPD read failed with error %s",
              vmk_StatusToString(rc));
    goto vpd_ops_failed;
  }

  if ((rc = efx_vpd_verify(pNic, pVpdBuf, size)) != VMK_OK) {
    if ((rc = efx_vpd_reinit(pNic, pVpdBuf, size)) != VMK_OK) {
	  SFVMK_ERR(pAdapter, "Set VPD reinit failed with error %s",
                vmk_StatusToString(rc));
      goto vpd_ops_failed;
    }

    if ((rc = efx_vpd_verify(pNic, pVpdBuf, size)) != VMK_OK) {
	  SFVMK_ERR(pAdapter, "Set VPD varify failed with error %s",
                vmk_StatusToString(rc));
      goto vpd_ops_failed;
    }
  }

  vpd.evv_tag = vpdTag;
  vpd.evv_keyword = vpdKeyword;
  vpd.evv_length = vpdLen;

  memcpy(&vpd.evv_value[0], vpdData, vpdLen);

  if ((rc = efx_vpd_set(pNic, pVpdBuf, size, &vpd)) != VMK_OK) {
    SFVMK_ERR(pAdapter, "Set VPD set failed with error %s",
              vmk_StatusToString(rc));
    goto vpd_ops_failed;
  }

  if ((rc = efx_vpd_verify(pNic, pVpdBuf, size)) != VMK_OK) {
    SFVMK_ERR(pAdapter, "Set VPD varify failed with error %s",
              vmk_StatusToString(rc));
    goto vpd_ops_failed;
  }

  /* And write the VPD back to the hardware */
  if ((rc = efx_vpd_write(pNic, pVpdBuf, size)) != VMK_OK) {
    SFVMK_ERR(pAdapter, "Set VPD write failed with error %s",
              vmk_StatusToString(rc));
    goto vpd_ops_failed;
  }

  sfvmk_MemFree(pVpdBuf);
  return VMK_OK;

vpd_ops_failed:
  sfvmk_MemFree(pVpdBuf);
end:
  return rc;
}

