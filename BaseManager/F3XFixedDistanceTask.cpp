#include <Logger.h>
#include "F3XFixedDistanceTask.h"

/*
 * 18.08.2024 RS: added F3X_IN_AIR_A_REV_CROSSING state
 */

/**
 * constructor for a F3X distance task with a fixed number of legs aLegNumberMax and a leg lenght aLegLength
 */
F3XFixedDistanceTask::F3XFixedDistanceTask(F3XType aType) {
  mySignalAListener = nullptr;
  mySignalBListener = nullptr;
  myStateChangeListener = nullptr;
  myTimeProceedingListener = nullptr;
  myTasktime = 180; // default tasktime 3 minutes
  myType = aType;
  myLaunchTime = 0L;
  switch (myType) {
    case F3BSpeedType:
      myLegLength = 150;
      myLegNumberMax = 4;
      break;
    case F3FType:
      myLegLength = 100;
      myLegNumberMax = 10;
      break;
  }
  mySignalTimeStamps = (unsigned long *) malloc(sizeof(unsigned long) * (myLegNumberMax+1));
  myDeadDistanceTimeStamp = (unsigned long *) malloc(sizeof(unsigned long) * (myLegNumberMax-1));
  myLoopTaskNum = 0;
  myLoopTaskEnabled = false;
  stop();
}

boolean F3XFixedDistanceTask::getLoopTasksEnabled() {
  return myLoopTaskEnabled;
}

void F3XFixedDistanceTask::setLoopTasksEnabled(boolean aEnable) {
  if (myLoopTaskEnabled != aEnable) {
    myLoopTaskEnabled = aEnable;
    myLoopTaskNum=0;
  }
}
uint8_t F3XFixedDistanceTask::getLoopTaskNum() {
  return myLoopTaskNum;
}
unsigned long F3XFixedDistanceTask::getLastLoopTaskCourseTime() {
  return myLastLoopTaskCourseTime;
}

F3XFixedDistanceTask::F3XType F3XFixedDistanceTask::getType() {
  return myType;
}

uint8_t F3XFixedDistanceTask::getLegNumberMax() {
  return myLegNumberMax;
}

/* rule 5.8.7 F3_soaring */
void F3XFixedDistanceTask::setLegLength(uint16_t aLength) {
  if (myType == F3FType && aLength >= 80 && aLength <=100) {
    myLegLength = aLength;
  }
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
  } else if (aIdx == F3X_LEG_MIN) {
    for (int i=0; (i<myLegNumberMax && mySignalTimeStamps[i+1] != -1UL) ; i++) {
      F3XLeg leg = getLeg(i);
      if (leg.time < retVal.time) {
        retVal = leg;
      }
    }
  } else if (aIdx == F3X_LEG_MAX) {
    retVal.time = 0L;
    for (int i=0; (i<myLegNumberMax && mySignalTimeStamps[i+1] != -1UL) ; i++) {
      F3XLeg leg = getLeg(i);
      if (leg.time > retVal.time) {
        retVal = leg;
      }
    }
  } else if (aIdx == F3X_LEG_AVG) {
    retVal.time = 0L;
    int i;
    for (i=0; (i<myLegNumberMax && mySignalTimeStamps[i+1] != -1UL) ; i++) {
      F3XLeg leg = getLeg(i);
      retVal.time += leg.time;
      retVal.speed += leg.speed;
      retVal.deadTime += leg.deadTime;
      retVal.deadDistance += leg.deadDistance;
    }
    retVal.idx = F3X_LEG_AVG;
    retVal.time         = retVal.time / i;
    retVal.speed        = retVal.speed / i;
    retVal.deadTime     = retVal.deadTime / i;
    retVal.deadDistance = retVal.deadDistance / i;
  }
  return retVal;
}

/** 
 * return the final speed in m/s 
 */
float F3XFixedDistanceTask::getFinalSpeed() {
   return (((float) myLegNumberMax) * 1000 * myLegLength) / getCourseTime(F3X_GFT_FINAL_TIME);
}

void F3XFixedDistanceTask::addSignalAListener( void (*aListener)()) {
  mySignalAListener = aListener;
}

void F3XFixedDistanceTask::addSignalBListener( void (*aListener)()) {
  mySignalBListener = aListener;
}

void F3XFixedDistanceTask::addStateChangeListener( void (*aListener)(State)) {
  myStateChangeListener = aListener;
}

void F3XFixedDistanceTask::addTimeProceedingListener( void (*aListener)()) {
  myTimeProceedingListener = aListener;
}

/**
 * get the course time in milliseconds depending on the given signal index argument aSignalIdx:
  aSignalIdx=F3X_GFT_LAST_SIGNALLED_TIME : return the time from the first A-line crossing till the last signalled crossing
  aSignalIdx=F3X_GFT_RUNNING_TIME : return the time from the first A-line crossing till now
  aSignalIdx=F3X_GFT_FINAL_TIME : return the time from the first A-line crossing till the final A-line crossing
  aSignalIdx=0..myLegNumberMax : return the time from the first A-line crossing till the aSignalIdx crossing 
  if the requested time is not yet reached -1 is returned
 */
unsigned long F3XFixedDistanceTask::getCourseTime(int8_t aSignalIdx) {
  unsigned long retVal = F3X_TIME_NOT_SET;

  if (aSignalIdx < F3X_GFT_MIN_ARG || aSignalIdx > getSignalledLegCount() || mySignalTimeStamps[0] == -1UL) {
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
  logMsg(LOG_MOD_SIG, INFO, String("FDT::timeOverflow"));
  setTaskState(TaskTimeOverflow);
}

/**
 * method should be called if a signal event is given by a controller or local switch
 */
void F3XFixedDistanceTask::signal(Signal aType) {
  logMsg(LOG_MOD_SIG, INFO, String("FDT::signal(") + (aType == SignalA?'A':'B')+ String(")"));
  if (myTaskState != TaskRunning) {
    logMsg(LOG_MOD_SIG, INFO, String(" not allowed in state ") + String(myTaskState));
    return;
  }
  if (mySignalAListener == nullptr) {
    logMsg(LOG_MOD_SIG, ERROR, String("mySignalAListener is null !!! "));
    return;
  }
  if (mySignalBListener == nullptr) {
    logMsg(LOG_MOD_SIG, ERROR, String("mySignalBListener is null !!! "));
    return;
  }

  if (aType == SignalA) {
    if (myType == F3BSpeedType 
        && ( mySignalledLegCount == F3X_COURSE_INIT // (-3)
             || mySignalledLegCount == F3X_COURSE_STARTED) ) { // (0) in case of reflight or first A-Line reverse crossing
      mySignalledLegCount = F3X_COURSE_STARTED;  // =0 legs , but started, first A-Line crossing
      mySignalTimeStamps[mySignalledLegCount] = millis();
      mySignalAListener();
    } else 
    if (myType == F3FType 
        && mySignalledLegCount == F3X_COURSE_INIT // (-3)
      ) { 
      mySignalledLegCount = F3X_IN_AIR;  // (-2) model started but not yet in course and not yet overfly A-Line
      inAir();
      mySignalAListener();  // force a A-Line signal
    } else 
    if (myType == F3FType 
        && mySignalledLegCount == F3X_IN_AIR // (-2)
      ) { 
      mySignalledLegCount = F3X_IN_AIR_A_REV_CROSSING;  // (-1) model in air and crossed A-Line in reverse direction to B-Line
      mySignalAListener();  // force a A-Line signal
    } else 
    if (myType == F3FType 
        && mySignalledLegCount == F3X_IN_AIR_A_REV_CROSSING // (-1)
      ) { 
      mySignalledLegCount = F3X_COURSE_STARTED;  // 0 legs , but started, first A-Line crossing
      if (mySignalTimeStamps[mySignalledLegCount] == -1UL) {
        // only set if not auto set 
        mySignalTimeStamps[mySignalledLegCount] = millis();
      }
      mySignalAListener();  // force a A-Line signal
    } else 
    if (mySignalledLegCount > 0) { // task is ongoing
      if (mySignalledLegCount%2 == 1) {  // REGULAR : A line crossing n.th time, start of  1/3/5/... leg
        mySignalledLegCount++;
        mySignalTimeStamps[mySignalledLegCount] = millis();
        if ( mySignalledLegCount == myLegNumberMax) { // last leg finished
          logMsg(LOG_MOD_SIG, INFO, String("FDT::TaskFinised"));
          setTaskState(TaskFinished);
        }  
        mySignalAListener();
      } else { // NO crossing turn, additional A signal is used for dead time/distance measurement
        myDeadDistanceTimeStamp[mySignalledLegCount-1] = millis();
      }
    }
  } else if (aType == SignalB) {
    if (mySignalledLegCount >= F3X_COURSE_STARTED) { // task is ongoing
      if (mySignalledLegCount%2 == 0) {  // REGULAR : B line crossing n.th time, start of 2/4/6/.. leg 
        mySignalledLegCount++;
        mySignalTimeStamps[mySignalledLegCount] = millis();
        mySignalBListener();
      } else { // NO crossing turn, additional B signal is used for dead time/distance measurement
        myDeadDistanceTimeStamp[mySignalledLegCount-1] = millis();
      }
    }
  }
}

void F3XFixedDistanceTask::startCourseTime() {
  mySignalTimeStamps[0] = millis();
}

void F3XFixedDistanceTask::stop() {
  if (getTaskState() == TaskFinished && getLoopTasksEnabled() ) {
    myLastLoopTaskCourseTime = getCourseTime(F3X_GFT_FINAL_TIME);
  } else {
    myLastLoopTaskCourseTime = 0;
  }
  logMsg(LOG_MOD_SIG, INFO, String("FDT::TaskWaiting"));
  setTaskState(TaskWaiting);
  resetSignals();
  myTaskStartTime = 0;
}

void F3XFixedDistanceTask::start() {
  logMsg(LOG_MOD_SIG, INFO, "FDT: start");
  switch (myTaskState) {
    case TaskWaiting:
      resetSignals();
      myTaskStartTime = millis();
      logMsg(LOG_MOD_SIG, INFO, String("FDT::TaskRunning"));
      setTaskState(TaskRunning);
      if (getLoopTasksEnabled() && myLastLoopTaskCourseTime != 0) {
        myLoopTaskNum++;
      }
      break;
  }
}

/** in F3F the time between launch and starting the course (crossing A-Line in direction to B-Line) is 30s
 */
void F3XFixedDistanceTask::inAir() {
  switch (myTaskState) {
    case TaskWaiting:
      start();
      myLaunchTime = millis();
      myListenerIndication = 0;
      break;
    case TaskRunning:
      myLaunchTime = millis();
      myListenerIndication = 0;
      break;
  }
  logMsg(LOG_MOD_SIG, INFO, String("FDT::inAir"));
}

/**
 * get the in air time in ms
 */
unsigned long F3XFixedDistanceTask::getInAirTime() {
  unsigned long retVal = 0L;

  if (getSignalledLegCount() >= F3X_COURSE_STARTED) {
    retVal = mySignalTimeStamps[F3X_COURSE_STARTED] - myLaunchTime;
  } else 
  if (myLaunchTime > 0L) {
     retVal = millis() - myLaunchTime;
  }
  return retVal;
}

void F3XFixedDistanceTask::resetSignals() {
  mySignalledLegCount = F3X_COURSE_INIT;
  for (int i=0; i<myLegNumberMax+1; i++) {
    mySignalTimeStamps[i] = -1UL;
  }
  for (int i=0; i<myLegNumberMax-1; i++) {
    myDeadDistanceTimeStamp[i] = 0;
  }
  myLaunchTime = 0L;
}


/**
 * return the remaining Tasktime in ms
 */
long F3XFixedDistanceTask::getRemainingTasktime() {
  long retVal = 0;
  switch (myTaskState) {
    case TaskRunning:
      if ( myLaunchTime > 0L) {
        retVal = myTaskStartTime+myTasktime*1000-myLaunchTime;
      } else {
        retVal = myTaskStartTime+myTasktime*1000-millis();
      }
      if (retVal <= 0) {
        retVal = 0;
      }
      break;
    case TaskFinished:
      if ( myLaunchTime > 0L) {
        retVal = myTaskStartTime+myTasktime*1000-myLaunchTime;
      } else {
        retVal = myTaskStartTime+myTasktime*1000 - mySignalTimeStamps[myLegNumberMax];
      }
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
  switch (myType) {
    case F3BSpeedType:
      if ( myTaskState == TaskRunning 
           && getRemainingTasktime() == 0 ) {
        logMsg(LOG_MOD_SIG, INFO, String(F("FDT: F3B Speed Task time overflow")));
        timeOverflow();
      }
      break;
    case F3FType:
      if ( myTaskState == TaskRunning 
           && mySignalledLegCount == F3X_COURSE_INIT
           && getRemainingTasktime()/1000 == 0 ) {
        logMsg(LOG_MOD_SIG, INFO, String(F("FDT: F3F Task time overflow")));
        timeOverflow();
      }
      if ( myTaskState == TaskRunning && getSignalledLegCount() == F3X_COURSE_INIT) {
        uint8_t taskTimeSecs = getRemainingTasktime()/1000;  // 30,29,28,...,1
        if (taskTimeSecs <= 5 && taskTimeSecs != myListenerIndication) {
          switch (taskTimeSecs) {
            case 5:
            case 4:
            case 3:
            case 2:
            case 1:
              myListenerIndication = taskTimeSecs;
              if (myTimeProceedingListener != nullptr) {
                logMsg(LOG_MOD_SIG, DEBUG, String(F("FDT: task time indication: ")) + String(taskTimeSecs));
                myTimeProceedingListener();
              } else {
                logMsg(LOG_MOD_SIG, ERROR, String(F("FDT: myTimeProceedingListener is null !!! ")));
              }
              break;
           }
        }
      } else 
      // F3F in air time handling 
      if ( myTaskState == TaskRunning && getSignalledLegCount() < F3X_COURSE_STARTED && myLaunchTime != 0L) {
        uint8_t inAirSecs = getInAirTime()/1000;  // 0,1,2,3,4,5,6, ... 30
        if (inAirSecs <= 30 && inAirSecs != myListenerIndication) {
          switch (inAirSecs) {
            case 5:
            case 10:
            case 15:
            case 20:
            case 25:
            case 26:
            case 27:
            case 28:
            case 29:
              myListenerIndication = inAirSecs;
              if (myTimeProceedingListener != nullptr) {
                logMsg(LOG_MOD_SIG, DEBUG, String(F("FDT: inAirIndication: ")) + String(inAirSecs));
                myTimeProceedingListener();
              } else {
                logMsg(LOG_MOD_SIG, ERROR, String(F("FDT: myTimeProceedingListener is null !!! ")));
              }
              break;
            case 30:
              myListenerIndication = inAirSecs;
              if (myTimeProceedingListener != nullptr) {
                logMsg(LOG_MOD_SIG, DEBUG, String(F("FDT: inAirIndication: ")) + String(inAirSecs));
                myTimeProceedingListener();
              } else {
                logMsg(LOG_MOD_SIG, ERROR, String(F("FDT: myTimeProceedingListener is null !!! ")));
              }
              logMsg(LOG_MOD_SIG, DEBUG, String(F("FDT: AutoASignal:inAirIndication: ")) + String(inAirSecs));
              startCourseTime();
              break;
          }
        }
      }
      break;
  }
}

void F3XFixedDistanceTask::setTaskState(State aTaskState) {
  logMsg(LOG_MOD_SIG, DEBUG, String("FDT::setTaskState: ") + String(aTaskState));
  myTaskState = aTaskState;
  if (myStateChangeListener != nullptr) {
    myStateChangeListener(myTaskState);
  }
}


/**
  return a leg time literal in format 
    00:09.41;05.39s;100km/h;00.76s;21m;
    representing turn-time/leg-time/leg-speed/dead-time/dead-distance 
*/
char* F3XFixedDistanceTask::getLegTimeString(
   unsigned long aTime, unsigned long aLegTime, uint16_t aLegSpeed,  
   unsigned long aDeadDelay, uint8_t aDeadDistance, 
   char aSeparator, bool aForceDeadData, bool aShowUnits) {
  int tseconds = aTime / 1000;
  int tminutes = tseconds / 60;
  int thours = tminutes / 60;
  int millisec  = aTime % 1000;
  int centies  = millisec/10;
  int seconds = tseconds % 60;
  int minutes = tminutes % 60;
  static char buffer[35];
  String format;
  if (aTime == F3X_TIME_NOT_SET ) {
    if (getTaskState() == F3XFixedDistanceTask::TaskTimeOverflow) {
      sprintf(&buffer[0],"XX:XX.XX : task time overflow");  // len=29+1
    } else {
      sprintf(&buffer[0],"__:__.__");
    }
  } else {
    format = F("%02d:%02d.%02d");
    if (aShowUnits) {
      format = F("%02d:%02d.%02dm:s:ms");
    }
    sprintf(&buffer[0],format.c_str(), minutes, seconds, centies);  // len=8
    if (aLegTime != F3X_TIME_NOT_SET) {
      if (aLegSpeed > 0) {
        format = F("%c%02d.%02d%c%d");
        if (aShowUnits) {
          format = F("%c%02d.%02ds%c%dkm/h");
        }
        sprintf(&buffer[strlen(buffer)], format.c_str(), aSeparator, aLegTime / 1000, aLegTime/10%100, aSeparator, aLegSpeed ); // len=8+15=23
      } else {
        format = F("%c%02d.%02d");
        if (aShowUnits) {
          format = F("%c%02d.%02ds");
        }
        sprintf(&buffer[strlen(buffer)], format.c_str(), aSeparator, aLegTime / 1000, aLegTime/10%100); // len=8+7=15
      }
    }
    if (aDeadDelay != 0 || aForceDeadData) {
      format = F("%c%02d.%02d%c%d");
      if (aShowUnits) {
        format = F("%c%02d.%02ds%c%dm");
      }
      sprintf(&buffer[strlen(buffer)], format.c_str(), aSeparator, aDeadDelay / 1000, aDeadDelay/10%100, aSeparator, aDeadDistance); // len=23+8=31+1
    }
  }
  return buffer;
}


/**
  return a time literal with the format Hours:Minutes:Seconds 00:12:23
*/
char* F3XFixedDistanceTask::getHMSTimeStr(unsigned long aTime, boolean aShort) {
  int tseconds = aTime / 1000;
  int tminutes = tseconds / 60;
  int thours = tminutes / 60;
  int millisec  = aTime % 1000;
  int seconds = tseconds % 60;
  int minutes = tminutes % 60;
  int hours = thours % 60;
  static char buffer[12];
  if (aShort) {
    if (aTime == F3X_TIME_NOT_SET ) {
      sprintf(buffer,"__:__");
    } else {
      sprintf(buffer,"%02d:%02d", minutes, seconds); 
    }
  } else {
    sprintf(buffer,"%02d:%02d:%02d",hours, minutes, seconds); // len=8+1
  }
  return buffer;
}
