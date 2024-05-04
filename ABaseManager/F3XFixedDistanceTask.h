#ifndef F3X_DISTANCE_TASK_H
#define F3B_DISTANCE_TASK_H

//
//    FILE: F3XMultiDistanceTask.h
//  AUTHOR: Rainer Stransky
// VERSION: 0.1.0
// PURPOSE: class supporting the different distance tasks with a fixed number of legs (F3B distance/speed F3F)

#include "Arduino.h"
#include "limits.h"

#define F3X_TIME_NOT_SET -1UL
#define F3X_LEG_COUNT_NOT_SET -1
#define F3X_GFT_LAST_SIGNALLED_TIME -1
#define F3X_GFT_RUNNING_TIME -2
#define F3X_GFT_FINAL_TIME -3
#define F3X_GFT_MIN_ARG -3
#define F3X_PROGRESS_NOT_STARTED -1


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
typedef enum Signal {
  SignalA,
  SignalB
} Signal;

enum State {
  TaskError,
  TaskWaiting,
  TaskRunning,
  TaskTimeOverflow,
  TaskFinished,
  TaskNotSet,
};
  F3XFixedDistanceTask(uint16_t aLegLength, uint8_t aLegNumberMax);
  uint16_t getLegLength();
  uint8_t getLegNumberMax();
  // void init(void (*)(), void (*)());
  void init(void (*aACallBack)(), void (*aBCallBack)());
  void addStateChangeCallback( void (*aStateChangeCallback)(State));
  void signal(Signal aSignal);
  void signal();
  void timeOverflow();
  void start();
  void stop();
  void resetSignals();
  long getRemainingTasktime();
  void setTasktime(uint16_t aTasktimeInSeconds);
  unsigned long getFlightTime(int8_t aSignalIdx=F3X_GFT_LAST_SIGNALLED_TIME);
  F3XLeg getLeg(int8_t aIndex);
  float getFinalSpeed();
  float getSpeed();
  int8_t getSignalledLegCount();
  void update();
  State getTaskState();
protected:
  unsigned long * mySignalTimeStamps;
  unsigned long * myDeadDistanceTimeStamp;
  unsigned long myTaskStartTime;
  unsigned long myNow;
  void (*mySignalACallback)(void);
  void (*mySignalBCallback)(void);
  void (*myStateChangeCallback)(State);
  int8_t mySignalledLegCount;
  State myTaskState;
  uint16_t myTasktime;
  uint16_t myLegLength;
  uint8_t myLegNumberMax;
  void setTaskState(State aTaskState);
};

#endif  
