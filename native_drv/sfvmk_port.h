
/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#ifndef __SFVMK_PORT_H__
#define __SFVMK_PORT_H__

int sfvmk_PortInit(sfvmk_adapter *adapter);
void sfvmk_PortFini(sfvmk_adapter *adapter);
int sfvmk_MacStatsUpdate(sfvmk_adapter *adapter);
VMK_ReturnStatus sfvmk_MacStatsLock(sfvmk_adapter *adapter);
void sfvmk_MacStatsUnlock(sfvmk_adapter *adapter);

#endif
