/*-
 * Copyright (c) 2010-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * This software was developed in part by Philip Paeps under contract for
 * Solarflare Communications, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */


#include "efx.h"
#include "efx_mcdi.h"
#include "efx_regs_mcdi.h"
#include "efsys.h"
#include "sfvmk.h"
#include "sfvmk_driver.h"


#define	SFVMK_MCDI_MAX_PAYLOAD 0x400

#define	SFVMK_MCDI_POLL_INTERVAL_MIN 10		/* 10us in 1us units */
#define	SFVMK_MCDI_POLL_INTERVAL_MAX 100000	/* 100ms in 1us units */
#define	SFVMK_MCDI_WATCHDOG_INTERVAL 10000000	/* 10s in 1us units */


#define SFVMK_MCDI_LOCK(mcdi)   \
      {                         \
            vmk_MutexLock(mcdi->lock);\
      }
          

#define SFVMK_MCDI_UNLOCK(mcdi)   \
      {                         \
            vmk_MutexUnlock(mcdi->lock);\
      }
          
void *
sfvmk_AllocCoherentDMAMapping(sfvmk_adapter *adapter, // IN:  adapter
                                vmk_uint32 size,          // IN:  size
                                vmk_IOA *ioAddr)  ;        // OUT: IO address


static void
sfvmk_mcdi_timeout(sfvmk_adapter *sfAdapter)
{
	//vmk_Device dev = sfAdapter->vmkDevice;

// praveen logging error
	//log(LOG_WARNING, "[%s%d] MC_TIMEOUT", device_get_name(dev),
	//	device_get_unit(dev));

	EFSYS_PROBE(mcdi_timeout);

	//praveen  call reset functinality will do it later
	//sfxge_schedule_reset(sc);
}

static void
sfvmk_mcdi_poll(sfvmk_adapter *sfAdapter)
{
	efx_nic_t *enp;
	// praveen
	vmk_uint32 delay_total;
	vmk_uint32 delay_us;
	boolean_t aborted;

	delay_total = 0;
	delay_us = SFVMK_MCDI_POLL_INTERVAL_MIN;
	enp = sfAdapter->enp;

	do {
		if (efx_mcdi_request_poll(enp)) {
			EFSYS_PROBE1(mcdi_delay, vmk_uint32, delay_total);
			return;
		}

		if (delay_total > SFVMK_MCDI_WATCHDOG_INTERVAL) {
			aborted = efx_mcdi_request_abort(enp);
			//KASSERT(aborted, ("abort failed"));
			sfvmk_mcdi_timeout(sfAdapter);
			return;
		}

		/* Spin or block depending on delay interval. */
		if (delay_us < 1000000)
		  vmk_DelayUsecs(delay_us);
		//else
		//	pause("mcdi wait", delay_us * hz / 1000000);

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
	sfvmk_adapter *sfAdapter;
	struct sfvmk_mcdi *mcdi;

	sfAdapter = (sfvmk_adapter *)arg;
	mcdi = &sfAdapter->mcdi;


	SFVMK_MCDI_LOCK(mcdi);
 
	VMK_ASSERT(mcdi->state == SFVMK_MCDI_INITIALIZED);

	/* Issue request and poll for completion. */
	efx_mcdi_request_start(sfAdapter->enp, emrp, B_FALSE);
	sfvmk_mcdi_poll(sfAdapter);

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
	sfvmk_adapter *sfAdapter;
	vmk_Device dev;

	sfAdapter= (sfvmk_adapter *)arg;
	dev = sfAdapter->vmkDevice;

  //praveen logging mechanism
     /*
	log(LOG_WARNING, "[%s%d] MC_%s", device_get_name(dev),
	    device_get_unit(dev),
	    (eme == EFX_MCDI_EXCEPTION_MC_REBOOT)
	    ? "REBOOT"
	    : (eme == EFX_MCDI_EXCEPTION_MC_BADASSERT)
	    ? "BADASSERT" : "UNKNOWN");
*/
	EFSYS_PROBE(mcdi_exception);

  // praveen needs reset functinality
	//sfxge_schedule_reset(sc);
}

int
sfvmk_mcdi_init(sfvmk_adapter *sfAdapter)
{
	efx_nic_t *enp;
	struct sfvmk_mcdi *mcdi;
	efx_mcdi_transport_t *emtp;
	efsys_mem_t *esmp;
	int max_msg_size;
	int rc = -1;
        VMK_ReturnStatus status = VMK_OK;

	enp = sfAdapter->enp;
	mcdi = &sfAdapter->mcdi;
	emtp = &mcdi->transport;
	esmp = &mcdi->mem;
	max_msg_size = sizeof (uint32_t) + MCDI_CTL_SDU_LEN_MAX_V2;


	VMK_ASSERT(mcdi->state == SFVMK_MCDI_UNINITIALIZED);


        status = sfvmk_MutexInit("mcdi" ,VMK_MUTEX_RANK_HIGHEST, &mcdi->lock);
        if (status != VMK_OK)
        {
           goto lock_create_fail;
        }
	mcdi->state = SFVMK_MCDI_INITIALIZED;

        //praveen 
        esmp->esm_base = sfvmk_AllocCoherentDMAMapping(sfAdapter, max_msg_size,&esmp->io_elem.ioAddr);
        esmp->io_elem.length = max_msg_size;
  	esmp->esm_handle = sfAdapter->vmkDmaEngine; 
        if (NULL== esmp->esm_base)
        {
          goto mem_alloc_fail; 
        }

	emtp->emt_context = sfAdapter;
	emtp->emt_dma_mem = esmp;
	emtp->emt_execute = sfvmk_mcdi_execute;
	emtp->emt_ev_cpl = sfvmk_mcdi_ev_cpl;
	emtp->emt_exception = sfvmk_mcdi_exception;

	/*
#if EFSYS_OPT_MCDI_LOGGING
	emtp->emt_logger = sfxge_mcdi_logger;
	SYSCTL_ADD_INT(device_get_sysctl_ctx(sc->dev),
		       SYSCTL_CHILDREN(device_get_sysctl_tree(sc->dev)),
		       OID_AUTO, "mcdi_logging", CTLFLAG_RW,
		       &sc->mcdi_logging, 0,
		       "MCDI logging");
#endif
*/
	if ((rc = efx_mcdi_init(enp, emtp)) != 0)
		goto fail;

	return rc;

fail:
  //praveen mem freee routine 

mem_alloc_fail:
        sfvmk_MutexDestroy(mcdi->lock);
lock_create_fail:
	mcdi->state = SFVMK_MCDI_UNINITIALIZED;
	return (rc);
}

void
sfxge_mcdi_fini(sfvmk_adapter *sfAdapter)
{
	struct sfvmk_mcdi *mcdi;
	efx_nic_t *enp;
	efx_mcdi_transport_t *emtp;
	efsys_mem_t *esmp;

	enp = sfAdapter->enp;
	mcdi = &sfAdapter->mcdi;
	emtp = &mcdi->transport;
	esmp = &mcdi->mem;

	SFVMK_MCDI_LOCK(mcdi);
	VMK_ASSERT(mcdi->state == SFVMK_MCDI_INITIALIZED);

	efx_mcdi_fini(enp);
	vmk_Memset(emtp, 0 , sizeof(*emtp));

	SFVMK_MCDI_UNLOCK(mcdi);

  // praveen
	//sfxge_dma_free(esmp);

 // lock destroy

        sfvmk_MutexDestroy(mcdi->lock);
}

