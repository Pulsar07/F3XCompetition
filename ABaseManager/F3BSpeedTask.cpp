#include <Logger.h>
#include "F3BSpeedTask.h"



F3BSpeedTask::F3BSpeedTask() {
  stop();
}

/**
 * return the speed of the given leg in m/s
 */
float F3BSpeedTask::getLegSpeed(int8_t aLeg) {
   return ((float) F3B_SPEED_LEG_LENGTH * 1000) / getLegTime(aLeg); 
}

/**
 * return the measured of the given leg in ms
 */
unsigned long F3BSpeedTask::getLegTime(int8_t aLeg) {
  unsigned long retVal = F3B_TIME_NOT_SET;

  if (1 <= aLeg && aLeg <= 4 ) {
    int idx;
    switch (aLeg) {
      case 1:
        idx = B_LINE_CROSSED_1;
        break;
      case 2:
        idx = A_LINE_CROSSED_2;
        break;
      case 3:
        idx = B_LINE_CROSSED_2;
        break;
      case 4:
        idx = A_LINE_CROSSED_FINAL;
        break;
    }
    if (mySignalTimeStamps[idx] != F3B_TIME_NOT_SET) {
      retVal = mySignalTimeStamps[idx] - mySignalTimeStamps[idx-1];
    }
  } 
  return retVal;
}


/** 
 * return the final speed in m/s 
 */
float F3BSpeedTask::getFinalSpeed() {
   return (4.0f * 1000 * F3B_SPEED_LEG_LENGTH) / F3BSpeedTask::getFlightTime(A_LINE_CROSSED_FINAL);
}

uint8_t F3BSpeedTask::getDeadDistance(int8_t aSignal) {
  return getLegSpeed(aSignal-1) * getDeadDelay(aSignal) / 1000;
}

/** 
 * return the delay of dead zone turn time in ms
 */
unsigned long F3BSpeedTask::getDeadDelay(int8_t aSignal) {
  unsigned long retVal = 0;
  if (mySignalDeadDelays[aSignal] != 0) {
    switch ((F3BSpeedSignalTypes) aSignal) {
      case B_LINE_CROSSED_1:
      case A_LINE_CROSSED_2:
      case B_LINE_CROSSED_2:
        retVal = mySignalDeadDelays[aSignal] - mySignalTimeStamps[aSignal];
        break;
    }
  }
  return retVal;
} 

void F3BSpeedTask::init(void (*aACallBack)(), void (*aBCallBack)()) {
  mySignalACallback = aACallBack;
  mySignalBCallback = aBCallBack;
}

/**
 * get the speed flight time in milliseconds depending on aIdx
  NOT_STARTED = -1,
  A_LINE_REVERSED,       // 0
  A_LINE_CROSSED_1,
  B_LINE_CROSSED_1,
  A_LINE_CROSSED_2,
  B_LINE_CROSSED_2,
  A_LINE_CROSSED_FINAL,   // 5
  RUNNING_VALUE,
 */
unsigned long F3BSpeedTask::getFlightTime(int8_t aIdx) {
  unsigned long retVal = F3B_TIME_NOT_SET;
  if (aIdx < -1 || aIdx > RUNNING_VALUE) {
    logMsg(ERROR, "ERROR: program error 004");
    myTaskState = TaskError;
    logMsg(ERROR, "F3BSpeedTask::TaskError" ); 
    return -1;
  }
  if (aIdx == -1) { // get last time signaled time
    if (getCurrentSignal() > A_LINE_CROSSED_1 ) { // speed flight started
      retVal = mySignalTimeStamps[getCurrentSignal()] - mySignalTimeStamps[A_LINE_CROSSED_1];
    } else {
      logMsg(ERROR, "ERROR: getFlightTime no time set 2");
    }
  } else if (aIdx == RUNNING_VALUE) {
    if (getCurrentSignal() == A_LINE_CROSSED_FINAL) {
      retVal = mySignalTimeStamps[getCurrentSignal()] - mySignalTimeStamps[A_LINE_CROSSED_1];
    } else {
      retVal = millis() - mySignalTimeStamps[A_LINE_CROSSED_1];
    }
  } else {
    // aIdx
    // 0: reverse A crossing
    // 1: first A crossing
    // 2: first B crossing
    // 3: second A crossing
    // 4: second B crossing
    // 5: final A crossing
    if (mySignalTimeStamps[aIdx] == F3B_TIME_NOT_SET) {
      retVal = F3B_TIME_NOT_SET;
    } else 
    if (aIdx == A_LINE_REVERSED) {
      retVal = mySignalTimeStamps[aIdx] - myTaskStartTime;
    } else {
      retVal = mySignalTimeStamps[aIdx] - mySignalTimeStamps[A_LINE_CROSSED_1];
    }
  }
  return retVal;
  
}

void F3BSpeedTask::timeOverflow() {
  if (getTaskState() != TaskRunning) {
    return;
  }
  logMsg(INFO, "F3BSpeedTask::TaskTimeOverflow" ); 
  myTaskState = TaskTimeOverflow;
}

void F3BSpeedTask::signal(F3BSignalType aType) {
  logMsg(INFO, String("F3BSpeedTask::signal(") + (aType == SignalA?'A':'B')+ String(")"));
  if (myTaskState != TaskRunning) {
    logMsg(ERROR, String(" not allowed in state ") + String(myTaskState));
    return;
  }

  if (aType == SignalA) {
    switch (myCurrentSignal) {
      case NOT_STARTED: // REGULAR : reversed A line crossing, 
        myCurrentSignal = A_LINE_REVERSED;
        mySignalTimeStamps[myCurrentSignal] = millis();
        mySignalACallback();
        break;
      case A_LINE_REVERSED: // REGULAR : A line crossing first time, start of first leg
        myCurrentSignal = A_LINE_CROSSED_1;
        mySignalTimeStamps[myCurrentSignal] = millis();
        mySignalACallback();
        break;
      case A_LINE_CROSSED_1: // in case of reflight
        mySignalTimeStamps[A_LINE_REVERSED] = mySignalTimeStamps[A_LINE_CROSSED_1];
        myCurrentSignal = A_LINE_CROSSED_1;
        mySignalTimeStamps[myCurrentSignal] = millis();
        mySignalACallback();
        break;
      case B_LINE_CROSSED_1: // REGULAR : A line crossing second time, start of third leg
        myCurrentSignal = A_LINE_CROSSED_2;
        mySignalTimeStamps[myCurrentSignal] = millis();
        mySignalACallback();
        break;
      case A_LINE_CROSSED_2: // TRAINING: A turn delay signal
        mySignalDeadDelays[A_LINE_CROSSED_2] = millis();
        break;
      case B_LINE_CROSSED_2: // REGULAR : A line crossing third time
        myCurrentSignal = A_LINE_CROSSED_FINAL;
        mySignalTimeStamps[myCurrentSignal] = millis();
        myTaskState = TaskFinished;
        mySignalACallback();
        logMsg(INFO, "F3BSpeedTask::TaskFinished" ); 
        break;
    }
  } else if (aType == SignalB) {
    switch (myCurrentSignal) {
      case A_LINE_CROSSED_1: // REGULAR : B line crossing first time, start of second leg
        myCurrentSignal = B_LINE_CROSSED_1;
        mySignalTimeStamps[myCurrentSignal] = millis();
        mySignalBCallback();
        break;
      case B_LINE_CROSSED_1: // TRAINING: first B turn delay signal
        mySignalDeadDelays[B_LINE_CROSSED_1] = millis();
        break;
      case A_LINE_CROSSED_2: // REGULAR : B line crossing second time, start of fourth leg
        myCurrentSignal = B_LINE_CROSSED_2;
        mySignalTimeStamps[myCurrentSignal] = millis();
        mySignalBCallback();
        break;
      case B_LINE_CROSSED_2: // TRAINING: second B turn delay signal
        mySignalDeadDelays[B_LINE_CROSSED_2] = millis();
        break;
    }
  }
}

void F3BSpeedTask::stop() {
  logMsg(INFO, "F3BSpeedTask::TaskWaiting" ); 
  myTaskState = TaskWaiting;
  resetSignals();
  myTaskStartTime = 0;
}

void F3BSpeedTask::start() {
  switch (myTaskState) {
    case TaskWaiting:
      logMsg(INFO, "start() " );
      resetSignals();
      myTaskStartTime = millis();
      myTaskState = TaskRunning;
      logMsg(INFO, "F3BSpeedTask::TaskRunning" );
      break;
  }
}

void F3BSpeedTask::resetSignals() {
  logMsg(INFO, "resetSignals() " );
  myCurrentSignal = NOT_STARTED;
  for (int i=0; i<SIGNAL_TIMER_CNT; i++) {
    mySignalTimeStamps[i] = F3B_TIME_NOT_SET;
    mySignalDeadDelays[i] = 0;
  }
}

long F3BSpeedTask::getTaskTime() {
  long retVal = 0;
  switch (myTaskState) {
    case TaskRunning:
      // retVal = millis() - myTaskStartTime;
      retVal = myTaskStartTime+F3B_SPEED_TASK_MAX_TASK_TIME*1000-millis();
      if (retVal < 0) {
        retVal = 0;
      }
      break;
    case TaskFinished:
      // retVal = mySignalTimeStamps[A_LINE_CROSSED_FINAL]  - myTaskStartTime;
      retVal = myTaskStartTime+F3B_SPEED_TASK_MAX_TASK_TIME*1000 - mySignalTimeStamps[A_LINE_CROSSED_FINAL] ;
      break;
    case TaskTimeOverflow:
      retVal = 0; 
      break;
  }
  return retVal;
}

F3BSpeedSignalTypes F3BSpeedTask::getCurrentSignal() {
  return myCurrentSignal;
}

F3BSpeedTaskState F3BSpeedTask::getTaskState() {
  return myTaskState;
}

void F3BSpeedTask::update() {
  if ( myTaskState == TaskRunning && getTaskTime() == 0 ) {
    logMsg(ERROR, "Task time overflow");
    timeOverflow();
  }
}
