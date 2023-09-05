# ISSKA Datalogger
Code for data-loggers which control different sensors, measure them with a given time step and store their values on an SD card. All the informations you need are in the ISSKA instruction pdf

The the simple_datalogger_code is the code with no wireless transmission, the notecard_datalogger_code is the code with wireless transmission.

The Arduino file is exactely the same, regardless of the sensors that you add, only the Sensors.cpp file depends on the sensors added.

Several different Sensors.cpp code are in the sensors_code folder, for different sensors configuration. All of these code include the BMP581 and the SHT35 which are already on each PCB. The header file Sensors.h is the same for all codes.

To upload these codes, simply put the Arduino file (wireless or not) with your Sensors.cpp file in the same folder which must have the name of the arduino file, minus the ".ino". Then upload the code with the Aerduino IDE

For more detailed informations about this code, please check the datalogger instruction PDF which is in this repository.
