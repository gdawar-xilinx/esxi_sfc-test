/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#ifndef __SFVMK_VPD_H__
#define __SFVMK_VPD_H__

int
sfvmk_vpdGetInfo(sfvmk_adapter_t *pAdapter, vmk_uint8 *vpdData,
                 vmk_uint32 maxPayloadSize, vmk_uint8 vpdTag,
                 vmk_uint16 vpdKeyword, vmk_uint8 *vpdLen);

int
sfvmk_vpdSetInfo(sfvmk_adapter_t *pAdapter, vmk_uint8 *vpdData,
                 vmk_uint8 vpdTag, vmk_uint16 vpdKeyword,
                 vmk_uint8 vpdLen);
#endif
