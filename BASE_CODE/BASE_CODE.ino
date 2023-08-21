
/* *******************************************************************************
Base code for datalogger with PCB IISKA-LOGGER-TINY 1.0 There are two sensors already installed on this PCB, the SHT35 (I2c adress 0x44 and TCA1) and the BMP581 (I2c adress 0x46 and TCA2)

Here are some important remarks when modifying to add or  add or remove a sensor:
    - in Sensors::getFileData () don't forget to add a "%s;" for each new sensor 
    - adjust the struct, getFileHeader (), getFileData (), serialPrint() and mesure() with the sensor used

The conf.txt file must have the following structure:
  300; //deepsleep time in seconds (must be either a multiple of 60, or it must be a divider of 60)
  1; //boolean value to choose if you want to set the RTC clock with time from SD card
  2023/03/31 16:05:00; //time to set the RTC if setRTC = 1

If you have any questions contact me per email at nicolas.schmid.6035@gmail.com
**********************************************************************************
*/  

#include <Wire.h>
#include <TinyPICO.h>
#include <RTClib.h>
#include <SD.h>
#include <U8x8lib.h>
#include "Sensors.h" //file sensor.cpp and sensor.h must be in the same folder


TinyPICO tp = TinyPICO();
RTC_DS3231 rtc; 
U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE); //initialise something for the OLED display
Sensors sensor = Sensors();

//values which must be on the SD card in the config file otherwise there will be an error
bool SetRTC; //read this value from the SD card. True means set the clock time with GSM signal
int SD_date_time[6]; //[year,month,day,hour,minute,second]
float start_time_programm; //time when the programm starts. Used to adjust more precisely the rtc clock

//PINS to power different devices
#define Enable5VPIN 27 //turns the 3V to 5V converter on if switched to high
#define MOSFET_SENSORS_PIN 14 //pin to control the power to 

//SD card
#define SD_CS_PIN 5//Define CS pin for the SD card module
#define DATA_FILENAME "/data.csv"
#define CONFIG_FILENAME "/conf.txt"


//Global Variables which must be stored on the RTC memory (only about 4kB of storage available)
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR bool first_time = true;
RTC_DATA_ATTR int start_time; //I couldn't store something of type DateTime so I have to store the values in an array
RTC_DATA_ATTR int start_date_time[6]; //[year,month,day,hour,minute,second]
RTC_DATA_ATTR int deep_sleep_time;

void setup(){ //The setup is recalled each time the micrcontroller wakes up from a deepsleep

  start_time_programm = micros();

  //Start communication protocol with external devices
  Serial.begin(115200);

  power_external_device();

  Wire.begin(); // initialize I2C bus 
  Wire.setClock(50000); // set I2C clock frequency to 50kHz which is rather slow but more reliable //change to lower value if cables get long

  if(first_time){ //sleep until rounded time, this snipet is only executed when the microcontroller is turned on for the first time

    tp.DotStar_SetPower(true); //This method turns on the Dotstar power switch, which enables the LED
    tp.DotStar_SetPixelColor(25, 25, 25 ); //Turn the LED white while it is not initialised

    //write initialize on the OLED display
    u8x8.begin();
    u8x8.setBusClock(50000); //Set the SCL at 50kHz since we can have long cables in some cases. Otherwise, the OLED set the frequency at 500kHz
    u8x8.setFont(u8x8_font_amstrad_cpc_extended_r);
    u8x8.setCursor(0, 0);
    u8x8.print("Init...");
    Serial.println("Init...");

    delay(2000); //wait 2 seconds to see the led

    //get stored values on SD card like deep_sleep_time, set_RTC etc.
    initialise_SD_card(); 

    //initialise the RTC clock
    rtc.begin();
    rtc.disable32K();
    if (SetRTC) adjust_rtc_time_with_time_from_SD();

    put_usefull_values_on_display(); // rread sensors

    //enter deep_sleep until a rounded time
    get_next_rounded_time(); //this finds the next rounded time and stores it in the start_date_time array and the start_time variable
    first_time = false;
    deep_sleep_mode(0); //deep sleep until start time (next rounded time)
  }

  else{ //normal operation
    bootCount++; //to know how many times we have booted the  microcontroller
    Serial.print("***********Starting the card, Boot count: ");
    Serial.print(bootCount);
    Serial.println(" ***********************");
    
    //setup for the clock
    rtc.begin();
    rtc.disable32K();
    readRTC();//print current time

    if (bootCount<=3){ //Turn the LED blue during first 3 measurements (this is only if you want to check that the logger works normally)
      tp.DotStar_SetPower(true);
      tp.DotStar_SetPixelColor(0, 0, 25 ); 
    }

    mesure_all_sensors();


    if(tp.GetBatteryVoltage()>3.5){
      deep_sleep_mode(deep_sleep_time);
    }
    else{
      deep_sleep_mode(60*60*24*365*10); //sleep during ten years (don't wake up) to avoid damaging the battery
    }
  }
}

void loop(){ //No loop is used to spare energy
}

void power_external_device(){
  //Turn on Pin to control the gate of MOSFET for all the sensors, display and microSD card(turned on)
  pinMode(MOSFET_SENSORS_PIN, OUTPUT);
  digitalWrite(MOSFET_SENSORS_PIN, HIGH);
}

void adjust_rtc_time_with_time_from_SD(){
  DateTime SD_time = DateTime(SD_date_time[0],SD_date_time[1],SD_date_time[2],SD_date_time[3],SD_date_time[4],SD_date_time[5]); // convert time written on conf.txt into DateTime type
  float adjustement_time = micros();
  rtc.adjust(SD_time+2+(adjustement_time-start_time_programm)/1000000); //compensate the booting time to be as accurate as possible with the RTC time.
  Serial.print("rtc set to: ");
  Serial.println(rtc.now().timestamp(DateTime::TIMESTAMP_FULL)); //print time set on RTC
}

void put_usefull_values_on_display(){
 
  //Display Vattery voltage
  float batvolt = tp.GetBatteryVoltage();
  u8x8.display(); // Power on the OLED display
  u8x8.clear();
  u8x8.setCursor(0, 0);
  u8x8.print("Batt: ");
  u8x8.print(batvolt);
  u8x8.print("V");

  //Display current time on RTC
  u8x8.setCursor(0, 2);
  u8x8.print(readRTC());

  //Display value of timestep read on conf.txt
  u8x8.setCursor(0, 4);
  u8x8.print("timestep: ");
  u8x8.print(deep_sleep_time);
  u8x8.print("s");
  delay(4000);

  //Display sensor values
  sensor.mesure();   //measure all sensors
  String measured_values_str = sensor.serialPrint();
  u8x8.clear();
  u8x8.setCursor(0, 0);
  int line_count = 0;
  for (char const &c: measured_values_str) {
    if(c=='\n'){
      line_count++;
      if (line_count%8 ==0){ //if we have more than 8 sensors to display and there is no more space on the screen
        delay(8000);
        u8x8.clear();
        u8x8.setCursor(0, 0);
      }
      else u8x8.println();
    }
    else u8x8.print(c);
  }
  delay(8000); //wait 8 seconds to see the values on the display
}

void get_next_rounded_time(){ 
  //this function finds the next rounded time and stores it in the start_date_time array and the start_time variable

  //the deep sleep time bust be either less than 60 or a multiple of 60 otherwise we will have an unexpected behaviour
  int rounded_min;     //if you chose rounded_min = 5, then it will start at :00, :05, :10 etc.
  if(deep_sleep_time>=60){
     rounded_min = deep_sleep_time/60; //change this to the number of minutes to which you want to round. per default it will be the same as the sleeping time. e.g. if the sleeping time is 15min, it will only start at 00, :15, :30 etc. But for example if you want it to start every 10 minutes regardless of the sleeping time, write: int rounded_min = 10;
  }
  else{
    rounded_min=1;
  }

  DateTime current_time=rtc.now();
  int seconds_remaining = 60-current_time.second()%60; //number of seconds until the next rounded minute
  DateTime newtime = current_time+seconds_remaining; 

  if(deep_sleep_time>=60){ 
    if(newtime.minute()%rounded_min!=0){
      int minutes_remaining = rounded_min - newtime.minute()%rounded_min; //number of minutes until the next rounded time (in minutes)
      newtime = (newtime + 60*minutes_remaining);
    }
  }
  start_time = newtime.unixtime();
  start_date_time[0]=newtime.year();
  start_date_time[1]=newtime.month();
  start_date_time[2]=newtime.day();
  start_date_time[3]=newtime.hour();
  start_date_time[4]=newtime.minute();
  start_date_time[5]=newtime.second();
  Serial.print("start time: ");
  Serial.println(start_time);
}

void initialise_SD_card(){ //open SD card. Write header in the data.csv file. read values from the conf.txt file.

  //initialisation
  bool SD_error = false;
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD card initialization failed!");
    SD_error = true;
  }
  else{
    //read value on the conf.txt file and store the values on RTC memory
    File configFile = SD.open(CONFIG_FILENAME);
    if (configFile.available()) {
      deep_sleep_time = configFile.readStringUntil(';').toInt();
      while (configFile.peek() != '\n') //if there are any black space if the config file
        {
          configFile.seek(configFile.position() + 1);
        }
      configFile.seek(configFile.position() + 1);
      String str_SetRTC = configFile.readStringUntil(';');
      if (str_SetRTC=="0") SetRTC = false;
      else if (str_SetRTC=="1") SetRTC = true;
      else {
        Serial.println("problem by reading the SetRTC value from SD card");
        SD_error = true;
      }

      while (configFile.peek() != '\n') //if there are any black space if the config file
        {
          configFile.seek(configFile.position() + 1);
        }
      configFile.seek(configFile.position() + 1);
      SD_date_time[0] = configFile.readStringUntil('/').toInt(); //year
      SD_date_time[1] = configFile.readStringUntil('/').toInt(); //month
      SD_date_time[2] = configFile.readStringUntil(' ').toInt(); //day
      SD_date_time[3] = configFile.readStringUntil(':').toInt(); //hour
      SD_date_time[4] = configFile.readStringUntil(':').toInt(); //minute
      SD_date_time[5] = configFile.readStringUntil(';').toInt(); //second
    }
    configFile.close();
    Serial.println("Values read from SD Card:");
    Serial.print("- Deep sleep time: ");
    Serial.println(deep_sleep_time);
    Serial.print("- Set the RTC clock: ");
    Serial.println(SetRTC);
    if(SetRTC){
      Serial.print("- Time on SD when you should push the reboot button : ");
      for(int i = 0; i<6;i++){
        Serial.print(SD_date_time[i]);
        Serial.print("/");
      }
      Serial.println();
    }
  }

  u8x8.setCursor(0, 2);
  if (SD_error == true){
    u8x8.print("SD Failure !");
    tp.DotStar_SetPixelColor( 50, 0, 0 ); //set led to red
  }
  //write header in SD
  else {
    File logFile = SD.open(DATA_FILENAME, FILE_APPEND);
    String header = sensor.getFileHeader();
    logFile.println();
    logFile.println();
    logFile.println();
    logFile.print("ID;DateTime;VBatt;");
    logFile.println(header);
    logFile.close();
    // show on display and LED if values on SD card are OK
    u8x8.setCursor(0, 2);
    u8x8.print("Success !");
    tp.DotStar_SetPixelColor( 0, 50, 0 );
  }
  delay(1500); //in [ms] in order to see the LED
}

void deep_sleep_mode(int sleeping_time){ //turnes off alomst everything and reboots the microconstroller after a given time in seceonds
  //turn off everything
  digitalWrite(MOSFET_SENSORS_PIN, LOW);
  tp.DotStar_SetPower(false);
  rtc.writeSqwPinMode(DS3231_OFF);
  rtc.clearAlarm(1);                // set alarm 1, 2 flag to false (so alarm 1, 2 didn't happen so far)
  rtc.clearAlarm(2);
  rtc.disableAlarm(2);  
  //enter deep sleep
  rtc.setAlarm1(start_time+bootCount*sleeping_time,DS3231_A1_Minute);  
  pinMode(GPIO_NUM_15, INPUT_PULLUP);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_15, 0);
  esp_deep_sleep_start();
}

String readRTC() { //print current time
  DateTime now = rtc.now();
  String time_str = String(now.day()) + "/" + String(now.month()) + " " + String(now.hour())+ ":" + String(now.minute()) + ":" + String(now.second());
  Serial.print("Current time: ");
  Serial.println(time_str);
  return time_str;
}

void save_data_in_SD(){ //give the measured values in a single string "ID;DateTime;VBatt;tempEXT;pressEXT;HumExt;..." eg 
  //measurement ID (bootcount)
  String data_message = (String) bootCount;
  char buffer [35] = "";
  DateTime starting_time_dt = DateTime(start_date_time[0],start_date_time[1],start_date_time[2],start_date_time[3],start_date_time[4],start_date_time[5]);
  DateTime curr_time = starting_time_dt +(bootCount-1)*deep_sleep_time;
  sprintf(buffer, "%02d/%02d/%04d %02d:%02d:%02d", curr_time.day(), curr_time.month(), curr_time.year(), curr_time.hour(), curr_time.minute(), curr_time.second());
  data_message = data_message + ";"+ buffer + ";";

  //get battery voltage to estimate current battery
  float batvolt = tp.GetBatteryVoltage();
  data_message = data_message + (String)batvolt + ";";
  String sensor_data = sensor.getFileData(); 
  data_message = data_message + sensor_data;
  Serial.println(data_message);

  //initialise SD card
  if (!SD.begin(SD_CS_PIN)) { //if there is a problem with the SD card
    Serial.println("SD card initialization failed!");
    //turn on display to show to the user that there is a problem with the SD card
    u8x8.begin();
    u8x8.setBusClock(50000); //Set the SCL at 50kHz since we can have long cables in some cases. Otherwise, the OLED set the frequency at 500kHz
    u8x8.setFont(u8x8_font_amstrad_cpc_extended_r);
    u8x8.setCursor(0, 0);
    u8x8.print("SD Failure !");
    tp.DotStar_SetPower(true);
    tp.DotStar_SetPixelColor( 50, 0, 0 ); //set led to red
    delay(5000); //in [ms] in order to see the LED and display because there is an important and recurent error 
  }

  else{ //no error with the Sd card
  //write data in SD
  File logFile = SD.open(DATA_FILENAME, FILE_APPEND);
  logFile.print(data_message);
  logFile.println();
  logFile.close();
  }
}

void mesure_all_sensors(){
  Serial.println("Start of the measurements");
  sensor.mesure(); // measures all sensors and stores the values in a struct sensor.mesures
  sensor.serialPrint(); //print all the sensor values in Serial monitor
  save_data_in_SD();
}
