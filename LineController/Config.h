#ifndef PersitConfigLocal_h
#define PersitConfigLocal_h

#define CONFIG_VERSION "F3X1"
#define CONFIG_VERSION_L 5

// enum
// {     
//   PC_BAT_CAL_FACTOR = 1,
//   // P_VOLTAGE_VALUE =        P_CAPACITY_VALUE+sizeof(float)
// };

// EEprom parameter addresses
enum
{ 
  P_VERSION =               1,
  P_BAT_CALIBRATION =       P_VERSION + CONFIG_VERSION_L,  // 1+5=6 , 
  P_NEXT = P_BAT_CALIBRATION + sizeof(float),
};



// Types 'byte' und 'word' doesn't work!
typedef struct {
  char version[CONFIG_VERSION_L];
  float batCalibration;
} configData_t;

#endif
