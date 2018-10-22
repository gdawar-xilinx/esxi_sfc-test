/*
 * Copyright (c) 2018, Solarflare Communications Inc.
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

/*! \brief Routine to perform read operation on NVRAM.
**
** \param[in]      pAdapter   pointer to sfvmk_adapter_t
** \param[in]      type       type of NVRAM
** \param[out]     pNvramBuf  pointer to data buffer
** \param[in,out]  pBufSize   size of the buffer also return
**                            actaul size of data read from
**                            NVRAM
** \param[in]      start      starting location for read
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_nvramRead(sfvmk_adapter_t *pAdapter,
                efx_nvram_type_t type,
                vmk_uint8 *pNvramBuf,
                vmk_uint32 *pBufSize, vmk_uint32 start)
{
  efx_nic_t        *pNic;
  uint32_t         numBytes = 0;
  size_t           chunkSize = 0;
  vmk_uint32       off = 0;
  VMK_ReturnStatus status;

  if (!pAdapter || !pNvramBuf || !pBufSize) {
    SFVMK_ADAPTER_ERROR(pAdapter,"Invalid function parameters");
    status = VMK_BAD_PARAM;
    goto end;
  }

  numBytes = *pBufSize;
  pNic = pAdapter->pNic;

  if ((status = efx_nvram_rw_start(pNic, type, &chunkSize)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM (0x%x) rw start failed with err %s",
                        type, vmk_StatusToString(status));
    goto end;
  }

  off = 0;

  while (numBytes) {
    size_t len = MIN(chunkSize, numBytes);
    if ((status = efx_nvram_read_chunk(pNic, type, start + off,
                                       (caddr_t)pNvramBuf, len)) != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM (0x%x) read failed with err %s at "
                          "offset %u remaining len %u",
                          type, vmk_StatusToString(status), off, numBytes);
      goto finish_rw;
    }

    numBytes -= len;
    off += len;
    pNvramBuf += len;
  }

  status = VMK_OK;

finish_rw:
  efx_nvram_rw_finish(pNic, type, NULL);

  /* Update actual data read size */
  *pBufSize = off;

end:
  return status;
}

/*! \brief Routine to perform write operation on NVRAM.
**
** \param[in]      pAdapter    pointer to sfvmk_adapter_t
** \param[in]      type        type of NVRAM
** \param[in]      pNvramBuf   pointer to data buffer
** \param[in,out]  pBufSize    size of the buffer also return
**                             actaul size of data written into
                               NVRAM
** \param[in]      eraseNvram  NVARAM erase flag
**
** \return: VMK_OK [success] error code [failure]
**
*/
VMK_ReturnStatus
sfvmk_nvramWriteAll(sfvmk_adapter_t *pAdapter,
                    efx_nvram_type_t type,
                    vmk_uint8 *pNvramBuf,
                    vmk_uint32 *pBufSize,
                    vmk_Bool eraseNvram)
{
  efx_nic_t        *pNic;
  vmk_uint32       numBytes = 0;
  size_t           chunkSize = 0;
  vmk_uint32       off = 0;
  VMK_ReturnStatus status;

  if (!pAdapter || !pNvramBuf || !pBufSize) {
    SFVMK_ADAPTER_ERROR(pAdapter,"Invalid function parameters");
    status = VMK_BAD_PARAM;
    goto end;
  }

  numBytes = *pBufSize;
  pNic = pAdapter->pNic;

  if ((status = efx_nvram_rw_start(pNic, type, &chunkSize)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM (0x%x) rw start failed with err %s",
                        type, vmk_StatusToString(status));
    goto end;
  }

  if (eraseNvram) {
    if ((status = efx_nvram_erase(pNic, type)) != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM erase failed with err %s",
                          vmk_StatusToString(status));
      goto finish_rw;
    }
  }

  off = 0;
  while (numBytes) {
    size_t len = MIN(chunkSize, numBytes);
    if ((status = efx_nvram_write_chunk(pNic, type, off,
                                        (caddr_t)pNvramBuf, len)) != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM (0x%x) write failed with err %s at "
                          "offset %u remaining len %u",
                          type, vmk_StatusToString(status), off, (vmk_uint32)numBytes);
      goto finish_rw;
    }

    numBytes -= len;
    off += len;
    pNvramBuf += len;
  }

  status = VMK_OK;

finish_rw:
  efx_nvram_rw_finish(pNic, type, NULL);

  /* Update actual length written */
  *pBufSize = off;

end:
  return status;
}
