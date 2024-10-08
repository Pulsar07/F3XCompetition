/**
 * \file BaseManager.ino
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
#include <limits.h>
#include <ESP8266WiFi.h>
// #define USE_MDNS
#ifdef USE_MDNS
#include <ESP8266mDNS.h>
#endif
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266httpUpdate.h>
#include <Bounce2.h>
#include <Encoder.h>

#include "Logger.h"
#include "PinManager.h"
#include "LittleFS.h"
#include "Config.h"
#include "F3XFixedDistanceTask.h"
#include "F3XFixedDistanceTaskData.h"
#include "settings.h"

#define USE_RXTX_AS_GPIO  // for usage of rotary encoder instead of Serial

#define APP_VERSION "V035"

/*
Version History
 V011 19.10.2023: RS : pre-version: test web interface enhanced, minor bugs solved, OTA implemented
 V010 18.10.2023: RS : pre-version: initial version for F3B speed task and training data, with web interface only
 V023 26.02.2024: RS : firmware update triggerd on ABaseManager via Einstellungen->3:Update Firmw.;
 V024 08.03.2024: RS : pre version of RF24 channel and power setting via controller and OLED
 V025 13.03.2024: RS : A-line controller with rotary encoder, settings in EEPROM via OLED and Web.
 V026 13.03.2024: RS : Web-pages redesigned
 V027 15.03.2024: RS : Buzzer refactored to PinManager-class to support beep sequences for battery low warning
 V028 18.03.2024: RS : new speed start / back menu added
 V029 19.03.2024: RS : F3B Speed Tasktime is now a config item.
 V030 28.03.2024: RS : Refactoring F3BSpeedTask -> F3XFixedDistanceTask, more consistent time strings and units (CSV)
 V031 04.05.2024: RS : added support for a radio buzzer 
 V032 31.07.2024: RS : F3F Task added, Logger refactored, TaskData handling generalized, address schema of RFTransceiver fixed.
 V033 01.08.2024: RS : Support for F3X Loop Task, finalizing the course will start frame time of next starter automatically
 V034 02.08.2024: RS : nRF24 address scheme modified to support old versions of remote devices 
 V035 19.08.2024: RS : bugfix: buzzer settings not saved, added missing F3F A-Line flyover signal,  long 1.5s finish signals F3F+F3B, 
                       interpreation of F3F Tasktime is now: time between StartTaskSignal and launch time,
                       more precise naming of entities/files: BaseManager, LineController  
*/

/**
Feature list:
* Battery warning
* F3B Speed, F3F Task
*
* ...
*/

static const char myName[] = "BaseM";
static const char MYSEP_STR[] = "~~~";
static const char CMDSEP_STR[] = ",";

// Used Ports as as summary
/*
D0 : RF24-NRF24L01 CE (brown-white)
D1 : OLED-SSD1306 SCL
D2 : OLED-SSD1306 SDA 
D3 : RF24-NRF24L01 CNS (brown) 
D4 : Signalling Button A-Line
D5 : RF24-NRF24L01 SCK (blue-white) 
D6 : RF24-NRF24L01 MISO (green-white)
D7 : RF24-NRF24L01 MOSI (blue)
D8 : BUZZER / LED
D9/RX  : KY-040 Encoder - DT 
D10/TX : KY-040 Encoder - CLK

*/


#define PIN_RF24_CE       D0
#define PIN_OLED_SCL      D1
#define PIN_OLED_SDA      D2
#define PIN_RF24_CNS      D3
#define PIN_SIGNAL_A_LINE D4
#define PIN_RF24_SCK      D5
#define PIN_RF24_MISO     D6
#define PIN_RF24_MOSI     D7
#define PIN_BUZZER_OUT    D8
#define PIN_ENCODER_DT    D9
#define PIN_ENCODER_CLK   D10

#define PIN_BATTERY_IN    A0  // connected wiht R=100kOhm to support V > 3.2V

#define USE_BATTERY_IN_VOLTAGE
#define PERF_DEBUG

// #define NOBUZZ
#ifdef NOBUZZ
#pragma message ("BUZZER IS OFF !!!!!!!!!!!")
#endif
#define BUZZ_TIME_LONG   1500 // course finised
#define BUZZ_TIME_NORMAL  500 // normal turn
#define BUZZ_TIME_SHORT    100 // user info

#include <RFTransceiver.h>
RFTransceiver ourRadio(myName, PIN_RF24_CE, PIN_RF24_CNS); // (CE, CSN)



#ifdef USE_RXTX_AS_GPIO
Encoder ourRotaryEncoder(PIN_ENCODER_DT, PIN_ENCODER_CLK);
#endif

#define RE_MULITPLIER_SLOW 1
#define RE_MULITPLIER_NORMAL 5
long ourRotaryMenuPosition=0;
uint8_t ourRotaryEncoderMultiplier;
static boolean ourREState = true;
static int8_t ourREInversion = 1;
static long ourREOldPos  = 0;

static boolean ourLoopF3XTask = false;
static unsigned long ourLoopF3XPrevCourseTime = 0L;

enum BuzzerSetting {
  BS_ALL = 0, // both buzzers are active 
  BS_BASEMANAGER, // only direct connected BaseManager Buzzer 
  BS_REMOTE_BUZZER, // only remote radio buzzer
  BS_NONE, // no buzzers are active 
  BS_LAST,
};

static boolean ourIsTimeCriticalOperationRunning = false;

enum ToolContext {
  TC_F3XBaseMenu,
  TC_F3XSettingsMenu,
  TC_F3BSpeedTasktimeCfg,
  TC_F3FTasktimeCfg,
  TC_F3FLegLengthCfg,
  TC_F3XRadioChannelCfg,
  TC_F3XRadioPowerCfg,
  TC_F3XInfo,
  TC_F3XRadioInfo,
  TC_F3XMessage,
  TC_F3BSpeedMenu,
  TC_F3BSpeedTask,
  TC_F3FTaskMenu,
  TC_F3FTask,
  TC_F3BDistanceTask,
  TC_F3BDurationTask,
};

#define F3XCONTEXT_HISTORY_SIZE 5
class F3XContext {
  public:
    F3XContext(ToolContext aContext) {
      for (uint8_t i=0; i<F3XCONTEXT_HISTORY_SIZE; i++) {
        myContextHistory[i] = TC_F3XBaseMenu;
      }
      set(aContext);
    }
    void set(ToolContext aContext) {
      logMsg(LOG_MOD_SIG, INFO, String("set context:") + String(aContext));
      
      for (uint8_t i=0; i<F3XCONTEXT_HISTORY_SIZE-1; i++) {
        myContextHistory[i+1] = myContextHistory[i];
      }
      myContextHistory[0] = aContext;
  
      myInfoString="";
      myInfoInt = 0;
      myInfoFloat = 0.0f;
      myIsTaskRunning = false;
    }
    void back() {
      for (uint8_t i=0; i<F3XCONTEXT_HISTORY_SIZE-1; i++) {
        myContextHistory[i] = myContextHistory[i+1];
      }
      myContextHistory[F3XCONTEXT_HISTORY_SIZE-1] = TC_F3XBaseMenu;

      logMsg(LOG_MOD_SIG, INFO, String("back context:") + String(get()));
    }
    ToolContext get() {
      return myContextHistory[0];
    }
    String getInfoString() {
      return myInfoString;
    }
    float getInfoFloat() {
      return myInfoFloat;
    }
    int getInfoInt() {
      return myInfoInt;
    }
    void setInfo(float aArg) {
      myInfoFloat = aArg;
    }
    void setInfo(int aArg) {
      myInfoInt = aArg;
    }
    void setInfo(String aArg) {
      myInfoString = aArg;
    }
  private:
    ToolContext myContextHistory[F3XCONTEXT_HISTORY_SIZE];
    float myInfoFloat;
    int myInfoInt;
    String myInfoString;
    bool myIsTaskRunning;
};

F3XContext ourContext(TC_F3XInfo);

// TC_F3XBaseMenu
const char* ourF3XBaseMenuName = "Main menu";
const char* ourF3XBaseMenu0 = "0:F3B Speedtask";
const char* ourF3XBaseMenu1 = "1:F3F Task";
const char* ourF3XBaseMenu2 = "2:Info";
const char* ourF3XBaseMenu3 = "3:Radio-Info";
const char* ourF3XBaseMenu4 = "4:Settings";
const char* ourF3XBaseMenuItems[] = {ourF3XBaseMenu0, ourF3XBaseMenu1, ourF3XBaseMenu2, ourF3XBaseMenu3, ourF3XBaseMenu4};
const uint8_t ourF3XBaseMenuSize = sizeof(ourF3XBaseMenuItems) / sizeof(char*);;

// TC_F3XSettingsMenu
const char* ourSettingsMenuName = "Settings";
const char* ourSettingsMenu0 = "0:F3B Speed Ttime";
const char* ourSettingsMenu1 = "1:F3F Ttime";
const char* ourSettingsMenu2 = "2:F3F LegLength";
const char* ourSettingsMenu3 = "3:Buzzers setup";
const char* ourSettingsMenu4 = "4:Competition setup";
const char* ourSettingsMenu5 = "5:Radio channel";
const char* ourSettingsMenu6 = "6:Radio power";
const char* ourSettingsMenu7 = "7:Display invert";
const char* ourSettingsMenu8 = "8:Rotary button inv.";
const char* ourSettingsMenu9 = "9:Update firmware";
const char* ourSettingsMenu10 = "10:Update filesystem";
const char* ourSettingsMenu11 = "11:WiFi on/off";
const char* ourSettingsMenu12 = "12:Save settings";
const char* ourSettingsMenu13 = "13:Main menu";
const char* ourSettingsMenuItems[] = {ourSettingsMenu0, ourSettingsMenu1, ourSettingsMenu2, ourSettingsMenu3, ourSettingsMenu4, ourSettingsMenu5, ourSettingsMenu6, ourSettingsMenu7, ourSettingsMenu8, ourSettingsMenu9, ourSettingsMenu10, ourSettingsMenu11, ourSettingsMenu12, ourSettingsMenu13};
const uint8_t ourSettingsMenuSize = sizeof(ourSettingsMenuItems) / sizeof(char*);

// TC_F3BSpeedMenu
const char* ourF3BSpeedMenuName = "F3B Speed";
const char* ourF3BSpeedMenu0 = "0:Start Task";
const char* ourF3BSpeedMenu1 = "1:Loop Task";
const char* ourF3BSpeedMenu2 = "2:Back";
const char* ourF3BSpeedMenuItems[] = {ourF3BSpeedMenu0, ourF3BSpeedMenu1, ourF3BSpeedMenu2};
const uint8_t ourF3BSpeedMenuSize = sizeof(ourF3BSpeedMenuItems) / sizeof(char*);

// TC_F3FTaskMenu
const char* ourF3FTaskMenuName = "F3F Task";
const char* ourF3FTaskMenu0 = "0:Start Task";
const char* ourF3FTaskMenu1 = "1:Loop Task";
const char* ourF3FTaskMenu2 = "2:Back";
const char* ourF3FTaskMenuItems[] = {ourF3FTaskMenu0, ourF3FTaskMenu1, ourF3FTaskMenu2};
const uint8_t ourF3FTaskMenuSize = sizeof(ourF3FTaskMenuItems) / sizeof(char*);


#include "F3XRemoteCommand.h"

#define OLED
#ifdef OLED
#include <U8g2lib.h>
/*
// TFT Display  ST7735S
//
https://github.com/olikraus/u8g2/wiki/u8g2setupcpp#introduction
SCL D1
SDA D2
U8G2_SSD1306_128X64_NONAME_1_HW_I2C(rotation, [reset [, clock, data]]) [page buffer, size = 128 bytes]
U8G2_SSD1306_128X64_NONAME_2_HW_I2C(rotation, [reset [, clock, data]]) [page buffer, size = 256 bytes]
U8G2_SSD1306_128X64_NONAME_F_HW_I2C(rotation, [reset [, clock, data]]) [full framebuffer, size = 1024 bytes]
*/

// U8G2_SSD1306_128X64_NONAME_1_HW_I2C ourOLED(U8G2_R0, U8X8_PIN_NONE, D1, D2);
// #define SSD1306_AZ_Delivery
// supported OLEDs
// OLED 0.96" SSD1306   0,96 Zoll OLED SSD1306 Display I2C 128 x 64 Pixel / AZ-Delivery
#define OLED_FULL_BUFFER
#ifdef OLED_FULL_BUFFER
U8G2_SSD1306_128X64_NONAME_F_HW_I2C ourOLED(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL /*SCL*/, PIN_OLED_SDA /*SDA*/);  
#else
U8G2_SSD1306_128X64_NONAME_1_HW_I2C ourOLED(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL /*SCL*/, PIN_OLED_SDA /*SDA*/);  
#endif
// OLED xxx
// U8G2_SSD1309_128X64_NONAME2_1_HW_I2C ourOLED(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL /*SCL*/, PIN_OLED_SDA /*SDA*/);  

#endif

#define OTA
#ifdef OTA
#include <ArduinoOTA.h>
#endif
 

IPAddress ourApIp(192,168,4,1);
IPAddress ourNetmask(255,255,255,0);
ESP8266WebServer ourWebServer(80);
unsigned long ourSecond = 0;

static configData_t ourConfig;
F3XFixedDistanceTask ourF3BSpeedTask(F3XFixedDistanceTask::F3BSpeedType);
F3XFixedDistanceTaskData ourF3BTaskData(&ourF3BSpeedTask);
F3XFixedDistanceTask ourF3FTask(F3XFixedDistanceTask::F3FType);
F3XFixedDistanceTaskData ourF3FTaskData(&ourF3FTask);
F3XFixedDistanceTask* ourF3XGenericTask = nullptr;
unsigned long ourWlanRoundTripTime=0;
unsigned long ourRadioRequestTime=0;
float ourRadioRoundTripTime=0;
uint8_t ourRadioRequestArg=0;
uint16_t ourRadioCycleSendCnt=0;
uint8_t ourRadioPacketsLossPercent=0;
uint16_t ourRadioCycleRecvCnt=0;
uint16_t ourRadioRoundTripMissed=0;
uint16_t ourRadioRoundtripIdx=0;
int8_t ourRadioPower=-1;
int8_t ourRadioChannel=-1;
int8_t ourRadioDatarate=-1;
boolean ourRadioAck=true;
boolean ourRadioSendSettings=false;
float ourRadioQuality=0.0f;
uint16_t ourRadioStatePacketsMissed=0;
uint16_t ourRadioSignalRoundTrip=0;
boolean ourStartupPhase=true;
F3XRemoteCommand ourRemoteCmd;
uint16_t ourBatteryAVoltage;
uint16_t ourBatteryBVoltage;
uint16_t ourBatteryBVoltageRaw;
uint16_t ourBatteryRemoteSignalRaw;
unsigned long ourTimedReset = 0;
unsigned long ourDialogTimer=0;
String ourDialogString="";

Bounce2::Button ourPushButton = Bounce2::Button();
PinManager ourBuzzer(PIN_BUZZER_OUT);

// =========== some function forward declarations ================

void updateOLED(unsigned long aNow, bool aForce);
void updateOLED(unsigned long aNow);
#ifdef USE_RXTX_AS_GPIO
void resetRotaryEncoder(long aPos=0);
#endif
uint8_t getModulo(long aDivident, uint8_t aDivisor);
void forceOLED(uint8_t aLevel, String aMessage);
void forceOLED(String aHead, String aMessage);
void setDefaultConfig();
void saveConfig();
void takeOLEDScreenshot();


void restartMCs(uint16_t aDelay, bool aRestartOnlyBLine=false) {
  ourRadio.transmit(ourRemoteCmd.createCommand(F3XRemoteCommandType::CmdRestartMC)->c_str(), 20);
  ourRadio.setWritingPipe(2);
  ourRadio.transmit(ourRemoteCmd.createCommand(F3XRemoteCommandType::CmdRestartMC)->c_str(), 20);
  ourRadio.setWritingPipe(0);
  if (!aRestartOnlyBLine) {
    // force a restart of ourself
    ourTimedReset = millis() + aDelay;
  }
}


/*
* send a RF24 command to the 2. pipe (radioBuzzer) 
*/
void radioBuzzer(uint16_t aDura) {
  // logMsg(INFO, String(F("=====> radioBuzzer on:")) + String(aDura)); 
  ourRadio.setWritingPipe(2);
  unsigned long a = millis();
  boolean sendSuccess = ourRadio.transmit(ourRemoteCmd.createCommand(F3XRemoteCommandType::RemoteSignalBuzz, String(aDura))->c_str(), 4);

  if (!sendSuccess) {
    logMsg(LOG_MOD_RADIO, ERROR, String(F("sending RemoteSignalBuzz NOT successsfull. Retransmissions: ")) 
      + String(ourRadio.getRetransmissionCount()));
  }
  logMsg(LOG_MOD_RADIO, INFO, String(F("sending RemoteSignalBuzz in: ") + String((millis() - a)))); 
  ourRadio.setWritingPipe(0);
}

void signalBuzzing(uint16_t aDuration) {
  logMsg(LOG_MOD_SIG, INFO, "ABM: signalBuzzing: " + String(aDuration));
  switch (ourConfig.buzzerSetting) {
    case BS_ALL: // both buzzers are active 
      radioBuzzer(aDuration);
      ourBuzzer.on(aDuration);
      break;
    case BS_BASEMANAGER: // only direct connected BaseManager Buzzer 
      ourBuzzer.on(aDuration);
      break;
    case BS_REMOTE_BUZZER: // only remote radio buzzer
      radioBuzzer(aDuration);
      break;
    case BS_NONE: // no buzzers are active 
      break;
  }
}

void signalAListener() {
  logMsg(LOG_MOD_SIG, INFO, "ABM: signalAListener");
  if (ourF3XGenericTask->getTaskState() == F3XFixedDistanceTask::TaskFinished) {
    // looong signal at final A-Line overfly signalling 1500ms
    signalBuzzing(BUZZ_TIME_LONG);
    if (ourF3XGenericTask->getLoopTasksEnabled()) {
      ourF3XGenericTask->stop();
      ourF3XGenericTask->start();
    }
  } else {
    // default signalling 500ms
    signalBuzzing(BUZZ_TIME_NORMAL);
  }
  switch(ourF3XGenericTask->getType()) {
    case F3XFixedDistanceTask::F3BSpeedType:
      if (ourF3XGenericTask->getTaskState() == F3XFixedDistanceTask::TaskFinished) {
        ourF3BTaskData.writeData();
      }
      break;
    case F3XFixedDistanceTask::F3FType:
      if (ourF3XGenericTask->getTaskState() == F3XFixedDistanceTask::TaskFinished) {
        ourF3FTaskData.writeData();
      }
      break;
  }
}

void signalBListener() {
  logMsg(LOG_MOD_SIG, INFO, "signalBListener");
  signalBuzzing(BUZZ_TIME_NORMAL);
}

/**
  return a leg time literal with the format 12.23s/43m  
*/
char* getLegTimeStr(unsigned long aLegTime,  unsigned long aDelay, uint8_t aDistance, char aSeparator='/') {
  static char buffer[25];
  if (aLegTime != F3X_TIME_NOT_SET) {
    sprintf(&buffer[0],"%02d.%02ds", aLegTime / 1000, aLegTime/10%100); // len=8+7=15
  }
  if (aDelay != 0) {
    sprintf(&buffer[strlen(buffer)],"%c%dm", aSeparator, aDistance); // len=15+8=23+1
  }
  return buffer;
}


/**
  return a time literal with the format Seconds.Centies 12.23s
*/
char* getSCTimeStr(unsigned long aTime, bool aShowUnit=false ) {
  static char buffer[10];
  unsigned long theTime = aTime;
  if (aTime == F3X_TIME_NOT_SET) {
    theTime = 0UL;
  }
  String format= F("%02d.%02d");
  if (aShowUnit) {
    format= F("%02d.%02ds");
  }
  sprintf(&buffer[0],format.c_str(), theTime/1000, theTime/10%100); 
  return buffer;
}


#ifdef OLED
const uint8_t *oledFontLarge;
const uint8_t *oledFontBig;
const uint8_t *oledFontNormal;
const uint8_t *oledFontSmall;
const uint8_t *oledFontTiny;


void setupOLED() {
  ourOLED.begin();
  int oledDisplayHeight = ourOLED.getDisplayHeight(); 
  int oledDisplayWidth = ourOLED.getDisplayWidth(); 
  logMsg(INFO, F("init OLED display: ") + String(oledDisplayWidth) + String(F("x")) + String(oledDisplayHeight)); 
  oledFontLarge  = u8g2_font_helvR12_tr;
  oledFontBig    = u8g2_font_helvR10_tr;
  oledFontNormal = u8g2_font_helvR08_tr;
  oledFontSmall  = u8g2_font_5x7_tr;
  oledFontTiny   = u8g2_font_4x6_tr;
  forceOLED(0, String(F("OLED ok")));
}
#endif

void setupRadio() {
  logMsg(INFO, F("setup RCTTransceiver/nRF24L01")); 
  ourRadio.begin(RFTransceiver::F3XBaseManager);  // set 0 for BaseManager
  logMsg(INFO, F("setup for RCTTransceiver/nRF24L01 successful")); 

  // in the RFTransceiver implementation the default values for the radio settings are defined 
  // for A- and B-Line Manager (Channel:110/Power:HIGH/Datarate:RF24_250KBPS/Ack:true)
  // if the user is changing radio settings, they are save in the EEPROM config ourConfig/loadConfig/saveConfig
  // at startup, these config value are NOT used, to provide in any case a commont radio setting for both sides.
  // Only the BaseManager, stores changed radio settings, boots with the default settings starts a communication with the 
  // B-Line-Manager and sends changed settings via radio  

  ourRadioPower=ourRadio.getPower();
  ourRadioChannel=ourRadio.getChannel();
  ourRadioDatarate = ourRadio.getDataRate();
  ourRadioAck=ourRadio.getAck();

  if (   ourRadioChannel != ourConfig.radioChannel
      || ourRadioPower   != ourConfig.radioPower
    ) {
    ourRadioChannel = ourConfig.radioChannel;
    ourRadioPower = ourConfig.radioPower;
    // force a radio data  CmdSetRadio packets to B-Line
    ourRadioSendSettings=true;
  }
  logMsg(LOG_MOD_WEB, INFO, F("RF24-cfg: (c/p)") + String(ourConfig.radioChannel) + String(F("/")) + String(ourConfig.radioPower)); 

  forceOLED(0, ("RF24 radio started"));
}


void setupLittleFS() {
  if(!LittleFS.begin()){
    logMsg(DEBUG, F("LittleFS: An Error has occurred while mounting LittleFS"));
    forceOLED(0, ("FS not ok"));
    return;
  }
  forceOLED(0, ("FS ok"));
}



void setupWiFi() {
  // first try to connect to the stored WLAN, if this does not work try to
  // start as Access Point
  // strncpy(ourConfig.wlanSsid , DEF_SSID, CONFIG_SSID_L);
  // strncpy(ourConfig.wlanPasswd, DEF_PSK, CONFIG_PASSW_L);
  // strncpy(ourConfig.apPasswd, "12345678", CONFIG_PASSW_L) ;
  if (!ourConfig.wifiIsActive) {
    WiFi.mode(WIFI_OFF) ; // client mode only
    forceOLED(0, (String("WiFi: inactive")));
    delay(1500);
    return;
  }

  if (String(ourConfig.wlanSsid).length() != 0 ) {
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA) ; // client mode only
    WiFi.begin(ourConfig.wlanSsid, ourConfig.wlanPasswd);

    logMsg(DEBUG, String(F("Connecting to ")) + ourConfig.wlanSsid);
    int connectCnt = 0;
    while (WiFi.status() != WL_CONNECTED && connectCnt++ < 20) {
      delay(500);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    logMsg(INFO, F("success!"));
    logMsg(LOG_MOD_WEB, INFO, F("IP Address is: ") + WiFi.localIP().toString());
    forceOLED(0, (String("WiFi: ") + WiFi.localIP().toString()));
  } else {
    logMsg(INFO, String(F("cannot connect to SSID :")) + ourConfig.wlanSsid);
    WiFi.mode(WIFI_AP) ; // client mode only
  }
  if (WiFi.status() != WL_CONNECTED) {
    logMsg(INFO, String(F("Starting WiFi Access Point with  SSID: ")) + ourConfig.apSsid);
      forceOLED(0, String(("Start WiFi-AP: ") + String(ourConfig.apSsid)));
      delay(1000);
    WiFi.softAPConfig(ourApIp, ourApIp, ourNetmask);    //Password length minimum 8 char
    boolean res = WiFi.softAP(ourConfig.apSsid, ourConfig.apPasswd, 3, 0, 1);    //Password length minimum 8 char, channel, hidden, #clients
    if(res ==true) {
      IPAddress myIP = WiFi.softAPIP();
      logMsg(INFO, F("AP setup done!"));
      logMsg(INFO, String(F("Host IP Address: ")) + myIP.toString());
      logMsg(INFO, String(F("Please connect to SSID: ")) + String(ourConfig.apSsid) + String(F(", PW: ")) + ourConfig.apPasswd);
      forceOLED(0, String(F("WiFi AP-SSID: ")) + String(ourConfig.apSsid) + String(F(" IP: ")) + myIP.toString());
      delay(1000);
    } else {
      forceOLED(0, String("WiFi: not started"));
      delay(1000);
    }
  }
  #ifdef USE_MDNS
  if (!MDNS.begin("f3x", WiFi.localIP())) {             
    logMsg(LOG_MOD_NET, ERROR, "Error starting mDNS");
  } else {
    logMsg(LOG_MOD_NET, INFO, "mDNS started");
  }
  #endif
}

String getWiFiIp(String* ret) {
  if (WiFi.status() == WL_CONNECTED) {
    ret->concat(WiFi.localIP().toString());
  } else {
    ret->concat(String(ourConfig.apSsid));
    ret->concat(String(F("/")));
    ret->concat(WiFi.softAPIP().toString());
  }
  return *ret;
}

// End: WIFI WIFI WIFI

// Start: WEBSERVER WEBSERVER WEBSERVER 
// =================================
// web server functions
// =================================

const int led = 13;

// convert the file extension to the MIME type
String getWebContentType(String filename) {
  if (filename.endsWith(F(".html"))) return F("text/html");
  else if (filename.endsWith(F(".png"))) return F("text/css");
  else if (filename.endsWith(F(".css"))) return F("text/css");
  else if (filename.endsWith(F(".js"))) return F("application/javascript");
  else if (filename.endsWith(F(".map"))) return F("application/json");
  else if (filename.endsWith(F(".ico"))) return F("image/x-icon");
  else if (filename.endsWith(F(".gz"))) return F("application/x-gzip");
  else if (filename.endsWith(F(".csv"))) return F("text/plane");
  return F("text/plain");
}

// send file to the client (if it exists)
bool handleWebFileRead(String path) {
  if (ourConfig.competitionSetting == true && ourIsTimeCriticalOperationRunning == true) {
    // do nothing, in case of competition setting and time critical operation is running
    ourWebServer.send(503, F("text/plain"), F("F3XCompetition in restricted web mode while time critical operation !"));
    return true;
  }
  // If a folder is requested, send the index file
  if (path.endsWith(F("/"))) path += F("index.html");
  String contentType = getWebContentType(path);
  String pathWithGz = path + F(".gz");
      
  // If the file exists, either as a compressed archive, or normal
  if (LittleFS.exists(pathWithGz) || LittleFS.exists(path)) {
    if (LittleFS.exists(pathWithGz))
      path += F(".gz");
    logMsg(DEBUG, F("WebServer: open file ") + path);
    File file = LittleFS.open(path, "r");
    size_t sent = ourWebServer.streamFile(file, contentType);
    file.close();
    return true;
  }     
  
  return false;
}   

void setWebDataReq() {
  String name = ourWebServer.arg(F("name"));
  String value = ourWebServer.arg(F("value"));
  #ifdef DO_LOG
  logMsg(DEBUG, ourWebServer.client().remoteIP().toString() + F(" : setWebDataReq()"));
  logMsg(DEBUG, String(F("  ")) + name + F("=") + value);
  #endif
  boolean sendResponse = true;

  String response = F("");
  int htmlResponseCode=200; // OK

  // general settings stuff
  if (name == F("signal_a")) {
    logMsg(INFO, F("signal A event from web client"));
    ourF3XGenericTask->signal(F3XFixedDistanceTask::SignalA);
  } else
  if (name == F("signal_b")) {
    logMsg(INFO, F("signal B event from web client"));
    ourF3XGenericTask->signal(F3XFixedDistanceTask::SignalB);
  } else 
  if (name == F("stop_task")) {
    logMsg(INFO, F("stop task event from web client"));
    ourF3XGenericTask->stop();
  } else 
  if (name == F("start_task")) {
    logMsg(INFO, F("start task event from web client"));
    ourLoopF3XTask = false;
    ourF3XGenericTask->setLoopTasksEnabled(ourLoopF3XTask);
    ourF3XGenericTask->start();
  } else 
  if (name == F("loop_task")) {
    logMsg(INFO, F("loop task event from web client"));
    ourLoopF3XTask = true;
    ourF3XGenericTask->setLoopTasksEnabled(ourLoopF3XTask);
    ourF3XGenericTask->start();
  } else 
  if (name == F("start_rt_measurement")) {
    logMsg(DEBUG, F("start_rt_measurement"));
    logMsg(INFO, F("start range test (nyi)"));
  } else 
  if (name == F("restart_mc")) {
    logMsg(INFO, F("web cmd restart mcs"));
    restartMCs(500);
  } else 
  if (name == F("f3f_tasktime")) {
    ourConfig.f3fTasktime=value.toInt();
    ourF3FTask.setTasktime(ourConfig.f3fTasktime);
    logMsg(LOG_MOD_TASK, INFO, F("set f3f_tasktime :") + String(ourConfig.f3fTasktime));
  } else 
  if (name == F("f3f_leg_length")) {
    ourConfig.f3fLegLength=value.toInt();
    ourF3FTask.setLegLength(ourConfig.f3fLegLength);
    logMsg(LOG_MOD_TASK, INFO, F("set f3f_leg_length :") + String(ourConfig.f3fLegLength));
  } else 
  if (name == F("f3b_speed_tasktime")) {
    ourConfig.f3bSpeedTasktime=value.toInt();
    ourF3BSpeedTask.setTasktime(ourConfig.f3bSpeedTasktime);
    logMsg(LOG_MOD_TASK, INFO, F("set f3b_speed_tasktime :") + String(ourConfig.f3bSpeedTasktime));
  } else 
  if (name == F("buzzer_setting")) {
    if (value == "all") {
      ourConfig.buzzerSetting = BS_ALL;
    } else
    if (value == "aline") {
      ourConfig.buzzerSetting = BS_BASEMANAGER;
    } else
    if (value == "radiobuzzer") {
      ourConfig.buzzerSetting = BS_REMOTE_BUZZER;
    } else
    if (value == "none") {
      ourConfig.buzzerSetting = BS_NONE;
    } else
    ourRadioSendSettings=true;
    logMsg(LOG_MOD_SIG, INFO, F("set buzzerSetting:") + String(ourConfig.buzzerSetting));
  } else 
  if (name == F("competition_setup")) {
    ourConfig.competitionSetting=false;
    if (value == F("true")) {
      ourConfig.competitionSetting=true;
    }
    logMsg(LOG_MOD_TASK, INFO, F("set competitionSetting:") + String(ourConfig.competitionSetting));
  } else 
  if (name == F("radio_channel")) {
    ourRadioChannel=value.toInt();
    ourRadioSendSettings=true;
    logMsg(LOG_MOD_WEB, INFO, F("set RF24 Channel:") + String(ourRadioChannel));
  } else 
  if (name == F("radio_datarate")) {
    if (value == F("250")) {
      ourRadioDatarate = RF24_250KBPS;
    } else
    if (value == F("1000")) {
      ourRadioDatarate = RF24_1MBPS;
    } else
    if (value == F("2000")) {
      ourRadioDatarate = RF24_1MBPS;
    } 
    ourRadioSendSettings=true;
    logMsg(LOG_MOD_WEB, INFO, F("set RF24 DataRate: ") + String(ourRadioDatarate));
  } else 
  if (name == F("radio_ack")) {
    if (value == F("true")) {
      ourRadioAck = true;
    } else
    if (value == F("false")) {
      ourRadioAck = false;
    }
    ourRadioSendSettings=true;
    logMsg(LOG_MOD_HTTP, INFO, F("set RF24 Ack:") + String(ourRadioAck));
  } else 
  if (name == F("radio_power")) {
    uint8_t power= RF24_PA_MIN;
    if (value == F("max")) {
      power = RF24_PA_MAX;
    } else
    if (value == F("high")) {
      power = RF24_PA_HIGH;
    } else
    if (value == F("low")) {
      power = RF24_PA_LOW;
    } else {
      power = RF24_PA_MIN;
    }
    ourRadioPower = power;
    ourRadioSendSettings=true;
    logMsg(LOG_MOD_HTTP, INFO, F("set RF24 Power:") + String(ourRadioPower));
  } else 
  if (name == F("delete_f3f_data")) {
    logMsg(LOG_MOD_HTTP, INFO, "remove F3FTaskData"); 
    ourF3FTaskData.remove();
  } else 
  if (name == F("delete_f3b_data")) {
    logMsg(LOG_MOD_HTTP, INFO, "remove F3BTaskData"); 
    ourF3BTaskData.remove();
  } else 
  if (name == F("cmd_fwupdate")) {
    logMsg(LOG_MOD_HTTP, INFO, "fw update"); 
    otaUpdate(false); // firmware
  } else 
  if (name == F("cmd_fsupdate")) {
    logMsg(LOG_MOD_HTTP, INFO, "fs update"); 
    otaUpdate(true); // filesystem
  } else 
  if (name == F("cmd_saveConfig")) {
    logMsg(LOG_MOD_HTTP, INFO, "save config"); 
    saveConfig();
  } else 
  if (name == F("cmd_resetConfig")) {
    logMsg(LOG_MOD_HTTP, INFO, "reset config"); 
    setDefaultConfig();
  } else 
  if (name == F("id_wifiActive")) {
    if (value == "true") {
      ourConfig.wifiIsActive = true;
    } else {
      ourConfig.wifiIsActive = false;
    }
      logMsg(LOG_MOD_HTTP, INFO, F("setting wifi: ") + String(ourConfig.wifiIsActive)); 
  } else 
  if (name == F("id_wlanSsid")) {
    logMsg(LOG_MOD_HTTP, INFO, "setting wlan ssid"); 
    strncpy(ourConfig.wlanSsid, value.c_str(), CONFIG_SSID_L);
  } else 
  if (name == F("id_wlanPasswd")) {
    logMsg(LOG_MOD_HTTP, INFO, "setting wlan passwd"); 
    strncpy(ourConfig.wlanPasswd, value.c_str(), CONFIG_PASSW_L);
  } else 
  if (name == F("id_apSsid")) {
    logMsg(LOG_MOD_HTTP, INFO, "setting ap password"); 
    strncpy(ourConfig.apSsid, value.c_str(), CONFIG_SSID_L);
  } else 
  if (name == F("id_apPasswd")) {
    logMsg(LOG_MOD_HTTP, INFO, "setting ap password"); 
    strncpy(ourConfig.apPasswd, value.c_str(), CONFIG_PASSW_L);
  } else 
  if (name == F("cmd_mcrestart")) {
    logMsg(LOG_MOD_HTTP, INFO, "restart MC"); 
    restartMCs(1000);
  } else 
  if (name == F("take_screenshot")) {
    logMsg(LOG_MOD_HTTP, INFO, "take OLED screenshot"); 
    takeOLEDScreenshot();
  } else {
    logMsg(LOG_MOD_HTTP, ERROR, F("ERROR: unknown name : ") + name  + F(" in set request, value ") + value);
  }

  if (sendResponse) {
    #ifdef DO_LOG
    logMsg(DEBUG, String(F("send response to WebServer: ")) + response);
    #endif
    ourWebServer.send(htmlResponseCode, F("text/plane"), response); // send an valid answer
  }
}


void getWebHeaderData(String* aReturnString, boolean aForce=false) {

  *aReturnString += String(F("id_time=")) + F3XFixedDistanceTask::getHMSTimeStr(millis()) + MYSEP_STR;

  static long web_rssi = 0;
  if (WiFi.RSSI() != web_rssi || aForce) {
    web_rssi = WiFi.RSSI();
    *aReturnString += String(F("id_wifi_rss=")) + WiFi.RSSI() + MYSEP_STR;
  }

  static String webBat = F("");
  String currBat = String((((float) ourBatteryAVoltage/1000)), 2) + F("/") + String((((float) ourBatteryBVoltage/1000)), 2);
  if (webBat != currBat || aForce) {
    webBat = currBat;
    *aReturnString += String(F("id_bat=")) + webBat + MYSEP_STR;
  }

  static String webRadio = F("");
  String currRadio = 
    String(ourRadio.getPower()) + F("/") + 
    String(ourRadio.getChannel()) + F("/") + 
    String(ourRadio.getDataRate()) + F("/") + 
    String(ourRadio.getAck()) + F(":") +
    String(ourRadioQuality, 0) + F("/") + String(ourRadioStatePacketsMissed) + F("/") + String(ourRadioSignalRoundTrip)+String(F("ms"));  

  if (webRadio != currRadio || aForce) {
    webRadio = currRadio;
    logMsg(LOG_MOD_HTTP, INFO, "Radio: " + currRadio);
    *aReturnString += String(F("id_radio=")) + webRadio + MYSEP_STR;
  }
}

void getF3FWebData(String* aReturnString, boolean aForce=false) {
  static int webTaskState = 0;
  if (ourF3XGenericTask->getType() != F3XFixedDistanceTask::F3FType ) {
    logMsg(ERROR, String(F("illegal F3FWebData req")));
    return;
  }

  int actState = ourF3XGenericTask->getTaskState()*100 + ourF3XGenericTask->getSignalledLegCount();
  if (actState != webTaskState || aForce) {
    webTaskState = ourF3XGenericTask->getTaskState()*100 + ourF3XGenericTask->getSignalledLegCount();
    String taskstr;
    switch (ourF3XGenericTask->getTaskState()) {
      case F3XFixedDistanceTask::TaskWaiting:
        *aReturnString += String(F("id_course_time"))
                      + F("=") 
                      + ourF3XGenericTask->getLegTimeString(F3X_TIME_NOT_SET, F3X_TIME_NOT_SET, 0, 0, 0,'/', false)
                      + MYSEP_STR;
        *aReturnString += String(F("id_last_leg_time")) 
            + F("=") + F("--:--") + MYSEP_STR;
        *aReturnString += String(F("id_current_course_distance")) 
            + F("=000") +
            + MYSEP_STR;
        taskstr = F("Ready, waiting for START F3F task");
        break;
      case F3XFixedDistanceTask::TaskRunning:
        switch (ourF3XGenericTask->getSignalledLegCount()) {
          case F3X_COURSE_INIT:
            taskstr = F("launch signal awaiting ...");
            break;
          case F3X_IN_AIR:
            taskstr = F("next signal: first A-Line");
            break;
          default:
            if (ourF3XGenericTask->getSignalledLegCount()%2==0) {
              taskstr = F("next Signal: B-Line");
            } else {
              taskstr = F("next Signal: A-Line");
            }
            break;
        }
        break;
      case F3XFixedDistanceTask::TaskTimeOverflow:
        taskstr = String(F("task stopped, task time overflow"));
        break;
      case F3XFixedDistanceTask::TaskFinished:
        taskstr = F("task finished!");
        break;
      default:
        taskstr = F("ERROR: program problem 003:");
        taskstr += String(ourF3XGenericTask->getTaskState());
        break;
    }
    *aReturnString += String(F("id_task_state=")) + taskstr + MYSEP_STR;
    logMsg(INFO, *aReturnString);
  }

  *aReturnString += String(F("id_course_time")) 
      + F("=") + ourF3XGenericTask->getLegTimeString(ourF3XGenericTask->getCourseTime(F3X_GFT_RUNNING_TIME), F3X_TIME_NOT_SET, 0, 0, 0)
      + MYSEP_STR;

  int8_t numLegs = ourF3XGenericTask->getSignalledLegCount();
  if (numLegs > 0 ) {
    F3XLeg leg = ourF3XGenericTask->getLeg(numLegs-1);
    *aReturnString += String(F("id_last_leg_time")) 
            + F("=") 
                + ourF3XGenericTask->getLegTimeString(
                    ourF3XGenericTask->getCourseTime(numLegs),  // for the i.th signal
                    leg.time,
                    leg.speed*3.6f,
                    leg.deadTime,
                    leg.deadDistance)
            + MYSEP_STR;
    *aReturnString += String(F("id_current_course_distance")) 
            + F("=") + (numLegs*ourF3XGenericTask->getLegLength())
            + MYSEP_STR;
    *aReturnString += String(F("id_leg_length")) 
            + F("=") + ourF3XGenericTask->getLegLength()
            + MYSEP_STR;
  }

  *aReturnString += String(F("id_inair_time=")) 
     + F3XFixedDistanceTask::getHMSTimeStr(ourF3XGenericTask->getInAirTime(), true) + MYSEP_STR;
  *aReturnString += String(F("id_task_time=")) 
     + F3XFixedDistanceTask::getHMSTimeStr(ourF3XGenericTask->getRemainingTasktime(), true) + MYSEP_STR;
  if (ourF3XGenericTask->getSignalledLegCount() == ourF3XGenericTask->getLegNumberMax()) {
    *aReturnString += String(F("id_course_time="))
      + ourF3XGenericTask->getLegTimeString(ourF3XGenericTask->getCourseTime(F3X_GFT_RUNNING_TIME), F3X_TIME_NOT_SET, 0, 0, 0)
      + MYSEP_STR;
  }
}

void getF3BSpeedWebData(String* aReturnString, boolean aForce=false) {
  String taskstr;

  static F3XFixedDistanceTask::State webTaskState = F3XFixedDistanceTask::TaskNotSet;
  if (ourF3XGenericTask->getTaskState() != webTaskState || aForce) {
    webTaskState = ourF3XGenericTask->getTaskState();
    switch (ourF3XGenericTask->getTaskState()) {
      case F3XFixedDistanceTask::TaskWaiting:
        *aReturnString += String(F("id_running_speed_time="))
                      + ourF3XGenericTask->getLegTimeString(F3X_TIME_NOT_SET, F3X_TIME_NOT_SET, 0, 0, 0,'/', false)
                      + MYSEP_STR;
        taskstr = F("Ready, waiting for START speed task");
        break;
      case F3XFixedDistanceTask::TaskRunning:
        taskstr = F("task started, signals will be handled");
        break;
      case F3XFixedDistanceTask::TaskTimeOverflow:
        taskstr = String(F("task stopped, task time overflow"));
        break;
      case F3XFixedDistanceTask::TaskFinished:
        taskstr = F("task finished!");
        break;
      default:
        taskstr = F("ERROR: program problem 003:");
        taskstr += String(ourF3XGenericTask->getTaskState());
        break;
    }
    *aReturnString += String(F("id_speed_task_state=")) + taskstr + MYSEP_STR;
  }
  int fromTimer=0;
  int numTimer=0;
  if (aForce) {
    // logMsg(DEBUG, F("getF3BSpeedWebData: forced")); 
    fromTimer = 0;
    numTimer = 5;
  } else
  if (ourF3XGenericTask->getTaskState() == F3XFixedDistanceTask::TaskWaiting ) {
    // logMsg(DEBUG, F("getF3BSpeedWebData: task waiting")); 
    numTimer = 5;
  } else
  if (ourF3XGenericTask->getTaskState() == F3XFixedDistanceTask::TaskTimeOverflow ) {
    // logMsg(DEBUG, F("getF3BSpeedWebData: task time overflow")); 
    fromTimer = ourF3XGenericTask->getSignalledLegCount()+1;
    numTimer = 5-fromTimer;
  } else
  if (ourF3XGenericTask->getTaskState() == F3XFixedDistanceTask::TaskRunning || ourF3XGenericTask->getTaskState() == F3XFixedDistanceTask::TaskFinished ) {
    // logMsg(DEBUG, F("getF3BSpeedWebData: running | finished")); 
    switch(ourF3XGenericTask->getSignalledLegCount()) {
      case F3X_COURSE_INIT:
        // logMsg(DEBUG, F("getF3BSpeedWebData: --> F3X_COURSE_INIT")); 
        // send all timer
        fromTimer=0;
        numTimer = 5;
        break;
      default:
        // send the current timer
        // logMsg(DEBUG, F("getF3BSpeedWebData: --> default")); 
        fromTimer = max(ourF3XGenericTask->getSignalledLegCount()-1, 0);
        numTimer = 2;
        break;
    }
  }
  taskstr="";
  // logMsg(DEBUG, String(F("getF3BSpeedWebData: --> for ")) + String(fromTimer) + "/" + String(fromTimer+numTimer) ); 
  for (int i=fromTimer; i<fromTimer+numTimer; i++) {
    F3XLeg leg = ourF3XGenericTask->getLeg(i-1);
    taskstr += String(F("id_speed_time_")) 
              + String(i) + F("=") 
                  + ourF3XGenericTask->getLegTimeString(
                      ourF3XGenericTask->getCourseTime(i),  // for the i.th signal
                      leg.time,
                      leg.speed*3.6f,
                      leg.deadTime,
                      leg.deadDistance)
              + MYSEP_STR;
  }
  if (taskstr.length() > 0) {
    *aReturnString += taskstr;
  }

  *aReturnString += String(F("id_speed_task_time=")) + F3XFixedDistanceTask::getHMSTimeStr(ourF3XGenericTask->getRemainingTasktime(), true) + MYSEP_STR;
  if (ourF3XGenericTask->getSignalledLegCount() == ourF3XGenericTask->getLegNumberMax()) {
    // logMsg(DEBUG, String(F("getF3BSpeedWebData: F3X_GFT_RUNNING_TIME ")) + String(ourF3XGenericTask->getCourseTime(F3X_GFT_RUNNING_TIME))); 
    *aReturnString += String(F("id_running_speed_time="))
                  + ourF3XGenericTask->getLegTimeString(ourF3XGenericTask->getCourseTime(F3X_GFT_RUNNING_TIME), F3X_TIME_NOT_SET, 0, 0, 0)
                  + MYSEP_STR;
  }
}

void getWebLogReq() {
  String response;

  response += F("<!DOCTYPE html>\n");
  response += F("<html>\n");
  response += F("<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"/>\n");
  response += F("<head>\n");
  response += F("<title>Logging</title>\n");
  response += F("</head>\n");
  response += F("<body>\n");

  for (uint8_t i=0; i<LOGBUFFSIZE; i++) {
    response += Logger::getInstance().getInternalMsg(i);
    response += "<br>\n";
  }
  response+=F("<hr>\n");
  response+=F("<button type=\"button\" id=\"id_backToRoot\" onclick=\"back()\">Back</button>\n");
  response+=F("<script>\n");
  response+=F("function back() { setTimeout(function() {window.location.href='/';}, 200, ); }");
  response+=F("</script>\n");
  response+=F("</body>\n");
  ourWebServer.send(200, F("text/html"), response.c_str()); 
}

void getWebDataReq() {
  String response;
  if (ourConfig.competitionSetting == true && ourIsTimeCriticalOperationRunning == true) {
    // do nothing, in case of competition setting and time critical operation is running
   //  ourWebServer.send(503, F("text/plain"), F("XXXXXX !"));
    response = String(F("id_time=")) + F3XFixedDistanceTask::getHMSTimeStr(millis()) + MYSEP_STR;
    ourWebServer.send(200, F("text/plane"), response.c_str()); //Send the response value only to client ajax request
    return;
  }
  #ifdef PERF_DEBUG
  unsigned long start = millis();
  #endif
  // logMsg(DEBUG, ourWebServer.client().remoteIP().toString());

  if (ourStartupPhase) {
    String message = F("getWebDataReq");

    message += F("client : ");
    message += ourWebServer.client().remoteIP().toString();
    message += F("URI: ");
    message += ourWebServer.uri();
    message += F("\nMethod: ");
    message += (ourWebServer.method() == HTTP_GET)?F("GET"):F("POST");
    message += F("\nArguments: ");
    message += ourWebServer.args();
    message += F("\n");
    for (uint8_t i=0; i<ourWebServer.args(); i++){
      message += F(" NAME:")+ourWebServer.argName(i) + F("\n VALUE:") + ourWebServer.arg(i) + F(")\n");
    }

    LOGGY2(INFO, F("ignoring HTTP req: ") + message);
    // ignore all requests in startup phase
    return;
  }
  for (uint8_t i=0; i<ourWebServer.args(); i++){
    String argName = ourWebServer.argName(i);
    String pushData;

     
    // #define HTTP_LOG
    #ifdef HTTP_LOG 
    logMsg(DEBUG, F("getWebDataReq: ") + argName); 
    #endif
    if (argName.equals(F("id_version"))) {
      response += argName + "=" + APP_VERSION + MYSEP_STR;
    } else
    // WiFi stuff
    if (argName.equals(F("id_wlanSsid"))) {
        response += argName + "=" + ourConfig.wlanSsid + MYSEP_STR;
    } else
    if (argName.equals(F("id_wlanPasswd"))) {
        response += argName + "=" + String(F("************")) + MYSEP_STR;
    } else
    if (argName.equals(F("id_apSsid"))) {
        response += argName + "=" + ourConfig.apSsid + MYSEP_STR;
    } else
    if (argName.equals(F("id_apPasswd"))) {
      if (String(ourConfig.apPasswd).length() != 0) {
        response += argName + "=" + String(F("************")) + MYSEP_STR;
      }
    } else
    if (argName.equals("id_wifiActive")) {
      if (ourConfig.wifiIsActive == true) {
        response += argName + "=" + String(F("checked")) + MYSEP_STR;
      }
    } else
    if (argName.equals(F("id_online_status"))) {
      if (WiFi.status() == WL_CONNECTED) {
        response += argName + "=online" + MYSEP_STR;
      } else {
        response += argName + "=offline" + MYSEP_STR;
      }
    } else
    if (argName.equals(F("id_f3b_speed_tasktime"))) {
      response += argName + "=" + String(ourConfig.f3bSpeedTasktime) + MYSEP_STR;
    } else
    if (argName.equals(F("id_f3f_tasktime"))) {
      response += argName + "=" + String(ourConfig.f3fTasktime) + MYSEP_STR;
    } else
    if (argName.equals(F("id_f3f_leg_length"))) {
      response += argName + "=" + String(ourConfig.f3fLegLength) + MYSEP_STR;
    } else
    if (argName.equals(F("id_buzzer_setting"))) {
      String setting;
      switch (ourConfig.buzzerSetting) {
        case BS_ALL: // both buzzers are active 
          setting=F("all");
          break;
        case BS_BASEMANAGER: // only direct connected BaseManager Buzzer 
          setting=F("aline");
          break;
        case BS_REMOTE_BUZZER: // only remote radio buzzer
          setting=F("radiobuzzer");
          break;
        case BS_NONE: // no buzzers are active 
          setting=F("none");
          break;
      }
      response += argName + "=" + setting + MYSEP_STR;
    } else
    if (argName.equals(F("id_competition_setup"))) {
      String setting="false";
      if (ourConfig.competitionSetting) {
        setting="true";
      }
      response += argName + "=" + setting + MYSEP_STR;
    } else
    if (argName.equals(F("id_radio_channel"))) {
        response += argName + "=" + String(ourRadio.getChannel()) + MYSEP_STR;
    } else
    if (argName.equals(F("id_radio_power"))) {
        response += argName + "=" + ourRadio.getPowerStr() + MYSEP_STR;
    } else
    if (argName.equals(F("initMainMenu"))) {
      ourContext.set(TC_F3XBaseMenu);
      response += String(F("id_version=")) + APP_VERSION + MYSEP_STR;
    } else
    if (argName.equals(F("initF3BSpeedTask"))) {
      ourContext.set(TC_F3BSpeedTask);
      setActiveTask(F3XFixedDistanceTask::F3BSpeedType);
      response += String(F("id_version=")) + APP_VERSION + MYSEP_STR;
      getWebHeaderData(&pushData, true);
      getF3BSpeedWebData(&pushData, true);
      response += pushData;
    } else
    if (argName.equals(F("pollF3BSpeedTask"))) {
      // logMsg(DEBUG, pollF3BSpeedTask");
      getWebHeaderData(&pushData, false);
      getF3BSpeedWebData(&pushData, false);
      response += pushData;
    } else
    if (argName.equals(F("initF3FTask"))) {
      ourContext.set(TC_F3FTask);
      setActiveTask(F3XFixedDistanceTask::F3FType);
      response += String(F("id_version=")) + APP_VERSION + MYSEP_STR;
      getWebHeaderData(&pushData, true);
      getF3FWebData(&pushData, true);
      response += pushData;
    } else
    if (argName.equals(F("pollF3FTask"))) {
      // logMsg(DEBUG, pollF3BSpeedTask");
      getWebHeaderData(&pushData, false);
      getF3FWebData(&pushData, false);
      response += pushData;
    } else
    if (argName.equals(F("initHeaderData"))) {
      response += String(F("id_version=")) + APP_VERSION + MYSEP_STR;
      getWebHeaderData(&response, true);
      response += pushData;
    } else
    if (argName.equals(F("id_running_speed_time"))) {
      if (ourF3XGenericTask->getTaskState() == F3XFixedDistanceTask::TaskRunning ) {
        String resp;
        if (ourF3XGenericTask->getSignalledLegCount() >= 0) {
          resp = String(F("id_running_speed_time="))
                    + ourF3XGenericTask->getLegTimeString(ourF3XGenericTask->getCourseTime(F3X_GFT_RUNNING_TIME), F3X_TIME_NOT_SET, 0, 0, 0)
                    + MYSEP_STR;
        } else {
          resp = String(F("id_running_speed_time="))
                    + ourF3XGenericTask->getLegTimeString(F3X_TIME_NOT_SET, F3X_TIME_NOT_SET, 0, 0, 0)
                    + MYSEP_STR;
        }
        logMsg(LOG_MOD_HTTP, DEBUG, resp);
        response +=resp;
      }

    } else
    {
      logMsg(ERROR, String(F("ERROR: unknown name of get request: ") + argName));
    }
  }
  #ifdef PERF_DEBUG
  unsigned long wstart = millis();
  #endif
  ourWebServer.send(200, F("text/plane"), response.c_str()); //Send the response value only to client ajax request
  #ifdef PERF_DEBUG
  if ((millis() - wstart) > 20) {
    logMsg(LOG_MOD_PERF, DEBUG, String(F("performance info for ourWebServer.send():")) + String(millis()-wstart));
  }
  #endif
  
  #ifdef PERF_DEBUG
  if ((millis() - start) > 20) {
    logMsg(LOG_MOD_PERF, DEBUG, String(F("performance info for getWebDataReq:")) + String(millis()-start));
    String message = F("getWebDataReq");

    message += F("client : ");
    message += ourWebServer.client().remoteIP().toString();
    message += F("URI: ");
    message += ourWebServer.uri();
    message += F("\nMethod: ");
    message += (ourWebServer.method() == HTTP_GET)?F("GET"):F("POST");
    message += F("\nArguments: ");
    message += ourWebServer.args();
    message += F("\n");
    for (uint8_t i=0; i<ourWebServer.args(); i++){
      message += F(" NAME:")+ourWebServer.argName(i) + F("\n VALUE:") + ourWebServer.arg(i) + F(")\n");
    }

    logMsg(DEBUG, message);
  }
  #endif
}


void setupWebServer() {
  // react on these "pages"
  ourWebServer.on(F("/getDataReq"),getWebDataReq);
  ourWebServer.on(F("/setDataReq"),setWebDataReq);
  ourWebServer.on(F("/internalLog.html"),getWebLogReq);

  // If the client requests any URI
  ourWebServer.onNotFound([]() {
    String message = F("file request");

    message += F("client : ");
    message += ourWebServer.client().remoteIP().toString();
    message += F("URI: ");
    message += ourWebServer.uri();
    message += F("\nMethod: ");
    message += (ourWebServer.method() == HTTP_GET)?F("GET"):F("POST");
    message += F("\nArguments: ");
    message += ourWebServer.args();
    message += F("\n");
    for (uint8_t i=0; i<ourWebServer.args(); i++){
      message += F(" NAME:")+ourWebServer.argName(i) + F("\n VALUE:") + ourWebServer.arg(i) + F(")\n");
    }

    logMsg(DEBUG, message);

    if (!handleWebFileRead(ourWebServer.uri())) {
      ourWebServer.send(404, F("text/plain"), F("F3B Training Error: 404\n File or URL not Found !"));
    }
  });

  ourWebServer.begin();               // Starte den Server
  logMsg(DEBUG, F("HTTP Server started"));
  #ifdef USE_MDNS
  MDNS.addService("http", "tcp", 80);
  #endif
  forceOLED(0, String("WebServer started"));
}

// End: WEBSERVER WEBSERVER WEBSERVER 

// OVER THE AIR
#ifdef OTA
void setup_ota() {
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname(String("esp8266" + WiFi.macAddress()).c_str());

  // No authentication by default
  ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    // first force a restart of the B-Line-Controller, to allow channel sync
    restartMCs(0, true);
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = F("sketch");
    } else { // U_FS
      type = F("filesystem");
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    logMsg(INFO, F("Start updating ") + type);
  });
  ArduinoOTA.onEnd([]() {
    logMsg(INFO, F("\nEnd"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // logMsg(DEBUG, Progress: %u%%\r", (progress / (total / 100)));
      forceOLED(String("OTA Update"), String(progress) + String(F("/")) + String(total));
    if (progress%5==0) {
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    String printout = F("Error[");
    printout.concat(error);
    printout.concat(F("]:"));
     
    if (error == OTA_AUTH_ERROR) {
      printout.concat(F("Auth Failed"));
    } else if (error == OTA_BEGIN_ERROR) {
      printout.concat(F("Begin Failed"));
    } else if (error == OTA_CONNECT_ERROR) {
      printout.concat(F("Connect Failed"));
    } else if (error == OTA_RECEIVE_ERROR) {
      printout.concat(F("Receive Failed"));
    } else if (error == OTA_END_ERROR) {
      printout.concat(F("End Failed"));
    }
    logMsg(ERROR, printout);
  });
  ArduinoOTA.begin();
  logMsg(INFO, String(F("ArduinoOTA: setup ready")) + F("  IP address: ") + WiFi.localIP().toString());
  forceOLED(0,  String("OTA started ok"));
}
#endif

void otaUpdate(bool aFileSystemUpdate) {
  String url;
  ourContext.set(TC_F3XMessage);
  if (aFileSystemUpdate) {
    url=F("http://f3b.so-fa.de/f3xtrainer/f3xa_fs.bin");
    ourContext.setInfo("updating fs ...");
  } else {
    url=F("http://f3b.so-fa.de/f3xtrainer/f3xa_fw.bin");
    ourContext.setInfo("updating fw ...");
  }
  // update the code
  if (WiFi.status() != WL_CONNECTED) {
    ourContext.setInfo("ERROR: WiFi not connected");
    return;
  }
  WiFiClient client;
  updateOLED(millis(), true);
  t_httpUpdate_return ret;
  if (aFileSystemUpdate) {
    ret = ESPhttpUpdate.updateFS(client, url, "");
  } else {
    ret = ESPhttpUpdate.update(client, url, "");
  }
  String retStr;
  switch (ret) {
    case HTTP_UPDATE_FAILED: 
      logMsg(LOG_MOD_HTTP, ERROR, String("HTTP_UPDATE_FAILED Error") + ESPhttpUpdate.getLastErrorString().c_str());

      ourContext.setInfo(String("ERROR: Update failed"));
      break;
    case HTTP_UPDATE_NO_UPDATES: 
      logMsg(LOG_MOD_HTTP, ERROR, "HTTP_UPDATE_NO_UPDATES"); 
      ourContext.setInfo("ERROR: no update found");
      break;
    case HTTP_UPDATE_OK: 
      logMsg(LOG_MOD_HTTP, INFO, "HTTP_UPDATE_OK"); 
      ourContext.setInfo("Update done, rebooting");
      restartMCs(1000);
      break;
    default:
      retStr = String("HTTP_UPDATE_ERR:") + String(ret); 
      logMsg(LOG_MOD_HTTP, INFO, retStr); 
      ourContext.setInfo(retStr);
      break;
  }
}
// End: OVER THE AIR

void setupRemoteCmd() {
  ourRemoteCmd.begin();
}

/**
 * this function will be call by the F3F Task object, to indicate
 * notifiable time proceedings
*/
void f3fTimeProceedingListener() {
  logMsg(LOG_MOD_SIG, INFO, F("Time Proceeding Notification"));
  signalBuzzing(BUZZ_TIME_SHORT);
}

void taskStateListener(F3XFixedDistanceTask::State aState) {
  switch(aState) {
    case F3XFixedDistanceTask::TaskRunning:
      ourIsTimeCriticalOperationRunning = true;
      signalBuzzing(BUZZ_TIME_NORMAL);
      break;
    case F3XFixedDistanceTask::TaskWaiting:
      ourIsTimeCriticalOperationRunning = false;
      break;
    case F3XFixedDistanceTask::TaskError:
    case F3XFixedDistanceTask::TaskTimeOverflow:
      ourIsTimeCriticalOperationRunning = false;
      signalBuzzing(BUZZ_TIME_LONG);
      break;
    case F3XFixedDistanceTask::TaskFinished:
      ourIsTimeCriticalOperationRunning = false;
      break;
    case F3XFixedDistanceTask::TaskNotSet:
      ourIsTimeCriticalOperationRunning = false;
      break;
  }
}

void setupF3XTasks() {
  // F3BSpeedTask
  ourF3BSpeedTask.addSignalAListener(signalAListener);
  ourF3BSpeedTask.addSignalBListener(signalBListener);
  ourF3BSpeedTask.addStateChangeListener(taskStateListener);
  ourF3BSpeedTask.setTasktime(ourConfig.f3bSpeedTasktime);
  ourF3BTaskData.init();

  // F3FTask
  ourF3FTask.addSignalAListener(signalAListener);
  ourF3FTask.addSignalBListener(signalBListener);
  ourF3FTask.addStateChangeListener(taskStateListener);
  ourF3FTask.addTimeProceedingListener(f3fTimeProceedingListener);
  ourF3FTask.setTasktime(ourConfig.f3fTasktime);
  ourF3FTask.setLegLength(ourConfig.f3fLegLength);
  ourF3FTaskData.init();
  
  // set a default task to avoid not initialized task settings
  setActiveTask(F3XFixedDistanceTask::F3BSpeedType);
}

void setupSignallingButton() {
  // BUTTON SETUP 
  // INPUT_PULLUP for bare ourPushButton connected from GND to input pin
  ourPushButton.attach(PIN_SIGNAL_A_LINE, INPUT); // USE EXTERNAL PULL-UP

  // DEBOUNCE INTERVAL IN MILLISECONDS
  ourPushButton.interval(10); 

  // INDICATE THAT THE LOW STATE CORRESPONDS TO PHYSICALLY PRESSING THE BUTTON
  ourPushButton.setPressedState(LOW); 
}
  

void setupSerial() {
  #ifdef USE_RXTX_AS_GPIO
    // to enable usage of D9/D10 as digtal inputs, the serial port 
    // has to be reconfigured
    //********** CHANGE PIN FUNCTION  TO GPIO **********
    //GPIO 1 (TX) swap the pin to a GPIO.
    pinMode(1, FUNCTION_3); 
    //GPIO 3 (RX) swap the pin to a GPIO.
    pinMode(3, FUNCTION_3);
    logMsg(INFO, String(myName) + String(F(": ")) + String(APP_VERSION));
//**************************************************
  #else
    //********** CHANGE PIN FUNCTION  TO TX/RX **********
    //GPIO 1 (TX) swap the pin to a TX.
    // pinMode(1, FUNCTION_0); 
    //GPIO 3 (RX) swap the pin to a RX.
    // pinMode(3, FUNCTION_0); 
    //***************************************************
    Serial.begin(115200);
    Serial.println();
    Serial.print(F("F3X Training :"));
    Serial.println(APP_VERSION);
  #endif
  delay(500);
}
#ifdef USE_RXTX_AS_GPIO
  #define SERIAL_LOG false
#else
  #define SERIAL_LOG true
#endif

void setupLog(const char* aName) {
  Logger::getInstance().setup(aName);
  Logger::getInstance().doSerialLogging(SERIAL_LOG);
  Logger::getInstance().setLogLevel(LOG_MOD_ALL, INFO);
  Logger::getInstance().setLogLevel(LOG_MOD_RADIO, DEBUG);
  Logger::getInstance().setLogLevel(LOG_MOD_INTERNAL, INFO);
  Logger::getInstance().setWebLogLevel(WARNING);

  if (SERIAL_LOG) {
    logMsg(INFO, F("serial loggin enabled"));
  }
}

// config in EEPROM
void saveConfig() {
  logMsg(LOG_MOD_INTERNAL, INFO, F("saving config to EEPROM "));
  // Save configuration from RAM into EEPROM
  EEPROM.begin(512);
  EEPROM.put(0, ourConfig );
  delay(10);
  EEPROM.commit();                      // Only needed for ESP8266 to get data written
  EEPROM.end();                         // Free RAM copy of structure
}

void setDefaultConfig() {
  logMsg(LOG_MOD_INTERNAL, INFO, F("setting default config to EEPROM "));
  // Reset EEPROM bytes to '0' for the length of the data structure
  strncpy(ourConfig.version , CONFIG_VERSION, CONFIG_VERSION_L);
  strncpy(ourConfig.wlanSsid, "", CONFIG_SSID_L);
  strncpy(ourConfig.wlanPasswd, "", CONFIG_PASSW_L);
  strncpy(ourConfig.apSsid, "f3xct", CONFIG_SSID_L);
  strncpy(ourConfig.apPasswd, "12345678", CONFIG_PASSW_L) ;
  ourConfig.wifiIsActive = true;
  ourConfig.oledFlipped = false;
  ourConfig.rotaryEncoderFlipped = false;
  ourConfig.radioChannel = 110;
  ourConfig.radioPower = RF24_PA_HIGH;
  ourConfig.f3bSpeedTasktime = 150;
  ourConfig.f3fTasktime = 30;
  ourConfig.f3fLegLength = 100;
  ourConfig.buzzerSetting = (uint8_t) BS_REMOTE_BUZZER;
  ourConfig.competitionSetting = false;
}

void loadConfig() {
  logMsg(INFO, F("loading config from EEPROM "));
  // Loads configuration from EEPROM into RAM
  EEPROM.begin(512);
  EEPROM.get(0, ourConfig );
  EEPROM.end();

  // config was never written to EEPROM, so set the default config data and save it to EEPROM
  if ( String(CONFIG_VERSION) == ourConfig.version ) {
     // ok do nothing, this is the expected version
    forceOLED(0, String("config ok"));
  } else if (String("XYZ_") == ourConfig.version ) {
     // ok do nothing, this is the expected version
    logMsg(LOG_MOD_INTERNAL, INFO, String(F("old but compatible config version found: ")) + String(ourConfig.version));
    forceOLED(0, String("config ok"));
  } else {
    logMsg(LOG_MOD_INTERNAL, WARNING, String(F("unexpected config version found: ")) + String(ourConfig.version));
    forceOLED(0, String("config reset"));
    setDefaultConfig();
    saveConfig();
  }
}

void setupConfig() {
  loadConfig();
  String cfg = F("Cfg: Radio(p/c):");
  String pow;
  switch(ourConfig.radioPower) {
    case RF24_PA_MAX:
      pow = F("MAX");
      break;
    case RF24_PA_HIGH:
      pow = F("HIGH");
      break;
    case RF24_PA_LOW:
      pow = F("LOW");
      break;
    case RF24_PA_MIN:
      pow = F("MIN");
      break;
  }
  cfg.concat(pow);
  cfg.concat(String(F("/")));
  cfg.concat(String(ourConfig.radioChannel));
  forceOLED(0, cfg);
  yield();
  delay(1100);
  forceOLED(0, cfg);
  cfg = F("Cfg: F3B Speed ttime :");
  if (ourConfig.f3bSpeedTasktime < 60 || ourConfig.f3bSpeedTasktime > 300) {
    ourConfig.f3bSpeedTasktime = 180;
  }
  cfg.concat(String(ourConfig.f3bSpeedTasktime));
  cfg.concat(String(F("s")));
  forceOLED(0, cfg);
  yield();
  delay(1100);
  if (ourConfig.f3fLegLength < 50 || ourConfig.f3fLegLength > 150) {
    ourConfig.f3fLegLength = 100;
  }
  cfg = F("Cfg: F3F leg length :");
  cfg.concat(String(ourConfig.f3fLegLength));
  cfg.concat(String(F("m")));
  forceOLED(0, cfg);
  yield();
  delay(1100);
  if (ourConfig.f3fTasktime < 0 || ourConfig.f3fTasktime > 300) {
    ourConfig.f3fTasktime = 30;
  }
  cfg = F("Cfg: F3F ttime :");
  cfg.concat(String(ourConfig.f3fTasktime));
  cfg.concat(String(F("s")));
  forceOLED(0, cfg);
  yield();
  delay(1100);
  if (ourConfig.buzzerSetting < 0 || ourConfig.buzzerSetting >= (uint8_t) BS_LAST) {
    ourConfig.buzzerSetting = (uint8_t) BS_REMOTE_BUZZER;
  }
  yield();
  delay(1100);
} 

void setup() {
  setupLog(myName);
  setupSerial();
  #ifdef OLED 
  setupOLED();
  #endif
  setupConfig();
  setupLittleFS();
  setupWiFi();
  setupWebServer();
  setupRadio();
  setupRemoteCmd();
  setupF3XTasks();
  #ifdef USE_BATTERY_IN_VOLTAGE
  setupBatteryIn();
  #endif
  setupSignallingButton();

  if (WiFi.status() == WL_CONNECTED) {
    #ifdef OTA
    setup_ota();
    #endif
  }
  delay(1000);
  ourContext.set(TC_F3XBaseMenu);
//  ourContext.set(TC_F3XInfo);
  #ifdef USE_RXTX_AS_GPIO
  resetRotaryEncoder(TC_F3XBaseMenu);
  #endif
}

void updateRadio(unsigned long aNow) {
  
  static unsigned long lastCmdCycleTestRequest = 0;
  #define CMD_CYCLE_REQUEST_DELAY 500

  static unsigned long lastBLineRequest = 0;
  #define B_LINE_REQUEST_DELAY 3000
  
  static boolean isCmdCycleAnswerReceived = true;
  uint8_t id=0;

  // first try to read all data comming from radio peer
  while (ourRadio.available()) {          
    ourRemoteCmd.write(ourRadio.read());
  }
  
  // if data from remote side builds a complete command handle it
  if (ourRemoteCmd.available()) {
    String* arg = NULL;

    // here the received F3XRemoteCommand (from A-/B-Line) are dispatched and handled 
    switch (ourRemoteCmd.getType()) {
      case F3XRemoteCommandType::SignalA: 
        logMsg(LOG_MOD_WEB, INFO, F("Signal-A received"));
        ourF3XGenericTask->signal(F3XFixedDistanceTask::SignalA);
        break;
      case F3XRemoteCommandType::SignalB:
        logMsg(LOG_MOD_WEB, INFO, F("Signal-B received"));
        ourF3XGenericTask->signal(F3XFixedDistanceTask::SignalB);
        switch(ourContext.get()) {
          case TC_F3FTaskMenu:
          case TC_F3BSpeedMenu:
            // for test purpose before task are started remote signals shall buzzer 
            ourBuzzer.pattern(5,50,100,50,100,500);
            break;
        }
        break;
      case F3XRemoteCommandType::CmdCycleTestAnswer:
        ourRadioCycleRecvCnt++;
        arg = ourRemoteCmd.getArg();
        ourRadioRoundtripIdx = arg->toInt();
        if (ourRadioRoundtripIdx == ourRadioRequestArg) {
          float rtt = millis() - ourRadioRequestTime;
          ourRadioRoundTripTime = rtt + 0.75f * (ourRadioRoundTripTime - rtt);
          isCmdCycleAnswerReceived = true;
          logMsg(LOG_MOD_RTEST, INFO, String(F("CmdCycleTestAnswer received: ")) 
            + String(ourRadioRoundTripTime, 1) + "/" + String(rtt,1)
            + String(F(" #[")) + String(ourRadioRoundtripIdx)+ String(F("]")));
        } else {
           logMsg(LOG_MOD_RTEST, WARNING, String(F("!!!! wrong CycleTest answer: ")) + *arg); 
        }
        break;
      case F3XRemoteCommandType::BLineStateResp: {
          ourBatteryBVoltageRaw = ourRemoteCmd.getArg()->toInt();;
          float volt=(((float) ourBatteryBVoltageRaw)/1023.0)*5.0f*1000*1.012f;
          ourBatteryBVoltage = volt;
          logMsg(LOG_MOD_SIG, INFO, String(F("Battery B voltage: ")) + String(ourBatteryBVoltage) + F("mV"));
        }
        break;
      case F3XRemoteCommandType::RemoteSignalStateResp: {
          ourBatteryRemoteSignalRaw = ourRemoteCmd.getArg()->toInt();;
          float volt=(((float) ourBatteryRemoteSignalRaw)/1023.0)*10.0f*1000*1.0f;
          logMsg(LOG_MOD_SIG, INFO, String(F("RemoteSignalBattery  voltage: ")) + String(ourBatteryRemoteSignalRaw) + String("/") + String(volt) + F("mV"));
        }
        break;
      default:
        logMsg(ERROR, F("unknow RTC data"));
        break;
    }
    ourRemoteCmd.consume();
  }

  // sending a state request to remote controller, but only if task is not running
  if (aNow > lastBLineRequest && ourF3XGenericTask->getTaskState() == F3XFixedDistanceTask::TaskWaiting) {
    lastBLineRequest = aNow + B_LINE_REQUEST_DELAY;
    unsigned long a = millis();
    boolean sendSuccess = ourRadio.transmit(
      ourRemoteCmd.createCommand(F3XRemoteCommandType::BLineStateReq, String(ourRadioRequestArg))->c_str(), 20);

    uint16_t signalRoundTrip = millis() - a;

    uint8_t lost=0;
    if (!sendSuccess) {
      logMsg(LOG_MOD_RADIO, INFO, String(F("sending TestRequest NOT successsfull. Retransmissions: ")) 
        + String(ourRadio.getRetransmissionCount()) + String(F("/")) + String(signalRoundTrip) + String(F("ms")));
      ourBuzzer.on(PinManager::SHORT);
      lost=100;
      signalRoundTrip = UINT16_MAX;
      ourBatteryBVoltage = 0;
    } else {
      logMsg(LOG_MOD_RADIO, DEBUG, String(F("sending TestRequest successsfull. Retransmissions: ")) 
        + String(ourRadio.getRetransmissionCount()) + String(F("/")) + String(signalRoundTrip) + String(F("ms")));
    }

    float quality = 100.0f - 100.0f * ourRadio.getRetransmissionCount()/20;
    if (lost) {
      quality=0.0f;
      ourRadioStatePacketsMissed++;
    }
    ourRadioQuality = irr_low_pass_filter(ourRadioQuality, quality, 0.4f);
    ourRadioSignalRoundTrip = irr_low_pass_filter(ourRadioSignalRoundTrip, signalRoundTrip, 0.4f);
    logMsg(LOG_MOD_RADIO, INFO, F("radio quality: ") + String(quality, 0) + F("%/") + String(ourRadioQuality,0) + F("%"));


    // request state infos from the remote radio signal device, but only while task is not running
    if (true) {
      ourRadio.setWritingPipe(2);
      ourRadio.transmit(ourRemoteCmd.createCommand(F3XRemoteCommandType::RemoteSignalStateReq, String(ourRadioRequestArg))->c_str(), 20);
      ourRadio.setWritingPipe(0);
    }
  }

  if (ourRadioSendSettings && ourRadioQuality > 99.0f) {
    static uint8_t power = -1;
    String settings;
    // power / channel / rate / ack
    settings=
      String(ourRadioPower) + CMDSEP_STR 
       + String(ourRadioChannel) + CMDSEP_STR 
       + String(ourRadioDatarate) + CMDSEP_STR 
       + String(ourRadioAck);
    // 1,83,0,1;
    
    if (ourRadio.transmit(ourRemoteCmd.createCommand(F3XRemoteCommandType::CmdSetRadio, settings)->c_str(), 20) ) {
      // changed radio settings are successfully transmitted to B-Line, now the BaseManager can also be switched
      ourRadio.setWritingPipe(2);
      ourRadio.transmit(ourRemoteCmd.createCommand(F3XRemoteCommandType::CmdSetRadio, settings)->c_str(), 20);
      ourRadio.setWritingPipe(0);
      ourRadio.setPower(ourRadioPower);
      ourRadio.setChannel(ourRadioChannel);
      ourRadio.setDataRate(ourRadioDatarate);
      ourRadio.setAck(ourRadioAck);

      // set the new radio settings to the configuration data, which can be stored in the EEPROM later on
      ourConfig.radioChannel = ourRadioChannel;
      ourConfig.radioPower = ourRadioPower;
      LOGGY3(LOG_MOD_RADIO, INFO, F("send radio setting to remote and set local") + settings);
    } else {
      logMsg(LOG_MOD_RADIO, ERROR, String(F("sending settings not possible: ") + settings));
    }
    ourRadioSendSettings=false;
  }
}

void updateBuzzer(unsigned long aNow) {
  ourBuzzer.update(aNow);
}

#ifdef OLED 
void takeOLEDScreenshot() {
  String screenshotPath = F("/screenshot.xbm");
  logMsg(LOG_MOD_INTERNAL, INFO, String(F("create a xbm screenshot file: ") + screenshotPath));
  File file = LittleFS.open(screenshotPath.c_str(), "w");
  if(file) {
    ourOLED.writeBufferXBM(file);
    file.close();
  } else {
   logMsg(LOG_MOD_INTERNAL, ERROR, String(F("cannot open screenshot file: ") + screenshotPath));
  }
}

void showF3XTasktimeCfg() {
  // TC_F3BSpeedTasktimeCfg:
  // TC_F3FTasktimeCfg:
  ourOLED.setFont(oledFontLarge);

  ourOLED.setCursor(0, 12+4);
  if (ourContext.get() == TC_F3BSpeedTasktimeCfg) {
    ourOLED.print(F("F3B Speed Tasktime"));
  } else if (ourContext.get() == TC_F3FTasktimeCfg) {
    ourOLED.print(F("F3F Tasktime"));
  }

  ourOLED.setFont(oledFontBig);
  ourOLED.setCursor(0, 50);
  if (ourContext.get() == TC_F3BSpeedTasktimeCfg) {
    ourOLED.print(String(ourConfig.f3bSpeedTasktime));
  } else if (ourContext.get() == TC_F3FTasktimeCfg) {
    ourOLED.print(String(ourConfig.f3fTasktime));
  } 
  ourOLED.print(F("s"));
}

void showF3XLegLengthCfg() {
  // TC_F3FLegLengthCfg:
  ourOLED.setFont(oledFontLarge);

  ourOLED.setCursor(0, 12+4);
  ourOLED.print(F("F3F Leg Length"));

  ourOLED.setFont(oledFontBig);
  ourOLED.setCursor(0, 50);
  ourOLED.print(String(ourConfig.f3fLegLength));
  ourOLED.print(F("m"));
}

void showRadioChannelPage() {
  // TC_F3XRadioChannel:
  ourOLED.drawBox(0, 0, 128, 16);
  ourOLED.drawBox(0, 28, 128, 14);

  ourOLED.setDrawColor(2);
  ourOLED.setFontMode(1);
  ourOLED.setFont(oledFontNormal);
  ourOLED.drawStr(5,12, String(F("Set Radio Channel:")).c_str());
  ourOLED.setFont(oledFontBig);

  int8_t items[3];
  items[0] = getModulo(ourRadioChannel-1, RF24_1MHZ_CHANNEL_NUM);
  items[1] = getModulo(ourRadioChannel, RF24_1MHZ_CHANNEL_NUM);
  items[2] = getModulo(ourRadioChannel+1, RF24_1MHZ_CHANNEL_NUM);
  for (int8_t i=0; i<3; i++) {
    String preFix = F(" ");
    if (items[i] == ourRadio.getChannel()) {
      preFix=F("*");
    }
    float ch = 2.400f + (float) items[i] / 1000;
    String str = preFix + String(ch, 3) + String(F("GHz"));
    ourOLED.drawStr(0,28+i*13, str.c_str());
  }
  ourOLED.setFontMode(0);
  ourOLED.setDrawColor(1);
}

void showRadioPowerPage() {
  // TC_F3XRadioPowerCfg:
  ourOLED.drawBox(0, 0, 128, 16);
  ourOLED.drawBox(0, 28, 128, 14);

  ourOLED.setDrawColor(2);
  ourOLED.setFontMode(1);
  ourOLED.setFont(oledFontNormal);
  ourOLED.drawStr(5,12, String(F("Set Radio Power:")).c_str());

  ourOLED.setFont(oledFontBig);

  int8_t items[3];
  items[0] = getModulo(ourRadioPower-1, 4);
  items[1] = getModulo(ourRadioPower, 4);
  items[2] = getModulo(ourRadioPower+1, 4);
  for (int8_t i=0; i<3; i++) {
    String preFix=" ";
    if (items[i] == ourRadio.getPower()) {
      preFix="*";
    }
    String item = F("");
    switch(items[i]) {
      case RF24_PA_MAX:
        item = F("MAX  (1.000 mW)");
        break;
      case RF24_PA_HIGH:
        item = F("HIGH (0.250 mW)");
        break;
      case RF24_PA_LOW:
        item = F("LOW  (0.060 mW)");
        break;
      case RF24_PA_MIN:
        item = F("MIN  (0.016 mW)");
        break;
    }
    String str = preFix + String(item);
    ourOLED.drawStr(0,28+i*13, str.c_str());
  }
  ourOLED.setFontMode(0);
  ourOLED.setDrawColor(1);
}

void showInfoPage() {
  ourOLED.setFont(oledFontLarge);

  ourOLED.setCursor(0, 12+4);
  ourOLED.print(F("F3X Info"));

  ourOLED.setCursor(0, 28);
  ourOLED.setFont(oledFontNormal);
  ourOLED.print(F("IP:"));
  String ip;
  ourOLED.print(getWiFiIp(&ip));
  ourOLED.setCursor(0, 40);
  ourOLED.print(F("Bat-A:"));
  ourOLED.print(String((((float) ourBatteryAVoltage/1000)), 2));
  ourOLED.print(F("V/-B:"));
  ourOLED.print(String((((float) ourBatteryBVoltage/1000)), 2));
  ourOLED.print(F("V"));
  ourOLED.setCursor(0, 52);
  ourOLED.print(F("Radio (p/c/rt):"));
  String radio = 
  String(ourRadio.getPower()) + F("/") + 
  String(ourRadio.getChannel()) + F("/") + 
  String(ourRadioSignalRoundTrip);  
  ourOLED.print(radio);

  ourOLED.setFont(oledFontSmall);
  ourOLED.setCursor(0, 64);
  ourOLED.print(APP_VERSION);
  ourOLED.print(F(" (c)'23 R.Stransky"));
}

void showRadioInfoPage() {
  ourOLED.setFont(oledFontLarge);

  ourOLED.setCursor(0, 12+4);
  ourOLED.print(F("F3X Radio Info"));

  ourOLED.setCursor(5, 28);
  ourOLED.setFont(oledFontNormal);
  ourOLED.print(F("Quality:"));
  ourOLED.print(String(ourRadioQuality, 0).c_str());
  ourOLED.print(F(", mpkts:"));
  ourOLED.print(String(ourRadioStatePacketsMissed).c_str());
  ourOLED.setCursor(5, 40);
  ourOLED.print(F("RoundTrip: "));
  ourOLED.print((String(ourRadioSignalRoundTrip)+String(F("ms"))).c_str());
  ourOLED.setCursor(5, 52);
  ourOLED.print(F("Radio (p/c):"));
  ourOLED.setCursor(5, 64);
  String pow;
  switch(ourRadio.getPower()) {
      case RF24_PA_MAX:
        pow = F("MAX");
        break;
      case RF24_PA_HIGH:
        pow = F("HIGH");
        break;
      case RF24_PA_LOW:
        pow = F("LOW");
        break;
      case RF24_PA_MIN:
        pow = F("MIN");
        break;
    }
  String radio = pow + F("/") + String(ourRadio.getChannel()) ;
  ourOLED.print(radio);
}

void showMessagePage() {
  ourOLED.setFont(oledFontLarge);

  ourOLED.setCursor(0, 12+4);
  ourOLED.print(F("F3X Message"));

  ourOLED.setCursor(5, 28);
  ourOLED.setFont(oledFontNormal);
  ourOLED.print(ourContext.getInfoString());
}

void showOLEDMenu(const char* aItems[], uint8_t aNumItems, const char* aName, const char* aExtra="") {
  ourOLED.drawBox(0, 0, 128, 16);
  ourOLED.drawBox(0, 28, 128, 14);

  ourOLED.setDrawColor(2);
  ourOLED.setFontMode(1);
  ourOLED.setFont(oledFontNormal);
  ourOLED.setCursor(5, 12);
  ourOLED.print(aName);
  ourOLED.print(F(": "));
  ourOLED.print(aExtra);

  ourOLED.setFont(oledFontBig);
  for (int8_t i=-1; i<2; i++) {
    ourOLED.drawStr(0,41+i*13, aItems[getModulo(ourRotaryMenuPosition+i, aNumItems)]);
  }
  ourOLED.setFontMode(0);
  ourOLED.setDrawColor(1);
}

void showDialog(int aDuration, boolean aVal) {
  String msg = F("false");
  if (aVal) {
    msg = F("true");
  }
  showDialog(aDuration, msg);
}

void showDialog(int aDuration, String aMessage) {
  ourDialogTimer = millis() + aDuration;
  ourDialogString = aMessage;
}

void showDialog() {
  showMessage(F("Info"), ourDialogString);
}

void showError(int aError) {
  showMessage(F("Error"), String(aError));
}

void showMessage(String aHead, String aMessage) {
  ourOLED.setFont(oledFontNormal);
  ourOLED.drawStr(5,12, aHead.c_str());

  ourOLED.setFont(oledFontBig);
  if (aMessage.length() > 15) {
    ourOLED.setFont(oledFontNormal);
  }
  ourOLED.drawStr(0,41, aMessage.c_str());
}

void showNotYetImplemented() {
  showMessage(F("Error"), F("not yet implemented"));
}

void showF3FTask() {
  static unsigned long lastFT = 0;
  unsigned long courseTime = ourF3XGenericTask->getCourseTime(F3X_GFT_RUNNING_TIME);
  

  String msgStr;
  String courseTimeStr;
  String taskTime;
  courseTimeStr = getSCTimeStr(0UL, true);
  String info;

  char stateInfo='?';
  // if (courseTime != lastFT || ourF3XGenericTask->getTaskState() != F3XFixedDistanceTask::TaskRunning) {
    lastFT = courseTime;
    switch (ourF3XGenericTask->getTaskState()) {
      case F3XFixedDistanceTask::TaskRunning:
        stateInfo='R';
        if (ourF3XGenericTask->getSignalledLegCount() >=0) {
          stateInfo= ourF3XGenericTask->getSignalledLegCount() +'0';
        }
        switch (ourF3XGenericTask->getSignalledLegCount()) {
          case F3X_COURSE_INIT:
            info=F("next:A:lauch");
            break;
          case F3X_IN_AIR:
            info=F("next:A:A rev. cross");
            break;
          case F3X_IN_AIR_A_REV_CROSSING:
            info=F("next:A: in course");
            break;
          default:
            if (ourF3XGenericTask->getSignalledLegCount()%2 == 0) {
              info=F("next:B:turn");
            } else {
              info=F("next:A:turn");
            }
            break;
        }
       
        if (ourF3XGenericTask->getSignalledLegCount() >= F3X_IN_AIR) {
          courseTimeStr = getSCTimeStr(courseTime, true);
        }
        break;
      case F3XFixedDistanceTask::TaskWaiting:
        stateInfo='W';
        msgStr = F("Please start task...");
        info=F("P:Start Tasktime");
        break;
      case F3XFixedDistanceTask::TaskTimeOverflow:
        msgStr = F("TaskTime exceeded!!!");
        stateInfo='O';
        info=F("PP:Reset");
        break;
      case F3XFixedDistanceTask::TaskError:
        msgStr = F("Internal ERROR");
        stateInfo='E';
        break;
      case F3XFixedDistanceTask::TaskFinished:
        stateInfo='F';
        // courseTimeStr=ourF3XGenericTask->getLegTimeString(courseTime, F3X_TIME_NOT_SET, 0, 0, 0);
        courseTimeStr = getSCTimeStr(courseTime, true);
        info=F("");
        break;
      default:
        stateInfo='?';
        break;
    }
  
    // OLED 128x64
    ourOLED.setFont(oledFontNormal);
    ourOLED.setCursor(0, 12);
    ourOLED.print(F("F3F Task:"));
    ourOLED.setFont(oledFontBig);
    ourOLED.print(F(" "));
    if (ourF3XGenericTask->getLoopTasksEnabled()) {
      ourOLED.print(F("["));
      ourOLED.print(ourF3XGenericTask->getLoopTaskNum());
      ourOLED.print(F("] "));
    }
    ourOLED.print(courseTimeStr);

    ourOLED.setFont(oledFontSmall);
    ourOLED.setCursor(0, 63);
    ourOLED.print(info);
    ourOLED.setCursor(100, 63);
    ourOLED.print(F("["));
    ourOLED.print(stateInfo);
    ourOLED.print(F("]"));
    
    switch (ourF3FTask.getTaskState()) {
      // case F3XFixedDistanceTask::TaskWaiting:
      case F3XFixedDistanceTask::TaskRunning:
        ourOLED.setFont(oledFontNormal);
        ourOLED.setCursor(10, 27);
        ourOLED.print(F("Task Time: "));
        ourOLED.print(F3XFixedDistanceTask::getHMSTimeStr(ourF3XGenericTask->getRemainingTasktime(), true));
        ourOLED.setCursor(10, 37);
        ourOLED.print(F("in air: "));
        ourOLED.print(F3XFixedDistanceTask::getHMSTimeStr(ourF3XGenericTask->getInAirTime(), true));
        {
          int8_t numLegs = ourF3XGenericTask->getSignalledLegCount();
          if (numLegs > 0 ) {
            ourOLED.setCursor(20, 47);
            F3XLeg leg = ourF3XGenericTask->getLeg(numLegs-1);
            ourOLED.print(String(numLegs).c_str());
            ourOLED.print(F(": "));
            ourOLED.print(getLegTimeStr(leg.time, leg.deadTime, leg.deadDistance));
          }

          unsigned long lastcourseTime = ourF3XGenericTask->getLastLoopTaskCourseTime();
          if (ourF3XGenericTask->getSignalledLegCount() <= F3X_IN_AIR && lastcourseTime != 0) {
            ourOLED.setFont(oledFontNormal);
            ourOLED.setCursor(0, 54);
            ourOLED.print(F("Last Task:"));
            ourOLED.setFont(oledFontBig);
            ourOLED.print(F(" "));
            ourOLED.print(F("["));
            ourOLED.print(ourF3XGenericTask->getLoopTaskNum()-1);
            ourOLED.print(F("] "));
            String lastcourseTimeStr = getSCTimeStr(lastcourseTime, true);
            ourOLED.print(lastcourseTimeStr);
          }
        }
        break;
      case F3XFixedDistanceTask::TaskFinished:
         ourOLED.setFont(oledFontNormal); 
        F3XLeg leg;

        ourOLED.setCursor(5, 27);
        leg = ourF3XGenericTask->getLeg(F3X_LEG_MIN);
        ourOLED.print(F("min: ("));
        ourOLED.print(leg.idx+1);
        ourOLED.print(F(") "));
        ourOLED.print(getLegTimeStr(leg.time, leg.deadTime, leg.deadDistance));

        ourOLED.setCursor(5, 39);
        leg = ourF3XGenericTask->getLeg(F3X_LEG_AVG);
        ourOLED.print(F("avg: (-"));
        ourOLED.print(F(") "));
        ourOLED.print(getLegTimeStr(leg.time, leg.deadTime, leg.deadDistance));

        ourOLED.setCursor(5, 51);
        leg = ourF3XGenericTask->getLeg(F3X_LEG_MAX);
        ourOLED.print(F("max: ("));
        ourOLED.print(leg.idx+1);
        ourOLED.print(F(") "));
        ourOLED.print(getLegTimeStr(leg.time, leg.deadTime, leg.deadDistance));
        break;
      case F3XFixedDistanceTask::TaskWaiting:
      case F3XFixedDistanceTask::TaskTimeOverflow:
      case F3XFixedDistanceTask::TaskError:
        ourOLED.setFont(oledFontBig);
        ourOLED.setCursor(0, 27);
        ourOLED.print(msgStr);
        break;
    }
  // }
}
void showF3BSpeedTask() {
  static unsigned long lastFT = 0;
  unsigned long courseTime = ourF3XGenericTask->getCourseTime(F3X_GFT_RUNNING_TIME);
  

  String courseTimeStr;
  String msgStr;
  String taskTime;
  String legTimeStr[4];
  // courseTimeStr = ourF3XGenericTask->getLegTimeString(F3X_TIME_NOT_SET, F3X_TIME_NOT_SET, 0, 0, 0,'/', false, true);
  courseTimeStr = getSCTimeStr(0UL, true);
  String info;

  char stateInfo='?';
  // if (courseTime != lastFT || ourF3XGenericTask->getTaskState() != F3XFixedDistanceTask::TaskRunning) {
    lastFT = courseTime;
    switch (ourF3XGenericTask->getTaskState()) {
      case F3XFixedDistanceTask::TaskRunning:
        stateInfo='R';
        if (ourF3XGenericTask->getSignalledLegCount() >=0) {
          stateInfo = '0' + ourF3XGenericTask->getSignalledLegCount();
        }
        switch (ourF3XGenericTask->getSignalledLegCount()) {
          case F3X_COURSE_INIT:
            info=F("next:A: enter course");
            break;
          case 0: // A-line crossed 1.time = 0m
            info=F("next:A:repeat|B:turn");
            break;
          default:
            if (ourF3XGenericTask->getSignalledLegCount()%2 == 0) {
              info=F("next:B:turn");
            } else {
              info=F("next:A:turn");
            }
            break;
        }
       
        if (ourF3XGenericTask->getSignalledLegCount() >= F3X_COURSE_STARTED) {
          // courseTimeStr=ourF3XGenericTask->getLegTimeString(courseTime, F3X_TIME_NOT_SET, 0, 0, 0,'/', false, true);
          courseTimeStr = getSCTimeStr(courseTime, true);
        }
        break;
      case F3XFixedDistanceTask::TaskWaiting:
        stateInfo='W';
        msgStr = F("Please start task...");
        info=F("P:Start Tasktime");
        break;
      case F3XFixedDistanceTask::TaskTimeOverflow:
        stateInfo='O';
        msgStr = F("TaskTime exceeded");
        info=F("PP:Reset");
        break;
      case F3XFixedDistanceTask::TaskError:
        msgStr = F("internal ERROR!");
        stateInfo='E';
        break;
      case F3XFixedDistanceTask::TaskFinished:
        stateInfo='F';
        // courseTimeStr=ourF3XGenericTask->getLegTimeString(courseTime, F3X_TIME_NOT_SET, 0, 0, 0);
        courseTimeStr = getSCTimeStr(courseTime, true);
        for (int i=0; i < 4; i++) {
          F3XLeg leg = ourF3XGenericTask->getLeg(i);
          legTimeStr[i] = getLegTimeStr(leg.time, leg.deadTime, leg.deadDistance);
        }
        info=F("");
        break;
      default:
        stateInfo='?';
        break;
    }
  
    // OLED 128x64
    ourOLED.setFont(oledFontNormal);
    ourOLED.setCursor(0, 12);
    ourOLED.print(F("F3B Speed:"));
    ourOLED.setFont(oledFontBig);
    ourOLED.print(courseTimeStr);

    ourOLED.setFont(oledFontSmall);
    ourOLED.setCursor(0, 63);
    ourOLED.print(info);
    ourOLED.setCursor(100, 63);
    ourOLED.print(F("["));
    ourOLED.print(stateInfo);
    ourOLED.print(F("]"));
    
    switch (ourF3XGenericTask->getTaskState()) {
      case F3XFixedDistanceTask::TaskWaiting:
      case F3XFixedDistanceTask::TaskError:
      case F3XFixedDistanceTask::TaskTimeOverflow:
        ourOLED.setFont(oledFontBig);
        ourOLED.setCursor(0, 27);
        ourOLED.print(msgStr);
        break;
      case F3XFixedDistanceTask::TaskRunning:
        ourOLED.setFont(oledFontNormal);
        ourOLED.setCursor(10, 27);
        ourOLED.print(F("Task Time: "));
        ourOLED.print(F3XFixedDistanceTask::getHMSTimeStr(ourF3XGenericTask->getRemainingTasktime(), true));

        {
          unsigned long lastcourseTime = ourF3XGenericTask->getLastLoopTaskCourseTime();
          if (ourF3XGenericTask->getSignalledLegCount() <= F3X_IN_AIR && lastcourseTime != 0) {
            ourOLED.setFont(oledFontNormal);
            ourOLED.setCursor(0, 54);
            ourOLED.print(F("Last Task:"));
            ourOLED.setFont(oledFontBig);
            ourOLED.print(F(" "));
            ourOLED.print(F("["));
            ourOLED.print(ourF3XGenericTask->getLoopTaskNum()-1);
            ourOLED.print(F("] "));
            String lastcourseTimeStr = getSCTimeStr(lastcourseTime, true);
            ourOLED.print(lastcourseTimeStr);
          }
        }
        break;
      case F3XFixedDistanceTask::TaskFinished:
        ourOLED.setFont(oledFontNormal);
        ourOLED.setCursor(10, 27);
        ourOLED.print(F("Leg 1: "));
        ourOLED.print(legTimeStr[0]);
        ourOLED.setCursor(10, 39);
        ourOLED.print(F("Leg 2: "));
        ourOLED.print(legTimeStr[1]);
        ourOLED.setCursor(10, 51);
        ourOLED.print(F("Leg 3: "));
        ourOLED.print(legTimeStr[2]);
        ourOLED.setCursor(10, 63);
        ourOLED.print(F("Leg 4: "));
        ourOLED.print(legTimeStr[3]);
        break;
    }
  // }
}

void updateOLED(unsigned long aNow) {
  updateOLED(aNow, false);
}

void updateOLED(unsigned long aNow, bool aForce) {
  static unsigned long last=0;
  #define OLED_REFRESH_CYCLE 250


  if (aNow < last && !aForce) {
    return;
  }

  last = aNow + OLED_REFRESH_CYCLE;
  
#ifdef OLED_FULL_BUFFER
  ourOLED.clearBuffer();
#else
  ourOLED.firstPage();
  do {
#endif
    if (ourDialogTimer > aNow) {
      showDialog();
    } else {
      switch(ourContext.get()) {
        case TC_F3BSpeedTask:
          showF3BSpeedTask();
          break;
        case TC_F3FTask:
          showF3FTask();
          break;
        case TC_F3XMessage:
          showMessagePage();
          break;
        case TC_F3BSpeedTasktimeCfg:
        case TC_F3FTasktimeCfg:
          showF3XTasktimeCfg();
          break;
        case TC_F3FLegLengthCfg:
          showF3XLegLengthCfg();
          break;
        case TC_F3XRadioChannelCfg:
          showRadioChannelPage();
          break;
        case TC_F3XRadioPowerCfg:
          showRadioPowerPage();
          break;
        case TC_F3XInfo:
          showInfoPage();
          break;
        case TC_F3XRadioInfo:
          showRadioInfoPage();
          break;
        case TC_F3XBaseMenu:
          showOLEDMenu(ourF3XBaseMenuItems, ourF3XBaseMenuSize, ourF3XBaseMenuName);
          break;
        case TC_F3BSpeedMenu: 
          showOLEDMenu(ourF3BSpeedMenuItems, ourF3BSpeedMenuSize, ourF3BSpeedMenuName);
          break;
        case TC_F3FTaskMenu: 
          { 
            String extra = String(F("(")) + String(ourF3XGenericTask->getLegLength() ) + String(F("m)"));
            showOLEDMenu(ourF3FTaskMenuItems, ourF3FTaskMenuSize, ourF3FTaskMenuName, extra.c_str());
          }
          break;
        case TC_F3XSettingsMenu: 
          showOLEDMenu(ourSettingsMenuItems, ourSettingsMenuSize, ourSettingsMenuName);
          break;
        default:
          showError(ourContext.get());
          break;
      }
    }
#ifdef OLED_FULL_BUFFER
  ourOLED.sendBuffer();
#else
  } while ( ourOLED.nextPage() );
#endif
}


void forceOLED(uint8_t aLevel, String aMessage) {
  String head;
  switch (aLevel) {
    case 0:
      forceOLED(String(F("F3X Comp. boot: ")), aMessage);
      break;
    default:
      forceOLED(String(aLevel), aMessage);
      break;
  }
  delay(100);
}

void forceOLED(String aHead, String aMessage) {
  logMsg(LOG_MOD_ALL, INFO, aHead+aMessage);
#ifdef OLED_FULL_BUFFER
  ourOLED.clearBuffer();
#else
  ourOLED.firstPage();
  do {
#endif
    showMessage(aHead, aMessage);
#ifdef OLED_FULL_BUFFER
  ourOLED.sendBuffer();
#else
  } while ( ourOLED.nextPage() );
#endif
}
#endif // OLED

#ifdef USE_BATTERY_IN_VOLTAGE
// primary battery voltage handling (LiIon Accu of battery shield)
void  setupBatteryIn() {
  logMsg(INFO, String(F("setup pin ")) + String(PIN_BATTERY_IN) + F(" for battery voltage input"));
  pinMode(PIN_BATTERY_IN, INPUT);
}     

void updateBatterySupervision(unsigned long aNow) {
  static unsigned long next = 0;
  #define BAT_IN_CYCLE 10000
  #
  // 1024÷1024×3290×47÷(100+47) = 10519
  // 3.9 gemessen / 3.8 angezeigt = 1.026 
  // 10519 * 1.026 = 10796
  // 10796
  #define V_REF 10796
  
  // 3920mV 
  if (aNow > next) {
    next = aNow + BAT_IN_CYCLE;
    uint16_t raw = analogRead(PIN_BATTERY_IN);
    // Wemos D1  can read 3.2V on analog in
    // 3V3 = 3290mV
    float volt=(((float) raw)/1023.0)*4.12f;
    ourBatteryAVoltage = volt*1000;  // convert to milli volt
    logMsg(LOG_MOD_BAT, INFO, String(F("battery A voltage: ")) + String(volt, 2) + F("V | sensor-value: ") + String(raw));


    bool battWarn = false;
    // check battery level and warn if low
    #define BAT_WARN_LEVEL 3200 // mV
    if(ourBatteryBVoltage > 500 && ourBatteryBVoltage < BAT_WARN_LEVEL) {
      logMsg(LOG_MOD_BAT, WARNING, F("Battery B: ") + String(ourBatteryBVoltage) + String(F("mV")));
      battWarn = true;
    }
    if(ourBatteryAVoltage > 500 && ourBatteryAVoltage < BAT_WARN_LEVEL) {
      logMsg(LOG_MOD_BAT, WARNING, F("Battery A: ") + String(ourBatteryAVoltage) + String(F("mV")));
      battWarn = true;
    }
    if (battWarn && ourF3XGenericTask->getTaskState() == F3XFixedDistanceTask::TaskWaiting) {
      ourBuzzer.pattern(5,50,100,50,100,50);
    }
  }
}
#endif

void perfCheck(void (*aExecute)(unsigned long), const char* aDescription, unsigned long aNow) {
  #ifdef PERF_DEBUG
  unsigned long start = millis();
  #endif
  // logMsg(LOG_MOD_PERF, DEBUG, String(F("execute ")) + aDescription);
  aExecute(aNow);
  #ifdef PERF_DEBUG
  if ((millis() - start) > 100) {
    logMsg(LOG_MOD_PERF, DEBUG, String(F("performance info for ")) + aDescription + F(":") + String(millis()-start));
  }
  #endif
}

void updateF3XTask(unsigned long aNow) {
  if (ourF3XGenericTask != nullptr) {
    ourF3XGenericTask->update();
  }
}

void updateWebServer(unsigned long aNow) {
  ourWebServer.handleClient();
}

void updatePushButton(unsigned long aNow) {
  ourPushButton.update();

  static unsigned long history[5] = {0UL};
  static unsigned long reactOnMultiplePressed = 0;
  static uint8_t buttonPressedCnt=0;
  
  #define CLEAR_HISTORY history[0] = 0L
  #define MULTI_PRESSED_FINISHED CLEAR_HISTORY; reactOnMultiplePressed=0;buttonPressedCnt=0
  #define MULTI_PRESS_TIMERANGE 1500
  #define MULTI_PRESS_REACTION_TIME 700


  boolean wasPressed=ourPushButton.pressed();
  // save the push button history
  if (wasPressed) {
    for (int i=4; i>0; i--) {
      history[i] = history[i-1];
    }
    history[0] = aNow;
    
    // calculate the number of press while MULTI_PRESS_TIMERANGE
    buttonPressedCnt=0;
    for (int i=0; i<5; i++) {
      if ((aNow - history[i]) < MULTI_PRESS_TIMERANGE) {
         buttonPressedCnt++;
      } else {
        break;
      }
    }
    logMsg(INFO, F("Button pressed: ") + String(buttonPressedCnt));
  } else if (ourPushButton.isPressed()) {
    // handling for long time pressed button and not released 
  }

  if (wasPressed) {
    // HIGH Prio Button Events:
    //  here the button press is handled for all cases NOT multi press state is needed
    // ONE_CLICK_EVENTS
    reactOnMultiplePressed = 0;
    switch(ourContext.get()) {
      case TC_F3BSpeedTask: // button press
      case TC_F3FTask: // button press
        logMsg(INFO, F("button press in F3F/F3BSpeeed task context"));
        reactOnMultiplePressed = history[buttonPressedCnt-1] + MULTI_PRESS_REACTION_TIME;
        switch(ourF3XGenericTask->getTaskState()) {
          case F3XFixedDistanceTask::TaskFinished:
          case F3XFixedDistanceTask::TaskTimeOverflow:
            ourF3XGenericTask->stop();
            logMsg(INFO, F("resetting task:"));
            
            switch(ourContext.get()) {
              case TC_F3BSpeedTask: // button press
                ourContext.set(TC_F3BSpeedMenu);
                break;
              case TC_F3FTask: // button press
                ourContext.set(TC_F3FTaskMenu);
                break;
            }
            CLEAR_HISTORY;
            MULTI_PRESSED_FINISHED;
            break;
          case F3XFixedDistanceTask::TaskRunning:
            ourF3XGenericTask->signal(F3XFixedDistanceTask::SignalA);
            break;
          default:
            break;
        }
        break;
      case TC_F3XMessage: // button press
      case TC_F3XInfo: // button press
      case TC_F3XRadioInfo: // button press
        logMsg(DEBUG, F("HW button in: ") + String(ourContext.get()));
        ourBuzzer.on(PinManager::SHORT); 
        ourContext.back();
        CLEAR_HISTORY;
        break;
      case TC_F3XRadioPowerCfg: // button press in radio power context
        ourRadioSendSettings=true;
        logMsg(LOG_MOD_RADIO, INFO, F("set RF24 Power:") + String(ourRadioPower));
        ourContext.set(TC_F3XSettingsMenu);
        #ifdef USE_RXTX_AS_GPIO
        resetRotaryEncoder((long) TC_F3XRadioPowerCfg);
        #endif
        CLEAR_HISTORY;
      case TC_F3BSpeedTasktimeCfg: // button press in F3B speed task time context
        logMsg(LOG_MOD_RADIO, INFO, F("set F3B speed tasktime :") + String(ourConfig.f3bSpeedTasktime));
        ourF3BSpeedTask.setTasktime(ourConfig.f3bSpeedTasktime);
        ourContext.set(TC_F3XSettingsMenu);
        #ifdef USE_RXTX_AS_GPIO
        resetRotaryEncoder(0);
        #endif
        CLEAR_HISTORY;
        break;
      case TC_F3FLegLengthCfg: // button press in F3B speed task time context
        logMsg(LOG_MOD_TASK, INFO, F("set F3F leg length :") + String(ourConfig.f3fLegLength));
        ourF3FTask.setLegLength(ourConfig.f3fLegLength);
        ourContext.set(TC_F3XSettingsMenu);
        #ifdef USE_RXTX_AS_GPIO
        resetRotaryEncoder(0);
        #endif
        CLEAR_HISTORY;
        break;
      case TC_F3FTasktimeCfg: // button press in F3B speed task time context
        logMsg(LOG_MOD_TASK, INFO, F("set F3F tasktime :") + String(ourConfig.f3fTasktime));
        ourF3FTask.setTasktime(ourConfig.f3fTasktime);
        ourContext.set(TC_F3XSettingsMenu);
        #ifdef USE_RXTX_AS_GPIO
        resetRotaryEncoder(0);
        #endif
        CLEAR_HISTORY;
        break;
      case TC_F3XRadioChannelCfg: // button press in radio channel context
        ourRadioSendSettings=true;
        logMsg(LOG_MOD_RADIO, INFO, F("set RF24 Channel:") + String(ourRadioChannel));
        ourContext.set(TC_F3XSettingsMenu);
        #ifdef USE_RXTX_AS_GPIO
        resetRotaryEncoder(2);
        #endif
        CLEAR_HISTORY;
        break;
      case TC_F3XBaseMenu: {
        uint8_t menuPos =  getModulo(ourRotaryMenuPosition, ourF3XBaseMenuSize); // !! use the right size here
        logMsg(DEBUG, String(F("HW button pressed in F3XBaseMenu -> setting menu context: ")) + String(menuPos));
        switch (menuPos) {  // !! use the right size here !!
            case 0: // "0:F3B-Speedtask";
              ourBuzzer.on(PinManager::SHORT); 
              logMsg(INFO, F("setting task: F3BSpeedMenu"));
              #ifdef USE_RXTX_AS_GPIO
              resetRotaryEncoder(0);
              #endif
              setActiveTask(F3XFixedDistanceTask::F3BSpeedType);
              ourContext.set(TC_F3BSpeedMenu);
              CLEAR_HISTORY;
              break;
            case 1: // "1:F3F-Task";
              ourBuzzer.on(PinManager::SHORT); 
              logMsg(INFO, F("setting task: F3FDistanceTask"));
              #ifdef USE_RXTX_AS_GPIO
              resetRotaryEncoder(0);
              #endif
              setActiveTask(F3XFixedDistanceTask::F3FType);
              ourContext.set(TC_F3FTaskMenu);
              CLEAR_HISTORY;
              break;
            case 2: // "2:Info";
              ourBuzzer.on(PinManager::SHORT); 
              logMsg(INFO, F("setting task: F3XInfo"));
              ourContext.set(TC_F3XInfo);
              CLEAR_HISTORY;
              break;
            case 3: // "3:Radio-Info";
              ourBuzzer.on(PinManager::SHORT); 
              logMsg(INFO, F("setting task: F3XRadioInfo"));
              ourContext.set(TC_F3XRadioInfo);
              CLEAR_HISTORY;
              break;
            case 4: // "4:Settings";
              ourBuzzer.on(PinManager::SHORT); 
              logMsg(INFO, F("setting task: F3XSettingsMenu"));
              ourContext.set(TC_F3XSettingsMenu);
              #ifdef USE_RXTX_AS_GPIO
              resetRotaryEncoder();
              #endif
              CLEAR_HISTORY;
              break;
          }
        }
        break;
      case TC_F3BSpeedMenu: { // -> ourF3BSpeedMenuItems
        uint8_t menuPos =  getModulo(ourRotaryMenuPosition, ourF3BSpeedMenuSize); // !! use the right size here
        logMsg(DEBUG, String(F("HW button pressed in TC_F3BSpeedMenu -> F3B speed menu context: ")) + String(menuPos));
        switch (menuPos) {  // !! use the right size here !!
          case 0: // "0:Start Task";
            logMsg(LOG_MOD_TASK, INFO, F("starting task: F3BSpeedTask"));
            ourLoopF3XTask = false;
            ourF3XGenericTask->setLoopTasksEnabled(ourLoopF3XTask);
            ourContext.set(TC_F3BSpeedTask);
            ourF3XGenericTask->start();
            CLEAR_HISTORY;
            break;
          case 1: // "1:Loop Task";
            logMsg(INFO, F("looping task: F3BSpeedTask"));
            // ourContext.set(TC_F3XMessage);
            // ourContext.setInfo(String(F("not yet implemented")));
            ourContext.set(TC_F3BSpeedTask);
            ourLoopF3XTask = true;
            ourF3XGenericTask->setLoopTasksEnabled(ourLoopF3XTask);
            ourF3XGenericTask->start();
            CLEAR_HISTORY;
            break;
          case 2: // "2:Back"
            ourF3XGenericTask->stop();
            #ifdef USE_RXTX_AS_GPIO
            resetRotaryEncoder(0);
            #endif
            ourContext.set(TC_F3XBaseMenu);
            break;
          }
        }
        break;
      case TC_F3FTaskMenu: { // -> ourF3FTaskMenuItems
        uint8_t menuPos =  getModulo(ourRotaryMenuPosition, ourF3FTaskMenuSize); // !! use the right size here
        logMsg(LOG_MOD_TASK, INFO, String(F("HW button pressed in TC_F3TaskMenu -> F3F task menu context: ")) + String(menuPos));
        
        switch (menuPos) {  // !! use the right size here !!
          case 0: // "0:Start Task";
            logMsg(INFO, F("starting task: F3FTask"));
            ourLoopF3XTask = false;
            ourF3XGenericTask->setLoopTasksEnabled(ourLoopF3XTask);
            ourContext.set(TC_F3FTask);
            ourF3XGenericTask->start();
            CLEAR_HISTORY;
            break;
          case 1: // "1:Loop Task";
            logMsg(INFO, F("looping task: F3FTask"));
            ourLoopF3XTask = true;
            ourF3XGenericTask->setLoopTasksEnabled(ourLoopF3XTask);
            ourContext.set(TC_F3FTask);
            ourF3XGenericTask->start();
            CLEAR_HISTORY;
            break;
          case 2: // "2:Back"
            ourF3FTask.stop();
            #ifdef USE_RXTX_AS_GPIO
            resetRotaryEncoder(0);
            #endif
            ourContext.set(TC_F3XBaseMenu);
            break;
          }
        }
        break;
      case TC_F3XSettingsMenu: { // -> ourSettingsMenuItems
        uint8_t menuPos =  getModulo(ourRotaryMenuPosition, ourSettingsMenuSize); // !! use the right size here
        logMsg(DEBUG, String(F("HW button pressed in F3XSettingsMenu -> setting menu context: ")) + String(menuPos));
        switch (menuPos){  // !! use the right size here !!
          case 0: // "0:F3B Speed Ttime";
            ourBuzzer.on(PinManager::SHORT);
            ourContext.set(TC_F3BSpeedTasktimeCfg);
            #ifdef USE_RXTX_AS_GPIO
            resetRotaryEncoder();
            #endif
            break;
          case 1: // "1:F3F Ttime";
            ourBuzzer.on(PinManager::SHORT);
            ourContext.set(TC_F3FTasktimeCfg);
            #ifdef USE_RXTX_AS_GPIO
            resetRotaryEncoder();
            #endif
            break;
          case 2: // "2:F3F LegLength";
            ourBuzzer.on(PinManager::SHORT);
            ourContext.set(TC_F3FLegLengthCfg);
            #ifdef USE_RXTX_AS_GPIO
            resetRotaryEncoder();
            #endif
            break;
          case 3: // "3:Buzzers setup";
            {
            uint8_t t = (uint8_t) ourConfig.buzzerSetting;
            if (ourDialogTimer > aNow) {
              // switch to the next setting value only if the button
              // is pressend more than once within the dialog time range
              t++;
            }
            ourConfig.buzzerSetting = (BuzzerSetting) (t % (uint8_t) BS_LAST);
            switch (ourConfig.buzzerSetting) {
              case BS_ALL: // both buzzers are active 
                ourBuzzer.enable();
                showDialog(2000, String(F("all buzzers")));
                break;
              case BS_BASEMANAGER: // only direct connected BaseManager Buzzer 
                ourBuzzer.enable();
                showDialog(2000, String(F("only A-Line")));
                break;
              case BS_REMOTE_BUZZER: // only remote radio buzzer
                ourBuzzer.disable();
                showDialog(2000, String(F("only radio")));
                break;
              case BS_NONE: // no buzzers are active 
                ourBuzzer.disable();
                showDialog(2000, String(F("no buzzers")));
                break;
            }
            logMsg(LOG_MOD_SIG, INFO, F("set buzzerSetting:") + String(ourConfig.buzzerSetting));
            ourBuzzer.on(PinManager::SHORT);
            }
            break;
          case 4: // "4:CompetitionSetup";
            ourConfig.competitionSetting = !ourConfig.competitionSetting;
            showDialog(2000, ourConfig.competitionSetting);
            break;
          case 5: // "5:Radio channel";
            ourBuzzer.on(PinManager::SHORT);
            if (!ourRadioSendSettings || ourRadioQuality > 99.0f) {
              ourContext.set(TC_F3XRadioChannelCfg);
              ourRadioChannel = ourRadio.getChannel();
              #ifdef USE_RXTX_AS_GPIO
              resetRotaryEncoder();
              #endif
            } else {
              ourContext.set(TC_F3XMessage);
              ourContext.setInfo(String(F("no B-Line connected")));
            }
            break;
          case 6: // "6:Radio power";
            ourBuzzer.on(PinManager::SHORT);
            if (!ourRadioSendSettings || ourRadioQuality > 99.0f) {
              ourContext.set(TC_F3XRadioPowerCfg);
              ourRadioPower=ourRadio.getPower();
              #ifdef USE_RXTX_AS_GPIO
              resetRotaryEncoder();
              #endif
            } else {
              ourContext.set(TC_F3XMessage);
              ourContext.setInfo(String(F("no B-Line connected")));
            }
            break;
          case 7: // "7:Display invert";
            ourBuzzer.on(PinManager::SHORT);
            ourConfig.oledFlipped = ourConfig.oledFlipped == true? false: true;
            ourOLED.setFlipMode(ourConfig.oledFlipped);
            break;
          case 8: // "8:Rotary button inv.";
            ourBuzzer.on(PinManager::SHORT);
            ourConfig.rotaryEncoderFlipped = ourConfig.rotaryEncoderFlipped ? false: true;
            ourREInversion = ourConfig.rotaryEncoderFlipped ? -1 : 1;
            #ifdef USE_RXTX_AS_GPIO
            {
              for (uint8_t i = 0; i < 254; i++) {
                ourRotaryEncoder.write(i);
                if ( 2 == getModulo(getRotaryEncoderPosition(), ourSettingsMenuSize)) {
                  ourRotaryEncoder.write(i+2);
                  break; // for loop}
                }
              }
            }
            #endif
            break;
          case 9: //  "9:Update firmware";
            otaUpdate(false); // firmware
            break;
          case 10: //  "10:Update filesystem";
            otaUpdate(true); // filesystem
            break;
          case 11: //  "11:WiFi on/off";
            WiFi.mode(WIFI_OFF) ; // client mode only
            ourConfig.wifiIsActive = !ourConfig.wifiIsActive;
            showDialog(2000, String(F("WiFi is ")) + (ourConfig.wifiIsActive ? String(F("enabled")) : String(F("disabled"))));
            break;
          case 12: //  "12:Save settings";
            ourBuzzer.on(PinManager::SHORT);
            saveConfig();
            showDialog(2000, String(F("Config saved ")));
            break;
          case 13: // "13:Main menu";
            ourBuzzer.on(PinManager::SHORT);
            #ifdef USE_RXTX_AS_GPIO
            resetRotaryEncoder();
            #endif
            ourContext.set(TC_F3XBaseMenu);
            break;
          }
        }
        break;
      default:
        break;
    }
    
    if (reactOnMultiplePressed) {
      // while multi button handling is in progress do not react on rotary changes
      #ifdef USE_RXTX_AS_GPIO
      controlRotaryEncoder(false);
      #endif
    } else {
      #ifdef USE_RXTX_AS_GPIO
      controlRotaryEncoder(true);
      #endif
      CLEAR_HISTORY;
    }
  
    logMsg(INFO, F("before multi: ") + String(aNow) + "/" + String(reactOnMultiplePressed));
  

    if (reactOnMultiplePressed && aNow < reactOnMultiplePressed) {
      logMsg(INFO, F("react multi:") + String(buttonPressedCnt));
     
      switch (buttonPressedCnt) {
        case 3:
          switch (ourContext.get()) {
            case TC_F3BSpeedTask:
            case TC_F3FTask:
              ourF3XGenericTask->stop();
              signalBuzzing(BUZZ_TIME_LONG);
              #ifdef USE_RXTX_AS_GPIO
              resetRotaryEncoder(0);
              controlRotaryEncoder(true);
              #endif
              ourContext.back();
              MULTI_PRESSED_FINISHED;
            break;
          }
        break;
      }
    }
  }
}
  
void updateTimedEvents(unsigned long aNow) {
  if (ourTimedReset != 0 && aNow > ourTimedReset) {
    ourTimedReset = 0;
    ESP.restart();
  } 
}


/**
  return the math result of a modulo operation instead of the symmetric (as implemented in the %-operator
*/
uint8_t getModulo(long aDivident, uint8_t aDivisor) {
  // Umrechnung der Modulo Resultate von der in C++ implementierten symmetrischen Modulo-Variante in die mathematische Variante
  return ((aDivident % aDivisor) + aDivisor) % aDivisor;
}

double irr_low_pass_filter(double aSmoothedValue, double aCurrentValue, double aSmoothingFactor) {
  // IIR Low Pass Filter
  // y[i] := α * x[i] + (1-α) * y[i-1]
  //      := α * x[i] + (1 * y[i-1]) - (α * y[i-1]) 
  //      := α * x[i] +  y[i-1] - α * y[i-1]
  //      := α * ( x[i] - y[i-1]) + y[i-1]
  //      := y[i-1] + α * (x[i] - y[i-1])
  // mit α = 1- β
  //      := y[i-1] + (1-ß) * (x[i] - y[i-1])
  //      := y[i-1] + 1 * (x[i] - y[i-1]) - ß * (x[i] - y[i-1])
  //      := y[i-1] + x[i] - y[i-1] - ß * x[i] + ß * y[i-1]
  //      := x[i] - ß * x[i] + ß * y[i-1]
  //      := x[i] + ß * y[i-1] - ß * x[i]
  //      := x[i] + ß * (y[i-1] - x[i])
  // see: https://en.wikipedia.org/wiki/Low-pass_filter#Simple_infinite_impulse_response_filter
  return aCurrentValue + aSmoothingFactor * (aSmoothedValue - aCurrentValue);
} 


#ifdef USE_RXTX_AS_GPIO
void resetRotaryEncoder(long aPos) {
  ourRotaryEncoder.write(aPos);
  ourREOldPos = LONG_MIN;
}

// with this function the rotary encoder can be enabled/disabled, to avoid unwanted rotation events
void controlRotaryEncoder(boolean aEnable) {
  static long storedPos = 0;
  if (aEnable && !ourREState) {
    ourRotaryEncoder.write(storedPos);
    ourREState = true;
  }
  if (!aEnable && ourREState) {
    storedPos = ourRotaryEncoder.read();
    ourREState = false;
  }
}


long getRotaryEncoderPosition() {
  long raw = ourRotaryEncoder.read();
  long pos;
  // suppress the four micro steps of the encoder 
  if (raw < -2) {
    pos = (raw-1)/4 * ourREInversion;
  } else {
    pos = (raw+2)/4 * ourREInversion; 
  }
  return pos;
}

void updateRotaryEncoder(unsigned long aNow) {
  if (!ourREState) {
    // rotary encoder is disabled (e.g. while button pressed handling)
    return;
  }
  
  long position = getRotaryEncoderPosition();

  if (position != ourREOldPos) {
    int8_t delta = 0;
    if (ourREOldPos != LONG_MIN) {
      delta = position - ourREOldPos;
    }
    ourRotaryMenuPosition = position;

    // ROTARY_EVENTS
    switch (ourContext.get()) {
      case TC_F3XBaseMenu:
      case TC_F3XSettingsMenu:
        ourBuzzer.on(PinManager::SHORT);
        break;
      case TC_F3BSpeedTask:
      case TC_F3FTask:
        switch(ourF3XGenericTask->getTaskState()) {
          case F3XFixedDistanceTask::TaskTimeOverflow:
          case F3XFixedDistanceTask::TaskWaiting:
          case F3XFixedDistanceTask::TaskFinished:
            ourF3XGenericTask->stop();
            ourContext.set(TC_F3XBaseMenu);
            resetRotaryEncoder(ourContext.get());
            ourBuzzer.on(PinManager::SHORT);
            break;
        }
        break;
      case TC_F3XRadioChannelCfg:
        ourRadioChannel += delta;
        ourRadioChannel = getModulo(ourRadioChannel, RF24_1MHZ_CHANNEL_NUM);
        break;
      case TC_F3BSpeedTasktimeCfg:
        ourConfig.f3bSpeedTasktime += delta*10;
        if (ourConfig.f3bSpeedTasktime < 60) ourConfig.f3bSpeedTasktime = 60;
        if (ourConfig.f3bSpeedTasktime > 300) ourConfig.f3bSpeedTasktime = 300;
        break;
      case TC_F3FTasktimeCfg:
        ourConfig.f3fTasktime += delta*10;
        if (ourConfig.f3fTasktime < 0) ourConfig.f3fTasktime = 0;
        if (ourConfig.f3fTasktime > 300) ourConfig.f3fTasktime = 300;
        break;
      case TC_F3FLegLengthCfg:
        ourConfig.f3fLegLength += delta*1;
        if (ourConfig.f3fLegLength < 50) ourConfig.f3fLegLength = 50;
        if (ourConfig.f3fLegLength > 150) ourConfig.f3fLegLength = 150;
        break;
      case TC_F3XRadioPowerCfg:
        ourRadioPower += delta;
        ourRadioPower = getModulo(ourRadioPower, 4);
        break;
    }
    ourREOldPos = position;
  }
} 
#endif

void setActiveTask(F3XFixedDistanceTask::F3XType aType) {
  if (aType == F3XFixedDistanceTask::F3BSpeedType) {
    if (ourF3XGenericTask == nullptr) {
      ourF3XGenericTask = &ourF3BSpeedTask;
    } else {
      if (ourF3XGenericTask->getType() != F3XFixedDistanceTask::F3BSpeedType) {
        ourF3XGenericTask->stop();
        ourF3XGenericTask = &ourF3BSpeedTask;
      }
    }
  } else 
  if (aType == F3XFixedDistanceTask::F3FType) {
    if (ourF3XGenericTask == nullptr) {
      ourF3XGenericTask = &ourF3FTask;
    } else {
      if (ourF3XGenericTask->getType() != F3XFixedDistanceTask::F3FType) {
        ourF3XGenericTask->stop();
        ourF3XGenericTask = &ourF3FTask;
      }
    }
  } else {
    logMsg(LOG_MOD_TASK, ERROR, String("illegeal F3XType:") + String(aType));
  }
}

void loop()
{
  static unsigned long next = 0;
  static unsigned long lastslow = 0;
  unsigned long now = millis();

  #ifdef PERF_DEBUG
  unsigned long lnow;
  boolean isSlow = false;
  if ((now - lastslow) > 200) {
    logMsg(LOG_MOD_PERF, WARNING, String(F("slow last loop:")) + String(now-lastslow));
    isSlow = true;
  }
  lastslow = now;
  #endif

  perfCheck(&updatePushButton, "time push button", now);

  perfCheck(&updateBuzzer, "time buzzer", now);

  perfCheck(&updateF3XTask, "time speedtask", now);

  perfCheck(&updateRadio, "time radio", now);

  perfCheck(&updateWebServer, "time webserver", now);

  perfCheck(&updateBatterySupervision, "time battery supervision", now);

  perfCheck(&updateTimedEvents, "time timedEvents", now);
  
  #ifdef OLED 
  perfCheck(&updateOLED, "time oled display", now);
  #endif

  #ifdef USE_RXTX_AS_GPIO
  perfCheck(&updateRotaryEncoder, "time rotary encoder", now);
  #endif


  if (WiFi.status() == WL_CONNECTED) {
    #ifdef OTA
    ArduinoOTA.handle();
    #endif
    #ifdef USE_MDNS
    MDNS.update();
    #endif
  }

  if (now >= next) {
    ourSecond++;
    next = now + 1000;
  } else {
    return;
  }

  if (ourSecond > 5) {
    ourStartupPhase = false;
  }
}
