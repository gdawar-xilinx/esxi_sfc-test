/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#ifndef __SFVMK_NVRAM_H__
#define __SFVMK_NVRAM_H__

int sfvmk_nvram_rw(sfvmk_adapter_t *pAdapter,
                   vmk_uint8 *pNvramBuf,
                   vmk_uint32 *pBufSize,
                   vmk_uint32 start,
                   efx_nvram_type_t type,
                   boolean_t write);

int sfvmk_nvram_erase(sfvmk_adapter_t *pAdapter, efx_nvram_type_t type);

#endif

