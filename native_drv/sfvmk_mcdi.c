
/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"
#include "sfvmk_utils.h"
#include "efx_mcdi.h"
#include "efx_regs_mcdi.h"



#define SFVMK_MCDI_MAX_PAYLOAD 0x400

#define SFVMK_MCDI_POLL_INTERVAL_MIN 10   /* 10us in 1us units */
#define SFVMK_MCDI_POLL_INTERVAL_MAX 100000 /* 100ms in 1us units */
#define SFVMK_MCDI_WATCHDOG_INTERVAL 10000000 /* 10s in 1us units */


#define SFVMK_MCDI_LOCK(mcdi)     vmk_MutexLock(mcdi->lock);
#define SFVMK_MCDI_UNLOCK(mcdi)   vmk_MutexUnlock(mcdi->lock);


static void sfvmk_mcdi_timeout(sfvmk_adapter *adapter);
static void sfvmk_mcdi_poll(sfvmk_adapter *sfAdapter);



static void
sfvmk_mcdi_timeout(sfvmk_adapter *adapter)
{
  SFVMK_ERR(adapter, " MC_TIMEOUT");

  EFSYS_PROBE(mcdi_timeout);

  //praveen  call reset functinality will do it later
  //sfxge_schedule_reset(sc);
}

static void
sfvmk_mcdi_poll(sfvmk_adapter *adapter)
{
  efx_nic_t *enp;
  vmk_uint32 delay_total;
  vmk_uint32 delay_us;
  boolean_t aborted;

  delay_total = 0;
  delay_us = SFVMK_MCDI_POLL_INTERVAL_MIN;
  enp = adapter->enp;

  do {
    if (efx_mcdi_request_poll(enp)) {
      EFSYS_PROBE1(mcdi_delay, vmk_uint32, delay_total);
      return;
    }

    if (delay_total > SFVMK_MCDI_WATCHDOG_INTERVAL) {
      aborted = efx_mcdi_request_abort(enp);
      VMK_ASSERT(aborted);
      sfvmk_mcdi_timeout(adapter);
      return;
    }


  vmk_DelayUsecs(delay_us);
  delay_total += delay_us;

  /* Exponentially back off the poll frequency. */
  delay_us = delay_us * 2;
  if (delay_us > SFVMK_MCDI_POLL_INTERVAL_MAX)
  delay_us = SFVMK_MCDI_POLL_INTERVAL_MAX;

  } while (1);
}

static void
sfvmk_mcdi_execute(void *arg, efx_mcdi_req_t *emrp)
{
  sfvmk_adapter *adapter;
  struct sfvmk_mcdi *mcdi;

  adapter = (sfvmk_adapter *)arg;
  mcdi = &adapter->mcdi;


  SFVMK_MCDI_LOCK(mcdi);

  VMK_ASSERT(mcdi->state == SFVMK_MCDI_INITIALIZED);

  /* Issue request and poll for completion. */
  efx_mcdi_request_start(adapter->enp, emrp, B_FALSE);
  sfvmk_mcdi_poll(adapter);

  SFVMK_MCDI_UNLOCK(mcdi);
}

static void
sfvmk_mcdi_ev_cpl(void *arg)
{

  sfvmk_adapter *sfAdapter;
  struct sfvmk_mcdi *mcdi;

  sfAdapter= (sfvmk_adapter *)arg;
  mcdi = &sfAdapter->mcdi;

  VMK_ASSERT(mcdi->state == SFVMK_MCDI_INITIALIZED);

}

static void
sfvmk_mcdi_exception(void *arg, efx_mcdi_exception_t eme)
{
  sfvmk_adapter *adapter;
  vmk_Device dev;

  adapter= (sfvmk_adapter *)arg;
  dev = adapter->vmkDevice;

  SFVMK_ERR(adapter, "MC_%s", (eme == EFX_MCDI_EXCEPTION_MC_REBOOT)
            ? "REBOOT": (eme == EFX_MCDI_EXCEPTION_MC_BADASSERT)
            ? "BADASSERT" : "UNKNOWN");

  EFSYS_PROBE(mcdi_exception);

  // praveen needs reset functinality
  //sfxge_schedule_reset(sc);
}

int
sfvmk_mcdi_init(sfvmk_adapter *adapter)
{
  efx_nic_t *enp;
  struct sfvmk_mcdi *mcdi;
  efx_mcdi_transport_t *emtp;
  efsys_mem_t *esmp;
  int max_msg_size;
  int rc = -1;
  VMK_ReturnStatus status = VMK_OK;

  enp = adapter->enp;
  mcdi = &adapter->mcdi;
  emtp = &mcdi->transport;
  esmp = &mcdi->mem;
  max_msg_size = sizeof (uint32_t) + MCDI_CTL_SDU_LEN_MAX_V2;


  VMK_ASSERT(mcdi->state == SFVMK_MCDI_UNINITIALIZED);


  status = sfvmk_MutexInit("mcdi" ,VMK_MUTEX_RANK_HIGHEST, &mcdi->lock);
  if (status != VMK_OK)
    goto lock_create_fail;

  mcdi->state = SFVMK_MCDI_INITIALIZED;

  esmp->esm_base = sfvmk_AllocCoherentDMAMapping(adapter, max_msg_size,
                                                &esmp->io_elem.ioAddr);
  esmp->io_elem.length = max_msg_size;
  esmp->esm_handle = adapter->vmkDmaEngine;

  if (NULL== esmp->esm_base)
    goto sfvmk_mem_alloc_fail;

  emtp->emt_context = adapter;
  emtp->emt_dma_mem = esmp;
  emtp->emt_execute = sfvmk_mcdi_execute;
  emtp->emt_ev_cpl = sfvmk_mcdi_ev_cpl;
  emtp->emt_exception = sfvmk_mcdi_exception;

  if ((rc = efx_mcdi_init(enp, emtp)) != 0)
    goto sfvmk_mcdi_init_fail;
 
  vmk_LogMessage("initialized MCDI\n");
  return rc;

sfvmk_mcdi_init_fail:
  sfvmk_FreeCoherentDMAMapping(adapter, esmp->esm_base , esmp->io_elem.ioAddr,
                                esmp->io_elem.length);
sfvmk_mem_alloc_fail:
  sfvmk_MutexDestroy(mcdi->lock);
lock_create_fail:
  mcdi->state = SFVMK_MCDI_UNINITIALIZED;

  return (rc);
}

void
sfvmk_mcdi_fini(sfvmk_adapter *adapter)
{
  struct sfvmk_mcdi *mcdi;
  efx_nic_t *enp;
  efx_mcdi_transport_t *emtp;
  efsys_mem_t *esmp;

  enp = adapter->enp;
  mcdi = &adapter->mcdi;
  emtp = &mcdi->transport;
  esmp = &mcdi->mem;

  SFVMK_MCDI_LOCK(mcdi);
  VMK_ASSERT(mcdi->state == SFVMK_MCDI_INITIALIZED);

  efx_mcdi_fini(enp);
  vmk_Memset(emtp, 0 , sizeof(*emtp));

  SFVMK_MCDI_UNLOCK(mcdi);

  // praveen
  sfvmk_FreeCoherentDMAMapping(adapter, esmp->esm_base , esmp->io_elem.ioAddr,
  esmp->io_elem.length);

  sfvmk_MutexDestroy(mcdi->lock);
}


