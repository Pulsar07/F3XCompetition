#ifndef PIN_MANAGER_H
#define PIN_MANAGER_H
#include "Logger.h"
class PinManager {
  private:
    static const uint8_t PATTERN_MAX=9;
    enum State { DISABLED, IDLE, ON, PATTERN }; 
    State myState;
    bool myOnState;
    bool myOffState;
    bool myPinState;
    uint8_t myPin;
    unsigned long myTimeTill;
    unsigned long myPattern[PinManager::PATTERN_MAX];
    int8_t myPatternIdx;
    unsigned long myStartTime;
    unsigned long myDuration;
    void set(bool aState, bool aForce=false) {
      if (myPinState != aState || aForce) {
        myPinState = aState;
				digitalWrite(myPin, myPinState);
        // logMsg(INFO, String(F("PinManager::set=")) + String(myPinState)); 
      } 
    }
  public:
    enum TIMES { SHORT=50, LONG=100 }; 
    PinManager(uint8_t aPin, bool aInvertPinState=false) {
      myOnState = aInvertPinState ? LOW:HIGH;
      myOffState = aInvertPinState ? HIGH:LOW;
      myState = IDLE;
      myPin = aPin;
      myTimeTill=0UL;
      pinMode(myPin, OUTPUT);
      set(myOffState, true);
    }
    void enable() {myState=IDLE;}
    void disable() {myState=DISABLED;}
    bool isEnabled() {return myState != DISABLED;}
    void pattern(int aCount, ...) {
      switch (myState) {
        case DISABLED:
          return;
        case IDLE: {
          va_list ap;
          va_start(ap, aCount); //Requires the last fixed parameter (to get the address)
          int j;
          uint16_t x;
          for(j=0; j<PinManager::PATTERN_MAX; j++) {
            if (j<aCount) {
              x=va_arg(ap, int); //Requires the type to cast to. Increments ap to the next argument.
              myPattern[j] = x;
            } else {
              myPattern[j] = 0;
            }
          }
          va_end(ap);

          myState = PATTERN;
          myPatternIdx=-1;
          myStartTime=0;
          myDuration=0;
        }
        break;
      }
    }

    void on(uint16_t aDuration) {
      // logMsg(INFO, String(F("=====> PinManager::on():")) + String(aDuration)); 
      switch (myState) {
        case DISABLED:
          return;
        case IDLE:
        case PATTERN:
          myState = ON;
          myStartTime = millis();
          myDuration = aDuration;
          // logMsg(INFO, String(F("PinManager::set():IDLE ")) + String(myStartTime) + "/" + String(myDuration)); 
				  set(myOnState);
          break;
        case ON:
          int16_t remain = myDuration - (millis() - myStartTime);
          if (remain < aDuration ) {
            myStartTime = millis();
            myDuration = aDuration;
            // logMsg(INFO, String(F("PinManager::set():ON ")) + String(myStartTime) + "/" + String(myDuration)); 
          }
          break;
      }
    }
    void update(unsigned long aNow) {
      unsigned long now = millis();
      switch (myState) {
        case DISABLED:
        case IDLE:
          break;
        case ON:
          if ((now - myStartTime) >= myDuration) {
            // logMsg(INFO, String(F("PinManager::update():ON ->IDLE ")) + String(now) + "/" + String(myStartTime) + "/" + String(myDuration)); 
            myState = IDLE;
				    set(myOffState);
          } else {
				    set(myOnState);
          }
          break;
        case PATTERN:
          if ((now - myStartTime) >= myDuration) { // job done, look for others
            myPatternIdx++;
            if (myPatternIdx < PinManager::PATTERN_MAX) {
              if (myPattern[myPatternIdx] == 0) { // job done no pattern anymore
                myState = IDLE;
                set(myOffState);
                // logMsg(INFO, String(F("PATTERN last"))); 
              }
              // logMsg(INFO, String(F("set PATTERN ")) + String(myPatternIdx) + "/" + String(myDuration)); 
              myStartTime = millis();
              myDuration = myPattern[myPatternIdx];
            } else { // no more jobs available
              myState = IDLE;
              set(myOffState);
              // logMsg(INFO, String(F("PATTERN no more"))); 
            }
          } else { // job in action
            if (myPatternIdx%2 == 0) { // ON
              set(myOnState);
              // logMsg(INFO, String(F("PATTERN ON"))); 
            } else { // OFF / pause
              set(myOffState);
              // logMsg(INFO, String(F("PATTERN OFF"))); 
            }
          }
      }
    }
};
#endif
