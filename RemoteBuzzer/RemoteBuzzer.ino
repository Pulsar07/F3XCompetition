/**     
 * \file RadioBuzzer.ino
 *    
 * \brief tool support trainings data measurement  
 *
 * \author Author: Rainer Stransky
 *      
 * \copyright This project is released under the GNU Public License v3
 *          see https://www.gnu.org/licenses/gpl.html.
 * Contact: opensource@so-fa.de
 *      
 */ 
    

#include <EEPROM.h>
#include <Logger.h>
#include "PinManager.h"
#include "Config.h"

#define APP_VERSION F("V010")

static const char myName[] = "RemoteBuzzer";


// Used Ports as as summary for a Arduino Nano
/*
8 : Buzzer Signal Out
9 : RF24-NRF24L01 (CE brownwhite)
10 : RF24-NRF24L01 (CNS brown)
11 : RF24-NRF24L01 MOSI (blue)
12 : RF24-NRF24L01 MISO (green-white)
13 : RF24-NRF24L01 SCK  (blue-white)
A0 : Analog Battery in with a 2K2 / 2K2 Ohm
     voltage divider for a LiIon 2s1p
*/

#define PIN_BUZZER_OUT    8
#define PIN_RF24_CE       9
#define PIN_RF24_CNS     10
#define PIN_RF24_MOSI    11
#define PIN_RF24_MISO    12
#define PIN_RF24_SCK     13
#define PIN_BATTERY_IN   A0


static configData_t ourConfig;

#include <RFTransceiver.h>
RFTransceiver ourRadio(myName, PIN_RF24_CE, PIN_RF24_CNS); // (CE, CNS)
    
unsigned long ourSecond = 0;


#include <F3XRemoteCommand.h>

F3XRemoteCommand ourRemoteCmd;
unsigned long ourTimedReset = 0;
uint16_t ourBatteryVoltage=0;
uint16_t ourBatteryVoltageRaw=0;

void(* resetFunc) (void) = 0;  //declare reset function at address 0

void saveConfig() {
  logMsg(INFO, F("saving config to EEPROM "));   

  for(int i=0; i<CONFIG_VERSION_L;i++) {
    EEPROM.write(P_VERSION+i, ourConfig.version[i]);
  }
  EEPROM.put(P_BAT_CALIBRATION, ourConfig.batCalibration);
}

void setDefaultConfig() {
  logMsg(INFO, F("setting default config to EEPROM "));   
  // Reset EEPROM bytes to '0' for the length of the data structure
  strncpy(ourConfig.version , CONFIG_VERSION, CONFIG_VERSION_L);
  ourConfig.batCalibration = 1.0f;
  saveConfig();
}


void loadConfig() {
  logMsg(INFO, F("loading config from EEPROM "));   
  // Loads configuration from EEPROM into RAM
  for(int i=0; i<CONFIG_VERSION_L;i++) {
    ourConfig.version[i] = EEPROM.read(P_VERSION+i);
  }
  EEPROM.get(P_BAT_CALIBRATION, ourConfig.batCalibration);

  // config was never written to EEPROM, so set the default config data and save it to EEPROM
  if ( String(CONFIG_VERSION) == ourConfig.version ) {
     // ok do nothing, this is the expected version
  } else if (String("XYZ_") == ourConfig.version ) {
     // ok do nothing, this is the expected version
    logMsg(WARNING, String(F("old but compatible config version found: ")) + String(ourConfig.version));
  } else {
    logMsg(WARNING, String(F("unexpected config version found: ")) + String(ourConfig.version));
    setDefaultConfig();
  }
}


void setupConfig() {
  loadConfig();
}

void setupRF() {
  ourRadio.begin(2);
  logMsg(INFO, F("setup for RCTTransceiver/nRF24L01 successful "));   
}


#define USE_BATTERY_IN_VOLTAGE
#ifdef USE_BATTERY_IN_VOLTAGE
// primary battery voltage handling (LiIon Accu of battery shield)
void  setupBatteryIn() {
  logMsg(INFO, String(F("setup pin ")) + String(PIN_BATTERY_IN) + F(" for battery voltage input"));   
  pinMode(PIN_BATTERY_IN, INPUT);
}

PinManager ourBuzzer(PIN_BUZZER_OUT);

void updateBatteryIn(unsigned long aNow) {
  static unsigned long last = 0;
  #define BAT_IN_CYCLE 3000
  #define V_REF 5000
  // DEFAULT 5127mV with USB connected
  // DEFAULT 4959mv with battery shield

  if (aNow > last) {
    last = aNow + BAT_IN_CYCLE;
    ourBatteryVoltageRaw = analogRead(PIN_BATTERY_IN);
    // Arduino Nano 5V can read 5V on analog in
    ourBatteryVoltage=((float) ourBatteryVoltageRaw)/1024.0*V_REF*2.0f*ourConfig.batCalibration;
  
    logMsg(INFO, String(F("battery voltage: ")) + String(ourBatteryVoltage) + String("/") + String(ourBatteryVoltageRaw));
  } 

}
#endif

void setupLog(const char* aName) {
  Logger::getInstance().setup(aName);
  Logger::getInstance().doSerialLogging(true);
  Logger::getInstance().setLogLevel(LOG_MOD_ALL, DEBUG);
}

void setup() {
  // Open default serial to dump config to
  Serial.begin(115200);
  while (!Serial) delay(10); // wait for serial monitor
  delay(1000);
  Serial.println();

  setupLog(myName);

  logMsg(INFO, String(F("RadioBuzzer: ")) + String(APP_VERSION));
 
  setupConfig();

  analogReference(DEFAULT);
  // DEFAULT 5127mV with USB connected
  // DEFAULT 4959mv with battery shield

   
  #ifdef USE_BATTERY_IN_VOLTAGE
  setupBatteryIn();
  #endif

  setupRF();
  ourRemoteCmd.begin();
  ourBuzzer.pattern(5, 100,50,100,50,500);
}


void updateRadio(unsigned long aNow) { 
  while (ourRadio.available()) { 
    ourRemoteCmd.write(ourRadio.read());
  }

  if (ourRemoteCmd.available()) {
    String* arg;
    uint8_t argNum = -1;
    int8_t radioPower;
    uint8_t radioChannel;
    int8_t radioDatarate;
    boolean radioAck;
    switch (ourRemoteCmd.getType()) {
      case F3XRemoteCommandType::RemoteSignalBuzz:
        {
          int duration=ourRemoteCmd.getArg(0)->toInt();
          LOGGY(INFO, String("received RemoteSignalBuzz:") + String(duration));
          ourBuzzer.on(duration);
        }
        break;
      case F3XRemoteCommandType::CmdSetRadio:
        radioPower=ourRemoteCmd.getArg(0)->toInt();
        radioChannel=ourRemoteCmd.getArg(1)->toInt();
        radioDatarate=ourRemoteCmd.getArg(2)->toInt();
        radioAck=ourRemoteCmd.getArg(3)->toInt()==1?true:false;

        ourRadio.setPower(radioPower);
        ourRadio.setChannel(radioChannel);
        ourRadio.setDataRate(radioDatarate);
        ourRadio.setAck(radioAck);


        logMsg(INFO, String("received CmdSetPower: power: ") + String(radioPower));
        logMsg(INFO, String("received CmdSetPower: channel: ") + String(radioChannel));
        logMsg(INFO, String("received CmdSetPower: datarate: ") + String(radioDatarate));
        logMsg(INFO, String("received CmdSetPower: ack: ") + String(radioAck));
        logMsg(INFO, String("received CmdSetPower: power,chan,rate,ack: ") + *ourRemoteCmd.getArg());
        break;
      case F3XRemoteCommandType::CmdRestartMC:
        ourTimedReset = aNow + 500; // reset in 500ms
        break;
      case F3XRemoteCommandType::CmdCycleTestRequest:
        arg = ourRemoteCmd.getArg();
        LOGGY(INFO, String("received CmdCycleTestRequest:") + *arg);
        if (arg->toInt() == 17) {
          LOGGY(INFO, String("!!!!!!!!!!!!!!  ignoring CmdCycleTestAnswer:") + *arg);
        } else {
          LOGGY(INFO, String("sending CmdCycleTestAnswer:") + *arg);
          boolean sendSuccess;
          sendSuccess = ourRadio.transmit(*ourRemoteCmd.createCommand(F3XRemoteCommandType::CmdCycleTestAnswer, *arg), 5);
          if (!sendSuccess) {
            logMsg(INFO, String(F("sending TestAnswer not successsfull. Retransmissions: ")) + String(ourRadio.getRetransmissionCount()));
          }
        }
        break;
      case F3XRemoteCommandType::RemoteSignalStateReq:
        {
        // String* arg = ourRemoteCmd.getArg();
        LOGGY(INFO, String("received RemoteSignalStateReq:"));
          boolean sendSuccess;
          sendSuccess = ourRadio.transmit(*ourRemoteCmd.createCommand(F3XRemoteCommandType::RemoteSignalStateResp, String(ourBatteryVoltageRaw)), 5);
          if (!sendSuccess) {
            logMsg(INFO, String(F("sending RemoteSignalStateResp not successsfull. Retransmissions: ")) + String(ourRadio.getRetransmissionCount()));
          }
        }
        break;
      default:
        logMsg(INFO, "consuming wrong command");
        break;
    }
    ourRemoteCmd.consume();
  }

  static unsigned long last = 0;
  #define SHOW_SETTING_CYCLE 60000

  if (aNow > last) {
    last = aNow + SHOW_SETTING_CYCLE;
    logMsg(INFO, String(F("radio power: ")) + String( ourRadio.getPower()));
    logMsg(INFO, String(F("radio channel: ")) + String( ourRadio.getChannel()));
    logMsg(INFO, String(F("radio datarate: ")) + String( ourRadio.getDataRate()));
    logMsg(INFO, String(F("radio ack: ")) + String( ourRadio.getAck()));
  }
}

void updateTimedEvents(unsigned long aNow) {
  if (ourTimedReset != 0 && aNow > ourTimedReset) {
     ourTimedReset = 0;
     resetFunc();
  }
}

void loop() {
  unsigned long now = millis();
  updateRadio(now);
  ourBuzzer.update(now);
  updateBatteryIn(now);
  updateTimedEvents(now);

  static unsigned long next_sec = 0;

  if (now >= next_sec) {
    ourSecond++;
    next_sec = now + 1000;
  } else {
    return;
  }
}
