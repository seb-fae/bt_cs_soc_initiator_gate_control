#include "sl_status.h"
#include "sl_bgapi.h"
#include "sl_bt_api_compatibility.h"
#include "sl_bt_api.h"
#include "config/token.h"
#include "autogen/gatt_db.h"


extern uint32_t BASELINE_WEIGHT;
extern uint32_t MOVING_THRESHOLD;
extern uint32_t OPEN_BLOCK_DELAY_MS;
extern uint32_t CLOSE_BLOCK_DELAY_MS;

void write_characteristic(sl_bt_evt_gatt_server_user_write_request_t * request)
{
   sl_status_t sc = SL_STATUS_OK;
   nvm3_ObjectKey_t key = NVM3KEY_DEVICE_MOVING_THRESHOLD;
   EFM_ASSERT(request->value.len == 1);

   switch (request->characteristic)
   {
     case gattdb_BASELINE:
       key = NVM3KEY_DEVICE_BASELINE_WEIGHT;
       BASELINE_WEIGHT = (uint32_t)request->value.data;
       break;
     case gattdb_OPEN_TIME:
       key = NVM3KEY_DEVICE_OPEN_TIME;
       OPEN_BLOCK_DELAY_MS = (uint32_t)request->value.data * 1000;
       break;
     case gattdb_CLOSE_TIME:
       key = NVM3KEY_DEVICE_CLOSE_TIME;
       CLOSE_BLOCK_DELAY_MS = (uint32_t)request->value.data * 1000;
       break;
     default:
       key = NVM3KEY_DEVICE_MOVING_THRESHOLD;
       MOVING_THRESHOLD = (uint32_t)request->value.data * 10 * 10;
       break;
   }

   nvm3_writeData(nvm3_defaultHandle, key, (const*)request->value.data, 1);

    // Send response to user write request.
    sc = sl_bt_gatt_server_send_user_write_response(
      request->connection,
      request->characteristic,
      (uint8_t)SL_STATUS_OK);

    EFM_ASSERT(sc == SL_STATUS_OK);
}

void read_characteristic(sl_bt_evt_gatt_server_user_read_request_t * request)
{
   sl_status_t sc = SL_STATUS_OK;
   nvm3_ObjectKey_t key = NVM3KEY_DEVICE_MOVING_THRESHOLD;
   uint8_t data;
   uint8_t sent_len;

   switch (request->characteristic)
   {
     case gattdb_BASELINE:
       key = NVM3KEY_DEVICE_BASELINE_WEIGHT;
       break;
     case gattdb_OPEN_TIME:
       key = NVM3KEY_DEVICE_OPEN_TIME;
       break;
     case gattdb_CLOSE_TIME:
       key = NVM3KEY_DEVICE_CLOSE_TIME;
       break;
     default:
       key = NVM3KEY_DEVICE_MOVING_THRESHOLD;
       break;
   }

   nvm3_readData(nvm3_defaultHandle, key, &data, 1);

   sc = sl_bt_gatt_server_send_user_read_response(
      request->connection,
      request->characteristic,
      (uint8_t)SL_STATUS_OK,
      1,
      &data,
      &sent_len);

    EFM_ASSERT(sc == SL_STATUS_OK);
}
