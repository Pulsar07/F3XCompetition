#ifndef F3XFixedDistanceTask_h
#define F3XFixedDistanceTask_h

//
//    FILE: F3XFixedDistanceTask.h
//  AUTHOR: Rainer Stransky
// VERSION: 0.1.0
// PURPOSE: class supporting the different distance tasks with a fixed number of legs (F3B distance/speed F3F)

#include "Arduino.h"
#include "limits.h"

#define F3X_TIME_NOT_SET -1UL
#define F3X_GFT_LAST_SIGNALLED_TIME -1
#define F3X_GFT_RUNNING_TIME -2
#define F3X_GFT_FINAL_TIME -3
#define F3X_GFT_MIN_ARG -3
#define F3X_COURSE_NOT_STARTED -2
#define F3X_IN_AIR -1
#define F3X_COURSE_STARTED 0 

#define F3X_LEG_MIN  -1
#define F3X_LEG_AVG  -2
#define F3X_LEG_MAX  -3

static const char* F3BSpeedTaskStateStr[] = {
  "TaskError",
  "TaskWaiting",
  "TaskRunning",
  "TaskTimeOverflow",
  "TaskFinished",
  "TaskNotSet",
};

class F3XLeg {
  public:
    bool valid;
    int8_t idx;
    unsigned long time;
    float speed;
    unsigned long deadTime;
    uint16_t deadDistance;
};

class F3XFixedDistanceTask
{
public:
  typedef enum F3XType {
    F3BSpeedType,
    F3FType
  } F3XType;
  
  typedef enum Signal {
    SignalA,
    SignalB
  } Signal;
  
  enum State {
    TaskError, 
    TaskWaiting,       // initial state, no task time is running
    TaskRunning,       // task time is running
    TaskTimeOverflow,  // task time overflow before last signal 
    TaskFinished,      // last signal received before running out of task time 
    TaskNotSet, 
  };
  F3XFixedDistanceTask(F3XType aType);
  uint16_t getLegLength();
  void setLegLength(uint16_t aLength);
  uint8_t getLegNumberMax();
  // void init(void (*)(), void (*)());
  void addSignalAListener( void (*aListener)());
  void addSignalBListener( void (*aListener)());
  void addStateChangeListener( void (*aListener)(State));
  void addInAirIndicationListener( void (*aListener)());
  void signal(Signal aSignal);
  void signal();
  void timeOverflow();
  void start();
  void stop();
  void inAir();
  unsigned long getInAirTime();
  void resetSignals();
  long getRemainingTasktime();
  void setTasktime(uint16_t aTasktimeInSeconds);
  unsigned long getCourseTime(int8_t aSignalIdx=F3X_GFT_LAST_SIGNALLED_TIME);
  F3XLeg getLeg(int8_t aIndex);
  float getFinalSpeed();
  float getSpeed();
  int8_t getSignalledLegCount();
  void update();
  State getTaskState();
  F3XType getType();
  char* getLegTimeString(unsigned long aTime, unsigned long aLegTime, uint16_t aLegSpeed,  unsigned long aDeadDelay, uint8_t aDeadDistance, char aSeparator='/', bool aForceDeadData=false, bool aShowUnits=false);
  
  static char* getHMSTimeStr(unsigned long aTime, boolean aShort=false);
  void startLoopTasks();
  unsigned long getLastLoopTaskCourseTime();
  void setLoopTasksEnabled(boolean);
  boolean getLoopTasksEnabled();
  uint8_t getLoopTaskNum();
protected:
  F3XType myType;
  unsigned long * mySignalTimeStamps;
  unsigned long * myDeadDistanceTimeStamp;
  unsigned long myTaskStartTime;
  unsigned long myNow;
  void (*mySignalAListener)(void);
  void (*mySignalBListener)(void);
  void (*myStateChangeListener)(State);
  void (*myInAirIndicationListener)(void);
  int8_t mySignalledLegCount;
  State myTaskState;
  unsigned long myLaunchTime;
  uint8_t myListenerIndication;
  unsigned long myLastInAirIndication;
  uint16_t myTasktime;
  uint16_t myLegLength;
  uint8_t myLegNumberMax;
  void setTaskState(State aTaskState);
  void startCourseTime();
  uint8_t myLoopTaskNum;
  boolean myLoopTaskEnabled;
  unsigned long myLastLoopTaskCourseTime;
};

#endif  
