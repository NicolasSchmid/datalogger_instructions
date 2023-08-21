
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

#include <Wire.h>
#include <TinyPICO.h>
#include <RTClib.h>
#include <SD.h>
#include <U8x8lib.h>
#include "Sensors.h" //file sensor.cpp and sensor.h must be in the same folder¨
#include <Notecard.h>


TinyPICO tp = TinyPICO();
RTC_DS3231 rtc; 
U8X8_SSD1306_128X64_NONAME_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE); //initialise something for the OLED display
Sensors sensor = Sensors();
Notecard notecard;

//values which must be on the SD card in the config file otherwise there will be an error
bool SetRTC; //read this value from the SD card. True means set the clock time with GSM signal

//PINS to power different devices
#define Enable5VPIN 27 //turns the 3V to 5V converter on if switched to high
#define MOSFET_SENSORS_PIN 14 //pin to control the power to 
#define MOSFET_NOTECARD_PIN 4

#define myProductID "com.gmail.ouaibeer:datalogger_isska" //for the notecard to know with which hub to connect

//SD card
#define SD_CS_PIN 5//Define CS pin for the SD card module
#define DATA_FILENAME "/data.csv"
#define CONFIG_FILENAME "/conf.txt"


//Global Variables which must be stored on the RTC memory (only about 4kB of storage available)
RTC_DATA_ATTR int bootCount_since_change = 0; //boot count since external change from notehub
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR bool first_time = true;
RTC_DATA_ATTR int start_time; //I couldn't store something of type DateTime so I have to store the values in an array
RTC_DATA_ATTR int start_date_time[6]; //[year,month,day,hour,minute,second]
RTC_DATA_ATTR int deep_sleep_time;
RTC_DATA_ATTR int MAX_MEASUREMENTS; //number of measurements sent at once with the notecard

void setup(){ //The setup is recalled each time the micrcontroller wakes up from a deepsleep

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
    if (SetRTC) set_RTC_with_GSM_time(); //also gives the number of bars of cellular network (0 to 4) 

    put_usefull_values_on_display(); // rread sensors

    //enter deep_sleep until a rounded time
    get_next_rounded_time(); //this finds the next rounded time and stores it in the start_date_time array and the start_time variable
    first_time = false;
    deep_sleep_mode(0); //deep sleep until start time (next rounded time)
  }

  else{ //normal operation

    bootCount++; //to know how many times we have booted the  microcontroller
    bootCount_since_change++;
    Serial.print("***********Starting the card, Boot count: ");
    Serial.print(bootCount);
    Serial.println(" ***********************");

    Serial.print("Boot count since change: ");
    Serial.println(bootCount_since_change);
    Serial.print("Start time: ");
    Serial.println(start_time);
    
    //setup for the clock
    rtc.begin();
    rtc.disable32K();
    readRTC();//print current time

    if (bootCount<=3){ //Turn the LED blue during first 3 measurements (this is only if you want to check that the logger works normally)
      tp.DotStar_SetPower(true);
      tp.DotStar_SetPixelColor(0, 0, 25 ); 
    }

    mesure_all_sensors();

    delay(100);

    if (bootCount_since_change%MAX_MEASUREMENTS == 0){ //after a number MAX_MEASUREMENTS of measurements, send the last few stored measurements over GSM
      send_data_overGSM();
    }

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

  //Pin to control the gate of MOSFET for notecard (turned off)
  pinMode(MOSFET_NOTECARD_PIN, OUTPUT);
  digitalWrite(MOSFET_NOTECARD_PIN, LOW);
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

  //Display value of nuber of measurements to be sent at once read on conf.txt
  u8x8.setCursor(0, 6);
  u8x8.print("Nb meas: ");
  u8x8.print(MAX_MEASUREMENTS);

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
      String str_MAX_MEASUREMENT = configFile.readStringUntil(';');
      MAX_MEASUREMENTS = str_MAX_MEASUREMENT.toInt();
    }
    configFile.close();
    Serial.println("Values read from SD Card:");
    Serial.print("- Deep sleep time: ");
    Serial.println(deep_sleep_time);
    Serial.print("- Set the RTC clock with GSM: ");
    Serial.println(SetRTC);
    Serial.print("- Number of measurements to be sent at once: ");
    Serial.println(MAX_MEASUREMENTS);
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
  digitalWrite(MOSFET_NOTECARD_PIN, LOW);
  tp.DotStar_SetPower(false);
  rtc.writeSqwPinMode(DS3231_OFF);
  rtc.clearAlarm(1);                // set alarm 1, 2 flag to false (so alarm 1, 2 didn't happen so far)
  rtc.clearAlarm(2);
  rtc.disableAlarm(2);  
  //enter deep sleep
  rtc.setAlarm1(start_time+bootCount_since_change*sleeping_time,DS3231_A1_Minute);  
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
  DateTime curr_time = starting_time_dt +(bootCount_since_change-1)*deep_sleep_time;
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

bool synchronize_notecard(){ //returns true if it was able to connect the notecard to GSM nework
  pinMode(MOSFET_NOTECARD_PIN, OUTPUT);
  digitalWrite(MOSFET_NOTECARD_PIN, HIGH);
  delay(100);
  notecard.begin();
  delay(400);
  
  //set communication parameters for the notecard
  J *req = notecard.newRequest("hub.set");    
  JAddStringToObject(req, "product", myProductID);
  JAddStringToObject(req, "mode", "minimum");
  notecard.sendRequest(req);

  //send command to sync the notecard
  notecard.sendRequest(notecard.newRequest("hub.sync"));

  //wait for syncronisation
  bool GSM_time_out = false;
  unsigned long start_time_connection = micros();
  int completed=0;
  do{
    J *SyncStatus = notecard.requestAndResponse(notecard.newRequest("hub.sync.status"));
    completed = (int)JGetNumber(SyncStatus,"completed");
    notecard.deleteResponse(SyncStatus); //delete the response
    if((micros()-start_time_connection)/1000000>240){ //after 240s stop waiting for the connexion
      GSM_time_out = true;
      return false;
    }
    delay(500);
  } while (completed==0 && !GSM_time_out);

  if (completed!=0){
    Serial.println("The notecard is syncronized!");
    return true;
  }
  else{
    Serial.println("The notecard failed to synchronize!");
    return false;
  }
}

void get_external_parameter(){
  J *notecard_changes = notecard.requestAndResponse(notecard.newRequest("file.changes"));

  J *changes_infos = JGetObjectItem(notecard_changes,"info");
  J *data_changes = JGetObjectItem(changes_infos,"data.qi");
  int nb_changes = (int) JGetInt(data_changes,"total");
  notecard.deleteResponse(notecard_changes);
  
  Serial.print("Nb Changes: ");
  Serial.println(nb_changes);

  int new_deep_sleep_time = 0;
  int new_MAX_MEASUREMENTS = 0;
  if (nb_changes >0){
    while (nb_changes>0){
      J *req = notecard.newRequest("note.get"); 
      JAddStringToObject(req, "file", "data.qi"); 
      JAddBoolToObject(req, "delete", true);
      J *parameter_changed = notecard.requestAndResponse(req);
      J *parameter_changed_body = JGetObjectItem(parameter_changed,"body");

      int temp_deep_sleep_time = (int) JGetInt(parameter_changed_body,"deep_sleep_time");
      if(temp_deep_sleep_time != 0){
        new_deep_sleep_time = temp_deep_sleep_time;
      }
      int temp_MAX_MEASUREMENTS = (int) JGetInt(parameter_changed_body,"MAX_MEASUREMENTS");
      if(temp_MAX_MEASUREMENTS != 0){
        new_MAX_MEASUREMENTS = temp_MAX_MEASUREMENTS;
      }
      notecard.deleteResponse(parameter_changed);
      nb_changes-=1;
    }
    if(new_deep_sleep_time!=0 || new_MAX_MEASUREMENTS!=0){
      if(new_deep_sleep_time==0) new_deep_sleep_time = deep_sleep_time; //no change in deep sleep time
      if(new_MAX_MEASUREMENTS==0) new_MAX_MEASUREMENTS = MAX_MEASUREMENTS; //no change in MAX_MEASUREMENTS
      Serial.print("new deep sleep time: ");
      Serial.println(new_deep_sleep_time);
      Serial.print("new MAX MEASUREMENT: ");
      Serial.println(new_MAX_MEASUREMENTS);
      synchronize_notecard(); //sync the fact that we read and deleted parameters

      //write changes to sd card conffile
      Serial.println("change conf file");
      String new_conf_file_str = String(new_deep_sleep_time)+"; //deep sleep time in seconds \n";
      new_conf_file_str = new_conf_file_str + String(SetRTC) +"; //set RTC with GSM time (1 or 0) \n";
      new_conf_file_str = new_conf_file_str + String(new_MAX_MEASUREMENTS) +"; //number of measurements to be sent at once via gsm \n";
      SD.remove(CONFIG_FILENAME); //delete conf file
      File configFile = SD.open(CONFIG_FILENAME, FILE_APPEND); //create a new conf file with new parameters
      configFile.print(new_conf_file_str);
      configFile.close();
      if(bootCount!=0){
        DateTime starting_time_dt = DateTime(start_date_time[0],start_date_time[1],start_date_time[2],start_date_time[3],start_date_time[4],start_date_time[5]);
        DateTime new_start_time = starting_time_dt +bootCount_since_change*deep_sleep_time;
        start_time = new_start_time.unixtime();
        start_date_time[0]=new_start_time.year();
        start_date_time[1]=new_start_time.month();
        start_date_time[2]=new_start_time.day();
        start_date_time[3]=new_start_time.hour();
        start_date_time[4]=new_start_time.minute();
        start_date_time[5]=new_start_time.second();
        bootCount_since_change = 0;
      }
      deep_sleep_time=new_deep_sleep_time;
      MAX_MEASUREMENTS=new_MAX_MEASUREMENTS;
    }
  }
}

void set_RTC_with_GSM_time(){ //gets the time from GSM and adjust the rtc clock with the swiss winter time (UTC+1)
  Serial.println("Try to use the notehub to request the GSM time ....");
  unsigned int GSM_time = getGSMtime()+2;
  rtc.adjust(DateTime(GSM_time));  
  readRTC();
  sleep(3);
}

unsigned int getGSMtime(){
  digitalWrite(MOSFET_NOTECARD_PIN, HIGH);
  sensor.tcaselect(3);
  delay(100); 

  u8x8.setCursor(0, 4);
  u8x8.print("Get GSM time...");
  delay(100); //time for MUX to be totally turned on (probably way faster but its only once anyway)
  tp.DotStar_SetPixelColor(25, 0, 25 ); //LED Purple
  if(synchronize_notecard()){
    get_external_parameter();
    J *rsp = notecard.requestAndResponse(notecard.newRequest("card.time"));
    unsigned int recieved_time = (unsigned int)JGetNumber(rsp, "time");
    notecard.deleteResponse(rsp);

    //show the numbers of bars of GSM network (0 to 4) like on a phone
    J *gsm_info = notecard.requestAndResponse(notecard.newRequest("card.wireless"));
    J *net_infos = JGetObjectItem(gsm_info,"net");
    int bars = JGetNumber(net_infos, "bars");
    notecard.deleteResponse(gsm_info);
    Serial.print("Bars: ");
    Serial.println(bars);
    u8x8.setCursor(0, 6);
    u8x8.print("GSM Bars: ");
    u8x8.print(bars);
    tp.DotStar_SetPixelColor(0, 50, 0 ); //LED Green
    Serial.println("The RTC time is adjusted with the GSM time (GMT + 1)");
    digitalWrite(MOSFET_NOTECARD_PIN, LOW); //turn off the notecard
    sensor.tcaselect(0);
    return recieved_time+3600; //+3600 for GMT+1 (winter time in switzerland)
  }
  else{
    Serial.println("could not connect the Notecard, we use RTC time instead");
    u8x8.setCursor(0, 6);
    u8x8.print("Failed to connect");
    tp.DotStar_SetPixelColor(50, 0, 0 ); //LED Red
    digitalWrite(MOSFET_NOTECARD_PIN, LOW); //turn off the notecard
    sensor.tcaselect(0);
    return (unsigned int)rtc.now().unixtime();
  }

}

void send_data_overGSM(){ //send several measurements all at once over GSM
  sensor.tcaselect(3);
  Serial.println("We now try to send several measurements over GSM all at once");

  File myFile = SD.open(DATA_FILENAME, FILE_READ);

  int position_in_csv = myFile.size(); //get the position of the last character in the csv file
  int nb_lines_to_send =MAX_MEASUREMENTS; 

  //find the position where the last few lines of the CSV begin but starting from the end of the CSV since the file might be very long
  //if there is some wierd behaviour with this function, check that you didn't forget a ";" in the Sensors::getFileData () function. I often forgot to write the last one
  for (int i = 0; i < nb_lines_to_send;i++){
    position_in_csv = position_in_csv -20; // avoid the last twenty characters since a line is certainly longer than 20 characters
    myFile.seek(position_in_csv); 
    while (myFile.peek() != '\n'){
      position_in_csv = position_in_csv - 1;
      myFile.seek(position_in_csv); // Go backwards until we detect the previous line separator
    }
  }
  myFile.seek(position_in_csv+1); //at this point we are at the beginning of the nb_lines_to_send last lines of the csv file

  //creat a matrix with the values to be sent
  String data_matrix[nb_lines_to_send][3+num_sensors]; //the "3 + ..." is there because we also store Bootcount,DateTime and Vbatt
  int counter=0;
  while (myFile.available()) {
    for (int i =0; i<3+num_sensors;i++){ //6 is for ID,Time,batVolt, +3sensors
      String element = myFile.readStringUntil(';');
      if (element.length()>0) data_matrix[counter][i] = element;
    }
    myFile.readStringUntil('\n');
    counter++;
  }
  myFile.close();

  //print the values which will be sent
  for (int i =0; i<nb_lines_to_send;i++) {
    Serial.print("line to be sent: ");
    for(int j=0;j<3+num_sensors;j++){        
      Serial.print(data_matrix[i][j]);
      Serial.print(";");
    }
    Serial.println();
  }

  /*data_matrix
      Bootcount DateTime Vbat sensor1 sensor2 ...
  meas1     x       x       x     x       x
  meas2     x       x       x     x       x
  meas3     x       x       x     x       x
  ...

  e.g. data_matrix[2][4] will get the value from sensor2 at meas3
  */

  //power the Notecard and connect the multiplexer with notecard
  digitalWrite(MOSFET_NOTECARD_PIN, HIGH); 

  tp.DotStar_SetPixelColor(25, 0, 25 ); //LED pink while connecting to hub 
  //send the last few measurements over GSM
  if(synchronize_notecard()){ //connect the notecard
    Serial.println("Connected!");
    tp.DotStar_SetPixelColor(25, 25, 0 ); //LED yellow while sending data

    //create the Jason file to be sent
    //first create the body
    String sensor_names[num_sensors+1] = {"VBat","tempSHT","press","hum","tempBMP"}; //change this if you add more sensors!!!
    J *body = JCreateObject();
    J *data = JCreateObject();
    JAddItemToObject(body, "data", data);
    for(int i=0; i<num_sensors+1;i++){
      J *sensor = JAddArrayToObject(data,sensor_names[i].c_str());
      for(int j=0; j<nb_lines_to_send;j++){
        J *sample = JCreateObject();
        JAddItemToArray(sensor, sample);
        JAddStringToObject(sample, "value", data_matrix[j][i+2].c_str());

        //we convert the time stored string to unixtime
        String time_stored_str = data_matrix[j][1]; //time store in the form "28/04/2023 12:38:00" (gmt+1) -> must be converted to unixtime gmt+0
        String day_stored = time_stored_str.substring(0,2);
        String month_stored = time_stored_str.substring(3,5);
        String year_stored = time_stored_str.substring(6,10);
        String hour_stored = time_stored_str.substring(11,13);
        String minute_stored = time_stored_str.substring(14,16);
        String second_stored = time_stored_str.substring(17,19);
        DateTime datetime_stored = DateTime(year_stored.toInt(),month_stored.toInt(),day_stored.toInt(),hour_stored.toInt(),minute_stored.toInt(),second_stored.toInt());
        JAddStringToObject(sample, "epoch", String(datetime_stored.unixtime()-3600).c_str());
      }
    }
    //print the Jason body
    char* tempString = JPrint(body);
    Serial.println("Print the body object ...");
    Serial.println(tempString);

    //create a request
    J *req = notecard.newRequest("note.add");
    JAddItemToObject(req, "body", body);
    //send Jason file and wait up until it is recieved
    notecard.sendRequest(req);

    //syncronizing
    if(synchronize_notecard()){
      Serial.println("data successfully sent!");
      tp.DotStar_SetPixelColor(0, 50, 0 ); //LED green
      get_external_parameter();
    }
    else{
      Serial.println("failed to send the data"); 
      tp.DotStar_SetPixelColor(50, 0, 0 ); //LED red
    }

  digitalWrite(MOSFET_NOTECARD_PIN, LOW); 
  sensor.tcaselect(0);
  delay(100); 
  }
}
 

