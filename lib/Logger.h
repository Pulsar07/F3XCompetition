#ifndef Logger_h
#define Logger_h

#include <Arduino.h>

#define LOG_MOD_HTTP 0x01
#define LOG_MOD_PERF 0x02
#define LOG_MOD_RTEST 0x04
#define LOG_MOD_RADIO 0x08
#define LOG_MOD_5 0x10
#define LOG_MOD_6 0x20
#define LOG_MOD_7 0x40
#define LOG_MOD_8 0x80

// LOGGING LOGGING
enum LogSeverity {
  LS_START=0,
  LS_INTERNAL,
  DEBUG,
  INFO,
  WARNING,
  ERROR,
  LS_END
};

#define LOGBUFFSIZE 10
class Logger {
  public:
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static Logger& getInstance() {
      static Logger instance;
      return instance;
    }

    void setup(const char* aName) {
      myApplication = aName;
      myModules = 0;
      setModule(LOG_MOD_HTTP);
      setModule(LOG_MOD_PERF);
      setModule(LOG_MOD_RTEST);
      setModule(LOG_MOD_RADIO);
      mySeverity=DEBUG;
      myDoSerialLogging=true;
    }

    void doSerialLogging(bool aArg) {
      myDoSerialLogging = aArg;
    }
    void setModule(byte aModule) {
      myModules = myModules | aModule;
    }

    void log(LogSeverity aSeverity, String aMessage) {
      if (aSeverity == LS_INTERNAL) {
        // b[9] = b[8];
        // b[8] = b[7];
        // ...
        // b[1] = b[0];
        for (int i=LOGBUFFSIZE-1 ; i>0; i--) {
          myInternalLogBuffer[i] = myInternalLogBuffer[i-1];
        }
        char t[25];
        snprintf (t, 25, "%08d: ", millis());
        String buf;
        buf.concat(t);
        buf.concat(aMessage);
        myInternalLogBuffer[0] = buf;
      }
      if (!myDoSerialLogging) return;
    
      if (aSeverity >= mySeverity) {
        log_printSecond();
        Serial.print(myApplication);
        Serial.print(':');
        Serial.print(aMessage);
        Serial.println();
      }
    }

    void log(byte aModule, LogSeverity aSeverity, String aMessage) {
      if (aModule & myModules) {
        log(aSeverity, aMessage);
      }
    }

    String getInternalMsg(uint8_t aIdx) {
      return myInternalLogBuffer[aIdx];
    }
  private:
    Logger() {
      mySeverity=DEBUG;
      myDoSerialLogging=true;
    }
    ~Logger() = default;

    void log_printSecond() {
      if (!myDoSerialLogging) return;
    
      char buf[25];
      unsigned long now = millis();
      snprintf (buf, 25, "%08lu: ", now);
      Serial.print(buf);
    }

    LogSeverity mySeverity;
    byte myModules;
    const char* myApplication;
    bool myDoSerialLogging;
    String myInternalLogBuffer[LOGBUFFSIZE];
};

#define LOGGY(a, b) logMsg(a, b)
// #define LOGGY(a, b) 
#define LOGGY2(a, b) logMsg(a, b)
#define LOGGY3(a, b, c) logMsg(a, b, c)
// #define LOGGY2(a, b) 
#define logMsg Logger::getInstance().log

#endif

