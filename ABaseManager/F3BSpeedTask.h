#ifndef F3B_SPEED_TASK_H
#define F3B_SPEED_TASK_H

//
//    FILE: F3BSpeedTask.h
//  AUTHOR: Rainer Stransky
// VERSION: 0.1.0
// PURPOSE: Arduino library for MS5611 temperature and pressure sensor
//     URL: https://github.com/RobTillaart/MS5611

#include "Arduino.h"
#include "limits.h"

#define F3B_SPEED_LEG_LENGTH                      150
#define F3B_TIME_NOT_SET 4294967294


static const char* F3BSpeedTaskStateStr[] = {
  "TaskError",
  "TaskWaiting",
  "TaskRunning",
  "TaskTimeOverflow",
  "TaskFinished",
  "TaskNotSet",
};

#define SIGNAL_TIMER_CNT 5 
class F3BSpeedTask
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
  typedef enum  Progress {
    NOT_STARTED = -1,
    // AL_REV A_LINE_REVERSED,       // 0
    A_LINE_CROSSED_1,       // 0
    B_LINE_CROSSED_1,
    A_LINE_CROSSED_2,
    B_LINE_CROSSED_2,
    A_LINE_CROSSED_FINAL,   // 4
    RUNNING_VALUE,
  } Progress;
  F3BSpeedTask();
  void init(void (*)(), void (*)());
  void signal(Signal aSignal);
  void signal();
  void timeOverflow();
  void start();
  void stop();
  void resetSignals();
  long getRemainingTasktime();
  void setTasktime(uint16_t aTasktimeInSeconds);
  unsigned long getFlightTime(int8_t aIdx=-1);
  unsigned long getLegTime(int8_t);
  unsigned long getDeadDelay(int8_t);
  float getLegSpeed(int8_t);
  float getFinalSpeed();
  float getSpeed();
  uint8_t getDeadDistance(int8_t);
  Progress getProgress();
  void update();
  State getTaskState();
protected:
  unsigned long mySignalTimeStamps[SIGNAL_TIMER_CNT];
  unsigned long mySignalDeadDelays[SIGNAL_TIMER_CNT];
  unsigned long myTaskStartTime;
  unsigned long myNow;
  void (*mySignalACallback)(void);
  void (*mySignalBCallback)(void);
  Progress myProgress;
  State myTaskState;
  uint16_t myTasktime;
};

#endif  
