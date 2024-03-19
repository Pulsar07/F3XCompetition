#ifndef Config_h
#define Config_h

#include <WString.h>

#define CONFIG_VERSION "RSV1"
#define CONFIG_VERSION_L 5
#define CONFIG_SSID_L 16
#define CONFIG_PASSW_L 64

#define MIN_IDX 0
#define MAX_IDX 1

// Types 'byte' und 'word' doesn't work!
typedef struct {
  char version[CONFIG_VERSION_L];
  char wlanSsid[CONFIG_SSID_L];
  char wlanPasswd[CONFIG_PASSW_L];
  char apSsid[CONFIG_SSID_L];
  char apPasswd[CONFIG_PASSW_L];
  boolean wifiIsActive;
  boolean oledFlipped;
  boolean rotaryEncoderFlipped;
  int8_t radioPower;
  int8_t radioChannel;
} configData_t;

#endif
