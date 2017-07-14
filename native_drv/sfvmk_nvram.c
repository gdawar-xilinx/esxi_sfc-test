#include<sfvmk_driver.h>
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
** \param[in]      size       size of the buffer
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
               vmk_uint32 size,
               vmk_uint32 start,
               efx_nvram_type_t type,
               boolean_t write)
{
  int (*op)(efx_nic_t *, efx_nvram_type_t, unsigned int, caddr_t, size_t);
  efx_nic_t *pNic = pAdapter->pNic;
  size_t chunk_size;
  vmk_uint32 off;
  int rc;

  if (!pNvramBuf) {
    rc = EINVAL;
    goto sfvmk_invalid_buf;
  }

  op = (write) ? efx_nvram_write_chunk : efx_nvram_read_chunk;

  if ((rc = efx_nvram_rw_start(pNic, type, &chunk_size)) != 0) {
    SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_FUNCTION,
              "NVRAM RW start failed");
    goto sfvmk_nvram_start_fail;
  }

  off = 0;
  while (size) {
    size_t len = MIN(chunk_size, size);
    if ((rc = op(pNic, type, start + off, (caddr_t)pNvramBuf, len)) != 0) {
      SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_FUNCTION,
                "NVRAM RW start failed");
      goto sfvmk_nvram_rw_fail;
    }

    size -= len;
    off += len;
  }

  efx_nvram_rw_finish(pNic, type, NULL);
  return VMK_OK;

sfvmk_nvram_rw_fail:
  efx_nvram_rw_finish(pNic, type, NULL);
sfvmk_nvram_start_fail:
sfvmk_invalid_buf:
  return rc;
}

/*! \brief Routine to erase NVRAM
**
**  \param[in] pAdapter pointer to sfvmk_adapter_t
**  \param[in] type     NVRAM type
**
** \return: VMK_OK <success> error code <failure>
**
*/
int
sfvmk_nvram_erase(sfvmk_adapter_t *pAdapter, efx_nvram_type_t type)
{
  efx_nic_t *pNic = pAdapter->pNic;
  size_t chunk_size;
  int rc;

  if ((rc = efx_nvram_rw_start(pNic, type, &chunk_size)) != 0) {
    SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_FUNCTION,
              "NVRAM RW start failed");
    goto sfvmk_nvram_fail;
  }

  if ((rc = efx_nvram_erase(pNic, type)) != 0) {
    SFVMK_DBG(pAdapter, SFVMK_DBG_UPLINK, SFVMK_LOG_LEVEL_FUNCTION,
              "NVRAM erase failed");
    goto sfvmk_nvram_erase_fail;
  }

  efx_nvram_rw_finish(pNic, type, NULL);
  return VMK_OK;

sfvmk_nvram_erase_fail:
  efx_nvram_rw_finish(pNic, type, NULL);
sfvmk_nvram_fail:
  return rc;
}

