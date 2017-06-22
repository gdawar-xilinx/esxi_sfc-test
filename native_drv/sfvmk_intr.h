/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/
#ifndef __SFVMK_INTR_H__
#define __SFVMK_INTR_H__

/* interrupt related DS */

/* MAX MSIX supported */
#define SFVMK_MAX_MSIX_VECTORS        EFX_MAXRSS

enum sfvmk_intrState {
  SFVMK_INTR_UNINITIALIZED = 0,
  SFVMK_INTR_INITIALIZED,
  SFVMK_INTR_TESTING,
  SFVMK_INTR_STARTED
};

typedef struct sfvmk_intr_s {
  enum sfvmk_intrState  state;
  vmk_IntrCookie        intrCookies[SFVMK_MAX_MSIX_VECTORS] ;
  int                   numIntrAlloc;
  efx_intr_type_t       type;
}sfvmk_intr_t;

/* functions */
VMK_ReturnStatus sfvmk_intrStart(struct sfvmk_adapter_s *pAdapter);
VMK_ReturnStatus sfvmk_intrStop(struct sfvmk_adapter_s * pAdapter);
VMK_ReturnStatus sfvmk_intrInit(struct sfvmk_adapter_s *pAdapter);
void sfvmk_freeInterrupts(struct sfvmk_adapter_s *pAdapter);

#endif /* __SFVMK_RX_H__ */

