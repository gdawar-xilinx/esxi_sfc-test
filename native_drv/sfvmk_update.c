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

static VMK_ReturnStatus sfvmk_getImageType(sfvmk_imageReflash_t type, efx_nvram_type_t *pNvramType)
{
  VMK_ReturnStatus      status = VMK_OK;

  switch (type) {
    case REFLASH_TARGET_PHY:
      *pNvramType = EFX_NVRAM_PHY;
    break;

    case REFLASH_TARGET_BOOTROM:
      *pNvramType = EFX_NVRAM_BOOTROM;
     break;

    case REFLASH_TARGET_MCFW:
      *pNvramType = EFX_NVRAM_MC_FIRMWARE;
    break;

    case REFLASH_TARGET_MCFW_BACKUP:
      *pNvramType = EFX_NVRAM_MC_GOLDEN;
    break;

    case REFLASH_TARGET_DISABLED_CALLISTO:
      *pNvramType = EFX_NVRAM_NULLPHY;
    break;

    case REFLASH_TARGET_FPGA:
      *pNvramType = EFX_NVRAM_FPGA;
    break;

    case REFLASH_TARGET_FPGA_BACKUP:
      *pNvramType = EFX_NVRAM_FPGA_BACKUP;
    break;

    case REFLASH_TARGET_UEFIROM:
      *pNvramType = EFX_NVRAM_UEFIROM;
    break;

    case REFLASH_TARGET_MUMFW:
      *pNvramType = EFX_NVRAM_MUM_FIRMWARE;
    break;

    default:
      SFVMK_ERROR("Unsupported Firmware : %d", type);
      status = VMK_NOT_SUPPORTED;
  }

  return status;
}

VMK_ReturnStatus sfvmk_performUpdate(sfvmk_imgUpdate_t *pImgUpdate,
                        sfvmk_adapter_t  *pAdapter)
{
  size_t                chunkSize;
  vmk_uint32            offset = 0;
  efx_nic_t             *pNic = NULL;
  vmk_uint8             *pBuffer = NULL;
  vmk_uint8             *pImgBuffer = NULL;
  vmk_uint8             *pUpdateBuffer = NULL;
  vmk_uint8             *pSignedImgBuffer = NULL;
  vmk_Bool              nvramLocked = VMK_FALSE;
  size_t                nvramSize;
  vmk_uint32            updateSize = 0;
  efx_nvram_type_t      type;
  efx_image_info_t      imageInfo;
  efx_image_header_t    *pImgHeader = NULL;
  VMK_ReturnStatus      status = VMK_FAILURE;

  VMK_ASSERT_NOT_NULL(pImgUpdate);
  VMK_ASSERT_NOT_NULL(pAdapter);

  pImgBuffer = (vmk_uint8 *)vmk_HeapAlloc(sfvmk_modInfo.heapID, pImgUpdate->size);
  if (pImgBuffer == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Memory Allocation failed for size: %d", pImgUpdate->size);
    status = VMK_NO_MEMORY;
    goto end;
  }

  if ((status =vmk_CopyFromUser((vmk_VA)pImgBuffer, (vmk_VA)pImgUpdate->pFileBuffer,
                                 pImgUpdate->size)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Copy from user failed with error: %s", vmk_StatusToString(status));
    goto fail1;
  }

  pNic = pAdapter->pNic;

  if ((status = efx_check_reflash_image(pImgBuffer, pImgUpdate->size, &imageInfo)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Image Checking Failed %s", vmk_StatusToString(status));
    goto fail1;
  }

  if ((status = sfvmk_getImageType(imageInfo.eii_headerp->eih_type, &type)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Image Type Failed %s", vmk_StatusToString(status));
    goto fail1;
  }

  if ((status = efx_nvram_size(pNic, type, &nvramSize)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Getting NVRAM Size failed with error: %s", vmk_StatusToString(status));
    goto fail1;
  }

  switch (imageInfo.eii_format) {
    case EFX_IMAGE_FORMAT_SIGNED:
      pSignedImgBuffer = (vmk_uint8*)vmk_HeapAlloc(sfvmk_modInfo.heapID, nvramSize);
      if (pSignedImgBuffer == NULL) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Memory Allocation failed for size: %d", (int)nvramSize);
        status = VMK_NO_MEMORY;
        goto fail1;
      }
      if ((status = efx_build_signed_image_write_buffer(pSignedImgBuffer,
                                                        nvramSize,
                                                        &imageInfo,
                                                        &pImgHeader)) != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Build Signed Image Write buffer failed with error: %s", vmk_StatusToString(status));
        goto fail1;
      }
      pUpdateBuffer = pSignedImgBuffer;
      updateSize = nvramSize;
      break;

    case EFX_IMAGE_FORMAT_UNSIGNED:
      pImgHeader = imageInfo.eii_headerp;
      updateSize = pImgUpdate->size - sizeof(efx_image_header_t);
      if (updateSize > nvramSize) {
        SFVMK_ADAPTER_ERROR(pAdapter, "Image Size greater than NVRAM size");
        goto fail1;
      }
      pUpdateBuffer = pImgBuffer + sizeof(efx_image_header_t);
      break;

    default:
      SFVMK_ADAPTER_ERROR(pAdapter, "Image type not identified");
      goto fail1;
      break;
  }

  if ((status = efx_nvram_rw_start(pNic, type, &chunkSize)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM RW start failed with err %s",
              vmk_StatusToString(status));
    goto fail1;
  }
  nvramLocked = VMK_TRUE;

  pBuffer = vmk_HeapAlloc(sfvmk_modInfo.heapID, chunkSize);
  if (!pBuffer) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Couldn't allocate memory chunk (size = %d) for Image Update", (int)chunkSize);
    goto fail2;
  }

  if ((status = efx_nvram_erase(pNic, type)) != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM RW erase failed with err %s",
              vmk_StatusToString(status));
    goto fail3;
  }

  for (offset = 0; offset < updateSize; offset += chunkSize) {
    if (offset + chunkSize > updateSize)
      chunkSize = updateSize - offset;

    /* Read existing content (if an A/B partition, from its writable store)*/
    status = efx_nvram_read_backup(pNic, type, offset, pBuffer, chunkSize);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM Read Failed with err %s",
                vmk_StatusToString(status));
      goto fail3;
    }

    if (vmk_Memcmp(pBuffer, pUpdateBuffer + offset, chunkSize) != 0) {
      status = efx_nvram_write_chunk(pNic, type, offset, pUpdateBuffer + offset, chunkSize);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM Write Failed with err %s",
                  vmk_StatusToString(status));
        goto fail3;
      }

      status = efx_nvram_read_backup(pNic, type, offset, pBuffer, chunkSize);
      if (status != VMK_OK) {
        SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM Read Failed with err %s",
                  vmk_StatusToString(status));
        goto fail3;
      }

      if (vmk_Memcmp(pBuffer, pUpdateBuffer + offset, chunkSize) != 0) {
        SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM Image update Mismatch");
        status = VMK_FAILURE;
        goto fail3;
      }
    }
  }

  status = efx_nvram_rw_finish(pNic, type, NULL);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NVRAM RW Finish Failed with err %s",
              vmk_StatusToString(status));
    goto fail3;
  }
  nvramLocked = VMK_FALSE;

  status = efx_nvram_set_version(pNic, type, &pImgHeader->eih_code_version_a);
  if (status != VMK_OK) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Image Version Update Failed with err %s",
              vmk_StatusToString(status));
    goto fail3;
  }

  if (type == EFX_NVRAM_MC_FIRMWARE) {
    status = efx_mcdi_reboot(pNic);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "MC Reboot Failed with err %s",
                vmk_StatusToString(status));
      goto fail3;
    }
    status = sfvmk_worldSleep(100000);
    if (status != VMK_OK) {
      SFVMK_ADAPTER_ERROR(pAdapter, "World Sleep Failed with err %s",
                vmk_StatusToString(status));
      goto fail3;
    }
    sfvmk_updateDrvInfo(pAdapter);
  }

fail3:
  vmk_HeapFree(sfvmk_modInfo.heapID, pBuffer);

fail2:
  if (nvramLocked == VMK_TRUE)
    efx_nvram_rw_finish(pNic, type, NULL);

fail1:
  if( pSignedImgBuffer != NULL)
    vmk_HeapFree(sfvmk_modInfo.heapID, pSignedImgBuffer);
  vmk_HeapFree(sfvmk_modInfo.heapID, pImgBuffer);

end:
  return status;
}
