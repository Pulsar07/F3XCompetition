#ifndef F3XFixedDistanceTaskData_h
#define F3XFixedDistanceTaskData_h

#include "F3XFixedDistanceTask.h"

class F3XFixedDistanceTaskData {
  private:
    String myProtocolFilePath;
    F3XFixedDistanceTask* myTask;
    uint16_t myTaskNum;
  public:
    F3XFixedDistanceTaskData(F3XFixedDistanceTask* aTask) {
      myTask = aTask;
      myTaskNum = 0;
      switch(myTask->getType()) {
        case F3XFixedDistanceTask::F3BSpeedType:
          myProtocolFilePath = F("/F3BSpeedData.csv");
          break;
        case F3XFixedDistanceTask::F3FType:
          myProtocolFilePath = F("/F3FTaskData.csv");
          break;
      }
    }

    void init() {
    }

    void remove() {
      logMsg(LOG_MOD_SIG, INFO, String(F("remove file: ")) + String(myProtocolFilePath.c_str()));
      if (!LittleFS.remove(myProtocolFilePath.c_str())) {
        logMsg(LOG_MOD_SIG, ERROR, String(F("remove file failed: ")) + String(myProtocolFilePath.c_str()));
      }
    }

    void writeHeader() {
      // check existing file
      logMsg(LOG_MOD_TASKDATA, INFO, String(F("write header: ")) + String(myProtocolFilePath.c_str()));
      logMsg(LOG_MOD_TASKDATA, INFO, String(F("check header file: ")) + String(myProtocolFilePath.c_str()));
      File file = LittleFS.open(myProtocolFilePath.c_str(), "r");
      if(!file) {
        // protocol file not existing, so create a empty one with header
        file = LittleFS.open(myProtocolFilePath.c_str(), "w");
        if(!file){
          logMsg(LOG_MOD_TASKDATA, ERROR, String(F("cannot create protocol file: ")) + String(myProtocolFilePath.c_str()));
        } else {
          logMsg(LOG_MOD_TASKDATA, INFO, String(F("write header to file: ")) + String(myProtocolFilePath.c_str()));
          String line;
          line += "No;";
          line += "Timestamp;";
          line += "Task;";
          line += "Leg length;";
          line += "Course time;";
          line += "Course Speed;";
          line += "Time 000m (A);";
          char buffer[20];
          for (uint8_t i=0; i<myTask->getLegNumberMax(); i++) { // e.g F3BSpeed: 0..3
            F3XLeg leg = myTask->getLeg(i);
            sprintf(buffer, "Course time %dm (%c);", ((i+1)*myTask->getLegLength()), (i%2==0) ? 'B':'A');
            // line += "Course time 150m (B);";
            line += String(buffer);
            sprintf(buffer, "Time %d.leg;", (i+1));
            // line += "Time 1.leg;";
            line += String(buffer);
            sprintf(buffer, "Speed %d.leg;", (i+1));
            // line += "Speed 1.leg;";
            line += String(buffer);
            if ( (i+1) != myTask->getLegNumberMax()) {
              line += "dead time;";
              line += "dead distance;";
            }
          }
          // /// 0
          // line += "Time 150m (B);";
          // line += "Time 1.leg;";
          // line += "Speed 1.leg;";
          // line += "deadtime;";
          // line += "dead distance;";
          // /// 1
          // line += "Time 300m (A);";
          // line += "Time 2.leg;";
          // line += "Speed 2.leg;";
          // line += "deadtime;";
          // line += "dead distance;";
          // line += "Time 450m (B);";
          // line += "Time 3.leg;";
          // line += "Speed 3.leg;";
          // line += "deadtime;";
          // line += "dead distance;";
          // line += "Time 600m (A);";
          // line += "Time 4.leg;";
          // line += "Speed 4.leg";
          if(!file.print(line)){
            logMsg(LOG_MOD_TASKDATA, ERROR, String(F("cannot write protocol file: ")) + String(myProtocolFilePath.c_str()));
          }
          line = "\n";
          line += ";"; // No
          line += "h:m:s;"; // Time
          line += ";";  // Task
          line += "meter;"; // Leg length
          line += "min:sec.msec;"; // course time
          line += "km/h;"; // course speed
          line += "min:sec.msec;"; // Time at 000m (0s)

          for (uint8_t i=0; i<myTask->getLegNumberMax(); i++) { // e.g F3BSpeed: 0..3
            line += "min:sec.msec;"; // course time at xxx m
            line += "sec.msec;";     // leg time
            line += "km/h;";         // leg speed
            if ( (i+1) != myTask->getLegNumberMax()) {
              // not for last leg
              line += "sec.msec;"; // dead time
              line += "meter;"; // dead distance
            }
          }

          if(!file.print(line)){
            logMsg(LOG_MOD_TASKDATA, ERROR, String(F("cannot write protocol file: ")) + String(myProtocolFilePath.c_str()));
          }
        }
        file.close();
      } else {
        file.close();
      } 
    }

    void writeData() {
      writeHeader();
      File file;
      file = LittleFS.open(myProtocolFilePath.c_str(), "a");
      logMsg(LOG_MOD_TASKDATA, INFO, String(F("write data log file: ")) + String(myProtocolFilePath.c_str()));
      if(!file){
        logMsg(LOG_MOD_TASKDATA, ERROR, String(F("cannot open protocol file for append: ")) + String(myProtocolFilePath.c_str()));
      } else {
        String taskName;
        switch(myTask->getType()) {
          case F3XFixedDistanceTask::F3BSpeedType:
            taskName = F("F3BSpeed");
            break;
          case F3XFixedDistanceTask::F3FType:
            taskName = F("F3F");
            break;
        }
        myTaskNum++;
        String line;
        line += "\n";
        line += myTaskNum;
        line += ";";
        line += F3XFixedDistanceTask::getHMSTimeStr(millis());
        line += ";";
        line += taskName;
        line += ";";
        line += myTask->getLegLength();
        line += ";";
        line += myTask->getLegTimeString(myTask->getCourseTime(F3X_GFT_FINAL_TIME), F3X_TIME_NOT_SET, 0, 0, 0);
        line += ";";
        line += myTask->getFinalSpeed()*3.6f;
        line += ";";
        line += myTask->getLegTimeString(myTask->getCourseTime(0),F3X_TIME_NOT_SET,0,0,0,';');
        line += ";";
        
        for (uint8_t i=0; i<myTask->getLegNumberMax(); i++) { // e.g F3BSpeed: 0..3
          F3XLeg leg = myTask->getLeg(i);
          line += myTask->getLegTimeString(
                    myTask->getCourseTime(i+1), 
                    leg.time,
                    leg.speed*3.6f, 
                    leg.deadTime,
                    leg.deadDistance, ';', (i==(myTask->getLegNumberMax()-1))?false:true, false);
          if (i != myTask->getLegNumberMax()) {
            line += ";";
          }
        }
        logMsg(LOG_MOD_TASKDATA, INFO, String(F("write data: ")) + String(myProtocolFilePath.c_str()));
        if(!file.print(line)){
          logMsg(LOG_MOD_TASKDATA, ERROR, String(F("cannot write protocol file: ")) + String(myProtocolFilePath.c_str()));
        }
        file.close();
      }
    }
};
#endif
