/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"
#include "sfvmk_nvram.h"

/*! \brief Routine to perform read or write op on NVRAM.
**
** \param[in]      pAdapter   pointer to sfvmk_adapter_t
** \param[in/out]  pNvramBuf  read from or write into
** \param[in/out]  pbufSize   size of the buffer
** \param[in]      start      Starting location for read/write op
** \param[in]      type       Type of NVRAM
** \param[in]      write      Read/Write
**
** \return: VMK_OK <success> error code <failure>
**
*/
int
sfvmk_nvram_rw(sfvmk_adapter_t *pAdapter,
               vmk_uint8 *pNvramBuf,
               vmk_uint32 *pBufSize,
               vmk_uint32 start,
               efx_nvram_type_t type,
               boolean_t write)
{
  int       (*op)(efx_nic_t *, efx_nvram_type_t, unsigned int, caddr_t, size_t);
  efx_nic_t  *pNic;
  uint32_t    size;
  size_t      chunkSize;
  vmk_uint32  off;
  int         rc;

  if (!pAdapter || !pNvramBuf || !pBufSize) {
    rc = VMK_BAD_PARAM;
    goto end;
  }

  size = *pBufSize;
  pNic = pAdapter->pNic;
  op = (write) ? efx_nvram_write_chunk : efx_nvram_read_chunk;

  if ((rc = efx_nvram_rw_start(pNic, type, &chunkSize)) != 0) {
    SFVMK_ERR(pAdapter, "NVRAM RW start failed with err %s",
              vmk_StatusToString(rc));
    goto end;
  }

  off = 0;
  while (size) {
    size_t len = MIN(chunkSize, size);
    if ((rc = op(pNic, type, start + off, (caddr_t)pNvramBuf, len)) != 0) {
      SFVMK_ERR(pAdapter, "NVRAM Read/Write failed with err %s",
                vmk_StatusToString(rc));
      efx_nvram_rw_finish(pNic, type, NULL);
      goto end;
    }

    size -= len;
    off += len;
  }

  efx_nvram_rw_finish(pNic, type, NULL);

  /* Update actual size of data read/write */
  *pBufSize = off;
  return VMK_OK;

end:
  return rc;
}

/*! \brief Routine to erase NVRAM
**
** \param[in] pAdapter pointer to sfvmk_adapter_t
** \param[in] type     NVRAM type
**
** \return: VMK_OK <success> error code <failure>
**
*/
int
sfvmk_nvram_erase(sfvmk_adapter_t *pAdapter, efx_nvram_type_t type)
{
  efx_nic_t *pNic;
  size_t     chunkSize;
  int        rc;

  if (!pAdapter) {
    rc = VMK_BAD_PARAM;
    goto end;
  }

  pNic = pAdapter->pNic;

  if ((rc = efx_nvram_rw_start(pNic, type, &chunkSize)) != 0) {
    SFVMK_ERR(pAdapter, "NVRAM RW start failed with err %s",
              vmk_StatusToString(rc));
    goto end;
  }

  if ((rc = efx_nvram_erase(pNic, type)) != 0) {
    SFVMK_ERR(pAdapter, "NVRAM erase failed with err %s",
              vmk_StatusToString(rc));
    efx_nvram_rw_finish(pNic, type, NULL);
    goto end;
  }

  efx_nvram_rw_finish(pNic, type, NULL);
  return VMK_OK;

end:
  return rc;
}

