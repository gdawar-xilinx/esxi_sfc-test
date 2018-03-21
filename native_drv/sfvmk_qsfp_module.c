/*
 * Copyright (c) 2017, Solarflare Communications Inc.
 * All rights reserved.
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
*/

#include "sfvmk_driver.h"

/* SFP Module definitions defined in SFF-8024 */
#define SFVMK_EFX_PHY_MEDIA_INFO_DEV_ADDR_SFP_BASE                 0xA0

#define SFP_A0_IDENTIFIER                                             0
#define   SFP_A0_IDENTIFIER_SFP                                       3
#define SFP_A0_EXT_IDENTIFIER                                         1
#define   SFP_A0_EXT_IDENTIFIER_TWO_WIRE_ID                           4
#define SFP_A0_CONNECTOR                                              2
#define   SFP_A0_CONNECTOR_COPPER                                  0x21
#define SFP_A0_COMPLIANCE_10G_IB                                      3
#define   SFP_A0_COMPLIANCE_10GBASE_SR                                4
#define   SFP_A0_COMPLIANCE_10GBASE_LR                                5
#define   SFP_A0_COMPLIANCE_10GBASE_LRM                               6
#define   SFP_A0_COMPLIANCE_10GBASE_ER                                7
#define SFP_A0_COMPLIANCE_ETHERNET                                    6
#define   SFP_A0_COMPLIANCE_1000BASE_SX                               0
#define   SFP_A0_COMPLIANCE_1000BASE_LX                               1
#define   SFP_A0_COMPLIANCE_1000BASE_CX                               2
#define   SFP_A0_COMPLIANCE_1000BASE_T                                3
#define SFP_A0_FC_TX_TECH                                             8
#define   SFP_A0_FC_TX_TECH_COPPER_PASSIVE                            2
#define   SFP_A0_FC_TX_TECH_COPPER_ACTIVE                             3
#define SFP_A0_ACTIVE_CABLE_COMPLIANCE                               60
#define   SFP_A0_ACTIVE_CABLE_COMPLIANCE_SFF_8431_APX_E               0
#define   SFP_A0_ACTIVE_CABLE_COMPLIANCE_SFF_8431_LIMITING            2
#define SFP_A0_PASSIVE_CABLE_COMPLIANCE                              60
#define   SFP_A0_PASSIVE_CABLE_COMPLIANCE_SFF_8431_APX_E              0
#define SFP_A0_ACTIVE_CABLE_COMPLIANCE                               60
#define   SFP_A0_ACTIVE_CABLE_COMPLIANCE_SFF_8431_APX_E               0
#define   SFP_A0_ACTIVE_CABLE_COMPLIANCE_SFF_8431_LIMITING            2

#define SFP_MODULE_PAGE_0_SIZE                                      128

/* QSFP Module definitions defined in SFF-8024 */
#define SFVMK_EFX_PHY_MEDIA_INFO_DEV_ADDR_QSFP_BASE                 0xA0
#define QSFP_A0_HIGH_PAGE_START                                      128
#define QSFP_A0_LOW_PAGE_SIZE                                        128
#define QSFP_A0_HIGH_PAGE_SIZE                                       128

#define QSFP_A0_PAGE0_IDENTIFIER                                       0
#define   QSFP_A0_PAGE0_IDENTIFIER_UNKNOWN                          0x00
#define   QSFP_A0_PAGE0_IDENTIFIER_SFP                              0x03
#define   QSFP_A0_PAGE0_IDENTIFIER_QSFP                             0x0c
#define   QSFP_A0_PAGE0_IDENTIFIER_QSFP_PLUS                        0x0d
#define QSFP_A0_PAGE0_EXT_IDENTIFIER                                   1
#define QSFP_A0_PAGE0_CONNECTOR                                        2
#define   QSFP_A0_PAGE0_CONNECTOR_UNKNOWN                            0x0
#define   QSFP_A0_PAGE0_CONNECTOR_OPTICAL_PIGTAIL                    0xb
#define   QSFP_A0_PAGE0_CONNECTOR_MPO                                0xc
#define   QSFP_A0_PAGE0_CONNECTOR_COPPER_PIGTAIL                    0x21
#define QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE                           3
#define   QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_40G_ACTIVE_CABLE        0
#define   QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_40GBASE_LR4             1
#define   QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_40GBASE_SR4             2
#define   QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_40GBASE_CR4             3
#define   QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_10GBASE_SR              4
#define   QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_10GBASE_LR              5
#define   QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_10GBASE_LRM             6

#define QSFP_A0_PAGE0_DEVICE_TECH                                      19
#define   QSFP_A0_PAGE0_DEVICE_TECH_TX_TUNABLE_BYTE                     0
#define   QSFP_A0_PAGE0_DEVICE_TECH_TX_TUNABLE_LBN                      0
#define   QSFP_A0_PAGE0_DEVICE_TECH_TX_TUNABLE_WIDTH                    1
#define   QSFP_A0_PAGE0_DEVICE_TECH_APD_PIN_DETECT_BYTE                 0
#define   QSFP_A0_PAGE0_DEVICE_TECH_APD_PIN_DETECT_LBN                  1
#define   QSFP_A0_PAGE0_DEVICE_TECH_APD_PIN_DETECT_WIDTH                1
#define   QSFP_A0_PAGE0_DEVICE_TECH_TX_COOLED_BYTE                      0
#define   QSFP_A0_PAGE0_DEVICE_TECH_TX_COOLED_LBN                       2
#define   QSFP_A0_PAGE0_DEVICE_TECH_TX_COOLED_WIDTH                     1
#define   QSFP_A0_PAGE0_DEVICE_TECH_WAVELEN_CONTROL_BYTE                0
#define   QSFP_A0_PAGE0_DEVICE_TECH_WAVELEN_CONTROL_LBN                 3
#define   QSFP_A0_PAGE0_DEVICE_TECH_WAVELEN_CONTROL_WIDTH               1
#define   QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_BYTE                        0
#define   QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_LBN                         4
#define   QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_WIDTH                       4
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_850NM_VCSEL               0
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1310NM_VCSEL              1
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1550NM_VCSEL              2
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1310NM_FP                 3
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1310NM_DFB                4
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1550NM_DFB                5
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1310NM_EML                6
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1550NM_EML                7
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_OTHER                     8
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1490NM_DFB                9
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_COPPER_UNEQUALIZED        10
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_COPPER_PASSIVE_EQUALIZED  11
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_COPPER_NEAR_FAR_LIMITING  12
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_COPPER_FAR_LIMITING       13
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_COPPER_NEAR_LIMITING      14
#define     QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_COPPER_LINEAR             15

/*! \brief read qsfp module's eeprom data to get the cable type
**
** \param[in]  pAdapter   pointer to sfvmk_adapter_t
**
** \return: vmk_UplinkCableType
*/
vmk_UplinkCableType sfvmk_decodeQsfpCableType(sfvmk_adapter_t *pAdapter)
{
  vmk_uint8 qsfpData[QSFP_A0_LOW_PAGE_SIZE + QSFP_A0_HIGH_PAGE_SIZE];
  vmk_uint8 *pQsfpHighPageData;
  VMK_ReturnStatus status = VMK_FAILURE;

  /* Read low and high both pages */
  status = efx_phy_module_get_info(pAdapter->pNic,
                                   SFVMK_EFX_PHY_MEDIA_INFO_DEV_ADDR_QSFP_BASE,
				   0,
                                   (QSFP_A0_LOW_PAGE_SIZE + QSFP_A0_HIGH_PAGE_SIZE -1),
                                   &qsfpData[0]);
  if (status != VMK_OK) {
    return VMK_UPLINK_CABLE_TYPE_OTHER;
  }

  pQsfpHighPageData = qsfpData + QSFP_A0_HIGH_PAGE_SIZE;

  if ((pQsfpHighPageData[QSFP_A0_PAGE0_IDENTIFIER] !=
       QSFP_A0_PAGE0_IDENTIFIER_QSFP) &&
      (pQsfpHighPageData[QSFP_A0_PAGE0_IDENTIFIER] !=
       QSFP_A0_PAGE0_IDENTIFIER_QSFP_PLUS))
    return VMK_UPLINK_CABLE_TYPE_OTHER;

  switch (pQsfpHighPageData[QSFP_A0_PAGE0_DEVICE_TECH] >> 4) {
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_850NM_VCSEL:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1310NM_VCSEL:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1550NM_VCSEL:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1310NM_FP:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1310NM_DFB:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1550NM_DFB:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1310NM_EML:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1550NM_EML:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_1490NM_DFB:
      return VMK_UPLINK_CABLE_TYPE_FIBRE;

    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_COPPER_UNEQUALIZED:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_COPPER_PASSIVE_EQUALIZED:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_COPPER_NEAR_FAR_LIMITING:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_COPPER_FAR_LIMITING:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_COPPER_NEAR_LIMITING:
    case QSFP_A0_PAGE0_DEVICE_TECH_TX_TECH_COPPER_LINEAR:
      return VMK_UPLINK_CABLE_TYPE_DA;

    default:
      /* Cable type could not get decoded from DEVICE_TECH
       * data, read further data to decode it */
      break;
  }

  if (pQsfpHighPageData[QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE] &
      (1 << QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_40G_ACTIVE_CABLE))
    return VMK_UPLINK_CABLE_TYPE_DA;

  if (pQsfpHighPageData[QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE] &
      (1 << QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_40GBASE_CR4))
    return VMK_UPLINK_CABLE_TYPE_DA;

  if (pQsfpHighPageData[QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE] &
      ((1 << QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_40GBASE_LR4) |
       (1 << QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_40GBASE_SR4) |
       (1 << QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_10GBASE_LRM) |
       (1 << QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_10GBASE_LR) |
       (1 << QSFP_A0_PAGE0_10G_40G_ETH_COMPLIANCE_10GBASE_SR)))
    return VMK_UPLINK_CABLE_TYPE_FIBRE;

  switch (pQsfpHighPageData[QSFP_A0_PAGE0_CONNECTOR]) {
    case QSFP_A0_PAGE0_CONNECTOR_OPTICAL_PIGTAIL:
    case QSFP_A0_PAGE0_CONNECTOR_MPO:
      return VMK_UPLINK_CABLE_TYPE_FIBRE;

    case QSFP_A0_PAGE0_CONNECTOR_COPPER_PIGTAIL:
      return VMK_UPLINK_CABLE_TYPE_DA;
  }

  /* TBD: -Bug72898- Cable type could not get detected,
   * lookup into quirk table */

  return VMK_UPLINK_CABLE_TYPE_OTHER;
}

/*! \brief read sfp module's eeprom data to get the cable type
**
** \param[in]  pAdapter    pointer to sfvmk_adapter_t
**
** \return: vmk_UplinkCableType
*/
vmk_UplinkCableType sfvmk_decodeSfpCableType(sfvmk_adapter_t *pAdapter)
{
  vmk_uint8 sfpData[SFP_MODULE_PAGE_0_SIZE];
  VMK_ReturnStatus status = VMK_FAILURE;

  status = efx_phy_module_get_info(pAdapter->pNic,
                                   SFVMK_EFX_PHY_MEDIA_INFO_DEV_ADDR_SFP_BASE,
                                   0,
                                   SFP_MODULE_PAGE_0_SIZE, &sfpData[0]);
  if (status != VMK_OK) {
    return VMK_UPLINK_CABLE_TYPE_OTHER;
  }

  if (sfpData[SFP_A0_IDENTIFIER] != SFP_A0_IDENTIFIER_SFP)
    return VMK_UPLINK_CABLE_TYPE_OTHER;

  if (sfpData[SFP_A0_EXT_IDENTIFIER] != SFP_A0_EXT_IDENTIFIER_TWO_WIRE_ID)
    return VMK_UPLINK_CABLE_TYPE_OTHER;

  if (sfpData[SFP_A0_CONNECTOR] == SFP_A0_CONNECTOR_COPPER)
    return VMK_UPLINK_CABLE_TYPE_DA;

  if (sfpData[SFP_A0_FC_TX_TECH] &
      (1 << SFP_A0_FC_TX_TECH_COPPER_ACTIVE)) {
    if (sfpData[SFP_A0_ACTIVE_CABLE_COMPLIANCE] &
        (1 << SFP_A0_ACTIVE_CABLE_COMPLIANCE_SFF_8431_APX_E))
      return VMK_UPLINK_CABLE_TYPE_DA;
  }

  if (sfpData[SFP_A0_COMPLIANCE_10G_IB] &
      ((1 << SFP_A0_COMPLIANCE_10GBASE_ER) |
       (1 << SFP_A0_COMPLIANCE_10GBASE_LRM) |
       (1 << SFP_A0_COMPLIANCE_10GBASE_LR) |
       (1 << SFP_A0_COMPLIANCE_10GBASE_SR)))
    return VMK_UPLINK_CABLE_TYPE_FIBRE;

  if (sfpData[SFP_A0_FC_TX_TECH] &
      (1 << SFP_A0_FC_TX_TECH_COPPER_PASSIVE))
    return VMK_UPLINK_CABLE_TYPE_DA;

  if (sfpData[SFP_A0_COMPLIANCE_ETHERNET] &
      ((1 << SFP_A0_COMPLIANCE_1000BASE_SX) |
       (1 << SFP_A0_COMPLIANCE_1000BASE_LX)))
    return VMK_UPLINK_CABLE_TYPE_FIBRE;

  if (sfpData[SFP_A0_COMPLIANCE_ETHERNET] &
      (1 << SFP_A0_COMPLIANCE_1000BASE_CX))
    return VMK_UPLINK_CABLE_TYPE_DA;

  if (sfpData[SFP_A0_COMPLIANCE_ETHERNET] &
      (1 << SFP_A0_COMPLIANCE_1000BASE_T))
    return VMK_UPLINK_CABLE_TYPE_TP;

  /* TBD: Cable type could not get detected, lookup into quirk table */

  return VMK_UPLINK_CABLE_TYPE_OTHER;
}
