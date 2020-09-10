//
// Copyright 2019 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
// These materials are licensed under the Amazon Software License in connection with the Alexa Gadgets Program.
// The Agreement is available at https://aws.amazon.com/asl/.
// See the Agreement for the specific terms and conditions of the Agreement.
// Capitalized terms not defined in this file have the meanings given to them in the Agreement.
//

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "accessories.pb.h"
#include "alexaDiscoveryDiscoverResponseEventPayload.pb.h"
#include "alexaDiscoveryDiscoverResponseEvent.pb.h"
#include "eventParser.pb.h"
#include "common.h"
#include "helpers.h"
#include "pb.h"
#include "pb_encode.h"
#include "alexa.h"
#include "app.h"
#include "gatt_db.h"

void InitMusicData(alexaDiscovery_DiscoverResponseEventPayloadProto_Endpoints_Capabilities *p);

static transaction_id_t getNextTransactionId(stream_id_t streamId) 
{
   static uint8_t lastTransactionId[3] = {0xff, 0xff, 0xff};

   int index = streamToIndex(streamId);
   if(index < 0) {
      return 0;
   }

   lastTransactionId[index] = (lastTransactionId[index] + 1) & 0x0f; // TransactionId is 4-bits only.
   return lastTransactionId[index];
}

packet_t createProtocolVersionPacket() 
{
   packet_t packet = {};
   uint8_t *buffer = malloc(PROTOCOL_VERSION_PACKET_SIZE);
   if(buffer) {
      packet.data = buffer;
      packet.dataSize = PROTOCOL_VERSION_PACKET_SIZE;
      memset(buffer, 0, PROTOCOL_VERSION_PACKET_SIZE);
      buffer[0] = (uint8_t) (PROTOCOL_IDENTIFIER >> 8U);
      buffer[1] = (uint8_t) (PROTOCOL_IDENTIFIER >> 0U);
      buffer[2] = PROTOCOL_VERSION_MAJOR;
      buffer[3] = PROTOCOL_VERSION_MINOR;
      buffer[4] = (uint8_t) (SAMPLE_NEGOTIATED_MTU >> 8U);
      buffer[5] = (uint8_t) (SAMPLE_NEGOTIATED_MTU >> 0U);
      buffer[6] = (uint8_t) (SAMPLE_MAX_TRANSACTION_SIZE >> 8U);
      buffer[7] = (uint8_t) (SAMPLE_MAX_TRANSACTION_SIZE >> 0U);
   }
   return packet;
}

packet_t createAdvertisingPacket(bool bPairingMode) 
{
   int i = 0;
   packet_t packet = {};

   // Advertising data (AD) is organized in LTV (Length/Tag/Value) triplets.
   uint8_t *buffer = malloc(ADV_DATA_LEN);
   if(buffer) {
      memset(buffer,0,ADV_DATA_LEN);
   // Flags
      buffer[i++] = 2;    // AD Type Length.
      buffer[i++] = 0x01; // AD Type Identifier: flags.
      buffer[i++] = BLE_AD_TYPE;
      if(bPairingMode) {
      // Service UUID
         buffer[i++] = 3;    // AD Type Length.
         buffer[i++] = 0x03; // AD Type Identifier: 16-bit service class UUID.
         buffer[i++] = (uint8_t) (SERVICE_UUID >> 0);
         buffer[i++] = (uint8_t) (SERVICE_UUID >> 8);
         buffer[i++] = 23;   // AD Type Length.
      }
      else {
         buffer[i++] = 27;   // AD Type Length.
      }
      // Service data
      buffer[i++] = 0x16; // AD Type identifier: service data.
      buffer[i++] = (uint8_t) (SERVICE_UUID >> 0);
      buffer[i++] = (uint8_t) (SERVICE_UUID >> 8);
      buffer[i++] = (uint8_t) (VENDOR_ID >> 0);
      buffer[i++] = (uint8_t) (VENDOR_ID >> 8);
      buffer[i++] = 0x00;          // reserved.
      buffer[i++] = 0xFF;          // Product category.
      buffer[i++] = 0x00;          // reserved.
      if(bPairingMode) {
         buffer[i++] = 1;          // gadget in pairing mode
      }
   }

   if(buffer) {
      packet.data = buffer;
      packet.dataSize = ADV_DATA_LEN;
   }
   return packet;
}

packet_t createControlAckPacket(
   stream_id_t streamId,
   transaction_id_t transactionId,
   bool ack,
   control_ack_result_t result) 
{
   packet_t packet = {};
   uint8_t *buffer;

   if(ack && (buffer = malloc(CONTROL_PACKET_LENGTH)) != NULL) {
      printLog("transactionId: %d, result: %d\n", transactionId, result); //  ack,
      buffer[0] = (streamId & STREAM_ID_MASK) << STREAM_ID_SHIFT;
      buffer[0] |= (transactionId & TRANSACTION_ID_MASK) << TRANSACTION_ID_SHIFT;
      buffer[1] = (TRANSACTION_TYPE_CONTROL & TRANSACTION_TYPE_MASK) << TRANSACTION_TYPE_SHIFT;
      buffer[1] |= (result == CONTROL_PACKET_RESULT_SUCCESS) ? (1U << ACK_BIT_SHIFT) : 0;
      buffer[2] = 0; // Reserved.
      buffer[3] = 2; // Length 2 bytes.
      buffer[4] = 1; // Reserved
      buffer[5] = result;

      packet.data = buffer;
      packet.dataSize = CONTROL_PACKET_LENGTH;
   }
   else if(ack) {
      printLog("malloc failed\n");
   }
   return packet;
}

static packet_list_t *buildStreamPacket(stream_id_t streamId, bool ack, uint8_t *payload, size_t payloadSize) 
{
   packet_list_t *Ret = NULL;
   size_t remainingSize = payloadSize;
   packet_list_t *packetListHead = NULL;
   uint8_t seqNum = 0;
   transaction_id_t transactionId = 0;
   size_t srcIndex = 0;

   do {
      // https://developer.amazon.com/docs/alexa-gadgets-toolkit/packet-ble.html#packet-format
      if(streamId != CONTROL_STREAM && streamId != OTA_STREAM && streamId != ALEXA_STREAM) {
         printLog( "Invalid argument streamId");
         break;
      }
      if(payloadSize > 0xffff) {
         printLog( "Invalid argument payloadSize");
         break;
      }

      while(remainingSize > 0) {
         // Calculate the current size.
         size_t currentPacketHeaderSize = 3;
         transaction_type_t transactionType = TRANSACTION_TYPE_CONTINUE;
         if(srcIndex == 0) { // First packet header
            transactionType = TRANSACTION_TYPE_INITIAL;
            transactionId = getNextTransactionId(streamId);
            printLog("New Tx Transaction [%d] :: Stream [%d]\n", transactionId, streamId);
            currentPacketHeaderSize += 3;
         }
         size_t currentPacketPayloadSize = MIN(SAMPLE_NEGOTIATED_MTU - currentPacketHeaderSize, remainingSize);
         bool extendedLength = false;
         if(currentPacketPayloadSize > 0xff) {
            extendedLength = true;
            currentPacketHeaderSize++;
            if(currentPacketPayloadSize > SAMPLE_NEGOTIATED_MTU - currentPacketHeaderSize) {
               currentPacketPayloadSize--;
            }
         }

         // Make the last packet as final.
         if(transactionType == TRANSACTION_TYPE_CONTINUE && currentPacketPayloadSize == remainingSize) {
            transactionType = TRANSACTION_TYPE_FINAL;
         }

         size_t currentPacketSize = currentPacketHeaderSize + currentPacketPayloadSize;
         uint8_t *const buffer = malloc(currentPacketSize);
         if(buffer) {
            size_t dstIndex = 0;
            // StreamId: 4 bits
            buffer[dstIndex] = (streamId & STREAM_ID_MASK) << STREAM_ID_SHIFT;
            // TransactionId: 4 bits
            buffer[dstIndex] |= (transactionId & TRANSACTION_ID_MASK) << TRANSACTION_ID_SHIFT;
            dstIndex++;

            // Sequence number: 4 bits
            buffer[dstIndex] = (seqNum & SEQ_NUM_ID_MASK) << SEQ_NUM_ID_SHIFT;
            seqNum = (seqNum + 1) & SEQ_NUM_ID_MASK;
            // Transaction type: 4 bits
            buffer[dstIndex] |= (transactionType & TRANSACTION_TYPE_MASK) << TRANSACTION_TYPE_SHIFT;
            // ACK: 1 bit
            buffer[dstIndex] |= (ack) ? (1U << ACK_BIT_SHIFT) : 0;
            // LengthExtender: 1 bit
            buffer[dstIndex] |= (extendedLength) ? (1U << EXTENDED_LENGTH_BIT_SHIFT) : 0;
            dstIndex++;

            if(transactionType == TRANSACTION_TYPE_INITIAL) {
               // Reserved: 8 bits
               buffer[dstIndex++] = 0x00;
               // Total transaction length.
               buffer[dstIndex++] = payloadSize >> 8U;
               buffer[dstIndex++] = payloadSize >> 0U;
            }

            if(extendedLength) {
               // ExtendedLength: extra 8 bits in payload length.
               buffer[dstIndex++] = currentPacketPayloadSize >> 8U;
            }
            // Length: 8 bits.
            buffer[dstIndex++] = currentPacketPayloadSize >> 0U;

            // Payload
            memcpy(&buffer[dstIndex],&payload[srcIndex],currentPacketPayloadSize);
            dstIndex += currentPacketPayloadSize;
            srcIndex += currentPacketPayloadSize;
            printLog("Tx Progress [%d/%d] :: Stream [%d] :: Transaction [%d]\n",
                srcIndex, payloadSize, streamId,transactionId);

            // Append this packet to the list of buffers ready for TX.
            packet_t packet = {};
            packet.data = buffer;
            packet.dataSize = currentPacketSize;
            packetListHead = PacketList_addToTail(packetListHead, &packet);
         }
         else {
            printLog("Failed to allocate memory for TX packet.");
            break;
         }
         // Update the remaining size.
         remainingSize -= currentPacketPayloadSize;
      }
      if(remainingSize == 0) {
         Ret = packetListHead;
      }
   } while(false);

   return Ret;
}

static packet_list_t *createControlPacket(ControlEnvelope const *const controlEnvelope, bool ackRequired) 
{
   size_t encoded_size;
   if(!pb_get_encoded_size(&encoded_size, ControlEnvelope_fields, controlEnvelope)) {
      printLog( "Failed To Calculate Control Envelope Encoded Size");
      return NULL;
   }
   uint8_t buffer[encoded_size];
   pb_ostream_t stream = pb_ostream_from_buffer(buffer, sizeof(buffer));
   bool status = pb_encode(&stream, ControlEnvelope_fields, controlEnvelope);
   if(!status) {
      printLog("pb_encode failed: %s\n",PB_GET_ERROR(&stream));
      return NULL;
   }
   return buildStreamPacket(CONTROL_STREAM, ackRequired, buffer, stream.bytes_written);
}

packet_list_t *createResponseError(Command cmd, ErrorCode errorCode, uint16_t tag) 
{
   ControlEnvelope controlEnvelope = ControlEnvelope_init_default;
   controlEnvelope.command = cmd;
   controlEnvelope.which_payload = ControlEnvelope_response_tag;

   Response *response = &controlEnvelope.payload.response;
   response->error_code = errorCode;
   response->which_payload = tag;

   printLog("Creating response error for command: %s\n", commandToString(controlEnvelope.command));
   return createControlPacket(&controlEnvelope, false);
}

packet_list_t *createResponseGetDeviceInformation() 
{
   ControlEnvelope controlEnvelope = ControlEnvelope_init_default;
   controlEnvelope.command = Command_GET_DEVICE_INFORMATION;
   controlEnvelope.which_payload = ControlEnvelope_response_tag;
   controlEnvelope.payload.response.error_code = ErrorCode_SUCCESS;
   controlEnvelope.payload.response.which_payload = Response_device_information_tag;
   DeviceInformation *deviceInformation = &controlEnvelope.payload.response.payload.device_information;

   strcpy(deviceInformation->serial_number,gAlexaSn);
   strcpy(deviceInformation->name,FRIENDLY_NAME);
   deviceInformation->supported_transports_count = 1;
   deviceInformation->supported_transports[0] = Transport_BLUETOOTH_LOW_ENERGY;
   strcpy(deviceInformation->device_type,AMAZON_DEVICE_TYPE);

   printLog("Creating response: %s\n", commandToString(controlEnvelope.command));
   return createControlPacket(&controlEnvelope, false);
}

packet_list_t *createResponseGetDeviceFeatures() 
{
   ControlEnvelope controlEnvelope = ControlEnvelope_init_default;
   controlEnvelope.command = Command_GET_DEVICE_FEATURES;
   controlEnvelope.which_payload = ControlEnvelope_response_tag;
   controlEnvelope.payload.response.error_code = ErrorCode_SUCCESS;
   controlEnvelope.payload.response.which_payload = Response_device_features_tag;

   DeviceFeatures *deviceFeatures = &controlEnvelope.payload.response.payload.device_features;
   deviceFeatures->features = 0x13; // Support Alexa Gadgets Toolkit and OTA.
   // deviceFeatures->features = 0x11; // Support Alexa Gadgets Toolkit

   printLog("Creating response: %s\n", commandToString(controlEnvelope.command));
   return createControlPacket(&controlEnvelope, false);
}

packet_list_t *createResponseUpdateComponentSegment() 
{
   ControlEnvelope controlEnvelope = ControlEnvelope_init_default;
   controlEnvelope.command = Command_UPDATE_COMPONENT_SEGMENT;
   controlEnvelope.which_payload = ControlEnvelope_response_tag;
   controlEnvelope.payload.response.error_code = ErrorCode_SUCCESS;

   printLog("Creating response: %s\n", commandToString(controlEnvelope.command));
   return createControlPacket(&controlEnvelope, false);
}

packet_list_t *createResponseApplyFirmware() 
{
   ControlEnvelope controlEnvelope = ControlEnvelope_init_default;
   controlEnvelope.command = Command_APPLY_FIRMWARE;
   controlEnvelope.which_payload = ControlEnvelope_response_tag;
   controlEnvelope.payload.response.error_code = ErrorCode_SUCCESS;

   printLog("Creating response: %s\n", commandToString(controlEnvelope.command));
   return createControlPacket(&controlEnvelope, false);
}

packet_list_t *CreateDiscoveryResponse() 
{
   packet_list_t *packetList = NULL;
   uint8_t *pBuf;
   pb_ostream_t stream;
   alexaDiscovery_DiscoverResponseEventProto *pResp;

   printLog("Creating discover response event:\n");

   printLog("sizeof(alexaDiscovery_DiscoverResponseEventProto): %u %u:\n",
       sizeof(alexaDiscovery_DiscoverResponseEventProto),
       sizeof(*pResp));

   pResp = (alexaDiscovery_DiscoverResponseEventProto *) 
            malloc(sizeof(*pResp));
   pBuf = malloc(BP_RESPONSE_BUF_LEN);

   do {
      if(pResp == NULL || pBuf == NULL) {
         printLog("malloc failed\n");
         break;
      }
      memset(pResp,0,sizeof(*pResp));
      stream = pb_ostream_from_buffer(pBuf,BP_RESPONSE_BUF_LEN);

      pResp->has_event = true;
      pResp->event.has_header = true;
      strcpy(pResp->event.header.namespace, "Alexa.Discovery");
      strcpy(pResp->event.header.name, "Discover.Response");

      pResp->event.has_payload = true;
      pResp->event.payload.endpoints_count = 1;
      strcpy(pResp->event.payload.endpoints[0].endpointId,gAlexaSn);
      strcpy(pResp->event.payload.endpoints[0].friendlyName,FRIENDLY_NAME);
      strcpy(pResp->event.payload.endpoints[0].manufacturerName,MANUFACTURE_NAME);

      pResp->event.payload.endpoints[0].capabilities_count = 4;
      strcpy(pResp->event.payload.endpoints[0].capabilities[0].type, "AlexaInterface");
      strcpy(pResp->event.payload.endpoints[0].capabilities[0].interface, "Notifications");
      strcpy(pResp->event.payload.endpoints[0].capabilities[0].version, "1.0");

      strcpy(pResp->event.payload.endpoints[0].capabilities[1].type, "AlexaInterface");
      strcpy(pResp->event.payload.endpoints[0].capabilities[1].interface, "Custom.ThunderGadget");
      strcpy(pResp->event.payload.endpoints[0].capabilities[1].version, "1.0");

      strcpy(pResp->event.payload.endpoints[0].capabilities[2].type, "AlexaInterface");
      strcpy(pResp->event.payload.endpoints[0].capabilities[2].interface, "Alexa.Gadget.StateListener");
      strcpy(pResp->event.payload.endpoints[0].capabilities[2].version, "1.0");
      pResp->event.payload.endpoints[0].capabilities[2].has_configuration = 1;
      pResp->event.payload.endpoints[0].capabilities[2].configuration.supportedTypes_count = 1;
      strcpy(pResp->event.payload.endpoints[0].capabilities[2].configuration.supportedTypes[0].name,
             "wakeword");

      pResp->event.payload.endpoints[0].has_additionalIdentification = true;
      strcpy(pResp->event.payload.endpoints[0].additionalIdentification.firmwareVersion,gFwVer);

      InitMusicData(&pResp->event.payload.endpoints[0].capabilities[3]);

   // A UTF-8-encoded string that contains the SHA256 of the following: the 
   // endpointId concatenated with the Alexa Gadget Secret that is shown in the 
   // developer portal after you register your gadget.

      strcpy(pResp->event.payload.endpoints[0].additionalIdentification.deviceToken,(const char *)gDeviceToken);
   // The device secret algorithm. The only valid value is currently 1, which means that the algorithm is SHA256.
      strcpy(pResp->event.payload.endpoints[0].additionalIdentification.deviceTokenEncryptionType,"1");
      strcpy(pResp->event.payload.endpoints[0].additionalIdentification.amazonDeviceType,AMAZON_DEVICE_TYPE);
      strcpy(pResp->event.payload.endpoints[0].additionalIdentification.modelName,MODEL_NAME);
      strcpy(pResp->event.payload.endpoints[0].additionalIdentification.radioAddress,&gAlexaSn[4]);

      printLog("pResp: \n");
      DumpHex(pResp,sizeof(*pResp));
      bool status = pb_encode(&stream,alexaDiscovery_DiscoverResponseEventProto_fields,pResp);
      if(!status) {
         printLog("pb_encode failed: %s\n",PB_GET_ERROR(&stream));
      }
      else {
         printLog("bytes written: %u\n",stream.bytes_written);
         DumpHex(pBuf,stream.bytes_written);
         free(pResp);
         pResp = NULL;
         packetList = buildStreamPacket(ALEXA_STREAM,false,pBuf,
                                        stream.bytes_written);
      }
   } while(false);

   if(pResp != NULL) {
      free(pResp);
   }

   if(pBuf != NULL) {
      free(pBuf);
   }
   return packetList;
}


void InitMusicData(alexaDiscovery_DiscoverResponseEventPayloadProto_Endpoints_Capabilities *p)
{
   printLog("Adding AlexaInterface: Alexa.Gadget.MusicData\n");

   strcpy(p->type,"AlexaInterface");
   strcpy(p->interface,"Alexa.Gadget.MusicData");
   strcpy(p->version,"1.0");
   p->has_configuration = true;
   p->configuration.supportedTypes_count = 1;
   strcpy(p->configuration.supportedTypes[0].name,"tempo");
}

void SendAlexaProtocolVerPkt()
{
   packet_t Pkt = createProtocolVersionPacket();
   AlexaTxPacket(Pkt.data,Pkt.dataSize);
   free(Pkt.data);
}

uint8_t CreateAlexaAdvertisingData(bool bPairingMode,uint8_t **pAdvData)
{
   packet_t Pkt = createAdvertisingPacket(bPairingMode);
   *pAdvData = Pkt.data;

   return (uint8_t) Pkt.dataSize;
}

void SendSensorData(int32_t F,uint32_t rhData)
{
   packet_list_t *packetList = NULL;
   uint8_t *pBuf;
   pb_ostream_t stream;
   event_EventParserProto *pResp;

   pResp = (event_EventParserProto *) malloc(sizeof(*pResp));
   pBuf = malloc(BP_RESPONSE_BUF_LEN);

   do {
      if(pResp == NULL || pBuf == NULL) {
         printLog("malloc failed\n");
         break;
      }
      memset(pResp,0,sizeof(*pResp));
      stream = pb_ostream_from_buffer(pBuf,BP_RESPONSE_BUF_LEN);

      pResp->has_event = true;
      pResp->event.has_header = true;
      strcpy(pResp->event.header.namespace, "Custom.ThunderGadget");
      strcpy(pResp->event.header.name, "GetDataReport");
      pResp->event.payload.size = 
         snprintf((char *) pResp->event.payload.bytes,
                  sizeof(pResp->event.payload.bytes),
                  "{\"temperature\": %ld, \"RH\": %ld}",F, rhData);
      printLog("Payload: \n");
      DumpHex(pResp->event.payload.bytes,pResp->event.payload.size);

      bool status = pb_encode(&stream,&event_EventParserProto_msg,pResp);
      if(!status) {
         printLog("pb_encode failed: %s\n",PB_GET_ERROR(&stream));
      }
      else {
         printLog("bytes written: %u\n",stream.bytes_written);
         DumpHex(pBuf,stream.bytes_written);
         free(pResp);
         pResp = NULL;
         packetList = buildStreamPacket(ALEXA_STREAM,false,pBuf,
                                        stream.bytes_written);
      }
   } while(false);

   if(pResp != NULL) {
      free(pResp);
   }

   if(pBuf != NULL) {
      free(pBuf);
   }

   AlexaTxPacket(packetList->packet.data,packetList->packet.dataSize);
   PacketList_freeList(packetList);
}

