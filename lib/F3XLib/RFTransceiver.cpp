#include <Arduino.h>
#include "Logger.h"
#include "RFTransceiver.h"


#define NUM_RETRIES 15
#define DELAY_RETRIES 5 

RFTransceiver::RFTransceiver(const char* aName, uint8_t aCEPin, uint8_t aCSNPin) {
  strncpy(myName, aName, 7);
  myRadio = new RF24(aCEPin, aCSNPin); // (CE, CSN)
}


void RFTransceiver::setDefaults() {
  /* Set the data rate:
   * RF24_250KBPS: 250 kbit per second
   * RF24_1MBPS:   1 megabit per second (default)
   * RF24_2MBPS:   2 megabit per second
   */
  myRadio->setDataRate(RF24_250KBPS);

  /* Set the power amplifier level rate:
   * RF24_PA_MIN:   -18 dBm
   * RF24_PA_LOW:   -12 dBm
   * RF24_PA_HIGH:   -6 dBm
   * RF24_PA_MAX:     0 dBm (default)
   */
  myRadio->setPALevel(RF24_PA_HIGH); // sufficient for tests side by side

  /* Set the channel x with x = 0...125 => 2400 MHz + x MHz 
   * Default: 76 => Frequency = 2476 MHz
   * use getChannel to query the channel
   */
  myRadio->setChannel(110);

  // setRetries(delay, count) delay = 250us+delay*250us (5 default), count 0..15 (15 default)
  // delay:5 = 1500us = 1.5ms
  // count:15
  // max delay before loss: 22.5ms
  myRadio->setRetries(DELAY_RETRIES,NUM_RETRIES);

  myAck = true;
  myRadio->setAutoAck(myAck);
  
  // /* You can choose if acknowlegdements shall be requested (true = default) or not (false) */
  // myRadio->setAutoAck(true);

  // /* with this you are able to choose if an acknowledgement is requested for 
  //  * INDIVIDUAL messages.
  //  */
  // myRadio->enableDynamicAck(); 
  // /* setRetries(byte delay, byte count) sets the number of retries until the message is
  //  * successfully sent. 
  //  * Delay time = 250 µs + delay * 250 µs. Default delay = 5 => 1500 µs. Max delay = 15.
  //  * Count: number of retries. Default = Max = 15. 
  //  */
  // myRadio->setRetries(5,15);
  // /* The default payload size is 32. You can set a fixed payload size which must be the
  //  * same on both the transmitter (TX) and receiver (RX)side. Alternatively, you can use 
  //  * dynamic payloads, which need to be enabled on RX and TX. 
  //  */
  // //myRadio->setPayloadSize(11);
  myRadio->enableDynamicPayloads();
}

void RFTransceiver::begin(F3XDeviceType aDeviceType) {

  if (!myRadio->begin()) {
    Logger::getInstance().log(LOG_MOD_RADIO, INFO, F("radio hardware not responding!"));
    delay(100);
    while (1) {} // hold program in infinite loop to prevent subsequent errors
  }
  String printout = F("nRF24L01 – 2.4 GHz Radio initialized\n   ");
  printout += myRadio->isPVariant()?String(F("(+ Variant)")) : String(F("(normal Variant)"));

  if (myRadio->isChipConnected()) {
    printout += F(": connected");
  } else {
    printout += F(": NOT connected!");
  }
  Logger::getInstance().log(LOG_MOD_RADIO, INFO, printout);

  setDefaults();

  // next major release 
  // byte rf24addr[][6]={"1abcd", "2efgh", "3efgh", "4efgh"}; //  for future releases
  // byte rf24addr[][6]={"F3X-A", "F3X-B", "G3X-B", "H3X-B"};
  byte rf24addr[][6]={"F3X-A", "F3X-B", "G3X-B", "H3X-B"};
  memcpy(&myAddress[0][0], &rf24addr[0][0], 6);  // better 1abcd : BaseManager  <-> ALineController 
  memcpy(&myAddress[1][0], &rf24addr[1][0], 6);  // better 2efgh : BaseManager  <-> BLineController
  memcpy(&myAddress[2][0], &rf24addr[2][0], 6);  // better 3efgh : BaseManager  <-> RemoteBuzzer
  memcpy(&myAddress[3][0], &rf24addr[3][0], 6);  // better 3efgh : BaseManager  <-> RemoteBuzzer

	myRadio->setAddressWidth(5);
  switch (aDeviceType) {
    case F3XBaseManager:
      myRadio->openReadingPipe(0, &myAddress[0][0]); // set the address
      myRadio->openReadingPipe(1, &myAddress[1][0]); // set the address
      myRadio->openReadingPipe(2, &myAddress[2][0]); // set the address
      myRadio->openWritingPipe(&myAddress[0][0]);
      break;
    case F3XALineController:
      Logger::getInstance().log(LOG_MOD_RADIO, ERROR, F("F3XALineController not yet implemented"));
      myRadio->openReadingPipe(0, &myAddress[1][0]); // set the address
      myRadio->openWritingPipe(&myAddress[1][0]);
      break;
    case F3XBLineController:
      myRadio->openReadingPipe(0, &myAddress[1][0]); // set the address
      myRadio->openReadingPipe(1, &myAddress[0][0]); // set the address  // to support backward comp.
      myRadio->openWritingPipe(&myAddress[1][0]);
      break;
    case F3XRemoteBuzzer:
      myRadio->openReadingPipe(0, &myAddress[2][0]); // set the address
      myRadio->openWritingPipe(&myAddress[2][0]);
      break;
  }
  myRadio->startListening(); // set as receiver
}


// void RFTransceiver::begin(uint8_t aNodeNum) {
//   // byte address[][6]={"F3X-A", "F3X-B"};
//   // memcpy(&myAddress[0][0], &address[aNodeNum%2][0], 6);
//   // memcpy(&myAddress[1][0], &address[(aNodeNum+1)%2][0], 6);
//   //
//   byte address[][6]={"F3X-A", "F3X-B", "G3X-B"};
//   return;
// 
// /*
//  * some thoughts to the RF24 addressing schema:
//  * Communication is done via pipes. 
//  * For a one-to-one communication of two peers a pipe with the same
//  * address has to be used on both sides
//  * The address does not address a source or a target, but the communication 
//  * relation / pipe between them
//  *
//  *
//  */
//   switch ( aNodeNum) {
//     case 0:
//       // better not to use something related to a peer name, 
//       // RF24 supports 6 pipes.
//       // Pipes 0 and 1 will store a full 5-byte address. 
//       // Pipes 2-5 will technically only store a single byte, 
//       // borrowing up to 4 additional bytes from pipe 1 per 
//       // the assigned address width. Pipes 1-5 should share 
//       // the same address, except the first byte. 
//       // Only the first byte in the array should be unique
//       memcpy(&myAddress[0][0], "F3X-A", 6);  // better 1abcd : BaseManager  <-> ALineController 
//       memcpy(&myAddress[1][0], "F3X-B", 6);  // better 2efgh : BaseManager  <-> BLineController
//       memcpy(&myAddress[2][0], "G3X-B", 6);  // better 3efgh : BaseManager  <-> RemoteBuzzer
//       break;
//     case 1:
//       memcpy(&myAddress[0][0], "F3X-A", 6);
//       memcpy(&myAddress[1][0], "-----", 6);
//       memcpy(&myAddress[2][0], "-----", 6);
//       break;
//     case 2:
//       memcpy(&myAddress[0][0], "G3X-B", 6);
//       memcpy(&myAddress[1][0], "-----", 6);
//       memcpy(&myAddress[2][0], "-----", 6);
//       break;
//   }
// 
//   if (!myRadio->begin()) {
//     Logger::getInstance().log(INFO, F("radio hardware not responding!"));
//     delay(100);
//     while (1) {} // hold program in infinite loop to prevent subsequent errors
//   }
//   String printout = F("nRF24L01 – 2.4 GHz Radio initialized\n   ");
//   printout += myRadio->isPVariant()?String(F("(+ Variant)")) : String(F("(normal Variant)"));
// 
//   if (myRadio->isChipConnected()) {
//     printout += F(": connected");
//   } else {
//     printout += F(": NOT connected!");
//   }
//   Logger::getInstance().log(INFO, printout);
// 
//   setDefaults();
// 
//   switch ( aNodeNum) {
//     case 0:
//       myRadio->openReadingPipe(1, &myAddress[0][0]); // set the address
//       myRadio->openReadingPipe(2, &myAddress[1][0]); // set the address
//       myRadio->openReadingPipe(3, &myAddress[2][0]); // set the address
//       myRadio->openWritingPipe(&myAddress[0][0]);
//       break;
//     case 1:
//       myRadio->openReadingPipe(1, &myAddress[0][0]); // set the address
//       myRadio->openWritingPipe(&myAddress[0][0]);
//       break;
//     case 2:
//       myRadio->openReadingPipe(1, &myAddress[0][0]); // set the address
//       myRadio->openWritingPipe(&myAddress[0][0]);  // this address will implicit set as reading pipe 0
//       break;
//   }
//   myRadio->startListening(); // set as receiver
// }

void RFTransceiver::setWritingPipe(uint8_t aPipeNumber) {
  myRadio->stopListening();
  myRadio->openWritingPipe(&myAddress[aPipeNumber][0]);
  myRadio->startListening(); // set as receiver
}

uint8_t  RFTransceiver::getSignalStrength() {

  myRadio->setRetries(0,0); // by default nrf tries 15 times. Change to no retries to measure strength
  char buffer[10];
  int counter = 0;

  for(int i=0; i<101; i++) {
    int status = myRadio->write(buffer,10); // send 32 bytes of data. It does not matter what it is
    if(status) {
       counter++;
    }
   delay(1); // try again in 1 millisecond
  }
  myRadio->setRetries(DELAY_RETRIES,NUM_RETRIES);
  return counter;
}

boolean RFTransceiver::getAck() {
  return myAck;
}

void RFTransceiver::setAck(boolean aAck) {
  myAck = aAck;
  if (aAck) {
    myRadio->setRetries(DELAY_RETRIES,NUM_RETRIES);
  } else {
    myRadio->setRetries(0, 0);
  }
  myRadio->setAutoAck(myAck);
}

uint8_t RFTransceiver::getDataRate() {
  return myRadio->getDataRate();
}

void RFTransceiver::setDataRate(uint8_t aRate) {
  myRadio->setDataRate((rf24_datarate_e) aRate);
}

uint8_t RFTransceiver::getChannel() {
  return myRadio->getChannel();
}

void RFTransceiver::setChannel(uint8_t aChannel) {
  myRadio->setChannel(aChannel);
}


uint8_t RFTransceiver::getPower() {
  return myRadio->getPALevel();
}

String RFTransceiver::getPowerStr() {
  switch (myRadio->getPALevel()) {
     case RF24_PA_MAX:
      myStrBuffer = F("max");
      break;
    case RF24_PA_HIGH: 
      myStrBuffer = F("high");
      break;
    case RF24_PA_LOW:
      myStrBuffer = F("low");
      break;
    case RF24_PA_MIN:
      myStrBuffer = F("min");
      break;
  }
  return myStrBuffer;
}

void RFTransceiver::setPower(uint8_t aPower) {
  myRadio->setPALevel(aPower);
}

boolean RFTransceiver::transmit(String aData, uint8_t aRetrans) {
  byte len = aData.length();
  memcpy(mySendBuffer, aData.c_str(), sizeof(char) * len+1);
  myRadio->stopListening();
  unsigned long start = millis();
  // aRetrans=0;
  boolean writeRet = false;
  for (myRetransmitCnt = 0 ; myRetransmitCnt < aRetrans && writeRet == false ;) {
    writeRet = myRadio->write(mySendBuffer, len);
    myRetransmitCnt += myRadio->getARC();
    //   #ifdef USE_RXTX_AS_GPIO
    //   Serial.print(myName);
    //   Serial.print(":msg send:");
    //   Serial.print(writeRet);
    //   Serial.print(":");
    //   Serial.println(mySendBuffer);
    //   #endif

    // trasmitting signal events from B->A-line is the most time critical and most essential task to be done.
    // So retransmissions can take a long time
    // to avoid "Soft WDT reset" due to long lasting loop handlings, "yielding" provides time for background processes 
    yield();
  }
  if ( (millis() - start) > 10) {
    Logger::getInstance().log(LOG_MOD_RADIO, INFO, String("RFTransceiver::transmit :") + String(writeRet) + F("in ") + String(millis() - start) + F("ms"));
  }
  myRadio->startListening();
  return writeRet;
}

boolean RFTransceiver::available() {
  boolean retVal=false;
  uint8_t pipe;
  retVal = myRadio->available(&pipe);
  if (retVal) {
    logMsg(LOG_MOD_RADIO, INFO, String(F("data  from pipe:")) + String(pipe));
  }
  return retVal;
}

uint8_t RFTransceiver::getRetransmissionCount() {
  return myRetransmitCnt;
}

char* RFTransceiver::read() {
  byte len = myRadio->getDynamicPayloadSize();
  if (len < 33) {
  myRadio->read(myRecvBuffer, len);
  myRecvBuffer[len] = 0;
  } else {
    Logger::getInstance().log(ERROR, "RFTransceiver cannot read large payload");
    myRecvBuffer[0] = 0;
  }
  // #ifdef USE_RXTX_AS_GPIO
  // Serial.print(myName);
  // Serial.print(": msg recv: ");
  // Serial.println(myRecvBuffer);
  // #endif
  return myRecvBuffer;
}
