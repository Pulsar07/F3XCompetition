#include <Logger.h>
#include "F3XFixedDistanceTask.h"


/**
 * constructor for a F3X distance task with a fixed number of legs aLegNumberMax and a leg lenght aLegLength
 */
F3XFixedDistanceTask::F3XFixedDistanceTask(uint16_t aLegLength, uint8_t aLegNumberMax) {
  mySignalACallback = nullptr;
  mySignalBCallback = nullptr;
  myStateChangeCallback = nullptr;
  myTasktime = 180; // default tasktime 3 minutes
  myLegLength = aLegLength;
  myLegNumberMax = aLegNumberMax;
  mySignalTimeStamps = (unsigned long *) malloc(sizeof(unsigned long) * (myLegNumberMax+1));
  myDeadDistanceTimeStamp = (unsigned long *) malloc(sizeof(unsigned long) * (myLegNumberMax-1));
  stop();
}

uint8_t F3XFixedDistanceTask::getLegNumberMax() {
  return myLegNumberMax;
}

uint16_t F3XFixedDistanceTask::getLegLength() {
  return myLegLength;
}

/**
 * set the overall task time of this distance task
 */
void F3XFixedDistanceTask::setTasktime(uint16_t aTasktimeInSeconds) {
  myTasktime = aTasktimeInSeconds;
}

F3XLeg F3XFixedDistanceTask::getLeg(int8_t aIdx) {
  F3XLeg retVal;
  retVal.valid = false;
  retVal.idx = aIdx;
  retVal.time = -1UL;
  retVal.speed = 0.0f;
  retVal.deadTime = 0;
  retVal.deadDistance = 0;
  if (aIdx >= 0 && aIdx < myLegNumberMax && mySignalTimeStamps[aIdx+1] != -1UL) {
    retVal.valid = true;
    retVal.time = mySignalTimeStamps[aIdx+1] - mySignalTimeStamps[aIdx];
    retVal.speed = ((float) myLegLength * 1000) / retVal.time;
    if (aIdx < myLegNumberMax-1 && myDeadDistanceTimeStamp[aIdx] != 0) { // last leg has no turn, so no delay is possible
      retVal.deadTime = myDeadDistanceTimeStamp[aIdx] - mySignalTimeStamps[aIdx+1];
      retVal.deadDistance = retVal.speed * retVal.deadTime / 1000;
    }
  }
  return retVal;
}

/** 
 * return the final speed in m/s 
 */
float F3XFixedDistanceTask::getFinalSpeed() {
   return (((float) myLegNumberMax) * 1000 * myLegLength) / getFlightTime(F3X_GFT_FINAL_TIME);
}

void F3XFixedDistanceTask::init(void (*aACallBack)(), void (*aBCallBack)()) {
  mySignalACallback = aACallBack;
  mySignalBCallback = aBCallBack;
}

void F3XFixedDistanceTask::addStateChangeCallback( void (*aStateChangeCallback)(State)) {
  myStateChangeCallback = aStateChangeCallback;
}

/**
 * get the overall flight time in milliseconds depending on the given signal index argument aSignalIdx:
  aSignalIdx=F3X_GFT_LAST_SIGNALLED_TIME : return the time from the first A-line crossing till the last signalled crossing
  aSignalIdx=F3X_GFT_RUNNING_TIME : return the time from the first A-line crossing till now
  aSignalIdx=F3X_GFT_FINAL_TIME : return the time from the first A-line crossing till the final A-line crossing
  aSignalIdx=0..myLegNumberMax : return the time from the first A-line crossing till the aSignalIdx crossing 
  if the requested time is not yet reached -1 is returned
 */
unsigned long F3XFixedDistanceTask::getFlightTime(int8_t aSignalIdx) {
  unsigned long retVal = F3X_TIME_NOT_SET;

  if (aSignalIdx < F3X_GFT_MIN_ARG || aSignalIdx > getSignalledLegCount()) {
    return retVal;
  }
  if (aSignalIdx == F3X_GFT_LAST_SIGNALLED_TIME) { // get last time signalled time
    if (getSignalledLegCount() >= 0 ) { // flight started
      retVal = mySignalTimeStamps[getSignalledLegCount()] - mySignalTimeStamps[0];
    } else {
    }
  } else if (aSignalIdx == F3X_GFT_RUNNING_TIME) {
    if (getSignalledLegCount() == myLegNumberMax) {
      retVal = mySignalTimeStamps[myLegNumberMax] - mySignalTimeStamps[0];
    } else {
      retVal = millis() - mySignalTimeStamps[0];
    }
  } else if (aSignalIdx == F3X_GFT_FINAL_TIME) {
    if (getSignalledLegCount() == myLegNumberMax) {
      retVal = mySignalTimeStamps[myLegNumberMax] - mySignalTimeStamps[0];
    }
  } else {
    retVal = mySignalTimeStamps[aSignalIdx] - mySignalTimeStamps[0];
  }
  return retVal;
}

void F3XFixedDistanceTask::timeOverflow() {
  if (getTaskState() != TaskRunning) {
    return;
  }
  logMsg(INFO, "F3XFixedDistanceTask::TaskTimeOverflow" ); 
  setTaskState(TaskTimeOverflow);
}

void F3XFixedDistanceTask::signal(Signal aType) {
  logMsg(INFO, String("F3XFixedDistanceTask::signal(") + (aType == SignalA?'A':'B')+ String(")"));
  if (myTaskState != TaskRunning) {
    logMsg(ERROR, String(" not allowed in state ") + String(myTaskState));
    return;
  }

  if (aType == SignalA) {
    if (   mySignalledLegCount == F3X_PROGRESS_NOT_STARTED
        || mySignalledLegCount == 0) { // in case of reflight or first A-Line reverse crossing
      mySignalledLegCount = 0;  // 0 legs , but started, first A-Line crossing
      mySignalTimeStamps[mySignalledLegCount] = millis();
      if (mySignalACallback != nullptr) {
        mySignalACallback();
      } else {
        logMsg(ERROR, String("mySignalACallback is null !!! "));
      }
    } else 
    if (mySignalledLegCount > 0) { // task is ongoing
      if (mySignalledLegCount%2 == 1) {  // REGULAR : A line crossing n.th time, start of  1/3/5/... leg
        mySignalledLegCount++;
        mySignalTimeStamps[mySignalledLegCount] = millis();
        if ( mySignalledLegCount == myLegNumberMax) { // last leg finished
          setTaskState(TaskFinished);
          logMsg(INFO, "F3XFixedDistanceTask::TaskFinished" ); 
        }  
        if (mySignalACallback != nullptr) {
          mySignalACallback();
        } else {
          logMsg(ERROR, String("mySignalACallback is null !!! "));
        }
      } else { // NO crossing turn, additional A signal is used for dead time/distance measurement
        myDeadDistanceTimeStamp[mySignalledLegCount-1] = millis();
      }
    }
  } else if (aType == SignalB) {
    if (mySignalledLegCount > F3X_PROGRESS_NOT_STARTED) { // task is ongoing
      if (mySignalledLegCount%2 == 0) {  // REGULAR : B line crossing n.th time, start of 2/4/6/.. leg 
        mySignalledLegCount++;
        mySignalTimeStamps[mySignalledLegCount] = millis();
        if (mySignalBCallback != nullptr) {
          mySignalBCallback();
        } else {
          logMsg(ERROR, String("mySignalBCallback is null !!! "));
        }
      } else { // NO crossing turn, additional B signal is used for dead time/distance measurement
        myDeadDistanceTimeStamp[mySignalledLegCount-1] = millis();
      }
    }
  }
}


void F3XFixedDistanceTask::stop() {
  setTaskState(TaskWaiting);
  resetSignals();
  myTaskStartTime = 0;
}

void F3XFixedDistanceTask::start() {
  switch (myTaskState) {
    case TaskWaiting:
      resetSignals();
      myTaskStartTime = millis();
      setTaskState(TaskRunning);
      break;
  }
}

void F3XFixedDistanceTask::resetSignals() {
  mySignalledLegCount = F3X_LEG_COUNT_NOT_SET;
  for (int i=0; i<myLegNumberMax+1; i++) {
    mySignalTimeStamps[i] = -1UL;
  }
  for (int i=0; i<myLegNumberMax-1; i++) {
    myDeadDistanceTimeStamp[i] = 0;
  }
}

long F3XFixedDistanceTask::getRemainingTasktime() {
  long retVal = 0;
  switch (myTaskState) {
    case TaskRunning:
      retVal = myTaskStartTime+myTasktime*1000-millis();
      if (retVal < 0) {
        retVal = 0;
      }
      break;
    case TaskFinished:
      retVal = myTaskStartTime+myTasktime*1000 - mySignalTimeStamps[myLegNumberMax];
      break;
    case TaskTimeOverflow:
      retVal = 0; 
      break;
  }
  return retVal;
}

/**
 * return the number of signalled legs
 */
int8_t F3XFixedDistanceTask::getSignalledLegCount() {
  return mySignalledLegCount;
}

F3XFixedDistanceTask::State F3XFixedDistanceTask::getTaskState() {
  return myTaskState;
}

void F3XFixedDistanceTask::update() {
  if ( myTaskState == TaskRunning && getRemainingTasktime() == 0 ) {
    logMsg(ERROR, "Task time overflow");
    timeOverflow();
  }
}

void F3XFixedDistanceTask::setTaskState(State aTaskState) {
  myTaskState = aTaskState;
  if (myStateChangeCallback != nullptr) {
    myStateChangeCallback(myTaskState);
  }
}
