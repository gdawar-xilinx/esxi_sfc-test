/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk.h"
#include "efsys.h"

#ifdef SFVMK_WITH_UNIT_TESTS

/* Test variable for memory allocation, only for UT */
static char *sfvmk_ut_mem_ptr = NULL;

int
sfvmk_ut_mem_alloc(vmk_ByteCount size)
{
  char *unused_ptr = NULL;

  vmk_LogMessage("\n\n Allocate %d bytes memory from heap \n ", (int)size);

  EFSYS_KMEM_ALLOC(unused_ptr, size, sfvmk_ut_mem_ptr);

  if(sfvmk_ut_mem_ptr == NULL) {
    vmk_LogMessage("\n Memory allocation : Failed . \n ");
    return 1;
  }
  else {
    vmk_LogMessage("\n Memory allocated. \n ");
    return 0;
  }
}

void
sfvmk_ut_mem_free(void)
{
  vmk_LogMessage("\n De-allocate heap memory /n ");
  EFSYS_KMEM_FREE(NULL, 0, sfvmk_ut_mem_ptr);
  sfvmk_ut_mem_ptr = NULL;
  vmk_LogMessage("\n Heap memory De-allocated /n ");
  vmk_LogMessage("\n sfvmk_ut_mem_ptr After EFSYS_KMEM_FREE ... %p", sfvmk_ut_mem_ptr);
}

unsigned int
sfvmk_run_ut(void)
{
  VMK_ReturnStatus status = 0;
  uint32_t start, end, test_num=0;
  uint32_t ts_delay = 5000;
  int arg1, arg2, arg3, arg4, arg5, arg6, arg7;
  efsys_lock_t esl;
  efsys_lock_t *eslp = &esl;
  int lock_state=0;


  /* 1. Memory Allocation */
  vmk_LogMessage(" #### Test : %d Memory Allocation #### ", ++test_num);
  status = sfvmk_ut_mem_alloc(1024);
  if (status == VMK_OK) {
    vmk_LogMessage(" @@@@ sfvmk_ut_mem_alloc : Pass @@@@");
  } else {
    vmk_LogMessage(" @@@@ sfvmk_ut_mem_alloc : Failed with staus: 0x%x @@@@ ", status);
  }

  /* 2. Memory de-allocation */
  vmk_LogMessage(" #### Test : %d Memory De-allocation #### ", ++test_num);
  sfvmk_ut_mem_free();


  /* 3. Timestamp */
  start = 0;
  end = 0;
  vmk_LogMessage(" @@@@ Test : %d Time Stamp @@@@ ", ++test_num);
  EFSYS_TIMESTAMP(&start);
  EFSYS_TIMESTAMP(&end);

  if(end == start) {
    vmk_LogMessage(" @@@@ Test : TimeStamp (w/o delay) : Pass @@@@ ");
    vmk_LogMessage("EFSYS_TIMESTAMP : start : %d", start);
    vmk_LogMessage("EFSYS_TIMESTAMP : end : %d", end);
  } else
    vmk_LogMessage(" @@@@ Test : TimeStamp (w/o delay) : Fail @@@@ " );

  /* 4. Timestamp and delay */
  start = 0;
  end = 0;
  vmk_LogMessage(" @@@@ Test : %d Time Stamp (with delay) @@@@ ", ++test_num);
  EFSYS_TIMESTAMP(&start);
  EFSYS_SPIN(ts_delay);
  EFSYS_TIMESTAMP(&end);
  if(end == start + ts_delay) {
    vmk_LogMessage(" @@@@ Test : TimeStamp (w/ delay) : Pass @@@@ ");
    vmk_LogMessage("EFSYS_TIMESTAMP : start : %d", start);
    vmk_LogMessage("EFSYS_TIMESTAMP : end : %d", end);
  } else
    vmk_LogMessage(" @@@@ Test : TimeStamp (w/ delay) : Fail @@@@ " );

  /* 6. Lock Creation,loc,unlock,lock destriy  : all these are being  done from attach callback  */


  /* 5. Lock Creation */
  vmk_LogMessage(" @@@@ Test : %d Mutex Create @@@@ ", ++test_num);

  status = sfvmk_CreateLock( "testlock", 0x1, &(esl.lock));
  if (status == VMK_OK) {
    vmk_LogMessage("sfvmk_CreateLock : Pass");
  } else {
    vmk_LogMessage("sfvmk_CreateLock : Failed with staus: 0x%x ", status);
  }
   /* Lock */
  EFSYS_LOCK(eslp, lock_state);

  /* UnLock */
  EFSYS_UNLOCK(eslp, lock_state);

  /* Lock Destroy  */
  if(esl.lock)
    sfvmk_DestroyLock(esl.lock);


  /* 7. Probe  */
  vmk_LogMessage(" @@@@ Test : %d EFSYS_PROBE<N> @@@@ ", ++test_num);

  EFSYS_PROBE(probe_ut);
  arg1=1;
  EFSYS_PROBE1(probe_ut1, int, arg1);
  arg2=2;
  EFSYS_PROBE2(probe_ut2, int, arg1, int, arg2);
  arg3=3;
  EFSYS_PROBE3(probe_ut3, int, arg1, int, arg2, int, arg3);
  arg4=4;
  EFSYS_PROBE4(probe_ut4, int, arg1, int, arg2, int, arg3, int, arg4);
  arg5=5;
  EFSYS_PROBE5(probe_ut5, int, arg1, int, arg2, int, arg3, int, arg4, int, arg5);
  arg6=6;
  EFSYS_PROBE6(probe_ut6, int, arg1, int, arg2, int, arg3, int, arg4, int, arg5, int, arg6);
  arg7=7;
  EFSYS_PROBE7(probe_ut7, int, arg1, int, arg2, int, arg3, int, arg4, int, arg5, int, arg6, int, arg7);

  return 0;

}

#endif



