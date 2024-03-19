/**
 * \file ABaseManager.ino
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
#include "F3BSpeedTask.h"
#include "settings.h"

#define APP_VERSION "V028"

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
*/

/**
Feature list:
* Battery warning
* ...
*/

static const char myName[] = "A-Base";
static const char MYSEP_STR[] = "~~~";
static const char CMDSEP_STR[] = ",";

// Used Ports as as summary
/*
D0 : RF24-NRF24L01 CE (brown-white)
D1 : OLED-SSD1306 SCL
D2 : OLED-SSD1306 SDA 
D3 : RF24-NRF24L01 CSN (brown) 
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
#define PIN_RF24_CSN      D3
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

#include <RFTransceiver.h>
RFTransceiver ourRadio(myName, PIN_RF24_CE, PIN_RF24_CSN); // (CE, CSN)

#define USE_RXTX_AS_GPIO


Encoder ourRotaryEncoder(PIN_ENCODER_DT, PIN_ENCODER_CLK);
#define RE_MULITPLIER_SLOW 1
#define RE_MULITPLIER_NORMAL 5
long ourRotaryMenuPosition=0;
uint8_t ourRotaryEncoderMultiplier;
static boolean ourREState = true;
static int8_t ourREInversion = 1;
static long ourREOldPos  = 0;


enum ToolContext {
  TC_F3XBaseMenu,
  TC_F3XSettingsMenu,
  TC_F3XRadioChannel,
  TC_F3XRadioPower,
  TC_F3XInfo,
  TC_F3XRadioInfo,
  TC_F3XOtaUpdate,
  TC_F3XMessage,
  TC_F3BSpeedMenu,
  TC_F3BSpeedTask,
  TC_F3BDistanceTask,
  TC_F3BDurationTask,
  TC_F3FTask,
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
      logMsg(LS_INTERNAL, String("set context:") + String(aContext));
      
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

      logMsg(LS_INTERNAL, String("back context:") + String(get()));
    }
    void setTaskRunning(bool aArg) {
      myIsTaskRunning = aArg;
    }
    bool getTaskRunning() {
      return myIsTaskRunning;
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
const char* ourSettingsMenu0 = "0:Buzzer on/off";
const char* ourSettingsMenu1 = "1:Radio channel";
const char* ourSettingsMenu2 = "2:Radio power";
const char* ourSettingsMenu3 = "3:Display invert";
const char* ourSettingsMenu4 = "4:Rotary button inv.";
const char* ourSettingsMenu5 = "5:Update firmware";
const char* ourSettingsMenu6 = "6:Update filesystem";
const char* ourSettingsMenu7 = "7:WiFi on/off";
const char* ourSettingsMenu8 = "8:Save settings";
const char* ourSettingsMenu9 = "9:Main menu";
const char* ourSettingsMenuItems[] = {ourSettingsMenu0, ourSettingsMenu1, ourSettingsMenu2, ourSettingsMenu3, ourSettingsMenu4, ourSettingsMenu5, ourSettingsMenu6, ourSettingsMenu7, ourSettingsMenu8, ourSettingsMenu9};
const uint8_t ourSettingsMenuSize = sizeof(ourSettingsMenuItems) / sizeof(char*);

// TC_F3BSpeedMenu
const char* ourF3BSpeedMenuName = "F3B Speed";
const char* ourF3BSpeedMenu0 = "0:Start Tasktime";
const char* ourF3BSpeedMenu1 = "1:Back";
const char* ourF3BSpeedMenuItems[] = {ourF3BSpeedMenu0, ourF3BSpeedMenu1};
const uint8_t ourF3BSpeedMenuSize = sizeof(ourF3BSpeedMenuItems) / sizeof(char*);


#include "F3XRemoteCommand.h"

#define OLED
#ifdef OLED
#include <U8g2lib.h>
/*
// TFT Display  ST7735S
//
// OLED 0.96" SSD1306   0,96 Zoll OLED SSD1306 Display I2C 128 x 64 Pixel / AZ-Delivery
// CGscale U8G2_SH1106_128X64_NONAME_1_HW_I2C oledDisplay(U8G2_R0, U8X8_PIN_NONE, D3, D4);
https://github.com/olikraus/u8g2/wiki/u8g2setupcpp#introduction
SCL D1
SDA D2
U8G2_SSD1306_128X64_NONAME_1_HW_I2C(rotation, [reset [, clock, data]]) [page buffer, size = 128 bytes]
U8G2_SSD1306_128X64_NONAME_2_HW_I2C(rotation, [reset [, clock, data]]) [page buffer, size = 256 bytes]
U8G2_SSD1306_128X64_NONAME_F_HW_I2C(rotation, [reset [, clock, data]]) [full framebuffer, size = 1024 bytes]
*/

// U8G2_SSD1306_128X64_NONAME_1_HW_I2C ourOLED(U8G2_R0, U8X8_PIN_NONE, D1, D2);
U8G2_SSD1306_128X64_NONAME_1_HW_I2C ourOLED(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL /*SCL*/, PIN_OLED_SDA /*SDA*/);  

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
F3BSpeedTask ourSpeedTask;
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
unsigned long ourTimedReset = 0;
unsigned long ourDialogTimer=0;
String ourDialogString="";

Bounce2::Button ourPushButton = Bounce2::Button();
PinManager ourBuzzer(PIN_BUZZER_OUT);

// =========== some function forward declarations ================

char* getSpeedTimeString(unsigned long aTime, unsigned long aLegTime, uint16_t aLegSpeed,  unsigned long aDeadDelay, uint8_t aDeadDistance, char aSeparator='/', bool aForceDeadData=false);
void updateOLED(unsigned long aNow, bool aForce);
void updateOLED(unsigned long aNow);
void resetRotaryEncoder(long aPos=0);
uint8_t getModulo(long aDivident, uint8_t aDivisor);
void forceOLED(uint8_t aLevel, String aMessage);
void forceOLED(String aHead, String aMessage);
void setDefaultConfig();
void saveConfig();

class F3BTaskData {
  private:
    String ourProtocolFilePath;
  public:
    F3BTaskData() {
      ourProtocolFilePath = F("/F3BSpeedData.csv");
    }
    void init() {
    }
    void remove() {
      LittleFS.remove(ourProtocolFilePath.c_str());
    }
    void writeHeader() {
      // check existing file
      logMsg(LS_INTERNAL, String(F("write header: ")) + String(ourProtocolFilePath.c_str()));
      File file = LittleFS.open(ourProtocolFilePath.c_str(), "r");
      if(!file) {
        // protocol file not existing, so create a empty one with header
        file = LittleFS.open(ourProtocolFilePath.c_str(), "w");
        if(!file){
          logMsg(LS_INTERNAL, String(F("cannot create protocol file: ")) + String(ourProtocolFilePath.c_str()));
        } else {
          String line;
          line += "Task;";
          line += "Time all;";
          line += "Speed all;";
          line += "Time 000m (A);";
          line += "Time 150m (B);";
          line += "Time 1.leg;";
          line += "Speed 1.leg;";
          line += "deadtime;";
          line += "dead distance;";
          line += "Time 300m (A);";
          line += "Time 2.leg;";
          line += "Speed 2.leg;";
          line += "deadtime;";
          line += "dead distance;";
          line += "Time 450m (B);";
          line += "Time 3.leg;";
          line += "Speed 3.leg;";
          line += "deadtime;";
          line += "dead distance;";
          line += "Time 600m (A);";
          line += "Time 4.leg;";
          line += "Speed 4.leg";
          if(!file.print(line)){
            logMsg(LS_INTERNAL, String(F("cannot write protocol file: ")) + String(ourProtocolFilePath.c_str()));
          }
        }
        file.close();
      } else {
        file.close();
      } 
    }

    void writeData() {
      writeHeader();
      File file;
      file = LittleFS.open(ourProtocolFilePath.c_str(), "a");
      if(!file){
        logMsg(LS_INTERNAL, String(F("cannot open protocol file for append: ")) + String(ourProtocolFilePath.c_str()));
      } else {
        String line;
        line += "\n";
        line += "F3BSpeed;";
        line += getSpeedTimeString(ourSpeedTask.getFlightTime(A_LINE_CROSSED_FINAL), F3B_TIME_NOT_SET, 0, 0, 0);
        line += ";";
        line += ourSpeedTask.getFinalSpeed()*3.6f;
        line += "km/h";
        line += ";";
        
        for (uint8_t i=0; i<5; i++) {
        logMsg(LS_INTERNAL, String(F("leg data: ")) + String(i) + "/" + String(ourSpeedTask.getLegSpeed(i)*3.6f));
          line += getSpeedTimeString(
                    ourSpeedTask.getFlightTime(i+1), 
                    ourSpeedTask.getLegTime(i), 
                    ourSpeedTask.getLegSpeed(i)*3.6f, 
                    ourSpeedTask.getDeadDelay(i+1),
                    ourSpeedTask.getDeadDistance(i+1), ';', (i==0||i==4)?false:true);
          if (i != 4) {
            line += ";";
          }
        }
        logMsg(LS_INTERNAL, String(F("write data: ")) + String(ourProtocolFilePath.c_str()));
        if(!file.print(line)){
          logMsg(LS_INTERNAL, String(F("cannot write protocol file: ")) + String(ourProtocolFilePath.c_str()));
        }
        file.close();
      }
    }
};

F3BTaskData ourF3BTaskData;

void restartMCs(uint16_t aDelay, bool aRestartOnlyBLine=false) {
  ourRadio.transmit(ourRemoteCmd.createCommand(F3XRemoteCommandType::CmdRestartMC)->c_str(), 20);
  if (!aRestartOnlyBLine) {
    // force a restart of ourself
    ourTimedReset = millis() + aDelay;
  }
}

void reactonSignalA() {
  ourBuzzer.on(500);
  logMsg(DEBUG, "reactonSignalA");
  if (ourSpeedTask.getTaskState() == TaskFinished) {
    ourF3BTaskData.writeData();
  }
}
void reactonSignalB() {
  ourBuzzer.on(500);
  logMsg(DEBUG, "reactonSignalB");
}

/**
  return a time literal with the format 12.23s/43m  
*/
char* getLegTimeStr(unsigned long aLegTime,  unsigned long aDelay, uint8_t aDistance, char aSeparator='/') {
  static char buffer[25];
  if (aLegTime != F3B_TIME_NOT_SET) {
    sprintf(&buffer[0],"%02d.%02ds", aLegTime / 1000, aLegTime/10%100); // len=8+7=15
  }
  if (aDelay != 0) {
    sprintf(&buffer[strlen(buffer)],"%c%dm", aSeparator, aDistance); // len=15+8=23+1
  }
  return buffer;
}

/**
  return a time literal in format 
    00:09.41;05.39s;100km/h;00.76s;21m;
    representing turn-time/leg-time/leg-speed/dead-time/dead-distance 
*/
char* getSpeedTimeString(unsigned long aTime, unsigned long aLegTime, uint16_t aLegSpeed,  unsigned long aDeadDelay, uint8_t aDeadDistance, char aSeparator, bool aForceDeadData) {
  int tseconds = aTime / 1000;
  int tminutes = tseconds / 60;
  int thours = tminutes / 60;
  int millisec  = aTime % 1000;
  int centies  = millisec/10;
  int seconds = tseconds % 60;
  int minutes = tminutes % 60;
  static char buffer[35];
  if (aTime == F3B_TIME_NOT_SET ) {
    if (ourSpeedTask.getTaskState() == TaskTimeOverflow) {
      sprintf(&buffer[0],"XX:XX.XX : task time overflow");  // len=29+1
    } else {
      sprintf(&buffer[0],"__:__.__");
    }
  } else {
    sprintf(&buffer[0],"%02d:%02d.%02d", minutes, seconds, centies);  // len=8
    if (aLegTime != F3B_TIME_NOT_SET) {
      if (aLegSpeed > 0) {
        sprintf(&buffer[strlen(buffer)],"%c%02d.%02ds%c%dkm/h", aSeparator, aLegTime / 1000, aLegTime/10%100, aSeparator, aLegSpeed ); // len=8+15=23
      } else {
        sprintf(&buffer[strlen(buffer)],"%c%02d.%02ds", aSeparator, aLegTime / 1000, aLegTime/10%100); // len=8+7=15
      }
    }
    if (aDeadDelay != 0 || aForceDeadData) {
      sprintf(&buffer[strlen(buffer)],"%c%02d.%02ds%c%dm", aSeparator, aDeadDelay / 1000, aDeadDelay/10%100, aSeparator, aDeadDistance); // len=23+8=31+1
    }
  }
  return buffer;
}

char* getTimeStr(unsigned long aTime, boolean aShort=false) {
  int tseconds = aTime / 1000;
  int tminutes = tseconds / 60;
  int thours = tminutes / 60;
  int millisec  = aTime % 1000;
  int seconds = tseconds % 60;
  int minutes = tminutes % 60;
  int hours = thours % 60;
  static char buffer[12];
  if (aShort) {
    if (aTime == F3B_TIME_NOT_SET ) {
      sprintf(buffer,"__:__");
    } else {
      sprintf(buffer,"%02d:%02d", minutes, seconds); 
    }
  } else {
    sprintf(buffer,"%02d:%02d:%02d",hours, minutes, seconds); // len=8+1
  }
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
  ourRadio.begin(0);  // set 0 for A-Line-Manager
  logMsg(INFO, F("setup for RCTTransceiver/nRF24L01 successful ")); 

  // in the RFTransceiver implementation the default values for the radio settings are defined 
  // for A- and B-Line Manager (Channel:110/Power:HIGH/Datarate:RF24_250KBPS/Ack:true)
  // if the user is changing radio settings, they are save in the EEPROM config ourConfig/loadConfig/saveConfig
  // at startup, these config value are NOT used, to provide in any case a commont radio setting for both sides.
  // Only the A-Line-Manager, stores changed radio settings, boots with the default settings starts a communication with the 
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
  logMsg(LS_INTERNAL, F("RF24-cfg: (c/p)") + String(ourConfig.radioChannel) + String(F("/")) + String(ourConfig.radioPower)); 

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
    logMsg(LS_INTERNAL, F("IP Address is: ") + WiFi.localIP().toString());
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
      logMsg(INFO, String(F("Please connect to SSID: ")) + ourConfig.apSsid + String(F(", PW: ")) + ourConfig.apPasswd);
      forceOLED(0, String(("WiFi IP: ") + myIP.toString()));
      delay(1000);
    } else {
      forceOLED(0, String("WiFi: not started"));
      delay(1000);
    }
  }
  #ifdef USE_MDNS
  if (!MDNS.begin("f3x", WiFi.localIP())) {             
    logMsg(LS_INTERNAL, "Error starting mDNS");
  } else {
    logMsg(LS_INTERNAL, "mDNS started");
  }
  #endif
}

String getWiFiIp(String* ret) {
  if (WiFi.status() == WL_CONNECTED) {
    ret->concat(WiFi.localIP().toString());
  } else {
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
    ourSpeedTask.signal(SignalA);
  } else
  if (name == F("signal_b")) {
    logMsg(INFO, F("signal B event from web client"));
    ourSpeedTask.signal(SignalB);
  } else 
  if (name == F("stop_task")) {
    logMsg(INFO, F("stop task event from web client"));
    ourSpeedTask.stop();
  } else 
  if (name == F("start_task")) {
    logMsg(INFO, F("start task event from web client"));
    ourSpeedTask.start();
  } else 
  if (name == F("start_rt_measurement")) {
    logMsg(DEBUG, F("start_rt_measurement"));
    logMsg(INFO, F("start range test (nyi)"));
  } else 
  if (name == F("restart_mc")) {
    logMsg(INFO, F("web cmd restart mcs"));
    restartMCs(500);
  } else 
  if (name == F("radio_channel")) {
    ourRadioChannel=value.toInt();
    ourRadioSendSettings=true;
    logMsg(LS_INTERNAL, F("set RF24 Channel:") + String(ourRadioChannel));
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
    logMsg(LS_INTERNAL, F("set RF24 DataRate: ") + String(ourRadioDatarate));
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
  if (name == F("delete_f3b_data")) {
    logMsg(LS_INTERNAL, "remove F3BTaskData"); 
    ourF3BTaskData.remove();
  } else 
  if (name == F("cmd_fwupdate")) {
    logMsg(LS_INTERNAL, "fw update"); 
    otaUpdate(false); // firmware
  } else 
  if (name == F("cmd_fsupdate")) {
    logMsg(LS_INTERNAL, "fs update"); 
    otaUpdate(true); // filesystem
  } else 
  if (name == F("cmd_saveConfig")) {
    logMsg(LS_INTERNAL, "save config"); 
    saveConfig();
  } else 
  if (name == F("cmd_resetConfig")) {
    logMsg(LS_INTERNAL, "reset config"); 
    setDefaultConfig();
  } else 
  if (name == F("id_wifiActive")) {
    if (value == "true") {
      logMsg(LS_INTERNAL, F("setting wifi active")); 
      ourConfig.wifiIsActive = true;
    } else {
      logMsg(LS_INTERNAL, F("setting wifi inactive")); 
      ourConfig.wifiIsActive = false;
    }
  } else 
  if (name == F("id_wlanSsid")) {
    logMsg(LS_INTERNAL, "setting wlan ssid"); 
    strncpy(ourConfig.wlanSsid, value.c_str(), CONFIG_SSID_L);
  } else 
  if (name == F("id_wlanPasswd")) {
    logMsg(LS_INTERNAL, "setting wlan passwd"); 
    strncpy(ourConfig.wlanPasswd, value.c_str(), CONFIG_PASSW_L);
  } else 
  if (name == F("id_apSsid")) {
    logMsg(LS_INTERNAL, "setting ap password"); 
    strncpy(ourConfig.apSsid, value.c_str(), CONFIG_SSID_L);
  } else 
  if (name == F("id_apPasswd")) {
    logMsg(LS_INTERNAL, "setting ap password"); 
    strncpy(ourConfig.apPasswd, value.c_str(), CONFIG_PASSW_L);

  } else 
  if (name == F("cmd_mcrestart")) {
    logMsg(LS_INTERNAL, "restart MC"); 
    restartMCs(1000);
  } else {
    logMsg(LS_INTERNAL, F("ERROR: unknown name : ") + name  + F(" in set request, value ") + value);
  }

  if (sendResponse) {
    #ifdef DO_LOG
    logMsg(DEBUG, String(F("send response to WebServer: ")) + response);
    #endif
    ourWebServer.send(htmlResponseCode, F("text/plane"), response); // send an valid answer
  }
}


void getWebHeaderData(String* aReturnString, boolean aForce=false) {

  *aReturnString += String(F("id_time=")) + getTimeStr(millis()) + MYSEP_STR;

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

void getF3BSpeedWebData(String* aReturnString, boolean aForce=false) {
  String taskstr;

  static F3BSpeedTaskState webTaskState = TaskNotSet;
  if (ourSpeedTask.getTaskState() != webTaskState || aForce) {
    webTaskState = ourSpeedTask.getTaskState();
    switch (ourSpeedTask.getTaskState()) {
      case TaskWaiting:
        *aReturnString += String(F("id_running_speed_time="))
                      + getSpeedTimeString(F3B_TIME_NOT_SET, F3B_TIME_NOT_SET, 0, 0, 0)
                      + MYSEP_STR;
        taskstr = F("Ready, waiting for START speed task");
        break;
      case TaskRunning:
        taskstr = F("task started, signals will be handled");
        break;
      case TaskTimeOverflow:
        taskstr = String(F("task stopped, task time overflow"));
        break;
      case TaskFinished:
        taskstr = F("task finished!");
        break;
      default:
        taskstr = F("ERROR: program problem 003");
        break;
    }
    *aReturnString += String(F("id_speed_task_state=")) + taskstr + MYSEP_STR;
  }
  int fromTimer=0;
  int numTimer=0;
  if (aForce) {
    fromTimer = 0;
    numTimer = 6;
  } else
  if (ourSpeedTask.getTaskState() == TaskWaiting ) {
    numTimer = 6;
  } else
  if (ourSpeedTask.getTaskState() == TaskTimeOverflow ) {
    fromTimer = ourSpeedTask.getCurrentSignal()+1;
    numTimer = 6-fromTimer;
  } else
  if (ourSpeedTask.getTaskState() == TaskRunning || ourSpeedTask.getTaskState() == TaskFinished ) {
    switch(ourSpeedTask.getCurrentSignal()) {
      case A_LINE_CROSSED_1:
        // in case of A_LINE_CROSSED_1, send also the A_LINE_REVERSED signal
        // due to possible reflight 
        fromTimer=A_LINE_REVERSED;
        numTimer = 2;
        break;
      case NOT_STARTED:
        // send all timer
        fromTimer=0;
        numTimer = 6;
        break;
      default:
        // send the current timer
        fromTimer = max(ourSpeedTask.getCurrentSignal()-1, 0);
        numTimer = 2;
        break;
    }
  }
  taskstr="";
  for (int i=fromTimer; i<fromTimer+numTimer; i++) {
    taskstr += String(F("id_speed_time_")) 
              + String(i) + F("=") 
                  + getSpeedTimeString(
                      ourSpeedTask.getFlightTime(i), 
                      ourSpeedTask.getLegTime(i-1), 
                      ourSpeedTask.getLegSpeed(i-1)*3.6f, 
                      ourSpeedTask.getDeadDelay(i),
                      ourSpeedTask.getDeadDistance(i))
              + MYSEP_STR;
  }
  if (taskstr.length() > 0) {
    *aReturnString += taskstr;
  }

  *aReturnString += String(F("id_speed_task_time=")) + getTimeStr(ourSpeedTask.getTaskTime(), true) + MYSEP_STR;
  if (ourSpeedTask.getCurrentSignal() == A_LINE_CROSSED_FINAL) {
    *aReturnString += String(F("id_running_speed_time="))
                  + getSpeedTimeString(ourSpeedTask.getFlightTime(RUNNING_VALUE), F3B_TIME_NOT_SET, 0, 0, 0)
                  + MYSEP_STR;
  }
}

// void getProtocolReq() {
  // ourWebServer.send(200, F("text/html"), response.c_str()); 
// }

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
  String response;
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
        response += argName + "=" + "************" + MYSEP_STR;
    } else
    if (argName.equals(F("id_apSsid"))) {
        response += argName + "=" + ourConfig.apSsid + MYSEP_STR;
    } else
    if (argName.equals(F("id_apPasswd"))) {
      if (String(ourConfig.apPasswd).length() != 0) {
        response += argName + "=" + "************" + MYSEP_STR;
      }
    } else
    if (argName.equals("id_wifiActive")) {
      if (ourConfig.wifiIsActive == true) {
        response += argName + "=" + "checked" + MYSEP_STR;
      }
    } else
    if (argName.equals(F("id_online_status"))) {
      Serial.print("wifi status: ");
      Serial.println(WiFi.status());
      if (WiFi.status() == WL_CONNECTED) {
        response += argName + "=online" + MYSEP_STR;
      } else {
        response += argName + "=offline" + MYSEP_STR;
      }
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
    if (argName.equals(F("initHeaderData"))) {
      response += String(F("id_version=")) + APP_VERSION + MYSEP_STR;
      getWebHeaderData(&response, true);
      response += pushData;
    } else
    if (argName.equals(F("id_running_speed_time"))) {
      if (ourSpeedTask.getTaskState() == TaskRunning ) {
        String resp;
        if (ourSpeedTask.getCurrentSignal() >= A_LINE_CROSSED_1) {
          resp = String(F("id_running_speed_time="))
                    + getSpeedTimeString(ourSpeedTask.getFlightTime(RUNNING_VALUE), F3B_TIME_NOT_SET, 0, 0, 0)
                    + MYSEP_STR;
        } else {
          resp = String(F("id_running_speed_time="))
                    + getSpeedTimeString(F3B_TIME_NOT_SET, F3B_TIME_NOT_SET, 0, 0, 0)
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
  forceOLED(0,  String("started ok"));
}
#endif

void otaUpdate(bool aFileSystemUpdate) {
  String url;
  ourContext.set(TC_F3XOtaUpdate);
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
      logMsg(LS_INTERNAL, String("HTTP_UPDATE_FAILED Error") + ESPhttpUpdate.getLastErrorString().c_str());

      ourContext.setInfo(String("ERROR: Update failed"));
      break;
    case HTTP_UPDATE_NO_UPDATES: 
      logMsg(LS_INTERNAL, "HTTP_UPDATE_NO_UPDATES"); 
      ourContext.setInfo("ERROR: no update found");
      break;
    case HTTP_UPDATE_OK: 
      logMsg(LS_INTERNAL, "HTTP_UPDATE_OK"); 
      ourContext.setInfo("Update done, rebooting");
      restartMCs(1000);
      break;
    default:
      retStr = String("HTTP_UPDATE_ERR:") + String(ret); 
      logMsg(LS_INTERNAL, retStr); 
      ourContext.setInfo(retStr);
      break;
  }
}
// End: OVER THE AIR

void setupRemoteCmd() {
  ourRemoteCmd.begin();
}

void setupSpeedTask() {
  ourSpeedTask.init(&reactonSignalA, &reactonSignalB);
  ourF3BTaskData.init();
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
    logMsg(LS_INTERNAL, String("F3X Training:") + String(APP_VERSION));
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
}

// config in EEPROM
void saveConfig() {
  logMsg(LS_INTERNAL, F("saving config to EEPROM "));
  // Save configuration from RAM into EEPROM
  EEPROM.begin(512);
  EEPROM.put(0, ourConfig );
  delay(10);
  EEPROM.commit();                      // Only needed for ESP8266 to get data written
  EEPROM.end();                         // Free RAM copy of structure
}

void setDefaultConfig() {
  logMsg(INFO, F("setting default config to EEPROM "));
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
    logMsg(LS_INTERNAL, String(F("old but compatible config version found: ")) + String(ourConfig.version));
    forceOLED(0, String("config ok"));
  } else {
    logMsg(LS_INTERNAL, String(F("unexpected config version found: ")) + String(ourConfig.version));
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
  delay(2500);

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
  setupSpeedTask();
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
  ourContext.set(TC_F3XInfo);
  resetRotaryEncoder(TC_F3XBaseMenu);
}

void updateRadio(unsigned long aNow) {
  
  static unsigned long lastCmdCycleTestRequest = 0;
  #define CMD_CYCLE_REQUEST_DELAY 500

  static unsigned long lastBLineRequest = 0;
  #define B_LINE_REQUEST_DELAY 3000
  
  static boolean isCmdCycleAnswerReceived = true;
  static uint8_t LastId = 255;
  uint8_t id=0;

  // first try to read all data comming from radio peer
  while (ourRadio.available()) {          
    ourRemoteCmd.write(ourRadio.read());
  }

  
  // if data from remote side builds a complete command handle it
  if (ourRemoteCmd.available()) {
    String* arg = NULL;

    // here the received F3XRemoteCommand (from B-Line) are dispatched and handled 
    switch (ourRemoteCmd.getType()) {
      case F3XRemoteCommandType::SignalB:
        arg = ourRemoteCmd.getArg();
        id = arg->toInt();
        if (id != LastId) { 
          logMsg(INFO, F("Signal-B received"));
          ourSpeedTask.signal(SignalB);
        } else {
          logMsg(WARNING, F("Signal-B retarded"));
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
          logMsg(INFO, String(F("Battery B voltage: ")) + String(ourBatteryBVoltage) + F("mV"));
        }
        break;
      default:
        logMsg(ERROR, F("unknow RTC data"));
        break;
    }
    ourRemoteCmd.consume();
  }

  // sending a state request to the B-Line-Controller
  if (aNow > lastBLineRequest) {
    lastBLineRequest = aNow + B_LINE_REQUEST_DELAY;
    unsigned long a = millis();
    boolean sendSuccess = ourRadio.transmit(
      ourRemoteCmd.createCommand(F3XRemoteCommandType::BLineStateReq, String(ourRadioRequestArg))->c_str(), 20);

    uint16_t signalRoundTrip = millis() - a;

    uint8_t lost=0;
    if (!sendSuccess) {
      logMsg(LS_INTERNAL, String(F("sending TestRequest NOT successsfull. Retransmissions: ")) 
        + String(ourRadio.getRetransmissionCount()) + String(F("/")) + String(signalRoundTrip) + String(F("ms")));
      ourBuzzer.on(PinManager::SHORT);
      lost=100;
      signalRoundTrip = UINT16_MAX;
      ourBatteryBVoltage = 0;
    } else {
      logMsg(DEBUG, String(F("sending TestRequest successsfull. Retransmissions: ")) 
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
      // changed radio settings are successfully transmitted to B-Line, now the A-Line can also be switched
      ourRadio.setPower(ourRadioPower);
      ourRadio.setChannel(ourRadioChannel);
      ourRadio.setDataRate(ourRadioDatarate);
      ourRadio.setAck(ourRadioAck);

      // set the new radio settings to the configuration data, which can be stored in the EEPROM later on
      ourConfig.radioChannel = ourRadioChannel;
      ourConfig.radioPower = ourRadioPower;
      LOGGY3(LOG_MOD_RTEST, INFO, F("send radio setting to remote and set local") + settings);
    } else {
      logMsg(LS_INTERNAL, ERROR, String(F("sending settings not possible: ") + settings));
    }
    ourRadioSendSettings=false;
  }
}

void updateBuzzer(unsigned long aNow) {
  ourBuzzer.update(aNow);
}

#ifdef OLED 
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
  // TC_F3XRadioPower:
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

void showUpdatePage() {
  ourOLED.setFont(oledFontLarge);

  ourOLED.setCursor(0, 12+4);
  ourOLED.print(F("F3X Update"));

  ourOLED.setCursor(5, 28);
  ourOLED.setFont(oledFontNormal);
  ourOLED.print(ourContext.getInfoString());
}

void showOLEDMenu(const char* aItems[], uint8_t aNumItems, const char* aName) {
  ourOLED.drawBox(0, 0, 128, 16);
  ourOLED.drawBox(0, 28, 128, 14);

  ourOLED.setDrawColor(2);
  ourOLED.setFontMode(1);
  ourOLED.setFont(oledFontNormal);
  ourOLED.drawStr(5,12, aName);

  ourOLED.setFont(oledFontBig);
  for (int8_t i=-1; i<2; i++) {
    ourOLED.drawStr(0,41+i*13, aItems[getModulo(ourRotaryMenuPosition+i, aNumItems)]);
  }
  ourOLED.setFontMode(0);
  ourOLED.setDrawColor(1);
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

void showF3BSpeedTask() {
  static unsigned long lastFT = 0;
  unsigned long flightTime = ourSpeedTask.getFlightTime(RUNNING_VALUE);
  

  String runSpeedTime;
  String taskTime;
  String legTime1, legTime2, legTime3, legTime4;
  legTime1 = legTime2 = legTime3 = legTime4 = String(F(""));
  runSpeedTime = getSpeedTimeString(F3B_TIME_NOT_SET, F3B_TIME_NOT_SET, 0, 0, 0);
  String info;

  char taskState='?';
  if (flightTime != lastFT || ourSpeedTask.getTaskState() != TaskRunning) {
    lastFT = flightTime;
    switch (ourSpeedTask.getTaskState()) {
      case TaskRunning:
        taskState='R';
        if (ourSpeedTask.getCurrentSignal() >=0) {
          taskState= ourSpeedTask.getCurrentSignal() +'0';
        }
        switch (ourSpeedTask.getCurrentSignal()) {
          case NOT_STARTED:
            info=F("P:A-Line rev Cross");
            break;
          case A_LINE_REVERSED:
            info=F("P:Enter 1.leg");
            break;
          case B_LINE_CROSSED_1:
            info=F("P:2. turn");
            break;
          case B_LINE_CROSSED_2:
            info=F("P:target cross");
            break;
          default:
            info=F("P:---");
            break;
        }
       
        if (ourSpeedTask.getCurrentSignal() >= A_LINE_CROSSED_1) {
          runSpeedTime=getSpeedTimeString(flightTime, F3B_TIME_NOT_SET, 0, 0, 0);
        }
        break;
      case TaskWaiting:
        taskState='W';
        info=F("P:Start Tasktime");
        break;
      case TaskTimeOverflow:
        taskState='O';
        info=F("PP:Reset");
        break;
      case TaskError:
        taskState='E';
        break;
      case TaskFinished:
        taskState='F';
        runSpeedTime=getSpeedTimeString(flightTime, F3B_TIME_NOT_SET, 0, 0, 0);
        legTime1 = getLegTimeStr(ourSpeedTask.getLegTime(1), ourSpeedTask.getDeadDelay(2), ourSpeedTask.getDeadDistance(2));
        legTime2 = getLegTimeStr(ourSpeedTask.getLegTime(2), ourSpeedTask.getDeadDelay(3), ourSpeedTask.getDeadDistance(3));
        legTime3 = getLegTimeStr(ourSpeedTask.getLegTime(3), ourSpeedTask.getDeadDelay(4), ourSpeedTask.getDeadDistance(4));
        legTime4 = getLegTimeStr(ourSpeedTask.getLegTime(4), ourSpeedTask.getDeadDelay(5), ourSpeedTask.getDeadDistance(5));
        info=F("");
        break;
      default:
        taskState='?';
        break;
    }
  
    // OLED 128x64
    ourOLED.setFont(oledFontNormal);
    ourOLED.setCursor(0, 12);
    ourOLED.print(F("F3B Speed: "));
    ourOLED.setFont(oledFontBig);
    ourOLED.print(runSpeedTime);

    ourOLED.setFont(oledFontSmall);
    ourOLED.setCursor(0, 63);
    ourOLED.print(info);
    ourOLED.setCursor(100, 63);
    ourOLED.print(F("["));
    ourOLED.print(taskState);
    ourOLED.print(F("]"));
    
    switch (ourSpeedTask.getTaskState()) {
      case TaskWaiting:
      case TaskRunning:
        ourOLED.setFont(oledFontNormal);
        ourOLED.setCursor(10, 27);
        ourOLED.print(F("Task Time: "));
        ourOLED.print(getTimeStr(ourSpeedTask.getTaskTime(), true));

        break;
      case TaskFinished:
        ourOLED.setFont(oledFontNormal);
        ourOLED.setCursor(10, 27);
        ourOLED.print(F("Leg 1: "));
        ourOLED.print(legTime1);
        ourOLED.setCursor(10, 39);
        ourOLED.print(F("Leg 2: "));
        ourOLED.print(legTime2);
        ourOLED.setCursor(10, 51);
        ourOLED.print(F("Leg 3: "));
        ourOLED.print(legTime3);
        ourOLED.setCursor(10, 63);
        ourOLED.print(F("Leg 4: "));
        ourOLED.print(legTime4);
        break;
    }
  }
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
  
  ourOLED.firstPage();
  do {
    if (ourDialogTimer > aNow) {
      showDialog();
    } else {
      switch(ourContext.get()) {
        case TC_F3BSpeedTask:
          showF3BSpeedTask();
          break;
        case TC_F3FTask:
          showNotYetImplemented();
          break;
        case TC_F3XOtaUpdate:
          showUpdatePage();
          break;
        case TC_F3XMessage:
          showMessagePage();
          break;
        case TC_F3XRadioChannel:
          showRadioChannelPage();
          break;
        case TC_F3XRadioPower:
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
        case TC_F3XSettingsMenu: 
          showOLEDMenu(ourSettingsMenuItems, ourSettingsMenuSize, ourSettingsMenuName);
          break;
        default:
          showError(ourContext.get());
          break;
      }
    }
  } while ( ourOLED.nextPage() );
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
  logMsg(LS_INTERNAL, aHead+aMessage);
  ourOLED.firstPage();
  do {
    showMessage(aHead, aMessage);
  } while ( ourOLED.nextPage() );
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
    logMsg(INFO, String(F("battery A voltage: ")) + String(volt, 2) + F("V | sensor-value: ") + String(raw));


    bool battWarn = false;
    // check battery level and warn if low
    #define BAT_WARN_LEVEL 3200 // mV
    if(ourBatteryBVoltage > 500 && ourBatteryBVoltage < BAT_WARN_LEVEL) {
      battWarn = true;
    }
    if(ourBatteryAVoltage > 500 && ourBatteryAVoltage < BAT_WARN_LEVEL) {
      battWarn = true;
    }
    if (battWarn) {
      ourBuzzer.pattern(5,100,30,100,30,200);
    }
  }
}
#endif

void perfCheck(void (*aExecute)(unsigned long), const char* aDescription, unsigned long aNow) {
  #ifdef PERF_DEBUG
  unsigned long start = millis();
  #endif
  aExecute(aNow);
  #ifdef PERF_DEBUG
  if ((millis() - start) > 100) {
    logMsg(LOG_MOD_PERF, DEBUG, String(F("performance info for ")) + aDescription + F(":") + String(millis()-start));
  }
  #endif
}

void updateSpeedTask(unsigned long aNow) {
 ourSpeedTask.update();
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
      case TC_F3BSpeedTask:
        logMsg(INFO, F("button F3BSpeedTask"));
        reactOnMultiplePressed = history[buttonPressedCnt-1] + MULTI_PRESS_REACTION_TIME;
        switch(ourSpeedTask.getTaskState()) {
          case TaskWaiting:
            ourSpeedTask.start();
            ourContext.setTaskRunning(true);
            ourBuzzer.on(PinManager::SHORT); 
            break;
          case TaskFinished:
          case TaskTimeOverflow:
            ourContext.setTaskRunning(false);
            ourSpeedTask.stop();
            logMsg(INFO, F("resetting task: F3BSpeedMenu"));
            resetRotaryEncoder();
            ourContext.set(TC_F3BSpeedMenu);
            CLEAR_HISTORY;
            MULTI_PRESSED_FINISHED;
            break;
          case TaskRunning:
            ourSpeedTask.signal(SignalA);
            break;
          default:
            break;
        }
        break;
      case TC_F3FTask: // button press
      case TC_F3XOtaUpdate: // button press
      case TC_F3XMessage: // button press
      case TC_F3XInfo: // button press
      case TC_F3XRadioInfo: // button press
        logMsg(DEBUG, F("HW button in: ") + String(ourContext.get()));
        ourBuzzer.on(PinManager::SHORT); 
        ourContext.back();
        CLEAR_HISTORY;
        break;
      case TC_F3XRadioPower: // button press in radio power context
        ourRadioSendSettings=true;
        logMsg(LS_INTERNAL, INFO, F("set RF24 Power:") + String(ourRadioPower));
        ourContext.set(TC_F3XSettingsMenu);
        resetRotaryEncoder((long) TC_F3XRadioPower);
        CLEAR_HISTORY;
      case TC_F3XRadioChannel: // button press in radio channel context
        ourRadioSendSettings=true;
        logMsg(LS_INTERNAL, INFO, F("set RF24 Channel:") + String(ourRadioChannel));
        // NYI
        ourContext.set(TC_F3XSettingsMenu);
        resetRotaryEncoder((long) TC_F3XRadioChannel);
        CLEAR_HISTORY;
        break;
      case TC_F3XBaseMenu: {
        uint8_t menuPos =  getModulo(ourRotaryMenuPosition, ourF3XBaseMenuSize); // !! use the right size here
        logMsg(DEBUG, String(F("HW button pressed in F3XBaseMenu -> setting menu context: ")) + String(menuPos));
        switch (menuPos) {  // !! use the right size here !!
            case 0: // "0:F3B-Speedtask";
              ourBuzzer.on(PinManager::SHORT); 
              logMsg(INFO, F("setting task: F3BSpeedMenu"));
              resetRotaryEncoder();
              ourContext.set(TC_F3BSpeedMenu);
              CLEAR_HISTORY;
              break;
            case 1: // "1:F3F-Task";
              ourBuzzer.on(PinManager::SHORT); 
              logMsg(INFO, F("setting task: F3FDistanceTask"));
              ourContext.set(TC_F3FTask);
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
              resetRotaryEncoder();
              CLEAR_HISTORY;
              break;
          }
        }
        break;
      case TC_F3BSpeedMenu: { // -> ourF3BSpeedMenuItems
        uint8_t menuPos =  getModulo(ourRotaryMenuPosition, ourF3BSpeedMenuSize); // !! use the right size here
        logMsg(DEBUG, String(F("HW button pressed in TC_F3BSpeedMenu -> F3B speed menu context: ")) + String(menuPos));
        switch (menuPos) {  // !! use the right size here !!
          case 0: // "0:Start Tasktime";
            logMsg(INFO, F("setting task: F3BSpeedTask"));
            ourContext.set(TC_F3BSpeedTask);
            ourSpeedTask.start();
            CLEAR_HISTORY;
            break;
          case 1: // "1:Back"
            ourSpeedTask.stop();
            resetRotaryEncoder(0);
            ourContext.set(TC_F3XBaseMenu);
            break;
          }
        }
        break;
      case TC_F3XSettingsMenu: { // -> ourSettingsMenuItems
        uint8_t menuPos =  getModulo(ourRotaryMenuPosition, ourSettingsMenuSize); // !! use the right size here
        logMsg(DEBUG, String(F("HW button pressed in F3XSettingsMenu -> setting menu context: ")) + String(menuPos));
        switch (menuPos){  // !! use the right size here !!
          case 0: // "0:Buzzer on/off";
            if (ourBuzzer.isEnabled()) {
              ourBuzzer.disable();
            } else {
              ourBuzzer.enable();
            }
            ourBuzzer.on(PinManager::SHORT);
            break;
          case 1: // "1:Radio channel";
            ourBuzzer.on(PinManager::SHORT);
            if (!ourRadioSendSettings || ourRadioQuality > 99.0f) {
              ourContext.set(TC_F3XRadioChannel);
              ourRadioChannel = ourRadio.getChannel();
              resetRotaryEncoder();
            } else {
              ourContext.set(TC_F3XMessage);
              ourContext.setInfo(String(F("no B-Line connected")));
            }
            break;
          case 2: // "2:Radio power";
            ourBuzzer.on(PinManager::SHORT);
            if (!ourRadioSendSettings || ourRadioQuality > 99.0f) {
              ourContext.set(TC_F3XRadioPower);
              ourRadioPower=ourRadio.getPower();
              resetRotaryEncoder();
            } else {
              ourContext.set(TC_F3XMessage);
              ourContext.setInfo(String(F("no B-Line connected")));
            }
            break;
          case 3: // "3:Display invert";
            ourBuzzer.on(PinManager::SHORT);
            ourConfig.oledFlipped = ourConfig.oledFlipped == true? false: true;
            ourOLED.setFlipMode(ourConfig.oledFlipped);
            break;
          case 4: // "4:Rotary button inv.";
            ourBuzzer.on(PinManager::SHORT);
            ourConfig.rotaryEncoderFlipped = ourConfig.rotaryEncoderFlipped ? false: true;
            ourREInversion = ourConfig.rotaryEncoderFlipped ? -1 : 1;
            {
              for (uint8_t i = 0; i < 254; i++) {
                ourRotaryEncoder.write(i);
                if ( 2 == getModulo(getRotaryEncoderPosition(), ourSettingsMenuSize)) {
                  ourRotaryEncoder.write(i+2);
                  break; // for loop}
                }
              }
            }
            break;
          case 5: //  "5:Update firmware";
            otaUpdate(false); // firmware
            break;
          case 6: //  "6:Update filesystem";
            otaUpdate(true); // filesystem
            break;
          case 7: //  "7:WiFi on/off";
            WiFi.mode(WIFI_OFF) ; // client mode only
            ourConfig.wifiIsActive = !ourConfig.wifiIsActive;
            showDialog(2000, String(F("WiFi is ")) + (ourConfig.wifiIsActive ? String(F("enabled")) : String(F("disabled"))));
            break;
          case 8: //  "8:Save settings";
            ourBuzzer.on(PinManager::SHORT);
            saveConfig();
            showDialog(2000, String(F("Config saved ")));
            break;
          case 9: // "9:Main menu";
            ourBuzzer.on(PinManager::SHORT);
            resetRotaryEncoder(ourContext.get());
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
      controlRotaryEncoder(false);
    } else {
      controlRotaryEncoder(true);
      CLEAR_HISTORY;
    }
  
    logMsg(INFO, F("before multi: ") + String(aNow) + "/" + String(reactOnMultiplePressed));
  

    if (reactOnMultiplePressed && aNow < reactOnMultiplePressed) {
      logMsg(INFO, F("react multi:") + String(buttonPressedCnt));
     
      switch (buttonPressedCnt) {
        case 3:
          switch (ourContext.get()) {
            case TC_F3BSpeedTask:
              ourSpeedTask.stop();
              resetRotaryEncoder(0);
              controlRotaryEncoder(true);
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
  logMsg(LS_INTERNAL, String(F("RotaryEncoder enabled:")) + String(aEnable));
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
        switch(ourSpeedTask.getTaskState()) {
          case TaskTimeOverflow:
          case TaskWaiting:
          case TaskFinished:
            ourSpeedTask.stop();
            ourContext.set(TC_F3XBaseMenu);
            resetRotaryEncoder(TC_F3BSpeedTask);
            ourBuzzer.on(PinManager::SHORT);
            break;
        }
        break;
      case TC_F3XRadioChannel:
        ourRadioChannel += delta;
        ourRadioChannel = getModulo(ourRadioChannel, RF24_1MHZ_CHANNEL_NUM);
        break;
      case TC_F3XRadioPower:
        ourRadioPower += delta;
        ourRadioPower = getModulo(ourRadioPower, 4);
        break;
    }
    ourREOldPos = position;
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
  if ((now - lastslow) > 100) {
    logMsg(DEBUG, String(F("> slow last loop: ")) + (now - lastslow));
    logMsg(LS_INTERNAL, String(F("slow last loop:")) + String(now-lastslow));
    isSlow = true;
  }
  lastslow = now;
  #endif

  perfCheck(&updatePushButton, "time push button", now);

  perfCheck(&updateBuzzer, "time buzzer", now);

  perfCheck(&updateSpeedTask, "time speedtask", now);

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
