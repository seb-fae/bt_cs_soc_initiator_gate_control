/*
 * alg.c
 *
 *  Created on: 31 août 2025
 *      Author: secerdan
 */

#include "cs_initiator_config.h"
#include "em_gpio.h"
#include "sl_sleeptimer.h"
#include "app.h"

#define OPEN_CMD 1
#define CLOSE_CMD 0

enum gate_state_e
{
    DOOR_CLOSED,
    DOOR_OPENNED,
};

enum reflector_state_e
{
    JUST_CONNECTED,
    MOVING,
    RED_ZONE,
};

enum gate_command_state_e
{
  RELAY_POSITION_0,
  RELAY_POSITION_1,
  RELAY_POSITION_2,
  RELAY_DELAY
};

volatile enum reflector_state_e reflector_state[CS_INITIATOR_MAX_CONNECTIONS];
volatile uint32_t baseline[CS_INITIATOR_MAX_CONNECTIONS];
volatile uint32_t previous[CS_INITIATOR_MAX_CONNECTIONS];
volatile enum gate_state_e gate_state = DOOR_CLOSED;

//#define GATE_AUTO_CLOSE_MODE

#define BASELINE_WEIGHT 100
#define DISTANCE_WEIGHT 500

#define DISTANCE_RED_ZONE 2000
#define DISTANCE_OPENING_ZONE 40000
#define MOVING_THRESHOLD 1000

#define OPENNING_BLOCK_DELAY_MS 8000
#define CLOSE_BLOCK_DELAY_MS 10000
#define RELAY_DELAY_TIME_MS 500

#define RELAY_OPEN_PORT  gpioPortC
#define RELAY_OPEN_PIN   2
#define RELAY_CLOSE_PORT gpioPortC
#define RELAY_CLOSE_PIN  3

sl_status_t status;
sl_sleeptimer_timer_handle_t my_timer;

volatile enum gate_command_state_e relay_state = RELAY_POSITION_0;

void open_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  relay_sequence(OPEN_CMD);
}

void close_callback(sl_sleeptimer_timer_handle_t *handle, void *data)
{
  relay_sequence(CLOSE_CMD);
}

/* Asynchronous relay command */
void relay_sequence(uint8_t open)
{
  /* relay sequence */
  printf("rs : %d\n", relay_state);
  uint32_t relay_delay = (RELAY_DELAY_TIME_MS * 32768)/1000;

  switch (relay_state)
  {
    case RELAY_POSITION_0:
      /* Set LED ON */
      GPIO_PinModeSet(gpioPortD, 4, gpioModePushPull, 1);
      if (open)
        GPIO_PinModeSet(RELAY_OPEN_PORT, RELAY_OPEN_PIN, gpioModePushPull, 0);
      else
        GPIO_PinModeSet(RELAY_CLOSE_PORT, RELAY_CLOSE_PIN, gpioModePushPull, 0);
      relay_state = RELAY_POSITION_1;
      break;
    case RELAY_POSITION_1:
      if (open)
        GPIO_PinModeSet(RELAY_OPEN_PORT, RELAY_OPEN_PIN, gpioModePushPull, 1);
      else
        GPIO_PinModeSet(RELAY_CLOSE_PORT, RELAY_CLOSE_PIN, gpioModePushPull, 1);
      relay_state = RELAY_POSITION_2;
      break;
    case RELAY_POSITION_2:
      if (open)
        GPIO_PinModeSet(RELAY_OPEN_PORT, RELAY_OPEN_PIN, gpioModePushPull, 0);
      else
        GPIO_PinModeSet(RELAY_CLOSE_PORT, RELAY_CLOSE_PIN, gpioModePushPull, 0);
      relay_state = RELAY_DELAY;
      /* After Opening/Closing the door we wait 5/30 seconds before new action */
      relay_delay = open ? (OPENNING_BLOCK_DELAY_MS * 32768)/1000 : (CLOSE_BLOCK_DELAY_MS * 32768)/1000;
      break;
    case RELAY_DELAY:
      /* Set LED OFF */
      GPIO_PinModeSet(gpioPortD, 4, gpioModePushPull, 0);
      relay_state = RELAY_POSITION_0;
      return;
  }

  if (open)
    status = sl_sleeptimer_start_timer(&my_timer, relay_delay, open_callback, (void *)NULL, 0, 0);
  else
    status = sl_sleeptimer_start_timer(&my_timer, relay_delay, close_callback, (void *)NULL, 0, 0);
}

void try_open_gate(uint32_t distance)
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


  gate_state = DOOR_OPENNED;
  relay_sequence(OPEN_CMD);

#ifdef GATE_AUTO_CLOSE_MODE
  /* Gate will auto close after user timeout*/
  status = sl_sleeptimer_start_timer(&my_timer,timer_timeout, my_timer_callback, (void *)NULL, 0, 0);
#endif

  printf("OPENNING\n");
}

void try_close_gate()
{
  switch (gate_state)
  {
    case DOOR_CLOSED:
     /* Door already closed, nothing to do */
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

  gate_state = DOOR_CLOSED;
  relay_sequence(CLOSE_CMD);

  printf("CLOSING\n");
}

void init_measure(uint8_t index)
{
   reflector_state[index] = JUST_CONNECTED;
}

volatile uint32_t cnt = 0;

void process_measure(uint8_t index, cs_initiator_instances_t * instances)
{
  cs_initiator_instances_t * initiator = instances + index;
  uint32_t new = (uint32_t)(initiator->measurement_mainmode.distance_filtered * 1000.f);
  uint32_t distance;

  switch (reflector_state[index])
  {
    case JUST_CONNECTED:
      baseline[index] = new;
      previous[index] = new;
      break;
    default:
      /* Update the baseline */
      baseline[index] = (new * BASELINE_WEIGHT) + (baseline[index] * (1000 - BASELINE_WEIGHT));
      baseline[index] /= 1000;

      distance = (new + previous[index])/2;

      previous[index] = distance;
      break;
  }

  // printf("diff: %d\nd: %d\nb: %d\n", distance - baseline[index], new, baseline[index]);
  // printf("d: %d, b: %d\n", distance, baseline[index]);
  // printf("s: %d \n", reflector_state[index]);

  switch (reflector_state[index])
  {
    case JUST_CONNECTED:
      reflector_state[index] = MOVING;
      break;
    case MOVING:
      if (distance <= DISTANCE_RED_ZONE)
      {
        reflector_state[index] = RED_ZONE;
        break;
      }
      /* This state is used to determine direction of mouvement */
      if (distance >= (baseline[index] + MOVING_THRESHOLD))
      /* We are moving away */
      {
        try_close_gate();
      }
      else if ((distance +  MOVING_THRESHOLD) < baseline[index])
      /* We are moving closer */
      {
        try_open_gate(distance);
      }
      break;
    case RED_ZONE:
      if (distance > DISTANCE_RED_ZONE)
        reflector_state[index] = MOVING;
      break;
    default:
      break;
  }

}

