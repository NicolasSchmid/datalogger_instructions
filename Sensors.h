
/* *******************************************************************************
Base code for datalogger with PCB IISKA-LOGGER V1.2 with Notecard. There are two sensors already installed on this PCB, the SHT35 (I2c adress 0x44 and TCA1) and the BMP581 (I2c adress 0x46 and TCA2)

Here are some important remarks when modifying to add or  add or remove a sensor:
    - in Sensors::getFileData () don't forget to add a "%s;" for each new sensor 
    - adjust the struct, getFileHeader (), getFileData (), serialPrint() and mesure() with the sensor used
    - In the arduino code change the sensor_names list (add your sensor names)
    - In Sensors.h adjust the variable num_sensors to the number of sensors

The conf.txt file must have the following structure:
  300; //deepsleep time in seconds (put at least 240 seconds)
  1; //boolean value to choose if you want to set the RTC clock with GSM time (recommended to put 1)
  12; // number of measurements to be sent at once

If you have any questions contact me per email at nicolas.schmid.6035@gmail.com
**********************************************************************************
*/ 

#ifndef sensors_h
#define sensors_h
#include "Arduino.h"

#define num_sensors 4 ///change here is you add sensors !!!!!!!! 

class Sensors {
  public: 
    Sensors(); 
    String getFileHeader();
    String getFileData();
    String serialPrint();
    void mesure();
    void tcaselect(uint8_t i);
};

#endif
