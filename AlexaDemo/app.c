/***************************************************************************//**
 * @file app.c
 * @brief Silicon Labs Empty Example Project
 *
 * This example demonstrates the bare minimum needed for a Blue Gecko C application
 * that allows Over-the-Air Device Firmware Upgrading (OTA DFU). The application
 * starts advertising after boot and restarts advertising after a connection is closed.
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

/* Bluetooth stack headers */
#include <stdlib.h>
#include <ctype.h>
#include "bg_types.h"
#include "native_gecko.h"
#include "gatt_db.h"
#include "mbedtls/sha256.h"
#include "si7021.h"
#include "app.h"
#include "alexa.h"

#define CON_NO_CONNECTION         0xFF

#define NOTIFY_NONE        0  // notifications and indications disabled
#define NOTIFY_ENABLED     1  // notifications enabled
#define NOTIFY_INDIATE     2  // indications enabled
#define NOTIFY_WAIT        3  // indication sent, waiting confirmation
#define NOTIFY_INVALID     0xff

#define ERR_CHK(x) { uint16 Err; \
   Err = x->result; \
   if(Err != 0) printLog("%s#%d: failure, %d (0x%x)\r\n",__FUNCTION__,__LINE__,Err,Err); \
   }

typedef struct AlexPacket_TAG {
   struct AlexPacket_TAG *Link;
   int Id;
   uint8_t DataLen;
   uint8_t *pData;
} AlexPacket;
/* Print boot message */
static void bootMessage(struct gecko_msg_system_boot_evt_t *bootevt);

/* Flag for indicating DFU Reset must be performed */
static uint8_t boot_to_dfu = 0;

const char gFwVer[FWVER_MAX_LEN] = "0." FIRMWARE_VER;
char gMacAdr[13];
char gAlexaSn[ALEXA_SN_LEN];
uint8_t gConnection = CON_NO_CONNECTION;
uint8_t gAlexaNotification;

static bool gBonded;
bool gAlexaPaired;
const char gOurSecret[] = AMAZON_SECRET;
unsigned char gDeviceToken[65];

bool gLedOn;

static void HandleCCC(struct gecko_msg_gatt_server_characteristic_status_evt_t *p);
void SetAlexaAdvertisingData(bool bPairingMode);
void CheckDeviceName(void);

/* Main application */
void appMain(gecko_configuration_t *pconfig)
{
#if DISABLE_SLEEP > 0
  pconfig->sleep.flags = 0;
#endif
  pconfig->bluetooth.max_advertisers = 2;

  /* Initialize debug prints. Note: debug prints are off by default. See DEBUG_LEVEL in app.h */
  initLog();
  printf("SI7021_init returned %lu\n",SI7021_init());


  /* Initialize stack */
  gecko_init(pconfig);

  while (1) {
    /* Event pointer for handling events */
    struct gecko_cmd_packet* evt;

    /* if there are no events pending then the next call to gecko_wait_event() may cause
     * device go to deep sleep. Make sure that debug prints are flushed before going to sleep */
    if (!gecko_event_pending()) {
      flushLog();
    }

    /* Check for stack event. This is a blocking event listener. If you want non-blocking please see UG136. */
    evt = gecko_wait_event();

    /* Handle events */
    switch (BGLIB_MSG_ID(evt->header)) {
      /* This boot event is generated when the system boots up after reset.
       * Do not call any stack commands before receiving the boot event.
       * Here the system is set to start advertising immediately after boot procedure. */
      case gecko_evt_system_boot_id:
        CheckDeviceName();
        bootMessage(&(evt->data.evt_system_boot));
        printLog("boot event - starting advertising\r\n");
    /*
     * Configure security manager
     * BIT 0 = require MITM protection
     * BIT 1 = encryption requires bonding
     * BIT 2 = secure connections only
     * BIT 3 = bonding requests need to be confirmed--requests are notified with sm_confirm_bonding events
     * BIT 7:4 = reserved
     */
        ERR_CHK(gecko_cmd_sm_configure(0x0A,sm_io_capability_noinputnooutput));
        ERR_CHK(gecko_cmd_sm_store_bonding_configuration(2,1));
        ERR_CHK(gecko_cmd_sm_set_bondable_mode(1));

        SetAlexaAdvertisingData(!gAlexaPaired);

        /*
         * Initialize LEDs
         */

        gLedOn = false;
        SetLeds(0,0,0);

        /* Set advertising parameters. 100ms advertisement interval.
         * The first parameter is advertising set handle
         * The next two parameters are minimum and maximum advertising interval, both in
         * units of (milliseconds * 1.6).
         * The last two parameters are duration and maxevents left as default. */
        gecko_cmd_le_gap_set_advertise_timing(0, 160, 160, 0, 0);
        gecko_cmd_le_gap_set_advertise_timing(1, 160, 160, 0, 0);

        /* Start general advertising and enable connections. */
        gecko_cmd_le_gap_start_advertising(0, le_gap_general_discoverable, le_gap_connectable_scannable);
        gecko_cmd_le_gap_start_advertising(1,le_gap_user_data,le_gap_connectable_scannable);
        break;

      case gecko_evt_le_connection_opened_id:
         printLog("connection opened\r\n");
         if(gConnection != CON_NO_CONNECTION) {
            printLog("Error: already connected to %d\r\n",gConnection);
         }
         else {
            gConnection = evt->data.evt_le_connection_opened.connection;
            if(evt->data.evt_le_connection_opened.bonding != 0xff) {
               gBonded = true;
            }
         }
        break;

      case gecko_evt_le_connection_closed_id:
        printLog("connection closed, reason: 0x%2.2x\r\n", evt->data.evt_le_connection_closed.reason);
        gConnection = CON_NO_CONNECTION;
        gBonded = false;

        /* Check if need to boot to OTA DFU mode */
        if (boot_to_dfu) {
          /* Enter to OTA DFU mode */
          gecko_cmd_system_reset(2);
        } else {
          /* Restart advertising after client has disconnected */
          gecko_cmd_le_gap_start_advertising(0, le_gap_general_discoverable, le_gap_connectable_scannable);
        }
        break;

      /* Events related to OTA upgrading
         ----------------------------------------------------------------------------- */

      /* Check if the user-type OTA Control Characteristic was written.
       * If ota_control was written, boot the device into Device Firmware Upgrade (DFU) mode. */
      case gecko_evt_gatt_server_user_write_request_id:

        if (evt->data.evt_gatt_server_user_write_request.characteristic == gattdb_ota_control) {
          /* Set flag to enter to OTA mode */
          boot_to_dfu = 1;
          /* Send response to Write Request */
          gecko_cmd_gatt_server_send_user_write_response(
            evt->data.evt_gatt_server_user_write_request.connection,
            gattdb_ota_control,
            bg_err_success);

          /* Close connection to enter to DFU OTA mode */
          gecko_cmd_le_connection_close(evt->data.evt_gatt_server_user_write_request.connection);
        }
        else if (evt->data.evt_gatt_server_user_write_request.characteristic == gattdb_AlexaTx) {
           gecko_cmd_gatt_server_send_user_write_response(
             evt->data.evt_gatt_server_user_write_request.connection,
             gattdb_AlexaTx,
             bg_err_success);
           if(!gAlexaPaired) {
              gAlexaPaired = true;
           // TODO: save gAlexaPaired in NVRAM
           }
           AlexaRxPacket(evt->data.evt_gatt_server_user_write_request.value.data,
                         evt->data.evt_gatt_server_user_write_request.value.len);
           if(gSendSensorData) {
              uint32_t RADIO_rhData = 50000;
              int32_t  RADIO_tempData = 25000;
              int32_t F;
              uint32_t Err;

              gSendSensorData = false;
              if((Err = SI7021_measure(&RADIO_rhData, &RADIO_tempData)) != SI7021_OK ) {
                 printf("Error: SI7021_measure returned %lu\n",Err);
                 RADIO_tempData = 25000;
                 RADIO_rhData = 50000;
              }
              else {
                 F = RADIO_tempData * 9;
                 F /= 5;
                 F += 32000;

                 printf("Temp: %lu.%03lu F, RH: %lu.%03lu%%\n",F / 1000,F %1000,
                        RADIO_rhData / 1000, RADIO_rhData % 1000);
                 F = (F + 500) / 1000;
                 RADIO_rhData = (RADIO_rhData + 500) / 1000;
                 SendSensorData(F,RADIO_rhData);
              }
           }
        }
        break;

       case gecko_evt_sm_bonded_id:
          printLog("Received sm_bonded, bonding: 0x%x, connection: %d\r\n",
                   evt->data.evt_sm_bonded.bonding,
                   evt->data.evt_sm_bonded.connection);
          if(evt->data.evt_sm_bonded.bonding != 0xff) {
             gBonded = true;
             if(gAlexaNotification == NOTIFY_INDIATE) {
                SendAlexaProtocolVerPkt();
             }
          }
          break;

       case gecko_evt_sm_bonding_failed_id:
          printLog("Received sm_bonding_failed, connection: %d, reason: 0x%x\r\n",
                   evt->data.evt_sm_bonding_failed.connection,
                   evt->data.evt_sm_bonding_failed.reason);
          break;

       case gecko_evt_sm_confirm_bonding_id:
          printLog("Received sm_confirm_bonding, connection: %d, bonding_handle %d\r\n",
                   evt->data.evt_sm_confirm_bonding.connection,
                   evt->data.evt_sm_confirm_bonding.bonding_handle);
          ERR_CHK(gecko_cmd_sm_bonding_confirm(evt->data.evt_sm_confirm_bonding.connection,1));
          break;

       case gecko_evt_gatt_server_characteristic_status_id: 
       /* 
         This event indicates either that a local Client Characteristic 
         Configuration descriptor has been changed by the remote GATT
         client, or that a confirmation from the remote GATT client was
         received upon a successful reception of the indication.
         Confirmation by the remote GATT client should be received within 30
         seconds after an indication has been sent with the
         gatt_server_send_characteristic_notification command, otherwise
         further GATT transactions over this connection are disabled by the
         stack.
       */
          HandleCCC((struct gecko_msg_gatt_server_characteristic_status_evt_t *) &evt->data);
          break;

       case gecko_evt_hardware_soft_timer_id:

    	   /* Toggle LEDs on a timer event */
    	      if(gLedOn) {
    	         gLedOn = false;
    	         SetLeds(0,0,0);
    	      }
    	      else {
    	         gLedOn = true;
    	         SetLeds(0,1,0);
    	      }
    	      break;

      default:
        break;
    }
  }
}

/* Print stack version and local Bluetooth address as boot message */
static void bootMessage(struct gecko_msg_system_boot_evt_t *bootevt)
{
#if DEBUG_LEVEL
  bd_addr local_addr;
  int i;

  printLog("stack version: %u.%u.%u\r\n", bootevt->major, bootevt->minor, bootevt->patch);
  local_addr = gecko_cmd_system_get_bt_address()->address;

  printLog("local BT device address: ");
  for (i = 0; i < 5; i++) {
    printLog("%2.2x:", local_addr.addr[5 - i]);
  }
  printLog("%2.2x\r\n", local_addr.addr[0]);
#endif
}

// Set LEDs, 0 = off, 1 = on
void SetLeds(uint8_t Red,uint8_t Green,uint8_t Blue)
{
   if(Green) {
      GPIO_PinOutSet(BSP_LED0_PORT, BSP_LED0_PIN);
   }
   else {
      GPIO_PinOutClear(BSP_LED0_PORT, BSP_LED0_PIN);
   }
}

// Handle Client Characteristic Configuration
static void HandleCCC(struct gecko_msg_gatt_server_characteristic_status_evt_t *p)
{
   uint8_t NewState = NOTIFY_INVALID;
   uint8_t *pState = NULL;
   const char *CharacteristicDesc = NULL;

   printLog("connection: %d, characteristic %d, status_flags: 0x%x, client_config_flags: 0x%x\r\n",
            p->connection,p->characteristic,p->status_flags,
            p->client_config_flags);
   switch(p->characteristic) {
#if 0
      case gattdb_Status:
         pState = &gStatusNotification;
         break;
#endif

      case gattdb_AlexaRx:
         pState = &gAlexaNotification;
         CharacteristicDesc = "Alexa";
         break;
   }

   if(p->status_flags == gatt_server_client_config) {
      const char *Desc = "???";
      switch(p->client_config_flags) {
         case gatt_disable:
            NewState = NOTIFY_NONE;
            Desc = "Notifications and indications disabled";
            break;

         case gatt_notification:
            NewState = NOTIFY_ENABLED;
            Desc = "Notifications enabled";
            break;

         case gatt_indication:
            NewState = NOTIFY_INDIATE;
            Desc = "Indications enabled";
            break;
      }
      if(CharacteristicDesc == NULL) {
         printLog("%s for characteristic %d\r\n",Desc,p->characteristic);
      }
      else {
         printLog("%s for %s characteristic\r\n",Desc,CharacteristicDesc);
      }
   }
   else if(p->status_flags == gatt_server_confirmation) {
      printLog("gatt_server_confirmation received for >characteristic %d\r\n",
               p->characteristic);
      if(*pState == NOTIFY_WAIT) {
         NewState = NOTIFY_INDIATE;
      }
   }
   else {
      printLog("Internal error\r\n");
   }

   if(pState != NULL && NewState != NOTIFY_INVALID) {
      *pState = NewState;
   }

   if(p->characteristic == gattdb_AlexaRx && 
      p->client_config_flags == gatt_notification &&
      gBonded) 
   {  // Alexa has connected, send protocol version packet
      SendAlexaProtocolVerPkt();
   }
}

void DumpHex(const void *AdrIn,int Len)
{
   unsigned char *Adr = (unsigned char *) AdrIn;
   int i = 0;
   int j;

   while(i < Len) {
      for(j = 0; j < 16; j++) {
         if((i + j) == Len) {
            break;
         }
         printLog("%02x ",Adr[i+j]);
      }

      printLog(" ");
      for(j = 0; j < 16; j++) {
         if((i + j) == Len) {
            break;
         }
         if(isprint(Adr[i+j])) {
            printLog("%c",Adr[i+j]);
         }
         else {
            printLog(".");
         }
      }
      i += 16;
      printLog("\r\n");
   }
}

void SetAlexaAdvertisingData(bool bPairingMode)
{
   static uint8_t *pAdvData = NULL;
   uint8_t AdvDataLen;

   printLog("Creating broadcast data for %s mode:\r\n",
            bPairingMode ? "pairing" : "reconnect");
   if(pAdvData != NULL) {
      free(pAdvData);
      pAdvData = NULL;
   }
   AdvDataLen = CreateAlexaAdvertisingData(bPairingMode,&pAdvData);

   printLog("AdvDataLen: %d, pAdvData: %p\r\n",AdvDataLen,pAdvData);
   DumpHex(pAdvData,AdvDataLen);
   ERR_CHK(gecko_cmd_le_gap_bt5_set_adv_data(1,0,AdvDataLen,pAdvData));
}

void AlexaTxPacket(uint8_t *pData,uint8_t Len)
{
   printLog("Alexa tx packet %d bytes:\r\n",Len);
   gecko_cmd_gatt_server_send_characteristic_notification(
      gConnection,gattdb_AlexaRx,Len,pData);
}

// If the device name is the default (contains ????) then replace
// "????" the with the last two bytes of the MAC address in HEX
void CheckDeviceName()
{
   struct gecko_msg_system_get_bt_address_rsp_t *pBt;
   //char *cp;
   unsigned char Temp[32];
   //uint8_t Len;
   int i;
   mbedtls_sha256_context Ctx;
   int Err;
   
   do {
      if((pBt = gecko_cmd_system_get_bt_address()) == NULL) {
         printLog("gecko_cmd_system_get_bt_address failed\r\n");
         break;
      }

      snprintf(gMacAdr,sizeof(gMacAdr),"%02x%02x%02x%02x%02x%02x",
               pBt->address.addr[0],pBt->address.addr[1],
               pBt->address.addr[2],pBt->address.addr[3],
               pBt->address.addr[4],pBt->address.addr[5]);
      snprintf(gAlexaSn,sizeof(gAlexaSn),"Demo%s",gMacAdr);
      printLog("gAlexaSn set to %s\r\n",gAlexaSn);
   // Precompute the Amazon deviceToken so we have it ready for later
      mbedtls_sha256_init(&Ctx);
      mbedtls_sha256_starts_ret(&Ctx,false);
      Err = mbedtls_sha256_update_ret(&Ctx,(unsigned char*)gAlexaSn,strlen(gAlexaSn));
      if(Err != 0) {
         printLog("mbedtls_sha256_update_ret failed: 0x%x\r\n",Err);
      }
      Err = mbedtls_sha256_update_ret(&Ctx,(unsigned char*)gOurSecret,strlen(gOurSecret));
      if(Err != 0) {
         printLog("mbedtls_sha256_update_ret failed: 0x%x\r\n",Err);
      }
      Err = mbedtls_sha256_finish_ret(&Ctx,Temp);
      if(Err != 0) {
         printLog("mbedtls_sha256_finish_ret failed: 0x%x\r\n",Err);
      }
      for(i = 0; i < 32; i++) {
         sprintf((char*)&gDeviceToken[i * 2],"%02x",Temp[i]);
      }
      printLog("gDeviceToken: %s\r\n",gDeviceToken);
   } while(false);
}

