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

#ifndef __SFVMK_PORT_H__
#define __SFVMK_PORT_H__

#include "sfvmk.h"

/* lock apis */
#define SFVMK_PORT_LOCK(pPort) {  \
  vmk_MutexLock(pPort->lock);     \
}

#define SFVMK_PORT_UNLOCK(pPort) {  \
  vmk_MutexUnlock(pPort->lock);     \
}

#define SFVMK_LINK_UP(pPort) ((pPort->linkMode != EFX_LINK_DOWN) && \
                               (pPort->linkMode != EFX_LINK_UNKNOWN))


typedef struct sfvmk_phyInfo_s {

   vmk_uint8 linkStatus;
   vmk_uint16 linkSpeed;
   vmk_uint16 phyType;
   efx_phy_media_type_t interfaceType;
   vmk_uint32 miscParams;
   vmk_uint32 advertising;
   vmk_uint32 supported;
   vmk_UplinkCableType cableType;
   vmk_UplinkTransceiverType transceiver;
   vmk_Bool asyncEvt;

}sfvmk_phyInfo_t;

enum sfvmk_port_state {
  SFVMK_PORT_UNINITIALIZED = 0,
  SFVMK_PORT_INITIALIZED,
  SFVMK_PORT_STARTED
};

typedef struct sfvmk_hw_stats_s {
  efsys_mem_t	dmaBuf;
  void		*pDecodeBuf;
}sfvmk_hw_stats_t;

typedef struct sfvmk_port_s {

  struct sfvmk_adapter_s  *pAdapter;
  enum sfvmk_port_state   initState;
  sfvmk_hw_stats_t        macStats;
  efx_link_mode_t         linkMode;
  vmk_uint32    fcRequested;
  vmk_Mutex     lock;
  vmk_uint8     mcastAddrs[EFX_MAC_MULTICAST_LIST_MAX * EFX_MAC_ADDR_LEN];
  vmk_uint32    mcastCount;

}sfvmk_port_t;

void sfvmk_portStop(struct sfvmk_adapter_s *pAdapter);
int sfvmk_portStart(struct sfvmk_adapter_s *pAdapter);

void sfvmk_macLinkUpdate(struct sfvmk_adapter_s *pAdapter,
                                    efx_link_mode_t linkMode);
VMK_ReturnStatus sfvmk_macStatsUpdate(struct sfvmk_adapter_s *pAdapter);
VMK_ReturnStatus sfvmk_portInit(struct sfvmk_adapter_s *pAdapter);
void sfvmk_portFini(struct sfvmk_adapter_s *pAdapter);
VMK_ReturnStatus sfvmk_phyLinkSpeedSet(struct sfvmk_adapter_s *pAdapter, vmk_uint32 speed);
VMK_ReturnStatus sfvmk_phyLinkSpeedGet(struct sfvmk_adapter_s *pAdapter,
                                       vmk_uint32 *pSpeed, vmk_Bool *pAutoNeg);
VMK_ReturnStatus sfvmk_linkStateGet(struct sfvmk_adapter_s *pAdapter, vmk_Bool *pLinkState);

#endif

