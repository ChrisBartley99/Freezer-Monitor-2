/////////////////////////////////////
//   Change to 0 to for test code  or 1 for live code
#define LIVE_CODE 0
////////////////////////////////////

#include <WiFi.h>
#include "secrets.h"

// include library to read and write from flash memory
// https://www.electronics-lab.com/project/using-esp32s-flash-memory-for-data-storage/
#include <EEPROM.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#include "cb_ssd1306_text.h"    //  OLED display 

#include "GetNTPtime.h"         // Internet Time protocol

#include "ESPtone.h"            // Buzzer stuff

#include "SendGmail.h"          // GMAIL handling

// #####################
// Includes for web server OTA code
// https://randomnerdtutorials.com/esp32-ota-over-the-air-arduino/
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
// ####################'


#include "ThingSpeak.h"         // always include thingspeak header file after other header files and custom macros


// -----------------------------------------------

// --- Screen Elements Postions
#define STATUS_INDICATOR_X  115
#define STATUS_INDICATOR_Y  25

#define FONT_SMALL 1
#define FONT_MEDIUM 2
#define FONT_LARGE 3


// --- Sounds
#define BEEP_SHORT  1
#define BEEP_LONG   2
#define BEEP_TUNE   3



// -----------------------------------------------

#define ThingSpeakUpdateInterval 10   // every 15 mins ( approx)

#define FreezerDoorSwitchPin 16

#define LED_BUILTIN  2        // ESP32 DoItAll board

#define DOOR_OPEN   1
#define DOOR_CLOSED 2

#define MinsBeforePanic  1.5

// define the number of bytes you want to access
#define EEPROM_SIZE 2

#define EEPROM_REBOOTED_CODE 0x55

//------------------------------------------------

long DaysRunning = 0;

bool DoorIsOpen = false;

int ThisDay;
int DayofYear;

int WifiConnectionCount;

int DoorStatus;

char buffer[80];
int loop_count;

float temperatureC;
float MaxtemperatureC;
float MintemperatureC;
float LongMaxtemperatureC = -100;
float LongMintemperatureC = 100;


int ThingSpeakUpdateTime;

unsigned long openDoorCountMillis;
unsigned long LastThinkSpeakUpdateMillis;
unsigned long currMillis;


String buff;
char tmp[350];


//  Email stuff

bool SentPanicEmailFlag;

char ToAddress[] = EMAILRECEIPIENT;

char Subject1[] =   "Freezer Monitor : DOOR LEFT OPEN !!!";

char Subject2[] =   "Freezer Monitor : Working OK";

char Subject3[] =   "Freezer Monitor : Door Closed";

char Subject4[] =   "ThinkSpeak update Problem";

char Subject5[] =   "Freezer Monitor : REBOOTED";


int first_temp_read;

// ThingSpeak update values
// Initialize our values
int number1 = 1;
int number2 = 0;
int number3 = 0;
int number4 = 0;
String myStatus = "";



// ---------------------------

// Set up the Wi_Fi params
char ssid[] = SECRET_SSID;   // your network SSID (name)  ff
char pass[] = SECRET_PASS;   // your network password
int keyIndex = 0;            // your network key Index number (needed only for WEP)


// Handle the ThingSpeak channel
unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;

// Handle the DS18B20 Temp sensor
// GPIO where the DS18B20 is connected to
const int oneWireBus = 4;

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(oneWireBus);

// Pass our oneWire reference to Dallas Temperature sensor
DallasTemperature sensors(&oneWire);


WiFiClient  client;


//  == Start the WebServer to allow OTA ===
AsyncWebServer server(80);



//++++++++++++++++++++++++++++++++++++++++++++++++
//       Multi Tasking variables
//
TaskHandle_t Task1;   // Update ThinkSpeak
TaskHandle_t Task2;   // Play tunes

QueueHandle_t    queue_ThinkSpeak;
QueueHandle_t    queue_Beep;

//++++++++++++++++++++++++++++++++++++++++++++++++



void setup() {


  StartSSD1306VCC();    // set up the voltage for the display
  display.clearDisplay();
  ScreenPrint("Booting...", 5, 20, FONT_SMALL);
  display.display();


  Serial.begin(115200);  //Initialize serial


  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(FreezerDoorSwitchPin, INPUT_PULLUP);


  WiFi.mode(WIFI_STA);


  ThingSpeak.begin(client);  // Initialize ThingSpeak


  // Start the DS18B20 sensor
  sensors.begin();

  // initialize EEPROM with predefined size
  EEPROM.begin(EEPROM_SIZE);


  ConnectToWiFi();


  // #### Set up the OTA update stuff
  //=================================
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    request->send(200, "text/plain", WebSignon().c_str());
  });



  AsyncElegantOTA.begin(&server);    // Start ElegantOTA
  server.begin();
  Serial.println("HTTP server started");


  ThingSpeakUpdateTime = ThingSpeakUpdateInterval;    // Set the defaultupdate frequency for ThinkSpeak

  LastThinkSpeakUpdateMillis = millis();

  DoorIsOpen = -1;
  DoorStatus = DOOR_CLOSED;

  SentPanicEmailFlag = false;


  printLocalTime();       // Get the NTP time
  ThisDay = 0;
  DayofYear = timeinfo.tm_yday;    // make note of Day number so we can send keep Gmail alive email

  ResetMaxMin();


  if (EEPROM.read(0) == EEPROM_REBOOTED_CODE )
  {
    SendNotificationEmail(5);
    EEPROM.write(0, 0);
    EEPROM.commit();

  }

  InitScreen();       // My Splash Screen
  delay(500);



  // Create the queues with 5 slots of 2 bytes

  queue_ThinkSpeak  = xQueueCreate(5, sizeof(int));
  queue_Beep        = xQueueCreate(5, sizeof(int));


  xTaskCreatePinnedToCore(
    loop1, /* Function to implement the task */
    "Task1", /* Name of the task */
    5000, /* Stack size in words */
    NULL, /* Task input parameter */
    0, /* Priority of the task */
    &Task1, /* Task handle. */
    1); /* Core where the task should run */

  xTaskCreatePinnedToCore(
    loop2, /* Function to implement the task */
    "Task2", /* Name of the task */
    5000, /* Stack size in words */
    NULL, /* Task input parameter */
    0, /* Priority of the task */
    &Task2, /* Task handle. */
    1); /* Core where the task should run */



}


//
//   -------   END OF SET UP ------------
//


void loop()
{
  int x_pos;

  for (;;) {

    //Serial.println("Loop start ...");

    ConnectToWiFi();          // If we are not already then re-connect to wi-fi

    display.clearDisplay();

    AsyncElegantOTA.loop();

    ReadTemperature();

    DisplayTemperature();

    DisplayTime();


    // See if fridge door is open
    if (digitalRead(FreezerDoorSwitchPin) == HIGH)
    {

      if (DoorIsOpen == false)            // Log this is the 1st time we noticed door is open
      {
        // Door has just been opened
        Serial.println(" //// Door opened");
        DoorIsOpen = true ;
        DoorStatus = DOOR_OPEN;
        openDoorCountMillis = millis();   // Note Time when door was opened

        // Update ThingSpeak NOW to report Door open
        QueueThingSpeak();
      }

      //  Serial.println("Freezer Door is open");
      display.clearDisplay();
      DisplayTemperature();
      if (loop_count % 2 == 0)
      {
        ScreenPrint("Door Open!", 5, 48, FONT_MEDIUM);
        QueueBeep(BEEP_SHORT);
      }

      currMillis = millis();
      if (currMillis > (openDoorCountMillis + ( (MinsBeforePanic * 60)  * 1000))) {

        // Door open tooo long !!
        //   Serial.print(" Door Open too long");
        display.clearDisplay();
        DisplayTemperature();
        if (loop_count % 2 == 0)    //  create a 'jumping' message to be more obvious
        {
          x_pos = 5;
        }
        else
        {
          x_pos = 55;
        }
        ScreenPrint("PANIC", x_pos, 50, FONT_MEDIUM);

        QueueBeep(BEEP_LONG);

        if (SentPanicEmailFlag == false)   // Only want to send the Panic email oncer per event
        {
          SendNotificationEmail(1);
          SentPanicEmailFlag = true;
        }

      }

    }
    else
    {

      if (DoorIsOpen == true)
      {
        // Door has just been closed
        //==========================
        Serial.println("\\\\\\Door Closed");

        DoorStatus = DOOR_CLOSED;

        QueueBeep(BEEP_TUNE);

        display.clearDisplay();
        ScreenPrint("DOOR", 32, 5, FONT_LARGE);
        ScreenPrint("CLOSED", 12, 40, FONT_LARGE);
        display.display();
        delay(3000);

        if (SentPanicEmailFlag == true)   // Email : confirm door now closed after PANIC event
        {
          SendNotificationEmail(3);  //
          SentPanicEmailFlag = false;       // Clear the One time only Email gate
        }


        // Update ThingSpeak NOW to report Door now closed
        //  Queue a ThinkSpeak notfication
        QueueThingSpeak();
      }

      // Reset door status
      DoorIsOpen = false ;
    }


    if (loop_count % 20 == 0)
    {
      InitScreen();         //  Tell the world via the OLED  who and what we are
    }


    // Every 10 mins or so update ThinkSpeak

    if (loop_count >  ThingSpeakUpdateTime * 60 )   // connect to wi-fi and ThingSpeak
      //if (loop_count > 50)
    {
      //  Queue a ThinkSpeak notfication
      QueueThingSpeak();

      loop_count = 0;                 // Re Start loop counting till next thinkspeak
    }

    CheckGmailAwake();                // Email at mid night to keep Gmail account awake

    display.display();                // update screen so we see latest changes

    loop_count++;

  }

}


//
// ====================================================
//   SUB-TASK HERE
// ====================================================
//

//
//    TASK 1 : Do a ThingSpeak update
//
void loop1(void * parameter) {

  for (;;) {

    // Task code here
    int DoorOpenClosed;
    xQueueReceive(queue_ThinkSpeak, &DoorOpenClosed, portMAX_DELAY);

    Serial.println("--- Task 1 running ---");

    Serial.println(DoorOpenClosed);

    digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
    delay(1000);
    digitalWrite(LED_BUILTIN, LOW);   // turn the LED off (LOW is the voltage level)

    UpdateThingSpeak(DoorOpenClosed);

    Serial.println("--- Task 1 ended ---");
  }
}


//
//    TASK 2 : Play a noise or tune
//
void loop2(void * parameter) {

  for (;;) {

    // Task code here
    int WhichTune;
    xQueueReceive(queue_Beep, &WhichTune, portMAX_DELAY);

    Serial.println("--- Task 2 running ---");
    Serial.println(WhichTune);

    digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
    delay(500);
    digitalWrite(LED_BUILTIN, LOW);   // turn the LED off (LOW is the voltage level)

    switch (WhichTune) {
      case BEEP_SHORT:
        sound_short_warning();
        break;
      case BEEP_LONG:
        sound_long_warning();
        break;
      case BEEP_TUNE:
        sound_tune();
        break;
    }

    Serial.println("--- Task 2 ended ---");
    
  }
}




//
// ====================================================
//   SUB-ROUTINES BELOW HERE
// ====================================================
//


void QueueThingSpeak() {

  Serial.println("add ThinkSpeak to Q");
  ScreenPrint("T", STATUS_INDICATOR_X, STATUS_INDICATOR_Y + 20, FONT_MEDIUM);
  xQueueSend(queue_ThinkSpeak, &DoorStatus, portMAX_DELAY);

}


void QueueBeep(int whichbeep ) {

  Serial.println("add Beep to Q");
  if (whichbeep == BEEP_TUNE)   // flush out the tune Q - this one takes priorty
  {
    xQueueReset(queue_Beep);
  }
  xQueueSend(queue_Beep, &whichbeep, portMAX_DELAY);

}





String WebSignon() {
  //
  //   Message used by web server to report various tempratures etc
  //   also used as the body of any notification emails
  //

  buff = "Hi! This is Freezer Monitor.\n";
  if (LIVE_CODE == 0)
  {
    buff = buff + "Testbed version";
  }
  buff = buff + "\n\nThe current Freezer Temperature : " + String(temperatureC);
  buff = buff + "\n\nTodays Max/Min temps : " + String(MaxtemperatureC)  + " ~ " + String(MintemperatureC);
  buff = buff + "\n\nLong term Max/Min temps : " + String(LongMaxtemperatureC) + " ~ " + String(LongMintemperatureC);
  buff = buff + "\n\n\nFor online update ";
  buff = buff + "\nhttp://" +  WiFi.localIP().toString() + "/update";
  buff = buff + "\n\nView long term history at ThingSpeak";
  buff = buff + "\nhttps://thingspeak.com/channels/" + String(SECRET_CH_ID) + "/private_show";
  buff = buff + "\n\n\nDays running = " + DaysRunning;
  buff = buff + "\n\n\nWiFi Connection Count = " + EEPROM.read(1);

  return String(buff);
}





void CheckGmailAwake()
{

  if ( DayofYear > ThisDay )        //   These things only happen once per day
  {

    SendNotificationEmail(2);

    DaysRunning += 1;

    ResetMaxMin();

    ThisDay = DayofYear;



  }

}



void SendNotificationEmail(int email_index)
{
  char * msg_pointer;

  WebSignon().toCharArray(tmp, sizeof(tmp));    // Use the server signon msg as the body of the email

  switch (email_index) {
    case 1:
      msg_pointer = Subject1;
      break;
    case 2:
      msg_pointer = Subject2;
      break;
    case 3:
      msg_pointer = Subject3;
      break;
    case 4:
      msg_pointer = Subject4;
      break;
    case 5:
      msg_pointer = Subject5;
      break;
  }

  Serial.println("Send email ..");
  Serial.println(msg_pointer);

  ScreenPrint("E", STATUS_INDICATOR_X, STATUS_INDICATOR_Y + 20, FONT_MEDIUM);
  SendGmail(ToAddress, msg_pointer , tmp );

}





void InitScreen()
{

  if (DoorIsOpen == true)
  {
    return;              // more important messages to display than this
  }
  display.clearDisplay();
  if (LIVE_CODE == 1 ) {
    ScreenPrint("FREEZER", 5, 0, FONT_MEDIUM );
  }
  else
  {
    ScreenPrint("Freezer", 5, 0, FONT_MEDIUM );
  }
  ScreenPrint("Monitor", 5, 20, FONT_MEDIUM );
  ScreenPrint("By Chris Bartley", 5, 38, FONT_SMALL );
  ScreenPrint(WiFi.localIP().toString(), 5, 50, FONT_SMALL);
  display.display();
  delay(500);
}




//   ===========================================
//
//   Temperature Measurment  + Max/Min handling
//
//   ===========================================





void ResetMaxMin()
//
//   Force the ReadTemprature() code to reset the daily Max and Min temp variables
//
{
  first_temp_read = true;
}



void  ReadTemperature() {
  // Get the temperature
  //Serial.print("Read Temp = ");

  sensors.requestTemperatures();
  temperatureC = sensors.getTempCByIndex(0);


  if (first_temp_read == true)
  {
    MaxtemperatureC = temperatureC;
    MintemperatureC = temperatureC;
    first_temp_read = false;
  }
  else
  {
    if (temperatureC > MaxtemperatureC )
      MaxtemperatureC = temperatureC;

    if (temperatureC < MintemperatureC )
      MintemperatureC = temperatureC;
  }

  if (temperatureC > LongMaxtemperatureC )
    LongMaxtemperatureC = temperatureC;

  if (temperatureC < LongMintemperatureC )
    LongMintemperatureC = temperatureC;



  //  Serial.print(temperatureC);
  //  Serial.print(" degC  Max : ");
  //  Serial.print(MaxtemperatureC);
  //  Serial.print("  - Min : ");
  //  Serial.println(MintemperatureC);


}




//   =======================================
//
//   Sending information to the OLED display
//
//   =======================================



void DisplayTime() {

  int x;

  if (loop_count % 50 == 0 )
  {
    printLocalTime();       // Get the NTP time into 'timeinfo' structure
    Serial.println("Getting NTP");

  }
  strftime(buffer, 80, "%H:%M  %a %b %e", &timeinfo);
  ScreenPrint(buffer, 5, 45, FONT_SMALL );

  //  TEMP - report the wi-fi strength
  ScreenPrint(String(WiFi.RSSI()) , 5, 55, FONT_SMALL);


  DayofYear = timeinfo.tm_yday;           // make a note of the Day - so we can detect Midnight to do once a night stuff


  x = timeinfo.tm_isdst;

  //  Serial.print("dst : ");
  //  Serial.println(x);

}



void DisplayTemperature() {
  String status_char;

  // Output it to the display
  ScreenPrint(String(temperatureC), 5, 10, FONT_LARGE );

  ScreenPrint("C", STATUS_INDICATOR_X, 0, FONT_MEDIUM);

  switch (loop_count % 4) {             //  Display a rotating Status Indicator to prove system is active
    case 0:
      status_char = "-";
      break;
    case 1:
      status_char = "\\";
      break;
    case 2:
      status_char = "|";
      break;
    case 3:
      status_char = "/";
      break;
  }
  ScreenPrint(status_char, STATUS_INDICATOR_X, STATUS_INDICATOR_Y, FONT_MEDIUM);

  //ScreenPrint(String(MaxtemperatureC) ,0, 50, 2 );
  //ScreenPrint(String(MintemperatureC) ,70, 50, 2 );


}



//   =============================================
//
//   Wi-Fi connection and ThingSpeak handling code
//
//   =============================================





void ConnectToWiFi() {
  long wifi_wait;

  // Connect or reconnect to WiFi


  wifi_wait = 0;

  if (WiFi.status() != WL_CONNECTED) {

    
    Serial.println("---- wifi connection start ---");


    Serial.print("Attempting to connect to SSID: ");
    Serial.println(SECRET_SSID);

    while (WiFi.status() != WL_CONNECTED) {


      display.clearDisplay();
      ScreenPrint("Wi-Fi", 5, 0, 2 );
      ScreenPrint("Connecting", 5, 20, 2 );
      ScreenPrint(String(wifi_wait), 5, 50, 2 );
      display.display();

      Serial.println("disconnect");
      WiFi.disconnect(true);

      delay(1000);

      WiFi.mode(WIFI_STA);        // from https://rntlab.com/question/wifi-connection-drops-auto-reconnect/
      WiFi.begin(ssid, pass);  // Connect to WPA/WPA2 network. Change this line if using open or WEP network
      Serial.print(".");
      delay(3000 * wifi_wait);
      
      Serial.println("bump wifi wait counter");
      wifi_wait += 1;
      Serial.println (wifi_wait);

      if (wifi_wait > 20 )
      {
        Serial.println ("bad wi-fi trigger reboot");
        EEPROM.write( 0 ,  EEPROM_REBOOTED_CODE) ;
        EEPROM.commit();

        ESP.restart();
      }
    }

    WifiConnectionCount += 1;
    EEPROM.write(1, WifiConnectionCount );
    EEPROM.commit();


    Serial.println("\nConnected.");

    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    
    Serial.println("---- wifi connection end ---");

  }

  
}







void    UpdateThingSpeak(int DoorState) {

  // Comunicate with ThingSpeak servers
  // Send Field 1 - just an incremneting counter
  // Send Field 2 - The Freezer Temp in deg C
  // Send Field 3 - A Door Open (0) / Closed flag (100)
  // Send Field 4 - The Max Temprature
  // Send Field 5 - The Min temprature
  // Send Status  - Door state in text form


  int sizeofdelay;      // 15 sec delay padding

  Serial.print("Update ThingSpeak=");
  Serial.println(DoorState);


  // ThingSpeak can only received updates as a mas of every 15 seconds
  // There is a chance with the Door Open/Close that we might report to quickly for ThinkSpeak
  // So check when last update was sent -
  // Check if last update was less than 15 seconds ago - if so wait a variable num of seconds < 15
  //
  currMillis = millis();          // note current timestamp

  //Serial.println(currMillis);
  //Serial.println((LastThinkSpeakUpdateMillis + (15  * 1000)));

  // See if this update is with the 15 sec window of the last update otherwise ThinkSpeak will choke
  if (currMillis < (LastThinkSpeakUpdateMillis + (15  * 1000)))
  {
    //  Force a delay size to pad it out so that the update is not inside 15 second window
    //
    sizeofdelay = (currMillis - LastThinkSpeakUpdateMillis) / 1000;      // in seconds
    //    Serial.println(sizeofdelay);
    sizeofdelay = 16 - sizeofdelay;  // we need a delay to pad it up to 15 seconds from last update
    //    Serial.println(sizeofdelay);

    Serial.print("Sizeofdelay=");
    Serial.println(sizeofdelay);

    if (sizeofdelay > 20)           // CRASH AFTER  n days FIX ? - ensure delay can never be way too big
      sizeofdelay = 20;

    delay(sizeofdelay * 1000);      // ok - so now we do the padding delay
  }


  // set the fields with the values
  //
  ThingSpeak.setField(1, number1);                      // the Incrementing counter
  ThingSpeak.setField(2, String(temperatureC));         // the  Temperature falue

  ThingSpeak.setField(4, String(MaxtemperatureC));      // the Max Temperature falue
  ThingSpeak.setField(5, String(MintemperatureC));      // the Min Temperature falue


  Serial.print("door state=");
  Serial.println(DoorState);

  switch (DoorState)                        // and the position of the door  open/closed
  {
    case DOOR_OPEN:
      // set the status
      ThingSpeak.setStatus("DoorOpen !");
      ThingSpeak.setField(3, 100);
      break;
    case DOOR_CLOSED:
      ThingSpeak.setStatus("DoorClosed ! - Wifi-Sig:" + String(WiFi.RSSI()));
      ThingSpeak.setField(3, 1);
      break;
  }

  // write to the ThingSpeak channel
  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  Serial.println("Write Channel\n");
  if (x == 200) {
    Serial.print(number1);
    Serial.println(" - Channel update successful.");
  }
  else {
    Serial.println("Problem updating channel. HTTP error code " + String(x));
    
    SendNotificationEmail(4);   //  made stack 6000 to avoid crach when sending  this email
  }




  //  bump the incrementing count number
  number1++;
  if (number1 > 99) {
    number1 = 0;
  }

  LastThinkSpeakUpdateMillis = millis();       // record timestamp of the latest update

  Serial.println("-------");

}
