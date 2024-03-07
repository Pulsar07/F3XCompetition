#include <Arduino.h>
#include "Logger.h"
#include "RFTransceiver.h"


#define NUM_RETRIES 15
#define DELAY_RETRIES 5 

RFTransceiver::RFTransceiver(const char* aName, uint8_t aCEPin, uint8_t aCSNPin) {
  strncpy(myName, aName, 7);
  myRadio = new RF24(aCEPin, aCSNPin); // (CE, CSN)
}


void RFTransceiver::begin(uint8_t aNodeNum) {
  byte address[][6]={"F3X-A", "F3X-B"};
  
  memcpy(&myAddress[0][0], &address[aNodeNum%2][0], 6);
  memcpy(&myAddress[1][0], &address[(aNodeNum+1)%2][0], 6);

  if (!myRadio->begin()) {
    Logger::getInstance().log(INFO, F("radio hardware not responding!"));
    delay(100);
    while (1) {} // hold program in infinite loop to prevent subsequent errors
  }
  String printout = F("nRF24L01 – 2.4 GHz Radio initialized");
  printout.concat( myRadio->isPVariant()?String(F("(+ Variant)")) : String(F("(normal Variant)")));
  printout.concat( String(F("\nnRF24L01 – addresses: write:")) + String((char*) &myAddress[0][0]) + "/read:"+String((char*) &myAddress[1][0]));
  Logger::getInstance().log(INFO, printout);

  if (myRadio->isChipConnected()) {
    Logger::getInstance().log(INFO, F("nRF24L01 – radio hardware is connected!"));
  }

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

  // myRadio->openWritingPipe(address); // set the address
  // myRadio->stopListening(); // set as transmitter

  myRadio->openReadingPipe(1, &myAddress[1][0]); // set the address
  myRadio->openWritingPipe(&myAddress[0][0]);
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
    Logger::getInstance().log(INFO, String("RFTransceiver::transmit :") + String(writeRet) + F("in ") + String(millis() - start) + F("ms"));
  }
  myRadio->startListening();
  return writeRet;
}

boolean RFTransceiver::available() {
  return myRadio->available();
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
