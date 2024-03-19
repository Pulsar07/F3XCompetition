#ifndef RFTransceiver_h
#define RFTransceiver_h

#include <RF24.h>

#define RF24_1MHZ_CHANNEL_NUM 126  // channels 0 - 125 MHz

class RFTransceiver
{
public:
  RFTransceiver(const char*, uint8_t, uint8_t);
  void begin(uint8_t aNodeNum);
  void setAck(boolean);
  boolean getAck();
  void setDataRate(uint8_t);
  uint8_t getDataRate();
  void setChannel(uint8_t);
  uint8_t getChannel();
  void setPower(uint8_t);
  uint8_t getPower();
  String getPowerStr();
  void setDefaults(); // all settings to default
  boolean transmit(String, uint8_t aRetrans=0);
  boolean available();
  // boolean write(const char*);
  char* read();
  uint8_t getRetransmissionCount();
  uint8_t  getSignalStrength();
protected:
  RF24 *myRadio;
  byte myAddress[2][6];
  char mySendBuffer[33];
  char myRecvBuffer[33];
  char myName[7];
  boolean myAck;
  int8_t myRetransmitCnt;
  String myStrBuffer;
};

#endif
