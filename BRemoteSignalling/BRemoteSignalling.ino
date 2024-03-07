#include <EEPROM.h>
#include <Bounce2.h>
#include <Logger.h>
#include "Config.h"

#define APP_VERSION F("V022")

static const char myName[] = "B-Line";


// Used Ports as as summary for a Arduino Nano
/*
2 : Signalling Button B-Line
3 : Feedback LED B-Line-Controller
8 : 
9 : RF24-NRF24L01 (CE brownwhite) / 433MHz HC-12 TX
10 : RF24-NRF24L01 (CSN brown)
11 : RF24-NRF24L01 MOSI (blue)
12 : RF24-NRF24L01 MISO (green-white)
13 : RF24-NRF24L01 SCK  (blue-white)
A7 : Analog Battery in
*/

#define PIN_SIGNAL_B_LINE 2
#define PIN_LED           3
#define PIN_RF24_CE       9
#define PIN_RF24_CSN     10
#define PIN_RF24_MOSI    11
#define PIN_RF24_MISO    12
#define PIN_RF24_SCK     13
#define PIN_BATTERY_IN   A7


static configData_t ourConfig;

#define USE_RF24
#if defined USE_RF_433
  #include <AltSoftSerial.h>
  AltSoftSerial ourRadio;  // uses internally 9:TX, 8:RX
#elif defined USE_RF24
  #include <RFTransceiver.h>
  // RFTransceiver ourRadio("Nano", 7, 8); // (CE, CSN)
  RFTransceiver ourRadio(myName, PIN_RF24_CE, PIN_RF24_CSN); // (CE, CSN)
  // USE_RF24 radio(D2, D1); // (CE, CSN)
#endif
    
unsigned long ourSecond = 0;


#include <F3XRemoteCommand.h>

F3XRemoteCommand ourRemoteCmd;
unsigned long ourTimedReset = 0;
uint16_t ourBatteryVoltage=0;

Bounce2::Button ourSignalButton = Bounce2::Button();

void(* resetFunc) (void) = 0;  //declare reset function at address 0

#ifdef USE_BUILTIN_LED
// !!! conflicts with  RF24
#define INTERNAL_LED_PIN 13    //  = LED_BUILTIN
bool ourLedState = LOW;
#endif

// SET A VARIABLE TO STORE THE LED STATE
uint8_t ourSignalBCounter=0;

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
  #if defined USE_RF_433
    ourRadio.begin(9600);
    logMsg(INFO, F("setup for RCTTransceiver/HC-12 successful "));   
  #elif defined USE_RF24
    ourRadio.begin(1);
    logMsg(INFO, F("setup for RCTTransceiver/nRF24L01 successful "));   
  #endif
  // delay(300);
}


#define USE_BATTERY_IN_VOLTAGE
#ifdef USE_BATTERY_IN_VOLTAGE
// primary battery voltage handling (LiIon Accu of battery shield)
void  setupBatteryIn() {
  logMsg(INFO, String(F("setup pin ")) + String(PIN_BATTERY_IN) + F(" for battery voltage input"));   
  pinMode(PIN_BATTERY_IN, INPUT);
}

static unsigned long ourLedTimeTill = 0;
static boolean ourLedOn = false;

void ledOn(uint16_t aDuration) {
  if (ourLedOn) {
    unsigned long dura = millis() + aDuration;
    ourLedTimeTill = max(dura, ourLedTimeTill);
  } else {
    ourLedTimeTill = millis() + aDuration;
  } 
  ourLedOn = true;
  digitalWrite(PIN_LED, HIGH);
  // logMsg(INFO, F("ledOn duration/till: ")
  //         + String(aDuration) + "/"
  //         + String(ourLedTimeTill));
}

void setupLED() {
  logMsg(INFO, F("setupLED"));
  pinMode (PIN_LED, OUTPUT );
  digitalWrite(PIN_LED, LOW);
}

void updateLED(unsigned long aNow) {
  if (ourLedOn) {
    if (aNow > ourLedTimeTill) {
      // logMsg(INFO, F("led_off"));
      ourLedOn = false;
      digitalWrite(PIN_LED, LOW);
    }
  }
}

void updateBatteryIn(unsigned long aNow) {
  static unsigned long last = 0;
  #define BAT_IN_CYCLE 3000
  #define V_REF 5000
  // DEFAULT 5127mV with USB connected
  // DEFAULT 4959mv with battery shield

  if (aNow > last) {
    last = aNow + BAT_IN_CYCLE;
    uint16_t raw = analogRead(PIN_BATTERY_IN);
    // Arduino Nano 5V can read 5V on analog in
    ourBatteryVoltage=((float) raw)/1024.0*V_REF*ourConfig.batCalibration;
  
    logMsg(INFO, String(F("battery voltage: ")) + String(ourBatteryVoltage) + String("/") + String(raw));
  } 

}
#endif

void setupSignallingButton() {
  // BUTTON SETUP 
  // INPUT_PULLUP for bare ourSignalButton connected from GND to input pin
  ourSignalButton.attach( PIN_SIGNAL_B_LINE, INPUT_PULLUP ); // USE EXTERNAL PULL-UP

  // DEBOUNCE INTERVAL IN MILLISECONDS
  ourSignalButton.interval(10); 

  // INDICATE THAT THE LOW STATE CORRESPONDS TO PHYSICALLY PRESSING THE BUTTON
  ourSignalButton.setPressedState(LOW); 
}
  
void setupLog(const char* aName) {
  Logger::getInstance().setup(aName);
  Logger::getInstance().doSerialLogging(true);
}

void setup() {

  // Open default serial to dump config to
  Serial.begin(115200);
  while (!Serial) delay(10); // wait for serial monitor
  delay(1000);
  Serial.println();

  setupLog(myName);

  logMsg(INFO, String(F("B-Line Remote Signalling: ")) + String(APP_VERSION));
 
  setupConfig();

  analogReference(DEFAULT);
  // DEFAULT 5127mV with USB connected
  // DEFAULT 4959mv with battery shield

   
  #ifdef USE_BATTERY_IN_VOLTAGE
  setupBatteryIn();
  #endif

  setupLED();

  setupRF();
  ourRemoteCmd.begin();


  setupSignallingButton();

  // LED SETUP
  #ifdef USE_BUILTIN_LED
  pinMode(INTERNAL_LED_PIN,OUTPUT);
  digitalWrite(INTERNAL_LED_PIN,ourLedState);
  #endif

}


void handleButtonEvents(unsigned long aNow) { 
  ourSignalButton.update();

  if ( ourSignalButton.pressed() ) {
    logMsg(INFO, "SignalButton pressed :" + String(++ourSignalBCounter));
    
    logMsg(INFO, "sending SignalB");
    ourRadio.transmit(ourRemoteCmd.createCommand(F3XRemoteCommandType::SignalB, String(ourSignalBCounter))->c_str(), 5);
    ledOn(400);

    #ifdef USE_BUILTIN_LED
    // TOGGLE THE LED STATE : 
    ourLedState = !ourLedState; // SET ledState TO THE OPPOSITE OF ledState
    digitalWrite(INTERNAL_LED_PIN,ourLedState);
    #endif
  }
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
      case F3XRemoteCommandType::BLineStateReq:
        {
        // String* arg = ourRemoteCmd.getArg();
        LOGGY(INFO, String("received BLineStateReq:"));
          boolean sendSuccess;
          sendSuccess = ourRadio.transmit(*ourRemoteCmd.createCommand(F3XRemoteCommandType::BLineStateResp, String(ourBatteryVoltage)), 5);
          if (!sendSuccess) {
            logMsg(INFO, String(F("sending BLineStateResp not successsfull. Retransmissions: ")) + String(ourRadio.getRetransmissionCount()));
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
  #define SHOW_SETTING_CYCLE 10000

  if (aNow > last) {
    last = aNow + SHOW_SETTING_CYCLE;
    logMsg(INFO, String(F("radio power: ")) + String( ourRadio.getPower()));
    logMsg(INFO, String(F("radio channel: ")) + String( ourRadio.getChannel()));
    logMsg(INFO, String(F("radio datarate: ")) + String( ourRadio.getDataRate()));
    logMsg(INFO, String(F("radio ack: ")) + String( ourRadio.getAck()));
  }

  if (ourTimedReset != 0 && aNow > ourTimedReset) {
     ourTimedReset = 0;
     resetFunc();
  }
}

void loop() {
  unsigned long now = millis();
  handleButtonEvents(now);
  updateRadio(now);
  updateBatteryIn(now);
  updateLED(now);

  static unsigned long next_sec = 0;

  if (now >= next_sec) {
    ourSecond++;
    next_sec = now + 1000;
  } else {
    return;
  }
  
  if (ourSecond%15 == 0) {
    ledOn(100);
  }
}
