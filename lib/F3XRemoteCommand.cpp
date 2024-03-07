#include <Arduino.h>
#include "Logger.h"
#include "F3XRemoteCommand.h"

#define F3X_RC_CMD_OUT_BUF_SIZE 30

// #define DEBUG
F3XRemoteCommand::F3XRemoteCommand() {
}

void F3XRemoteCommand::write(char aChar) {
  // reads char by char from a serial interface and put it to buffer
  //
  // #ifdef USE_RXTX_AS_GPIO
  // Serial.print("F3XRemoteCommand::write : ");
  // Serial.println(aChar);
  // Serial.print("F3XRemoteCommand::myBuffer : ");
  // Serial.println(myBuffer);
  // #endif
  myBuffer += aChar;
}
void F3XRemoteCommand::write(char* aData) {
  // reads char by char from a serial interface and put it to buffer
  //
  // #ifdef USE_RXTX_AS_GPIO
  // Serial.print("F3XRemoteCommand::write : ");
  // Serial.println(aChar);
  // Serial.print("F3XRemoteCommand::myBuffer : ");
  // Serial.println(myBuffer);
  // #endif
  myBuffer += String(aData);
}

void F3XRemoteCommand::consume() {
  int eofCmd = myBuffer.indexOf(';');
  // #ifdef USE_RXTX_AS_GPIO
  // Serial.print("F3XRemoteCommand::consume : ");
  // Serial.println(eofCmd);
  // Serial.print("F3XRemoteCommand::myBuffer : ");
  // Serial.println(myBuffer);
  // #endif
  if (eofCmd != -1) {
    myBuffer.remove(0, eofCmd+1);
  // #ifdef USE_RXTX_AS_GPIO
  // Serial.print("F3XRemoteCommand::myBuffer : ");
  // Serial.println(myBuffer);
  // #endif
  }
}

void F3XRemoteCommand::begin() {
  myBuffer = "";
}

boolean F3XRemoteCommand::available() {
  // #ifdef USE_RXTX_AS_GPIO
  // Serial.print("F3XRemoteCommand::available : ");
  // Serial.println(myBuffer.indexOf(';') != -1);
  // #endif
  return (myBuffer.indexOf(';') != -1);
}

String* F3XRemoteCommand::createCommand(F3XRemoteCommandType aCmdType, String aArg) {
  String* cmd = createCommand(aCmdType);
  cmd->remove(1,1);
  *cmd = *cmd + aArg;
  *cmd = *cmd + ";";
  #ifdef DEBUG
  #ifdef USE_RXTX_AS_GPIO
  Serial.print("F3XRemoteCommand::createCommand : ");
  Serial.println(*cmd);
  #endif
  #endif
  return cmd;
}

String* F3XRemoteCommand::createCommand(F3XRemoteCommandType aCmdType) {
  static String BUFFER;
  switch (aCmdType) {
    case F3XRemoteCommandType::CmdCycleTestAnswer:
      BUFFER="A;";
      break;
    case F3XRemoteCommandType::SignalB:
      BUFFER="B;";
      break;
    case F3XRemoteCommandType::CmdCycleTestRequest:
      BUFFER="R;";
      break;
    case F3XRemoteCommandType::CmdSetRadio:
      BUFFER="S;";
      break;
    case F3XRemoteCommandType::ValBatB:
      BUFFER="X;";
      break;
    case F3XRemoteCommandType::CmdRestartMC:
      BUFFER="Y;";
      break;
    case F3XRemoteCommandType::BLineStateReq:
      BUFFER="M;";
      break;
    case F3XRemoteCommandType::BLineStateResp:
      BUFFER="N;";
      break;
  }

  #ifdef DEBUG
  #ifdef USE_RXTX_AS_GPIO
  Serial.print("F3XRemoteCommand::createCommand : "); 
  Serial.println(BUFFER);
  #endif
  #endif
  return &BUFFER;
}

String* F3XRemoteCommand::getBuffer() {
  static String retVal;
  retVal = myBuffer;
  return &retVal;
}

String* F3XRemoteCommand::getArg(int8_t aIdx) {
  static String retVal;
  retVal=F(""); 

  int eofCmd = myBuffer.indexOf(';');
  if (eofCmd > 1) {
    retVal = myBuffer.substring(1, eofCmd);
  }
  #ifdef DEBUG
  #ifdef USE_RXTX_AS_GPIO
  Serial.print("F3XRemoteCommand::getArg : " );
  Serial.print(myBuffer);
  Serial.print("/");
  Serial.print(retVal);
  Serial.print("/");
  Serial.print(String(eofCmd));
  Serial.print("/");
  Serial.print(String(aIdx));
  Serial.println();
  #endif
  #endif

  if (aIdx > -1) {
    int found = 0;
    int strIndex[] = {0, -1};
    int maxIndex = retVal.length()-1;
      
    for(int i=0; i<=maxIndex && found<=aIdx; i++){
      if(retVal.charAt(i)== ',' || i==maxIndex){
          found++;
          strIndex[0] = strIndex[1]+1;
          strIndex[1] = (i == maxIndex) ? i+1 : i;
      }
    }
  
    retVal =  found>aIdx ? retVal.substring(strIndex[0], strIndex[1]) : "";
  }
  return &retVal;
}

F3XRemoteCommandType F3XRemoteCommand::getType() {
  F3XRemoteCommandType retVal = F3XRemoteCommandType::Invalid;
  int eofCmd = myBuffer.indexOf(';');
  if (eofCmd != -1) {
    switch ( myBuffer.charAt(0)) {
      case 'A':
        retVal = F3XRemoteCommandType::CmdCycleTestAnswer;
        break;
      case 'B':
        retVal = F3XRemoteCommandType::SignalB;
        break;
      case 'M':
        retVal = F3XRemoteCommandType::BLineStateReq;
        break;
      case 'N':
        retVal = F3XRemoteCommandType::BLineStateResp;
        break;
      case 'R':
        retVal = F3XRemoteCommandType::CmdCycleTestRequest;
        break;
      case 'S':
        retVal = F3XRemoteCommandType::CmdSetRadio;
        break;
      case 'X':
        retVal = F3XRemoteCommandType::ValBatB;
        break;
      case 'Y':
        retVal = F3XRemoteCommandType::CmdRestartMC;
        break;
      default:
        Logger::getInstance().log(ERROR, String("ERROR: F3XRemoteCommand::getCommand unknown command type, buf : ") + myBuffer);
        retVal = F3XRemoteCommandType::Invalid;
        break;
    }
  }
  return retVal;
}
