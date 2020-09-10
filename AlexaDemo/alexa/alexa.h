/******************************************************************************
* (C) Copyright 2020 Darwin Tech, LLC, http://www.darwintechnologiesllc.com
*******************************************************************************
* This file is licensed under the Darwin Tech Embedded Software License Agreement.
* See the file "Darwin Tech - Embedded Software License Agreement.pdf" for 
* details. Read the terms of that agreement carefully.
*
* Using or distributing any product utilizing this software for any purpose
* constitutes acceptance of the terms of that agreement.
******************************************************************************/

#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#define FIRMWARE_VER             "1"

#define MANUFACTURE_NAME      "Darwin Tech"
#define FRIENDLY_NAME         "Darwin"
#define MODEL_NAME            "Darwin-003"

// #define AMAZON_DEVICE_TYPE    "<Device_Type>"   // Darwin Tech (This needs to be Unique to the Product and Company)
// #define AMAZON_SECRET         "<Amazon_Secret>"  // Darwin Tech (This is needs to be Unique to the Product and Company)

#define FWVER_MAX_LEN         8

extern const char gFwVer[FWVER_MAX_LEN];

#define BP_RESPONSE_BUF_LEN   512
/* 
   A gadget identifier, is a unique ID that you put into the firmware of a                                                                                                                            .
   gadget. It is also known as the device serial number (DSN) and endpointId,
   it never changes.  It must be unique within a device type. (The device type
   is the gadget's Amazon ID shown in the developer portal.) 
   It can contain letters or numbers, spaces, and the following special
   characters: _ - = # ; : ? @ &.  It cannot exceed 256 characters.
*/
#define ALEXA_SN_LEN       20
extern char gAlexaSn[ALEXA_SN_LEN];
extern unsigned char gDeviceToken[65];


// in alexa/rx.c
extern int32_t gBPM;
extern bool gSendSensorData;

int AlexaRxPacket(uint8_t *pData,uint8_t Len);

// in alexa/tx.c
void AlexTxPacket(uint8_t *pData,uint8_t Len);
void SetAlexAdvertisingData(bool bPairingMode);
void SendAlexaProtocolVerPkt(void);
uint8_t CreateAlexaAdvertisingData(bool bPairingMode,uint8_t **pAdvData);
void SendSensorData(int32_t F,uint32_t rhData);


// in app.c
extern bool gAlexaPaired;
void SetLeds(uint8_t Red,uint8_t Green,uint8_t Blue);

#endif   // _GLOBAL_H_

