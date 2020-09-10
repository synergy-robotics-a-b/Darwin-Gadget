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
//
// Copyright 2019 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
// These materials are licensed under the Amazon Software License in connection with the Alexa Gadgets Program.
// The Agreement is available at https://aws.amazon.com/asl/.
// See the Agreement for the specific terms and conditions of the Agreement.
// Capitalized terms not defined in this file have the meanings given to them in the Agreement.
//


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "accessories.pb.h"
#include "common.h"
#include "helpers.h"
#include "ecode.h"
#include "em_timer.h"

#include "pb.h"
#include "pb_decode.h"
#include "rx.h"
#include "tx.h"
#include "app.h"
#include "notificationsClearIndicatorDirective.pb.h"
#include "notificationsClearIndicatorDirectivePayload.pb.h"
#include "alexaGadgetStateListenerStateUpdateDirectivePayload.pb.h"
#include "alexaGadgetMusicDataTempoDirectivePayload.pb.h"
#include "alexaGadgetMusicDataTempoDirective.pb.h"
#include "directiveParser.pb.h"

#include "native_gecko.h"
#define TIMER_TICKS_PER_SEC   (19200000/ 1024)

bool gDumpRxPacket;
bool gSendSensorData;

typedef struct rx_buffer_s {
   transaction_id_t transactionId;
   stream_id_t streamId;
   uint8_t seqNum;
   size_t bufferSize;
   size_t dataSize;
   uint8_t data[];
} rx_buffer_t;

static void freeRxBufferPtr(rx_buffer_t **ppRxBuffer) {
   if(!ppRxBuffer) return;
   if(*ppRxBuffer != NULL) {
      free(*ppRxBuffer);
      *ppRxBuffer = NULL;
   }
}

void HandleTempoData(pb_istream_t *pStream);

static void handleDeviceInformationReceived(DeviceInformation const *const deviceInformation) 
{
   printLog("Called\n");
   printLog("Device Information is:\n");
   printLog("        Serial number: %s\n", deviceInformation->serial_number);
   printLog("                 Name: %s\n", deviceInformation->name);
   printLog("          Device type: %s\n", deviceInformation->device_type);
   printLog("       Num transports: %d\n", deviceInformation->supported_transports_count);
   for(int i = 0; i < deviceInformation->supported_transports_count; i++) {
      printLog("  transport[%d]: %d\n", i, deviceInformation->supported_transports[i]);
   }
}

static void handleDeviceFeaturesReceived(DeviceFeatures const *const devicefeatures) {
   printLog("Called\n");
   printLog("Device Features are:\n");
   printLog("features           : %llu\n", devicefeatures->features);
   printLog("attributes         : %llu\n", devicefeatures->device_attributes);
}

static packet_list_t *handleReceivedResponse(packet_list_t *rspPacketList, ControlEnvelope *controlEnvelope) 
{
   printLog("Received response for command: %s\n", commandToString(controlEnvelope->command));
   switch(controlEnvelope->payload.response.which_payload) {
      case Response_device_information_tag:
         handleDeviceInformationReceived(&controlEnvelope->payload.response.payload.device_information);
         break;
      case Response_device_features_tag:
         handleDeviceFeaturesReceived(&controlEnvelope->payload.response.payload.device_features);
         break;
      default:
         break;
   }
   return rspPacketList;
}

packet_list_t *handleCommandUpdateComponentSegment(packet_list_t *rspPacketList, UpdateComponentSegment *message) 
{
   printLog("Called\n");
   printLog("Segment size = %lu\n", message->segment_size);
   printLog("Component name = %s\n", message->component_name);
   printLog("Component offset = %lu\n", message->component_offset);
   printLog("signature: ");
   DumpHex((uint8_t *) &message->segment_signature[0], sizeof(message->segment_signature));

   rspPacketList = PacketList_appendList(rspPacketList, createResponseUpdateComponentSegment());
   return rspPacketList;
}

packet_list_t *handleCommandApplyFirmware(packet_list_t *rspPacketList, ApplyFirmware *applyFirmware) {
   printLog("Inside %s\n", __FUNCTION__);
   printLog("restart_required = %s\n", applyFirmware->restart_required ? "true" : "false");
   printLog("Firmware information is:\n");
   printLog("                  name : %s\n", applyFirmware->firmware_information.name);
   printLog("          version name : %s\n", applyFirmware->firmware_information.version_name);
   printLog("                locale : %s\n", applyFirmware->firmware_information.locale);
   printLog("               version : %lu\n", applyFirmware->firmware_information.version);
   printLog("       component count : %u\n", applyFirmware->firmware_information.components_count);
   if(applyFirmware->firmware_information.components_count > 0) {
      printLog("     component[0] name : %s\n", applyFirmware->firmware_information.components[0].name);
      printLog("  component[0] version : %lu\n", applyFirmware->firmware_information.components[0].version);
      printLog("     component[0] size : %lu\n", applyFirmware->firmware_information.components[0].size);
      printLog("component[0] signature: ");
      DumpHex((uint8_t *) &applyFirmware->firmware_information.components[0].signature[0],
                     sizeof(applyFirmware->firmware_information.components[0].signature));
   }
   rspPacketList = PacketList_appendList(rspPacketList, createResponseApplyFirmware());
   return rspPacketList;
}

packet_list_t *handleReceivedCommand(packet_list_t *rspPacketList, ControlEnvelope *controlEnvelope) {
   switch(controlEnvelope->command) {
      case Command_GET_DEVICE_INFORMATION:
         rspPacketList = PacketList_appendList(rspPacketList, createResponseGetDeviceInformation());
         break;
      case Command_GET_DEVICE_FEATURES:
         rspPacketList = PacketList_appendList(rspPacketList, createResponseGetDeviceFeatures());
         break;
      case Command_UPDATE_COMPONENT_SEGMENT:
         rspPacketList = handleCommandUpdateComponentSegment(rspPacketList,
                                                             &controlEnvelope->payload.update_component_segment);
         break;
      case Command_APPLY_FIRMWARE:
         rspPacketList = handleCommandApplyFirmware(rspPacketList, &controlEnvelope->payload.apply_firmware);
         break;
      default:
         rspPacketList = PacketList_appendList(rspPacketList,
                                               createResponseError(controlEnvelope->command,
                                                                   ErrorCode_UNSUPPORTED,
                                                                   0));
         break;
   }
   return rspPacketList;
}

packet_list_t *handleControlMessage(packet_list_t *rspPacketList, uint8_t *buffer, size_t bufferSize) 
{
   ControlEnvelope controlEnvelope = ControlEnvelope_init_default;
   pb_istream_t stream = pb_istream_from_buffer(buffer, bufferSize);
   if(!pb_decode(&stream, ControlEnvelope_fields, &controlEnvelope)) {
      printLog("pb_decode Failed: %s\n", PB_GET_ERROR(&stream));
      return rspPacketList;
   }
   if(controlEnvelope.which_payload == ControlEnvelope_response_tag) {
      rspPacketList = handleReceivedResponse(rspPacketList, &controlEnvelope);
   }
   else {
      rspPacketList = handleReceivedCommand(rspPacketList, &controlEnvelope);
   }
   return rspPacketList;
}

packet_list_t *handleAlexaDirective(
   packet_list_t *rspPacketList,
   uint8_t *buffer,
   size_t len) 
{
   pb_istream_t stream = pb_istream_from_buffer(buffer, len);
   directive_DirectiveParserProto *pEnv;

   pEnv = (directive_DirectiveParserProto *) malloc(sizeof(directive_DirectiveParserProto));
   do {
      if(pEnv == NULL) {
         printLog("malloc failed\n");
         break;
      }

      if(!pb_decode(&stream,directive_DirectiveParserProto_fields,pEnv)) {
         printLog("pb_decode failed: %s\n",PB_GET_ERROR(&stream));
         gDumpRxPacket = false;
         break;
      }

      printLog("Received directive %s/%s\n",pEnv->directive.header.namespace,
           pEnv->directive.header.name);

      if(strcmp(pEnv->directive.header.name, "Discover") == 0 &&
         strcmp(pEnv->directive.header.namespace, "Alexa.Discovery") == 0) 
      {
         free(pEnv);
         pEnv = NULL;
         rspPacketList = PacketList_appendList(rspPacketList,CreateDiscoveryResponse());
         break;
      }

      if(strcmp(pEnv->directive.header.namespace,"Notifications") == 0 &&
         strcmp(pEnv->directive.header.name,"ClearIndicator") == 0)
      {
         break;
      }

      if(strcmp(pEnv->directive.header.namespace,"Alexa.Gadget.StateListener") == 0 &&
         strcmp(pEnv->directive.header.name,"StateUpdate") == 0)
      {
         pb_istream_t Temp;
         alexaGadgetStateListener_StateUpdateDirectivePayloadProto *pPayload;

         pPayload = (alexaGadgetStateListener_StateUpdateDirectivePayloadProto *)
            malloc(sizeof(alexaGadgetStateListener_StateUpdateDirectivePayloadProto));
         if(pPayload == NULL) {
            printLog("malloc failed\n");
         }
         Temp = pb_istream_from_buffer(pEnv->directive.payload.bytes,
                                       pEnv->directive.payload.size);
         if(!pb_decode(&Temp,alexaGadgetStateListener_StateUpdateDirectivePayloadProto_fields,
                       pPayload))
         {
            printLog("pb_decode Failed - %s\n", PB_GET_ERROR(&stream));
         }
         else {
            int i;
            for(i = 0; i < pPayload->states_count; i++) {
            printLog("  %s = %s\n",pPayload->states[i].name,
                pPayload->states[i].value);
            }
         }
         free(pPayload);
         gDumpRxPacket = false;
         break;
      }
      if(strcmp(pEnv->directive.header.namespace,"Alexa.Gadget.MusicData") == 0 &&
         strcmp(pEnv->directive.header.name,"Tempo") == 0) 
      {
         pb_istream_t Temp;
         printLog("Received Alexa.Gadget.MusicData/Tempo:\n");

         Temp = pb_istream_from_buffer(pEnv->directive.payload.bytes,
                                       pEnv->directive.payload.size);
         HandleTempoData(&Temp);
         break;
      }
      if(strcmp(pEnv->directive.header.namespace,"Custom.ThunderGadget") == 0) {
         if(strcmp(pEnv->directive.header.name,"GetData") == 0) {
            gDumpRxPacket = false;
            gSendSensorData = true;
         }
      }
      printLog("Error: unknown directive\n");
   } while(false);

   if(pEnv != NULL && gDumpRxPacket) {
      if(pEnv->directive.payload.size > 0) {
         printLog("%d byte payload:\n",pEnv->directive.payload.size);
         DumpHex(pEnv->directive.payload.bytes,pEnv->directive.payload.size);
      }
   }

   if(pEnv != NULL) {
      free(pEnv);
   }
   return rspPacketList;
}

static packet_list_t *handleDataReceived(
   role_t role,
   packet_list_t *rspPacketList,
   stream_id_t streamId,
   transaction_id_t transactionId,
   uint8_t *buffer,
   size_t bufferSize,
   bool ack) 
{
   switch(streamId) {
      case CONTROL_STREAM: {
            if(role == ROLE_GADGET) {
               packet_t controlAck = createControlAckPacket(streamId, transactionId, ack,
                                                            CONTROL_PACKET_RESULT_SUCCESS);
               rspPacketList = PacketList_addToTail(rspPacketList, &controlAck);
            }
            rspPacketList = handleControlMessage(rspPacketList, buffer, bufferSize);
         }
         break;
      case OTA_STREAM: {
            // Handle OTA stream packets.
         }
         break;

      case ALEXA_STREAM: {
         packet_t controlAck;
         controlAck = createControlAckPacket(streamId,transactionId, ack,
                                             CONTROL_PACKET_RESULT_SUCCESS);
         rspPacketList = PacketList_addToTail(rspPacketList,&controlAck);
         rspPacketList = handleAlexaDirective(rspPacketList,buffer,bufferSize);
         break;
      }

      default:
         printLog("Unhandled stream [%u]\n",streamId);
   }
   return rspPacketList;
}

packet_list_t *decodePacket(role_t role, packet_list_t *rspPacketList, packet_t const *const packet) 
{
   if(!packet) return rspPacketList;

   uint8_t const *const buffer = packet->data;
   size_t const bufferSize = packet->dataSize;

   static rx_buffer_t *rxBuffers[3] = {NULL, NULL, NULL};
   size_t offset = 0;

   while(bufferSize > offset) {
      if(bufferSize - offset < 2) {
         printLog("Insufficient Length :: [%u/%d]\n",bufferSize - offset, 2);
         return rspPacketList;
      }
      stream_id_t streamId = (buffer[offset] >> STREAM_ID_SHIFT) & STREAM_ID_MASK;
      transaction_id_t transactionId = (buffer[offset] >> TRANSACTION_ID_SHIFT) & TRANSACTION_ID_MASK;
      offset++;
      uint8_t seqNum = (buffer[offset] >> SEQ_NUM_ID_SHIFT) & SEQ_NUM_ID_MASK;
      transaction_type_t transactionType = (buffer[offset] >> TRANSACTION_TYPE_SHIFT) & TRANSACTION_TYPE_MASK;
      bool ack = (buffer[offset] & (1U << ACK_BIT_SHIFT)) != 0;
      bool extendLength = (buffer[offset] & (1U << EXTENDED_LENGTH_BIT_SHIFT)) != 0;
      offset++;
      printLog("streamId: %d, transactionId: %d, seqNum: %d, transactionType: %d, ack: %d, extendLength: %d\n",
          streamId,transactionId,seqNum,transactionType,ack,extendLength);

      if(transactionType == TRANSACTION_TYPE_CONTROL) {
         if(bufferSize - offset < (CONTROL_PACKET_LENGTH - 2)) {
            printLog("Insufficient Length :: Control Packet [%u/%d]\n",
                  bufferSize - offset,CONTROL_PACKET_LENGTH - 2);
            return rspPacketList;
         }
         // Reserved: 1 byte.
         offset++;
         // Length: 1 byte.
         size_t length = buffer[offset++];
         assert(length == 2);
         // Reserved: 1 byte.
         offset++;
         control_ack_result_t result = buffer[offset++];
         printLog("RX ControlPacketAck: result=%s (%d)\n",
             (result == CONTROL_PACKET_RESULT_SUCCESS) ? "SUCCESS" : "UNSUPPORTED",
             result);
         continue;
      }

      int rxBufferIndex = -1;
      if(transactionType == TRANSACTION_TYPE_INITIAL) {
         if(bufferSize - offset < 5) {
            printLog("Insufficient Length :: Initial Packet [%u/%d]\n",
                 bufferSize - offset, 5);
         }
         // Reserved: 1 byte.
         offset++;

         // Total transaction Length
         uint16_t transactionLength;
         transactionLength = buffer[offset++] << 8U;
         transactionLength |= buffer[offset++] << 0U;
         printLog("New Rx Transaction %d, Stream %d, transactionLength %d\n",
             transactionId,streamId,transactionLength);
         rxBufferIndex = streamToIndex(streamId);
         if(rxBufferIndex < 0) {
            printLog("Invalid streamId [%d]. Could not create an RX Buffer.\n",
                 streamId);
            return rspPacketList;
         }
         // Ensure that this packet does not exist and then create it.
         assert(rxBuffers[rxBufferIndex] == NULL);
         rxBuffers[rxBufferIndex] = malloc(sizeof(rx_buffer_t) + transactionLength);
         if(!rxBuffers[rxBufferIndex]) {
            printLog("Failed to alloc a new RX packet for Transaction [%d] :: stream [%d]\n",
                 transactionId, streamId);
            return rspPacketList;
         }

         // Initialize the new packet.
         rxBuffers[rxBufferIndex]->streamId = streamId;
         rxBuffers[rxBufferIndex]->transactionId = transactionId;
         rxBuffers[rxBufferIndex]->bufferSize = transactionLength;
         rxBuffers[rxBufferIndex]->seqNum = 0;
         rxBuffers[rxBufferIndex]->dataSize = 0;
      }
      else {
         // Find an existing packet
         rxBufferIndex = streamToIndex(streamId);
         if(rxBufferIndex < 0) {
            printLog("Invalid streamId [%d]. Could not find an RX Buffer\n",
                 streamId);
            return rspPacketList;
         }
         if(rxBuffers[rxBufferIndex] == NULL) { // Ensure that this packet exists.
            printLog("Unable to find Rx packet :: Transaction [%d] :: Stream [%d]\n",
                 transactionId, streamId);
            return rspPacketList;
         }
         assert(rxBuffers[rxBufferIndex]->transactionId == transactionId);
         assert(rxBuffers[rxBufferIndex]->streamId == streamId);
      }
      size_t currentPayloadLength = 0;
      if(extendLength) {
         if(bufferSize - offset < 2) {
            printLog("Insufficient Length :: Extended Payload Header [%u/%d]\n",
                  bufferSize - offset,2);
            packet_t controlAck = createControlAckPacket(streamId, transactionId, ack,
                                                         CONTROL_PACKET_RESULT_FAILURE);
            rspPacketList = PacketList_addToTail(rspPacketList, &controlAck);
            freeRxBufferPtr(&rxBuffers[rxBufferIndex]);
            return rspPacketList;
         }
         currentPayloadLength |= buffer[offset++] << 8U; // MSB of payload length.
      }
      else {
         if(bufferSize - offset < 1) {
            printLog("Insufficient Length :: Extended Payload Header [%u/%d]\n",
                 bufferSize - offset, 1);
            packet_t controlAck = createControlAckPacket(streamId, transactionId, ack,
                                                         CONTROL_PACKET_RESULT_FAILURE);
            rspPacketList = PacketList_addToTail(rspPacketList, &controlAck);
            freeRxBufferPtr(&rxBuffers[rxBufferIndex]);
            return rspPacketList;
         }
      }
      currentPayloadLength |= buffer[offset++] << 0U; // LSB of payload length.

      if(bufferSize - offset < currentPayloadLength) {
         printLog("Insufficient Length :: payload [%u/%u]\n",
               bufferSize - offset,currentPayloadLength);
         packet_t controlAck = createControlAckPacket(streamId, transactionId, ack, CONTROL_PACKET_RESULT_FAILURE);
         rspPacketList = PacketList_addToTail(rspPacketList, &controlAck);
         freeRxBufferPtr(&rxBuffers[rxBufferIndex]);
         return rspPacketList;
      }

      if(rxBuffers[rxBufferIndex]->seqNum != seqNum) {
         printLog("Sequence Failed [%d] :: Expected [%d]\n", seqNum, rxBuffers[rxBufferIndex]->seqNum);
         packet_t controlAck = createControlAckPacket(streamId, transactionId, ack, CONTROL_PACKET_RESULT_FAILURE);
         rspPacketList = PacketList_addToTail(rspPacketList, &controlAck);
         freeRxBufferPtr(&rxBuffers[rxBufferIndex]);
         return rspPacketList;
      }

      // Check if destination packet has sufficient length.
      if(rxBuffers[rxBufferIndex]->bufferSize - rxBuffers[rxBufferIndex]->dataSize < currentPayloadLength) {
         printLog("Buffer Overflow :: Transaction [%d] :: Received [%u/%u] :: Packet %u\n",
              transactionId,rxBuffers[rxBufferIndex]->dataSize,
              rxBuffers[rxBufferIndex]->bufferSize, currentPayloadLength);
         packet_t controlAck = createControlAckPacket(streamId, transactionId, ack, CONTROL_PACKET_RESULT_FAILURE);
         rspPacketList = PacketList_addToTail(rspPacketList, &controlAck);
         freeRxBufferPtr(&rxBuffers[rxBufferIndex]);
         return rspPacketList;
      }
      memcpy(rxBuffers[rxBufferIndex]->data + rxBuffers[rxBufferIndex]->dataSize,
             &buffer[offset],
             currentPayloadLength);
      rxBuffers[rxBufferIndex]->dataSize += currentPayloadLength;
      offset += currentPayloadLength;
      rxBuffers[rxBufferIndex]->seqNum = (rxBuffers[rxBufferIndex]->seqNum + 1) & 0x0FU;
      printLog("Rx Progress [%u/%u] :: Stream [%d] :: Transaction [%d]\n",
             rxBuffers[rxBufferIndex]->dataSize, rxBuffers[rxBufferIndex]->bufferSize, streamId, transactionId);
      if(rxBuffers[rxBufferIndex]->dataSize == rxBuffers[rxBufferIndex]->bufferSize) {
         rspPacketList = handleDataReceived(role, rspPacketList, streamId, transactionId,
                                            rxBuffers[rxBufferIndex]->data, rxBuffers[rxBufferIndex]->dataSize, ack);
         freeRxBufferPtr(&rxBuffers[rxBufferIndex]);
      }
   }
   return rspPacketList;
}


int AlexaRxPacket(uint8_t *pData,uint8_t Len)
{
   packet_t Pkt;
   packet_list_t *response = NULL;
   packet_list_t *pLastResp;
   int Responses = 0;

   Pkt.data = pData;
   Pkt.dataSize = Len;

   gDumpRxPacket = true;

   response = decodePacket(ROLE_GADGET,response,&Pkt);
   while(response != NULL) {
      Responses++;
      AlexaTxPacket(response->packet.data,response->packet.dataSize);
      pLastResp = response;
      response = response->next;
      free(pLastResp->packet.data);
      free(pLastResp);  // data is free'ed after ack is received
   }
   printLog("%d responses sent\n",Responses);

   return 0;
}

void HandleTempoData(pb_istream_t *pStream)
{
   //pb_istream_t Temp;
   alexaGadgetMusicData_TempoDirectivePayloadProto *pPayload = NULL;
   int i;

   do {
      pPayload = (alexaGadgetMusicData_TempoDirectivePayloadProto *)
         malloc(sizeof(alexaGadgetStateListener_StateUpdateDirectivePayloadProto));
      if(pPayload == NULL) {
         printLog("malloc failed\n");
         break;
      }
      if(!pb_decode(pStream,alexaGadgetMusicData_TempoDirectivePayloadProto_fields,
                    pPayload))
      {
         printLog("pb_decode Failed - %s\n", PB_GET_ERROR(pStream));
         break;
      }
      printLog("  playerOffsetInMilliSeconds: %lu\n",
          pPayload->playerOffsetInMilliSeconds);
      printLog("  tempoData_count: %u\n",pPayload->tempoData_count);
      for(i = 0; i < pPayload->tempoData_count; i++) {
      printLog("    @%lu: %lu\n",pPayload->tempoData[i].startOffsetInMilliSeconds,
          pPayload->tempoData[i].value);
      }
      if(pPayload->tempoData[0].value == 0) {
      // End of song, turn off LEDs
         printLog("Turning off LEDS\n");
         gLedOn = false;
         SetLeds(0,0,0);
         gecko_cmd_hardware_set_soft_timer(0, 0, 0);  // Disable the timer
      }
      else {
    	  // Start LED flashing with the temp
		  // Flash @ BPM, 50% duty cycle
		  // native Gecko timer units are 1/32768s
    	  // Set timer to value based on beats per minute from MusicTempo
    	  gecko_cmd_hardware_set_soft_timer((32768*60)/(pPayload->tempoData[0].value * 2), 0, 0);
      }
      gDumpRxPacket = false;
   } while(false);

   if(pPayload != NULL) {
      free(pPayload);
   }
}
