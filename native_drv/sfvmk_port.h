/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
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
  void		*decodeBuf;
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

#endif

