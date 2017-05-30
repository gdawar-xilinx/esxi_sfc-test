
/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#ifndef __SFVMK_PORT_H__
#define __SFVMK_PORT_H__

#if 0
struct sfvmk_hw_stats {
	//clock_t			update_time;
	efsys_mem_t		dma_buf;
	void			*decode_buf;
};

typedef struct sfvmk_port {
  struct sfvmk_adapter  *sc;
 // struct mtx    lock;
  enum sfvmk_port_state init_state;
#ifndef SFXGE_HAVE_PAUSE_MEDIAOPTS
  unsigned int    wanted_fc;
#endif
  struct sfvmk_hw_stats mac_stats;
  //efx_link_mode_t   link_mode;
  //uint8_t     mcast_addrs[EFX_MAC_MULTICAST_LIST_MAX *
  //            EFX_MAC_ADDR_LEN];
  unsigned int    mcast_count;

  /* Only used in debugging output */
//  char      lock_name[SFVMK_LOCK_NAME_MAX];
}sfvmk_port;
#endif
int sfvmk_PortInit(sfvmk_adapter *adapter);
void sfvmk_PortFini(sfvmk_adapter *adapter);
int sfvmk_MacStatsUpdate(sfvmk_adapter *adapter);

#endif
