#ifndef Logger_h
#define Logger_h

#include <Arduino.h>

// #define LOG_MOD_HTTP 0x01
// #define LOG_MOD_PERF 0x02
// #define LOG_MOD_RTEST 0x04
// #define LOG_MOD_RADIO 0x08
// #define LOG_MOD_5 0x10
// #define LOG_MOD_6 0x20
// #define LOG_MOD_SIG 0x40
// #define LOG_MOD_BAT 0x80


// LOGGING LOGGING
enum LogSeverity {
  LS_OFF=0,
  DEBUG,
  INFO,
  WARNING,
  ERROR,
  // LS_INTERNAL,
  LS_END
};

#define LOG_MOD_ALL       0
#define LOG_MOD_WEB       1    // this module will be logged via buffer to WEB logging, independent of severity
#define LOG_MOD_HTTP      2
#define LOG_MOD_PERF      3
#define LOG_MOD_RTEST     4
#define LOG_MOD_RADIO     5
#define LOG_MOD_SIG       6
#define LOG_MOD_BAT       7
#define LOG_MOD_TASK      8 
#define LOG_MOD_TASKDATA  9
#define LOG_MOD_NET      10 
#define LOG_MOD_INTERNAL 11 

#define NUM_MOD_LOG      12 

#define LOGBUFFSIZE 10

class Logger {
  public:
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static Logger& getInstance() {
      static Logger instance;
      return instance;
    }

    void setLogLevel(int aModule, LogSeverity aSeverity) {
      myLogSevArray[aModule] = aSeverity;
    }

    /**
     *  set severity for all LOG_MOD_* for logging via buffer to WEB logging
     */
    void setWebLogLevel(LogSeverity aSeverity) {
      myWebLogLevel = aSeverity;
    }

    void setup(const char* aName) {
      myApplication = aName;
      mySeverity=INFO;
      myDoSerialLogging=true;
      for(int i=0; i<NUM_MOD_LOG; i++) {
        myLogSevArray[i] = ERROR;
      }
      setLogLevel(LOG_MOD_ALL, WARNING);
      setLogLevel(LOG_MOD_WEB, WARNING);
      setLogLevel(LOG_MOD_HTTP, WARNING);
      setLogLevel(LOG_MOD_PERF, WARNING);
      setLogLevel(LOG_MOD_RTEST, WARNING);
      setLogLevel(LOG_MOD_RADIO, WARNING);
      setLogLevel(LOG_MOD_SIG, DEBUG);
      setLogLevel(LOG_MOD_BAT, WARNING);
      setLogLevel(LOG_MOD_TASK, WARNING);
      setLogLevel(LOG_MOD_TASKDATA, WARNING);
      setLogLevel(LOG_MOD_NET, WARNING);
      setLogLevel(LOG_MOD_INTERNAL, WARNING);
      setWebLogLevel(WARNING);

    }

    void doSerialLogging(bool aArg) {
      myDoSerialLogging = aArg;
    }

    void log(LogSeverity aSeverity, String aMessage) {
      log(LOG_MOD_ALL, aSeverity, aMessage);
    } 
    
    void log(byte aModule, LogSeverity aSeverity, String aMessage) {
      if (aModule == LOG_MOD_WEB || aSeverity >= myWebLogLevel ) {
      myWebLogLevel = aSeverity;
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
    
    //   if (aSeverity >= mySeverity) {
      if (aSeverity >= myLogSevArray[aModule]) {
        log_printSecond();
        Serial.print(myApplication);
        Serial.print(':');
        Serial.print(aSeverity);
        Serial.print(':');
        Serial.print(aMessage);
        Serial.println();
      }
    }

    // void log(byte aModule, LogSeverity aSeverity, String aMessage) {
    //   // if (aModule & myModules) {
    //   if (aSeverity >= myLogSevArray[aModule])
    //     log_(aSeverity, aMessage);
    //   }
    // }

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

    int myLogSevArray[NUM_MOD_LOG];
    int myWebLogLevel;
    LogSeverity mySeverity;
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

