/***************************************************************************//**
 * @file
 * @brief Application interface provided to main().
 *******************************************************************************
 * # License
 * <b>Copyright 2022 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/

#ifndef APP_H
#define APP_H

//#define LOG_ENABLED

#include "ble_peer_manager_common.h"

// initiator content
#include "cs_antenna.h"
#include "cs_result.h"
#include "cs_initiator.h"
#include "cs_initiator_client.h"
#include "cs_initiator_config.h"
#include "cs_initiator_display_core.h"
#include "cs_initiator_display.h"

// Enums, structs, typedef

// Measurement structure
typedef struct {
  float distance_filtered;
  float distance_raw;
  float likeliness;
  float distance_estimate_rssi;
  float velocity;
  float bit_error_rate;
} cs_measurement_data_t;

// CS initiator instance
typedef struct {
  uint8_t conn_handle;
  uint32_t measurement_cnt;
  uint32_t ranging_counter;
  cs_measurement_data_t measurement_mainmode;
  cs_measurement_data_t measurement_submode;
  cs_intermediate_result_t measurement_progress;
  bool measurement_arrived;
  bool measurement_progress_changed;
  bool read_remote_capabilities;
  uint8_t number_of_measurements;
} cs_initiator_instances_t;

#define BOARD_EVB

/**************************************************************************//**
 * Peer manager event handler.
 * @param[in] event peer manager event
 *****************************************************************************/
void ble_peer_manager_on_event_initiator(ble_peer_manager_evt_type_t *event);

#endif // APP_H
