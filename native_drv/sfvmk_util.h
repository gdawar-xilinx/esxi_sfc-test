
#ifndef __SFVMK_UTIL_H__
#define __SFVMK_UTIL_H__

#include "sfvmk_driver.h"

void *sfvmk_memPoolAlloc(vmk_uint64 size);
void
sfvmk_memPoolFree(void *va,          // IN
                 vmk_uint64 size);   // IN
void *
sfvmk_AllocCoherentDMAMapping(sfvmk_adapter *adapter, // IN:  adapter
                                vmk_uint32 size,          // IN:  size
                                vmk_IOA *ioAddr)  ;        // OUT: IO address

void
sfvmk_FreeCoherentDMAMapping(sfvmk_adapter *adapter,  // IN: adapter
                               void *va,                  // IN: virtual address
                               vmk_IOA ioAddr,            // IN: IO address
                               vmk_uint32 size);           // IN: size

VMK_ReturnStatus
sfvmk_setup_Interrupts(sfvmk_adapter *adapter);

#endif
