/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#ifndef __SFVMK_MCDI_H__
#define __SFVMK_MCDI_H__

enum sfvmk_mcdiState {
  SFVMK_MCDI_UNINITIALIZED = 0,
  SFVMK_MCDI_INITIALIZED,
  SFVMK_MCDI_BUSY,
  SFVMK_MCDI_COMPLETED
};

enum sfvmk_mcdiMode {
  SFVMK_MCDI_MODE_POLL,
  SFVMK_MCDI_MODE_EVENT,
};

typedef struct sfvmk_mcdi_s {
  efsys_mem_t           mem;
  enum sfvmk_mcdiState  state;
  enum sfvmk_mcdiMode   mode;
  efx_mcdi_transport_t  transport;
  vmk_Mutex             lock;
}sfvmk_mcdi_t;

/* functions */
void sfvmk_mcdiFini(struct sfvmk_adapter_s *pAdapter);
int sfvmk_mcdiInit(struct sfvmk_adapter_s *pAdapter);

#endif

