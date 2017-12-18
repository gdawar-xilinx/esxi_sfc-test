/*************************************************************************
 * Copyright (c) 2017 Solarflare Communications Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * -- Solarflare Confidential
 ************************************************************************/

#include "sfvmk_driver.h"

/*! \brief  Allocate resource for port module.
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
**
** \return: VMK_OK <success> Error code <failure>
*/
VMK_ReturnStatus
sfvmk_portInit(sfvmk_adapter_t *pAdapter)
{
  sfvmk_port_t *pPort;
  VMK_ReturnStatus status = VMK_FAILURE;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter error");
    status = VMK_BAD_PARAM;
    goto done;
  }

  pPort = &pAdapter->port;
  if (pPort->state != SFVMK_PORT_STATE_UNINITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Port is already initialized");
    goto done;
  }

  pPort->state = SFVMK_PORT_STATE_INITIALIZED;
  status = VMK_OK;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);

  return status;
}

/*! \brief  Releases resource for port module.
**
** \param[in]  pAdapter     pointer to sfvmk_adapter_t
**
** \return: void
*/
void
sfvmk_portFini(sfvmk_adapter_t *pAdapter)
{
  sfvmk_port_t *pPort;

  SFVMK_ADAPTER_DEBUG_FUNC_ENTRY(pAdapter, SFVMK_DEBUG_PORT);

  if (pAdapter == NULL) {
    SFVMK_ADAPTER_ERROR(pAdapter, "NULL adapter error");
    goto done;
  }

  pPort = &pAdapter->port;
  if (pPort->state != SFVMK_PORT_STATE_INITIALIZED) {
    SFVMK_ADAPTER_ERROR(pAdapter, "Port is not initialized");
    goto done;
  }

  pPort->state = SFVMK_PORT_STATE_UNINITIALIZED;

done:
  SFVMK_ADAPTER_DEBUG_FUNC_EXIT(pAdapter, SFVMK_DEBUG_PORT);
}

