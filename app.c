/***************************************************************************//**
 * @file
 * @brief CS Initiator example application logic
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
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
// -----------------------------------------------------------------------------
// Includes
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "sl_bluetooth.h"
#include "sl_component_catalog.h"
#include "app_assert.h"

// app content
#include "sl_main_init.h"
#include "app.h"
#include "trace.h"
#include "app_config.h"
#include "app_timer.h"

// initiator content
#include "cs_antenna.h"
#include "cs_result.h"
#include "cs_initiator.h"
#include "cs_initiator_client.h"
#include "cs_initiator_config.h"
#include "cs_initiator_display_core.h"
#include "cs_initiator_display.h"

#include "em_gpio.h"

// RAS
#include "cs_ras_client.h"

// other required content
#include "ble_peer_manager_common.h"
#include "ble_peer_manager_connections.h"
#include "ble_peer_manager_central.h"
#include "ble_peer_manager_filter.h"

#ifdef SL_CATALOG_CS_INITIATOR_CLI_PRESENT
#include "cs_initiator_cli.h"
#endif // SL_CATALOG_CS_INITIATOR_CLI_PRESENT

#ifdef SL_CATALOG_SIMPLE_BUTTON_PRESENT
#include "sl_simple_button.h"
#include "sl_simple_button_instances.h"
#endif // SL_CATALOG_SIMPLE_BUTTON_PRESENT

// -----------------------------------------------------------------------------
// Macros

#define MAX_PERCENTAGE                   100u
#define NL                               APP_LOG_NL
#define APP_PREFIX                       "[APP] "
#define INSTANCE_PREFIX                  "[%u] "
#define APP_INSTANCE_PREFIX              APP_PREFIX INSTANCE_PREFIX
#define BT_ADDR_LEN                      sizeof(bd_addr)
#define DISPLAY_REFRESH_RATE             1000u // ms
#define ABS(x)                           ((x < 0) ? ((-1) * x) : x)

// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Static function declarations

static uint8_t get_algo_mode(void);
static const char *antenna_usage_to_str(const cs_initiator_config_t *config);
static const char *algo_mode_to_str(uint8_t algo_mode);
static void cs_on_result(const uint8_t conn_handle,
                         const uint16_t ranging_counter,
                         const uint8_t *result,
                         const cs_result_session_data_t *result_data,
                         const cs_ranging_data_t *ranging_data,
                         const void *user_data);
static void cs_on_intermediate_result(const cs_intermediate_result_t *intermediate_result,
                                      const void *user_data);
static void cs_on_error(uint8_t conn_handle,
                        cs_error_event_t err_evt,
                        sl_status_t sc);
static sl_status_t get_instance_number(uint8_t conn_handle, uint8_t *instance_num);
static void check_cli_values(void);
static sl_status_t create_new_initiator_instance(uint8_t conn_handle);
static void delete_initiator_instance(uint8_t conn_handle);
static void app_timer_callback(app_timer_t *timer, void *data);

// -----------------------------------------------------------------------------
// Static variables

static bool antenna_set_pbr = false;
static bool antenna_set_rtt = false;
static cs_initiator_config_t initiator_config = INITIATOR_CONFIG_DEFAULT;
static rtl_config_t rtl_config = RTL_CONFIG_DEFAULT;
static uint8_t num_reflector_connections = 0u;
static cs_initiator_instances_t cs_initiator_instances[CS_INITIATOR_MAX_CONNECTIONS];
static app_timer_t display_timer;

/******************************************************************************
 * Application Init
 *****************************************************************************/
void app_init(void)
{
  sl_status_t sc = SL_STATUS_OK;

  trace_init();

  // initialize initiator instances
  for (uint32_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    cs_initiator_instances[i].conn_handle = SL_BT_INVALID_CONNECTION_HANDLE;
    cs_initiator_instances[i].measurement_cnt = 0u;
    cs_initiator_instances[i].ranging_counter = 0u;
    memset(&cs_initiator_instances[i].measurement_mainmode, 0u, sizeof(cs_measurement_data_t));
    memset(&cs_initiator_instances[i].measurement_submode, 0u, sizeof(cs_measurement_data_t));
    memset(&cs_initiator_instances[i].measurement_progress, 0u, sizeof(cs_intermediate_result_t));
    cs_initiator_instances[i].measurement_arrived = false;
    cs_initiator_instances[i].measurement_progress_changed = false;
    cs_initiator_instances[i].read_remote_capabilities = false;
    cs_initiator_instances[i].number_of_measurements = 0u;
  }

  // Set configuration parameters
  rtl_config.algo_mode = get_algo_mode();
  cs_initiator_apply_channel_map_preset(initiator_config.channel_map_preset,
                                        initiator_config.channel_map.data);

  if ((initiator_config.cs_main_mode == sl_bt_cs_mode_pbr)
      && (initiator_config.cs_sub_mode == sl_bt_cs_mode_rtt)) {
    // Set mode and submode. Currently, only main mode = pbr and submode = rtt is supported
    initiator_config.min_main_mode_steps = CS_INITIATOR_MIXED_MODE_MAIN_MODE_STEPS;
    initiator_config.max_main_mode_steps = CS_INITIATOR_MIXED_MODE_MAIN_MODE_STEPS;
    initiator_config.channel_map_preset = CS_CHANNEL_MAP_PRESET_HIGH;
    app_log_info(APP_PREFIX "Channel map preset set to high" APP_LOG_NL);
  }

  // Log configuration parameters
  log_info("+-[CS initiator by Silicon Labs]--------------------------+" NL);
  log_info("+---------------------------------------------------------+" NL);
  if (initiator_config.procedure_scheduling != CS_PROCEDURE_SCHEDULING_CUSTOM) {
    log_info(APP_PREFIX "Using %s based procedure scheduling." NL,
             initiator_config.procedure_scheduling == CS_PROCEDURE_SCHEDULING_OPTIMIZED_FOR_FREQUENCY
             ? "frequency update" : "energy consumption");
  } else {
    log_info(APP_PREFIX "Using custom procedure scheduling." NL);
  }
  log_info(APP_PREFIX "%s" NL,
           (initiator_config.max_procedure_count == 0) ? "Free running." : "Start new procedure after one finished.");
  log_info(APP_PREFIX "Antenna offset: wire%s" NL,
           CS_INITIATOR_ANTENNA_OFFSET ? "d" : "less");
  log_info(APP_PREFIX "Default CS procedure interval: %u" NL, initiator_config.min_procedure_interval);
  log_info(APP_PREFIX "CS main mode: %s (%u)" NL,
           (initiator_config.cs_main_mode == sl_bt_cs_mode_pbr) ? "PBR" : "RTT",
           initiator_config.cs_main_mode);
  log_info(APP_PREFIX "CS sub mode: %s (%u)" NL,
           (initiator_config.cs_sub_mode == sl_bt_cs_submode_disabled) ? "Disabled" : "RTT",
           initiator_config.cs_sub_mode);
  log_info(APP_PREFIX "Requested antenna usage: %s" NL, antenna_usage_to_str(&initiator_config));
  log_info(APP_PREFIX "Object tracking mode: %s" NL, algo_mode_to_str(rtl_config.algo_mode));
  log_info(APP_PREFIX "CS channel map preset: %d" NL, initiator_config.channel_map_preset);
  log_info(APP_PREFIX "CS channel map: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X" NL,
           initiator_config.channel_map.data[0],
           initiator_config.channel_map.data[1],
           initiator_config.channel_map.data[2],
           initiator_config.channel_map.data[3],
           initiator_config.channel_map.data[4],
           initiator_config.channel_map.data[5],
           initiator_config.channel_map.data[6],
           initiator_config.channel_map.data[7],
           initiator_config.channel_map.data[8],
           initiator_config.channel_map.data[9]);
  log_info(APP_PREFIX "RSSI reference TX power @ 1m: %d dBm" NL,
           (int)initiator_config.rssi_ref_tx_power);
  log_info("+-------------------------------------------------------+" NL);

  sc = cs_initiator_display_init();
  app_assert_status_f(sc, "cs_initiator_display_init failed");
  cs_initiator_display_set_measurement_mode(initiator_config.cs_main_mode, rtl_config.algo_mode);
  app_timer_start(&display_timer, DISPLAY_REFRESH_RATE, app_timer_callback, NULL, true);

  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application init code here!                         //
  // This is called once during start-up.                                    //
  /////////////////////////////////////////////////////////////////////////////


  GPIO_PinModeSet(gpioPortD, 2, gpioModePushPull, 0);
  GPIO_PinModeSet(gpioPortA, 4, gpioModePushPull, 0);
}
enum gate_state_e
{
    DOOR_CLOSED,
    DOOR_OPENNED,
};

enum reflector_state_e
{
    JUST_CONNECTED,
    MOVING,
    MOVING_AWAY,
    MOVING_CLOSER,
    RED_ZONE,
};

enum gate_command_state_e
{
  RELAY_POSITION_0,
  RELAY_POSITION_1,
  RELAY_POSITION_2,
};

volatile enum reflector_state_e reflector_state[CS_INITIATOR_MAX_CONNECTIONS];
volatile uint32_t baseline[CS_INITIATOR_MAX_CONNECTIONS];
volatile uint32_t pdistance[CS_INITIATOR_MAX_CONNECTIONS];
volatile enum gate_state_e gate_state = DOOR_CLOSED;

//#define GATE_AUTO_CLOSE_MODE

#define NEW_VALUE_WEIGHT 5
#define DISTANCE_RED_ZONE 1000
#define DISTANCE_OPENING_ZONE 40000
#define MOVING_THRESHOLD 500

uint8_t volatile control_disable = 0;

sl_status_t status;
sl_sleeptimer_timer_handle_t my_timer;
uint32_t timer_timeout = 900000;
uint32_t relay_delay = (500 * 32768)/1000;

enum gate_command_state_e relay_state = RELAY_POSITION_0;

void my_timer_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  gate_state = DOOR_CLOSED;
}

void open_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  relay_sequence(1);
}

void close_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  relay_sequence(0);
}

/* Asynchronous relay command */
void relay_sequence(uint8_t open)
{
  /* relay sequence */
  printf("rs : %d\n", relay_state);
  switch (relay_state)
  {
    case RELAY_POSITION_0:
      if (open)
        GPIO_PinModeSet(gpioPortD, 2, gpioModePushPull, 0);
      else
        GPIO_PinModeSet(gpioPortA, 4, gpioModePushPull, 0);
      relay_state = RELAY_POSITION_1;
      break;
    case RELAY_POSITION_1:
      if (open)
        GPIO_PinModeSet(gpioPortD, 2, gpioModePushPull, 1);
      else
        GPIO_PinModeSet(gpioPortA, 4, gpioModePushPull, 1);
      relay_state = RELAY_POSITION_2;
      break;
    case RELAY_POSITION_2:
      if (open)
        GPIO_PinModeSet(gpioPortD, 2, gpioModePushPull, 0);
      else
        GPIO_PinModeSet(gpioPortA, 4, gpioModePushPull, 0);
      relay_state = RELAY_POSITION_0;
      return;
  }

  if (open)
    status = sl_sleeptimer_start_timer(&my_timer, relay_delay, open_callback, (void *)NULL, 0, 0);
  else
    status = sl_sleeptimer_start_timer(&my_timer, relay_delay, close_callback, (void *)NULL, 0, 0);
}

void try_open_gate(uint8_t index, uint32_t distance)
{
  switch (gate_state)
  {
    case DOOR_OPENNED:
     /* Door already opened, nothing to do */
      return;
    default:
      break;
  }

  if (relay_state != RELAY_POSITION_0)
  /* An action is already on-going on relays */
    return;

  for (uint8_t i = 0; i < CS_INITIATOR_MAX_CONNECTIONS; i++)
  {
    if (reflector_state[i] == RED_ZONE)
    /* At least one reflector is in risk area */
      return;
  }

  if (distance > DISTANCE_OPENING_ZONE)
  /* Just a security not to open the door too soon */
    return;

  if (distance >= baseline[index])
  /* We are moving away */
    return;

  if ((baseline[index] - distance) < MOVING_THRESHOLD)
  /* We are moving closer */
    return;

  gate_state = DOOR_OPENNED;
  relay_sequence(1);

#ifdef GATE_AUTO_CLOSE_MODE
  /* Gate will auto close after user timeout*/
  status = sl_sleeptimer_start_timer(&my_timer,timer_timeout, my_timer_callback, (void *)NULL, 0, 0);
#endif

  printf("OPENNING\n");
}

void try_close_gate(uint8_t index, uint32_t distance)
{

  switch (gate_state)
  {
    case DOOR_CLOSED:
     /* Door already opened, nothing to do */
      return;
    default:
      break;
  }

  if (relay_state != RELAY_POSITION_0)
  /* An action is already on-going on relays */
    return;

  for (uint8_t i = 0; i < CS_INITIATOR_MAX_CONNECTIONS; i++)
  {
    if (reflector_state[i] == RED_ZONE)
    /* At least one reflector is in risk area */
      return;
  }

  if (distance < baseline[index])
  /* We are moving closer */
    return;


  if ((distance - baseline[index]) < MOVING_THRESHOLD)
  /* We are moving closer */
    return;

  gate_state = DOOR_CLOSED;
  relay_sequence(0);

  printf("CLOSING\n");
}

void process_measure(uint8_t index)
{
  cs_initiator_instances_t * initiator = &cs_initiator_instances[index];
  uint32_t distance = (uint32_t)(initiator->measurement_mainmode.distance_filtered * 1000.f);

  switch (reflector_state[index])
  {
    case JUST_CONNECTED:
      baseline[index] = distance;
      pdistance[index] = distance;
      break;
    default:
      distance = (distance + pdistance[index])/2;
      break;
  }

  printf("d: %d, b: %d\n", distance, baseline[index]);
 // printf("s: %d \n", reflector_state[index]);

  switch (reflector_state[index])
  {
    case JUST_CONNECTED:
      reflector_state[index] = MOVING;
      break;
    case MOVING:
      /* This state is used to determine direction of mouvement */
      if (distance > baseline[index])
      /* We are moving away */
        reflector_state[index] = MOVING_AWAY;
      else if (distance < baseline[index])
      /* We are moving closer */
        reflector_state[index] = MOVING_CLOSER;
      break;
    case MOVING_CLOSER:
      if (distance <= DISTANCE_RED_ZONE)
      /* Reflector enters red zone */
         reflector_state[index] = RED_ZONE;
      else if (distance > baseline[index])
      /* We are moving away */
        reflector_state[index] = MOVING;
      try_open_gate(index, distance);
      break;
    case MOVING_AWAY:
      if (distance < baseline[index])
      /* We are moving closer */
        reflector_state[index] = MOVING;
      try_close_gate(index, distance);
      break;
    case RED_ZONE:
      if (distance > DISTANCE_RED_ZONE)
      /* We are leaving the red zone */
        reflector_state[index] = MOVING_AWAY;
      break;

  }

  baseline[index] = distance * NEW_VALUE_WEIGHT + baseline[index] * (100 - NEW_VALUE_WEIGHT);
  baseline[index] /= 100;
}

/******************************************************************************
 * Application Process Action
 *****************************************************************************/
void app_process_action(void)
{
  for (uint8_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    if (cs_initiator_instances[i].measurement_arrived) {
      // write results to the display & to the iostream
      cs_initiator_instances[i].measurement_arrived = false;
      process_measure(i);
/*
      log_info(APP_INSTANCE_PREFIX "# %04lu --- Ranging Counter = %04lu" NL,
               cs_initiator_instances[i].conn_handle,
               cs_initiator_instances[i].measurement_cnt,
               cs_initiator_instances[i].ranging_counter);

      const bd_addr *bt_address = ble_peer_manager_get_bt_address(cs_initiator_instances[i].conn_handle);
      log_info(APP_INSTANCE_PREFIX "BT Address: %02X:%02X:%02X:%02X:%02X:%02X" NL,
               cs_initiator_instances[i].conn_handle,
               bt_address->addr[5],
               bt_address->addr[4],
               bt_address->addr[3],
               bt_address->addr[2],
               bt_address->addr[1],
               bt_address->addr[0]);

      log_info(APP_INSTANCE_PREFIX "Measurement main mode result: %lu mm" NL,
               cs_initiator_instances[i].conn_handle,
               (uint32_t)(cs_initiator_instances[i].measurement_mainmode.distance_filtered * 1000.f));
      if (initiator_config.cs_sub_mode != sl_bt_cs_submode_disabled) {
        log_info(APP_INSTANCE_PREFIX "Measurement sub mode result: %lu mm" NL,
                 cs_initiator_instances[i].conn_handle,
                 (uint32_t)(cs_initiator_instances[i].measurement_submode.distance_filtered * 1000.f));
      }

      log_info(APP_INSTANCE_PREFIX "Raw main mode distance: %lu mm" NL,
               cs_initiator_instances[i].conn_handle,
               (uint32_t)(cs_initiator_instances[i].measurement_mainmode.distance_raw * 1000.f));

      if (initiator_config.cs_sub_mode != sl_bt_cs_submode_disabled) {
        log_info(APP_INSTANCE_PREFIX "Raw sub mode distance: %lu mm" NL,
                 cs_initiator_instances[i].conn_handle,
                 (uint32_t)(cs_initiator_instances[i].measurement_submode.distance_raw * 1000.f));
      }

      log_info(APP_INSTANCE_PREFIX "Measurement main mode likeliness: %01u.%02u" NL,
               cs_initiator_instances[i].conn_handle,
               ((uint8_t)cs_initiator_instances[i].measurement_mainmode.likeliness),
               (uint16_t)((uint32_t)(cs_initiator_instances[i].measurement_mainmode.likeliness * 100.f)) % 100);

      if (initiator_config.cs_sub_mode != sl_bt_cs_submode_disabled) {
        log_info(APP_INSTANCE_PREFIX "Measurement sub mode likeliness: %01u.%02u" NL,
                 cs_initiator_instances[i].conn_handle,
                 ((uint8_t)cs_initiator_instances[i].measurement_submode.likeliness),
                 (uint16_t)((uint32_t)(cs_initiator_instances[i].measurement_submode.likeliness * 100.f)) % 100);
      }

      log_info(APP_INSTANCE_PREFIX "RSSI distance: %lu mm" NL,
               cs_initiator_instances[i].conn_handle,
               (uint32_t)(cs_initiator_instances[i].measurement_mainmode.distance_estimate_rssi * 1000.f));

      if (rtl_config.algo_mode == SL_RTL_CS_ALGO_MODE_REAL_TIME_FAST
          && initiator_config.cs_main_mode == sl_bt_cs_mode_pbr
          && (initiator_config.channel_map_preset == CS_CHANNEL_MAP_PRESET_HIGH
              || initiator_config.channel_map_preset == CS_CHANNEL_MAP_PRESET_MEDIUM)) {
        log_info(APP_INSTANCE_PREFIX "Velocity: %s%lu.%02u" NL,
                 cs_initiator_instances[i].conn_handle,
                 (cs_initiator_instances[i].measurement_mainmode.velocity >= 0) ? " " : "-",
                 ((uint32_t)ABS(cs_initiator_instances[i].measurement_mainmode.velocity)),
                 (uint16_t)((uint32_t)(ABS(cs_initiator_instances[i].measurement_mainmode.velocity) * 100.f + 0.5f)) % 100);
      }
      if ((initiator_config.cs_main_mode == sl_bt_cs_mode_rtt)
          && !isnan(cs_initiator_instances[i].measurement_mainmode.bit_error_rate)) {
        log_info(APP_INSTANCE_PREFIX "CS bit error rate: %1u.%02u" NL,
                 cs_initiator_instances[i].conn_handle,
                 ((uint8_t)cs_initiator_instances[i].measurement_mainmode.bit_error_rate),
                 (uint16_t)((uint32_t)(ABS(cs_initiator_instances[i].measurement_mainmode.bit_error_rate) * 100.f)) % 100);
      }
      cs_initiator_display_update_data(i,
                                       cs_initiator_instances[i].conn_handle,
                                       CS_INITIATOR_DISPLAY_STATUS_CONNECTED,
                                       cs_initiator_instances[i].measurement_mainmode.distance_filtered,
                                       cs_initiator_instances[i].measurement_mainmode.distance_estimate_rssi,
                                       cs_initiator_instances[i].measurement_mainmode.likeliness,
                                       cs_initiator_instances[i].measurement_mainmode.bit_error_rate,
                                       cs_initiator_instances[i].measurement_mainmode.distance_raw,
                                       cs_initiator_instances[i].measurement_progress.progress_percentage,
                                       rtl_config.algo_mode,
                                       initiator_config.cs_main_mode);
   */
    } else if (cs_initiator_instances[i].measurement_progress_changed) {
      // write measurement progress to the display without changing the last valid
      // measurement results
      cs_initiator_instances[i].measurement_progress_changed = false;
      log_info(APP_INSTANCE_PREFIX "# %04lu ---" NL,
               cs_initiator_instances[i].measurement_progress.connection,
               cs_initiator_instances[i].measurement_cnt);

      log_info(APP_INSTANCE_PREFIX "Estimation in progress: %3u.%02u %%" NL,
               cs_initiator_instances[i].measurement_progress.connection,
               ((uint8_t)cs_initiator_instances[i].measurement_progress.progress_percentage),
               (uint16_t)((uint32_t)(cs_initiator_instances[i].measurement_progress.progress_percentage * 100.f)) % 100);

      cs_initiator_display_update_data(i,
                                       cs_initiator_instances[i].conn_handle,
                                       CS_INITIATOR_DISPLAY_STATUS_CONNECTED,
                                       cs_initiator_instances[i].measurement_mainmode.distance_filtered,
                                       cs_initiator_instances[i].measurement_mainmode.distance_estimate_rssi,
                                       cs_initiator_instances[i].measurement_mainmode.likeliness,
                                       cs_initiator_instances[i].measurement_mainmode.bit_error_rate,
                                       cs_initiator_instances[i].measurement_mainmode.distance_raw,
                                       cs_initiator_instances[i].measurement_progress.progress_percentage,
                                       rtl_config.algo_mode,
                                       initiator_config.cs_main_mode);
    }

  }

  /////////////////////////////////////////////////////////////////////////////
  // Put your additional application code here!                              //
  // This is called infinitely.                                              //
  // Do not call blocking functions from here!                               //
  /////////////////////////////////////////////////////////////////////////////
}

// -----------------------------------------------------------------------------
// Static function definitions

static void app_timer_callback(app_timer_t *timer, void *data)
{
  (void)timer;
  (void)data;
  cs_initiator_display_update();
}

/******************************************************************************
 * Return runtime configurable value for object tracking mode
 *****************************************************************************/
#if (SL_SIMPLE_BUTTON_COUNT > 1)
  #if CS_INITIATOR_DEFAULT_ALGO_MODE == SL_RTL_CS_ALGO_MODE_REAL_TIME_FAST
    #define CS_INITIATOR_ALTERNATIVE_ALGO_MODE SL_RTL_CS_ALGO_MODE_STATIC_HIGH_ACCURACY
  #else
    #define CS_INITIATOR_ALTERNATIVE_ALGO_MODE SL_RTL_CS_ALGO_MODE_REAL_TIME_FAST
  #endif
static uint8_t get_algo_mode(void)
{
  if (sl_button_get_state(SL_SIMPLE_BUTTON_INSTANCE(1)) == SL_SIMPLE_BUTTON_PRESSED) {
    return CS_INITIATOR_ALTERNATIVE_ALGO_MODE;
  }
  return CS_INITIATOR_DEFAULT_ALGO_MODE;
}
#else
static uint8_t get_algo_mode(void)
{
  return CS_INITIATOR_DEFAULT_ALGO_MODE;
}
#endif

/******************************************************************************
 * Get requested antenna usage configuration as string
 *****************************************************************************/
static const char *antenna_usage_to_str(const cs_initiator_config_t *config)
{
  if (config->cs_main_mode == sl_bt_cs_mode_rtt) {
    switch (config->cs_sync_antenna_req) {
      case CS_SYNC_ANTENNA_1:
        return "antenna ID 1";
      case CS_SYNC_ANTENNA_2:
        return "antenna ID 2";
      case CS_SYNC_SWITCHING:
        return "switch between all antenna IDs";
      default:
        return "unknown";
    }
  } else {
    switch (config->cs_tone_antenna_config_idx_req) {
      case CS_ANTENNA_CONFIG_INDEX_SINGLE_ONLY:
        return "single antenna on both sides (1:1)";
      case CS_ANTENNA_CONFIG_INDEX_DUAL_I_SINGLE_R:
        return "dual antenna initiator & single antenna reflector (2:1)";
      case CS_ANTENNA_CONFIG_INDEX_SINGLE_I_DUAL_R:
        return "single antenna initiator & dual antenna reflector (1:2)";
      case CS_ANTENNA_CONFIG_INDEX_DUAL_ONLY:
        return "dual antennas on both sides (2:2)";
      default:
        return "unknown";
    }
  }
}

/******************************************************************************
 * Get algo mode as string
 *****************************************************************************/
static const char *algo_mode_to_str(uint8_t algo_mode)
{
  switch (algo_mode) {
    case SL_RTL_CS_ALGO_MODE_REAL_TIME_BASIC:
      return "real time basic (moving)";
    case SL_RTL_CS_ALGO_MODE_STATIC_HIGH_ACCURACY:
      return "stationary object tracking";
    case SL_RTL_CS_ALGO_MODE_REAL_TIME_FAST:
      return "real time fast (moving)";
    default:
      return "unknown";
  }
}

/******************************************************************************
 * Get instance number based on connection handle
 *****************************************************************************/
static sl_status_t get_instance_number(uint8_t conn_handle, uint8_t *instance_num)
{
  for (uint8_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    if (cs_initiator_instances[i].conn_handle == conn_handle) {
      *instance_num = i;
      return SL_STATUS_OK;
    }
  }
  return SL_STATUS_FAIL;
}

/******************************************************************************
 * Extract measurement results
 *****************************************************************************/
static void cs_on_result(const uint8_t conn_handle,
                         const uint16_t ranging_counter,
                         const uint8_t *result,
                         const cs_result_session_data_t *result_data,
                         const cs_ranging_data_t *ranging_data,
                         const void *user_data)
{
  (void)ranging_data;
  (void)user_data;
  uint8_t initiator_num;

  if (result != NULL) {
    sl_status_t sc = get_instance_number(conn_handle, &initiator_num);
    if (sc != SL_STATUS_OK) {
      log_error(APP_INSTANCE_PREFIX "Failed to get instance number for connection! [sc: 0x%lx]" NL,
                conn_handle,
                sc);
      return;
    }

    sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                 CS_RESULT_FIELD_DISTANCE_MAINMODE,
                                 (uint8_t *)result,
                                 (uint8_t *)&cs_initiator_instances[initiator_num].measurement_mainmode.distance_filtered);
    if (sc != SL_STATUS_OK) {
      log_error(APP_INSTANCE_PREFIX "Failed to extract distance! [sc: 0x%lx]" NL,
                conn_handle,
                sc);
    }

    if (initiator_config.cs_sub_mode != sl_bt_cs_submode_disabled) {
      sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                   CS_RESULT_FIELD_DISTANCE_SUBMODE,
                                   (uint8_t *)result,
                                   (uint8_t *)&cs_initiator_instances[initiator_num].measurement_submode.distance_filtered);
      if (sc != SL_STATUS_OK) {
        log_error(APP_INSTANCE_PREFIX "Failed to extract sub mode distance! [sc: 0x%lx]" NL,
                  conn_handle,
                  sc);
      }
    }

    sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                 CS_RESULT_FIELD_DISTANCE_RAW_MAINMODE,
                                 (uint8_t *)result,
                                 (uint8_t *)&cs_initiator_instances[initiator_num].measurement_mainmode.distance_raw);
    if (sc != SL_STATUS_OK) {
      log_error(APP_INSTANCE_PREFIX "Failed to extract RAW distance! [sc: 0x%lx]" NL,
                conn_handle,
                sc);
    }

    if (initiator_config.cs_sub_mode != sl_bt_cs_submode_disabled) {
      sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                   CS_RESULT_FIELD_DISTANCE_RAW_SUBMODE,
                                   (uint8_t *)result,
                                   (uint8_t *)&cs_initiator_instances[initiator_num].measurement_submode.distance_raw);
      if (sc != SL_STATUS_OK) {
        log_error(APP_INSTANCE_PREFIX "Failed to extract sub mode RAW distance! [sc: 0x%lx]" NL,
                  conn_handle,
                  sc);
      }
    }

    sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                 CS_RESULT_FIELD_LIKELINESS_MAINMODE,
                                 (uint8_t *)result,
                                 (uint8_t *)&cs_initiator_instances[initiator_num].measurement_mainmode.likeliness);
    if (sc != SL_STATUS_OK) {
      log_error(APP_INSTANCE_PREFIX "Failed to extract likeliness! [sc: 0x%lx]" NL,
                conn_handle,
                sc);
    }

    if (initiator_config.cs_sub_mode != sl_bt_cs_submode_disabled) {
      sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                   CS_RESULT_FIELD_LIKELINESS_SUBMODE,
                                   (uint8_t *)result,
                                   (uint8_t *)&cs_initiator_instances[initiator_num].measurement_submode.likeliness);
      if (sc != SL_STATUS_OK) {
        log_error(APP_INSTANCE_PREFIX "Failed to extract sub mode likeliness! [sc: 0x%lx]" NL,
                  conn_handle,
                  sc);
      }
    }

    if (rtl_config.algo_mode == SL_RTL_CS_ALGO_MODE_REAL_TIME_FAST
        && initiator_config.cs_main_mode == sl_bt_cs_mode_pbr
        && (initiator_config.channel_map_preset == CS_CHANNEL_MAP_PRESET_HIGH
            || initiator_config.channel_map_preset == CS_CHANNEL_MAP_PRESET_MEDIUM)) {
      sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                   CS_RESULT_FIELD_VELOCITY_MAINMODE,
                                   (uint8_t *)result,
                                   (uint8_t *)&cs_initiator_instances[initiator_num].measurement_mainmode.velocity);
      if (sc != SL_STATUS_OK) {
        log_error(APP_INSTANCE_PREFIX "Failed to extract velocity! [sc: 0x%lx]" NL,
                  conn_handle,
                  sc);
      }
    }

    // BER is only for RTT
    if (initiator_config.cs_main_mode == sl_bt_cs_mode_rtt) {
      sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                   CS_RESULT_FIELD_BIT_ERROR_RATE,
                                   (uint8_t *)result,
                                   (uint8_t *)&cs_initiator_instances[initiator_num].measurement_mainmode.bit_error_rate);
      if (sc != SL_STATUS_OK) {
        log_error(APP_INSTANCE_PREFIX "Failed to extract BER! [sc: 0x%lx]" NL,
                  conn_handle,
                  sc);
      }
    }

    // Extract RSSI distance always
    sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                 CS_RESULT_FIELD_DISTANCE_RSSI,
                                 (uint8_t *)result,
                                 (uint8_t *)&cs_initiator_instances[initiator_num].measurement_mainmode.distance_estimate_rssi);
    if (sc != SL_STATUS_OK) {
      log_error(APP_INSTANCE_PREFIX "Failed to extract RSSI distance! [sc: 0x%lx]" NL,
                conn_handle,
                sc);
    }
    cs_initiator_instances[initiator_num].measurement_arrived = true;
    cs_initiator_instances[initiator_num].measurement_cnt++;
    cs_initiator_instances[initiator_num].ranging_counter = ranging_counter;
  } else {
    log_error(APP_INSTANCE_PREFIX "Null result reference!" NL,
              conn_handle);
  }
}

/******************************************************************************
 * Extract intermediate results between measurement results
 * Note: only called when stationary object tracking used
 *****************************************************************************/
static void cs_on_intermediate_result(const cs_intermediate_result_t * intermediate_result, const void *user_data)
{
  (void) user_data;
  uint8_t instance_num;
  if (intermediate_result != NULL) {
    sl_status_t sc = get_instance_number(intermediate_result->connection, &instance_num);
    if (sc != SL_STATUS_OK) {
      log_error(APP_INSTANCE_PREFIX "Failed to get instance number for connection" NL,
                intermediate_result->connection);
      return;
    }
    memcpy(&cs_initiator_instances[instance_num].measurement_progress,
           intermediate_result,
           sizeof(cs_intermediate_result_t));
    cs_initiator_instances[instance_num].measurement_progress_changed = true;
  }
}

/******************************************************************************
 * Check if the CLI has changed any default values of the initiator
 *****************************************************************************/
static void check_cli_values(void)
{
#ifdef SL_CATALOG_CS_INITIATOR_CLI_PRESENT
  if (cs_initiator_cli_get_antenna_config_index() != initiator_config.cs_tone_antenna_config_idx_req) {
    antenna_set_pbr = true;
  }
  initiator_config.cs_tone_antenna_config_idx_req = cs_initiator_cli_get_antenna_config_index();
  if (cs_initiator_cli_get_cs_sync_antenna_usage() != initiator_config.cs_sync_antenna_req) {
    antenna_set_rtt = true;
  }
  initiator_config.cs_sub_mode = cs_initiator_cli_get_sub_mode();
  if (initiator_config.cs_sub_mode == sl_bt_cs_submode_disabled) {
    initiator_config.min_main_mode_steps = CS_INITIATOR_DEFAULT_MIN_MAIN_MODE_STEPS;
    initiator_config.max_main_mode_steps = CS_INITIATOR_DEFAULT_MAX_MAIN_MODE_STEPS;
  } else {
    initiator_config.min_main_mode_steps = CS_INITIATOR_MIXED_MODE_MAIN_MODE_STEPS;
    initiator_config.max_main_mode_steps = CS_INITIATOR_MIXED_MODE_MAIN_MODE_STEPS;
  }
  initiator_config.cs_sync_antenna_req = cs_initiator_cli_get_cs_sync_antenna_usage();
  initiator_config.cs_main_mode = cs_initiator_cli_get_mode();
  initiator_config.conn_phy = cs_initiator_cli_get_conn_phy();
  rtl_config.algo_mode = cs_initiator_cli_get_algo_mode();
  initiator_config.channel_map_preset = cs_initiator_cli_get_preset();
  cs_initiator_apply_channel_map_preset(initiator_config.channel_map_preset,
                                        initiator_config.channel_map.data);
#endif // SL_CATALOG_CS_INITIATOR_CLI_PRESENT
}

/******************************************************************************
 * Create new initiator instance
 *****************************************************************************/
static sl_status_t create_new_initiator_instance(uint8_t conn_handle)
{
  sl_status_t sc;
  cs_intermediate_result_t measurement_progress;
  // Check if we can accept one more reflector connection
  if (num_reflector_connections >= CS_INITIATOR_MAX_CONNECTIONS) {
    log_error(APP_PREFIX "Maximum number of initiator instances (%u) reached, "
                         "dropping connection..." NL,
              CS_INITIATOR_MAX_CONNECTIONS);
    return SL_STATUS_FULL;
  }
  // Store the new initiator instance
  for (uint32_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    if (cs_initiator_instances[i].conn_handle == SL_BT_INVALID_CONNECTION_HANDLE) {
      cs_initiator_instances[i].conn_handle = conn_handle;
      cs_initiator_instances[i].measurement_cnt = 0u;
      memset(&cs_initiator_instances[i].measurement_mainmode, 0u, sizeof(cs_measurement_data_t));
      memset(&cs_initiator_instances[i].measurement_submode, 0u, sizeof(cs_measurement_data_t));
      memset(&cs_initiator_instances[i].measurement_progress, 0u, sizeof(measurement_progress));
      num_reflector_connections++;
      break;
    }
  }

  sc = cs_initiator_create(conn_handle,
                           &initiator_config,
                           &rtl_config,
                           cs_on_result,
                           cs_on_intermediate_result,
                           cs_on_error,
                           NULL);
  if (sc != SL_STATUS_OK) {
    log_error(APP_INSTANCE_PREFIX "Failed to create initiator instance, "
                                  "error:0x%lx" NL,
              conn_handle,
              sc);
    (void)ble_peer_manager_central_close_connection(conn_handle);
  }
  return sc;
}

/******************************************************************************
 * Delete initiator instance
 *****************************************************************************/
static void delete_initiator_instance(uint8_t conn_handle)
{
  for (uint32_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    if (cs_initiator_instances[i].conn_handle == conn_handle) {
      cs_initiator_instances[i].conn_handle = SL_BT_INVALID_CONNECTION_HANDLE;
      cs_initiator_instances[i].measurement_cnt = 0u;
      memset(&cs_initiator_instances[i].measurement_mainmode, 0u, sizeof(cs_measurement_data_t));
      memset(&cs_initiator_instances[i].measurement_submode, 0u, sizeof(cs_measurement_data_t));
      memset(&cs_initiator_instances[i].measurement_progress, 0u, sizeof(cs_intermediate_result_t));
      cs_initiator_instances[i].measurement_arrived = false;
      cs_initiator_instances[i].measurement_progress_changed = false;
      cs_initiator_instances[i].read_remote_capabilities = false;
      num_reflector_connections--;
      break;
    }
  }
}

/******************************************************************************
 * CS error handler
 *****************************************************************************/
static void cs_on_error(uint8_t conn_handle, cs_error_event_t err_evt, sl_status_t sc)
{
  switch (err_evt) {
    // Assert
    case CS_ERROR_EVENT_CS_PROCEDURE_STOP_TIMER_FAILED:
    case CS_ERROR_EVENT_CS_PROCEDURE_UNEXPECTED_DATA:
      app_assert(false,
                 APP_INSTANCE_PREFIX "Unrecoverable CS procedure error happened!"
                                     "[E: 0x%x sc: 0x%lx]" NL,
                 conn_handle,
                 err_evt,
                 (unsigned long)sc);
      break;

    // Discard
    case CS_ERROR_EVENT_RTL_PROCESS_ERROR:
      log_error(APP_INSTANCE_PREFIX "RTL processing error happened!"
                                    "[E: 0x%x sc: 0x%lx]" NL,
                conn_handle,
                err_evt,
                (unsigned long)sc);
      break;

    case CS_ERROR_EVENT_INITIATOR_FAILED_TO_SET_INTERVALS:
      log_error(APP_INSTANCE_PREFIX "Failed to set CS procedure scheduling!"
                                    "[E: 0x%x sc: 0x%lx]" NL,
                conn_handle,
                err_evt,
                (unsigned long)sc);
      break;
    // Antenna usage not supported
    case CS_ERROR_EVENT_INITIATOR_PBR_ANTENNA_USAGE_NOT_SUPPORTED:
      if (antenna_set_pbr) {
        log_error(APP_INSTANCE_PREFIX "The requested PBR antenna configuration is not supported!"
                                      " Will use the closest one and continue."
                                      "[E: 0x%x sc: 0x%lx]" NL,
                  conn_handle,
                  err_evt,
                  (unsigned long)sc);
      } else {
        log_info(APP_INSTANCE_PREFIX "Default PBR antenna configuration not supported!"
                                     " Will use the closest one and continue."
                                     "[E: 0x%x sc: 0x%lx]" NL,
                 conn_handle,
                 err_evt,
                 (unsigned long)sc);
      }
      break;
    case CS_ERROR_EVENT_INITIATOR_RTT_ANTENNA_USAGE_NOT_SUPPORTED:
      if (antenna_set_rtt) {
        log_error(APP_INSTANCE_PREFIX "The requested RTT antenna configuration is not supported!"
                                      " Will use the closest one and continue."
                                      "[E: 0x%x sc: 0x%lx]" NL,
                  conn_handle,
                  err_evt,
                  (unsigned long)sc);
      } else {
        log_info(APP_INSTANCE_PREFIX "Default RTT antenna configuration not supported!"
                                     " Will use the closest one and continue."
                                     "[E: 0x%x sc: 0x%lx]" NL,
                 conn_handle,
                 err_evt,
                 (unsigned long)sc);
      }
      break;

    // Close connection
    default:
      log_error(APP_INSTANCE_PREFIX "Error happened! Closing connection."
                                    "[E: 0x%x sc: 0x%lx]" NL,
                conn_handle,
                err_evt,
                (unsigned long)sc);
      // Common errors
      if (err_evt == CS_ERROR_EVENT_TIMER_ELAPSED) {
        log_error(APP_INSTANCE_PREFIX "Operation timeout." NL, conn_handle);
      } else if (err_evt == CS_ERROR_EVENT_INITIATOR_FAILED_TO_INCREASE_SECURITY) {
        log_error(APP_INSTANCE_PREFIX "Security level increase failed." NL, conn_handle);
      }
      // Close the connection
      (void)ble_peer_manager_central_close_connection(conn_handle);
      break;
  }
}

// -----------------------------------------------------------------------------
// Event / callback definitions

/**************************************************************************//**
 * Bluetooth stack event handler
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t * evt)
{
  sl_status_t sc;
  uint8_t instance_num;
  const char* device_name = REFLECTOR_DEVICE_NAME;

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:
    {
      // Set TX power
      int16_t min_tx_power_x10 = SYSTEM_MIN_TX_POWER_DBM * 10;
      int16_t max_tx_power_x10 = SYSTEM_MAX_TX_POWER_DBM * 10;
      sc = sl_bt_system_set_tx_power(min_tx_power_x10,
                                     max_tx_power_x10,
                                     &min_tx_power_x10,
                                     &max_tx_power_x10);
      app_assert_status(sc);
      log_info(APP_PREFIX "Minimum system TX power is set to: %d dBm" NL, min_tx_power_x10 / 10);
      log_info(APP_PREFIX "Maximum system TX power is set to: %d dBm" NL, max_tx_power_x10 / 10);

      // Reset to initial state
      ble_peer_manager_central_init();
      ble_peer_manager_filter_init();
      cs_initiator_init();

      // Print the Bluetooth address
      bd_addr address;
      uint8_t address_type;
      sc = sl_bt_gap_get_identity_address(&address, &address_type);
      app_assert_status(sc);
      log_info(APP_PREFIX "Bluetooth %s address: %02X:%02X:%02X:%02X:%02X:%02X\n",
               address_type ? "static random" : "public device",
               address.addr[5],
               address.addr[4],
               address.addr[3],
               address.addr[2],
               address.addr[1],
               address.addr[0]);

      sc = cs_antenna_configure(CS_INITIATOR_ANTENNA_OFFSET);
      app_assert_status(sc);

      // Filter for advertised name (CS_RFLCT)
      sc = ble_peer_manager_set_filter_device_name(device_name,
                                                   strlen(device_name),
                                                   false);
      app_assert_status(sc);

      uint16_t ras_service_uuid = CS_RAS_SERVICE_UUID;
      sc = ble_peer_manager_set_filter_service_uuid16((sl_bt_uuid_16_t *)&ras_service_uuid);
      app_assert_status(sc);

#ifndef SL_CATALOG_CS_INITIATOR_CLI_PRESENT
      sc = ble_peer_manager_central_create_connection();
      app_assert_status(sc);
      cs_initiator_display_start_scanning();
      // Start scanning for reflector connections
      log_info(APP_PREFIX "Scanning started for reflector connections..." NL);
#else
      log_info("CS CLI is active." NL);
#endif // SL_CATALOG_CS_INITIATOR_CLI_PRESENT

      break;
    }
    case sl_bt_evt_connection_parameters_id:
      sc = get_instance_number(evt->data.evt_connection_parameters.connection, &instance_num);
      // Initiator instance not created yet
      if (sc != SL_STATUS_OK) {
        if (evt->data.evt_connection_parameters.security_mode != sl_bt_connection_mode1_level1) {
          sc = sl_bt_cs_read_remote_supported_capabilities(evt->data.evt_connection_parameters.connection);
          app_assert_status(sc);
        } else {
          sc = sl_bt_sm_increase_security(evt->data.evt_connection_parameters.connection);
          app_assert_status(sc);
        }
      }
      break;

    // --------------------------------
    // MTU exchange event
    case sl_bt_evt_gatt_mtu_exchanged_id:
    {
      initiator_config.mtu = evt->data.evt_gatt_mtu_exchanged.mtu;
      log_info(APP_PREFIX "MTU set to: %u" NL,
               initiator_config.mtu);
    }
    break;

    case sl_bt_evt_cs_read_remote_supported_capabilities_complete_id:
    {
      uint16_t proc_interval;
      uint16_t conn_interval;
      uint8_t cs_tone_antenna_config_index_temp = initiator_config.cs_tone_antenna_config_idx;
      uint8_t connection = evt->data.evt_cs_read_remote_supported_capabilities_complete.connection;
      sc = sl_bt_cs_read_local_supported_capabilities(NULL,
                                                      NULL,
                                                      &initiator_config.num_antennas,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL,
                                                      NULL);
      app_assert_status(sc);
      if (initiator_config.max_procedure_count == 0) {
        sc = cs_initiator_get_intervals(initiator_config.cs_main_mode,
                                        initiator_config.cs_sub_mode,
                                        initiator_config.procedure_scheduling,
                                        initiator_config.channel_map_preset,
                                        rtl_config.algo_mode,
                                        initiator_config.cs_tone_antenna_config_idx,
                                        initiator_config.use_real_time_ras_mode,
                                        &conn_interval,
                                        &proc_interval);
        if (sc == SL_STATUS_NOT_SUPPORTED) {
          log_info(APP_INSTANCE_PREFIX "Parameter optimization is not supported with the given input parameters" NL, connection);
        } else if (sc == SL_STATUS_IDLE) {
          log_info(APP_PREFIX "No optimization - using custom procedure scheduling" NL);
        } else if (sc == SL_STATUS_OK) {
          initiator_config.max_connection_interval = initiator_config.min_connection_interval = conn_interval;
          initiator_config.max_procedure_interval = initiator_config.min_procedure_interval = proc_interval;
          log_info(APP_INSTANCE_PREFIX "Optimized parameters for connection interval and procedure interval." NL, connection);
        } else {
          log_error(APP_INSTANCE_PREFIX "Invalid input, cannot optimize parameters." NL, connection);
        }
        float period_ms = initiator_config.max_connection_interval * 1.25f * initiator_config.max_procedure_interval;
        log_info(APP_INSTANCE_PREFIX "Connection interval: %u  Procedure interval: %u  Period: %d ms  Frequency: %u.%03u Hz" NL,
                 connection,
                 initiator_config.max_connection_interval,
                 initiator_config.max_procedure_interval,
                 (int)period_ms,
                 (uint16_t)(1000.0f / period_ms),
                 (((uint16_t)(1000000.0f / period_ms)) % 1000));
        // put remote antenna num into cs_tone_antenna_config_idx
        initiator_config.cs_tone_antenna_config_idx = evt->data.evt_cs_read_remote_supported_capabilities_complete.num_antennas;
      }
      sc = create_new_initiator_instance(connection);
      if (sc != SL_STATUS_OK) {
        log_error(APP_INSTANCE_PREFIX "Failed to create initiator instance, "
                                      "error:0x%lx" NL,
                  connection,
                  sc);
        (void)ble_peer_manager_central_close_connection(connection);
      } else {
        log_info(APP_INSTANCE_PREFIX "New initiator instance created" NL,
                 connection);
      }
      // set cs_tone_antenna_config_idx to default
      initiator_config.cs_tone_antenna_config_idx = cs_tone_antenna_config_index_temp;
      sc = get_instance_number(connection, &instance_num);
      if (sc != SL_STATUS_OK) {
        log_error(APP_INSTANCE_PREFIX "Failed to get instance number for connection" NL,
                  connection);
        return;
      }
      cs_initiator_instances[instance_num].read_remote_capabilities = true;
      // Scan for new reflector connections if we have room for more
      if (num_reflector_connections < CS_INITIATOR_MAX_CONNECTIONS) {
        sc = ble_peer_manager_central_create_connection();
        app_assert_status(sc);
        cs_initiator_display_start_scanning();
        log_info(APP_PREFIX "Scanning restarted for new reflector connections..." NL);
      }
      break;
    }
    default:
      break;
  }
}

/******************************************************************************
 * BLE peer manager event handler
 *
 * @param[in] evt Event coming from the peer manager.
 *****************************************************************************/
void ble_peer_manager_on_event_initiator(ble_peer_manager_evt_type_t * event)
{
  sl_status_t sc;
  bd_addr *address;
  uint8_t instance_num;

  switch (event->evt_id) {
    case BLE_PEER_MANAGER_ON_CONN_OPENED_CENTRAL:
      address = ble_peer_manager_get_bt_address(event->connection_id);
      log_info(APP_INSTANCE_PREFIX "Connection opened as central with CS Reflector"
                                   " '%02X:%02X:%02X:%02X:%02X:%02X'" NL,
               event->connection_id,
               address->addr[5],
               address->addr[4],
               address->addr[3],
               address->addr[2],
               address->addr[1],
               address->addr[0]);

      /* Initialise reflector FSM state */
      sc = get_instance_number(event->connection_id, &instance_num);
      reflector_state[instance_num] = JUST_CONNECTED;

      check_cli_values();
      cs_initiator_display_set_measurement_mode(initiator_config.cs_main_mode, rtl_config.algo_mode);
      break;
    case BLE_PEER_MANAGER_ON_CONN_CLOSED:
      log_info(APP_INSTANCE_PREFIX "Connection closed" NL, event->connection_id);
      sc = cs_initiator_delete(event->connection_id);
      if ((sc == SL_STATUS_NOT_FOUND) || (sc == SL_STATUS_INVALID_HANDLE)) {
        log_info(APP_INSTANCE_PREFIX "Initiator instance not found" NL, event->connection_id);
      } else {
        app_assert_status(sc);
        log_info(APP_INSTANCE_PREFIX "Initiator instance removed" NL, event->connection_id);
      }
      delete_initiator_instance(event->connection_id);
      // Restart scanning for new reflector connections
      (void)ble_peer_manager_central_create_connection();
      cs_initiator_display_start_scanning();
      log_info(APP_PREFIX "Scanning started for reflector connections..." NL);
      break;

    case BLE_PEER_MANAGER_ERROR:
      log_error(APP_INSTANCE_PREFIX "Peer Manager error" NL,
                event->connection_id);
      break;

    default:
      log_info(APP_INSTANCE_PREFIX "Unhandled Peer Manager event (%u)" NL,
               event->connection_id,
               event->evt_id);
      break;
  }
}
