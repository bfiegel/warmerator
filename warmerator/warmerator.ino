// This Arduino sketch reads DS18B20 "1-Wire" digital temperature sensors.
// as derived from the Tutorial:
// http://www.hacktronics.com/Tutorials/arduino-1-wire-tutorial.html
//
// then it uses that temperature information to control a relay in order to warm/cool the monitor space.
//
// Various UI elements are displayed on an LCD and control of the SET temperature is through a potentiometer
//
// Various pieces of code have been taken from different sources.
// Although I have brought them together and made a number of changes, I have not written it all from scratch.
//
#define USE_LCD        1
#define USE_SD_CARD    0
#define USE_THINGSPEAK 0
#define USE_GOOGLE     0

#if USE_THINGSPEAK || USE_GOOGLE
# define USE_INTERNET   1
# define USE_ETHERNET   1
# define USE_WIFI       0
#else
# define USE_ETHERNET   0
# define USE_WIFI       0
#endif

#include <avr/wdt.h>
#include <PID_v1.h>

#include <OneWire.h>
#include <DallasTemperature.h>

#if USE_LCD
#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>
#endif

#if USE_SD_CARD
#include <SPI.h>
#include <SD.h>
#endif

// In order to prevent the IDE from including these, you have to comment them out, not just ifdef them
#if USE_ETHERNET
#include <SPI.h>
#include <Ethernet.h>
#endif

#define numberof(x) (sizeof((x))/sizeof 0[(x)])

#define TEMPERATURE_POT_PIN A0
#define MIN_SET_TEMPERATURE 60
#define MAX_SET_TEMPERATURE 120

#define HARDWARE_SS_PIN 10
#define SHIELD_SS_PIN   4
#define LOG_FILE_NAME   "w.csv"

#define SERIAL_PRINT_PERIOD 500

#define INITIAL_PID_WINDOW_SIZE    5000
#define AGGRESSIVE_PID_KP INITIAL_PID_WINDOW_SIZE/5
#define AGGRESSIVE_PID_KI 4
#define AGGRESSIVE_PID_KD 0
#define CONSERVATIVE_PID_KP INITIAL_PID_WINDOW_SIZE/5
#define CONSERVATIVE_PID_KI 4
#define CONSERVATIVE_PID_KD 0
#define INITIAL_TARGET_TEMPERATURE 72
#define CRITICALLY_HOT_OFFSET      10
#define CRITICALLY_COOL_OFFSET     INITIAL_TARGET_TEMPERATURE

// Data wire is plugged into pin 13 on the Arduino
#define ONE_WIRE_BUS_PIN 13
#define THERMOMETER_SAMPLE_TIME 5000

#define LCD_RESET_PERIOD 60000
#define I2C_ADDR 0x27
#define BACKLIGHT_PIN 3
#define En_pin 2
#define Rw_pin 1
#define Rs_pin 0
#define D4_pin 4
#define D5_pin 5
#define D6_pin 6
#define D7_pin 7

#define LCD_SET_TEMP_LINE     0
#define LCD_CURRENT_TEMP_LINE 1
#define LCD_UPTIME_LINE       2
#define LCD_STATUS_LINE       3
#define HEATING_STRING        "Heat"
#define COOLING_STRING        "Cool"
#define COASTING_STRING       "Coast"


#if USE_INTERNET
// assign a MAC address for the ethernet controller.
// fill in your address here:
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};

#if USE_ETHERNET
// initialize the library instance:
EthernetClient client;
#endif

char dataServer[] = 
#if USE_THINGSPEAK
  "www.thingspeak.com";
#else
#if USE_GOOGLE
  "www.google.com";
#endif
#endif

#define APIKEY         "YOUR API KEY GOES HERE" // replace your xively api key here
#define FEEDID         00000 // replace your feed ID
#define USERAGENT      "My Project" // user agent is the project name

#endif

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS_PIN);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature temperatureSensors(&oneWire);

#if USE_LCD
// UI LCD
LiquidCrystal_I2C lcd(I2C_ADDR,En_pin,Rw_pin,Rs_pin, D4_pin,D5_pin,D6_pin,D7_pin);
unsigned long lcdResetTime;
#endif

// Assign the addresses of your 1-Wire temp sensors.
// See the tutorial on how to obtain these addresses:
// http://www.hacktronics.com/Tutorials/arduino-1-wire-address-finder.html

//DeviceAddress thermometer = { 0x28, 0x24, 0xF0, 0xB4, 0x05, 0x00, 0x00, 0x78 };
//DeviceAddress thermometer = { 0x28, 0x24, 0xF0, 0xB4, 0x05, 0x00, 0x00, 0x78 };
//DeviceAddress thermometer = { 0x28, 0x24, 0xF0, 0xB4, 0x05, 0x00, 0x00, 0x78 };
//DeviceAddress thermometer = { 0x28, 0x24, 0xF0, 0xB4, 0x05, 0x00, 0x00, 0x78 };
DeviceAddress thermometer = { 0x28, 0xF0, 0xFD, 0xB4, 0x05, 0x00, 0x00, 0x8C };

char heatRelayPin = 5;

// Define Temperature control variables
double  currentTemp     = INITIAL_TARGET_TEMPERATURE;
double  targetTemp      = INITIAL_TARGET_TEMPERATURE;
double  pidOutput;
char   *currentActivity = COASTING_STRING;
char   *previousActivity = COASTING_STRING;

//Specify the links and initial tuning parameters
PID warmingPID(&currentTemp, &pidOutput, &targetTemp, 
               CONSERVATIVE_PID_KP, CONSERVATIVE_PID_KI, CONSERVATIVE_PID_KD, DIRECT);

int WindowSize = INITIAL_PID_WINDOW_SIZE;
unsigned long windowStartTime;
unsigned long serialTime;

/******************************************************************/
#if USE_SD_CARD
static void logTemperature(String logData)
{
  File logFile;

  logFile = SD.open(LOG_FILE_NAME, FILE_WRITE);

  if (logFile)
  {
    logFile.println(logData);
    logFile.close();
  }
  else
  {
    Serial.println("#error writing to log file");
  }
}
#endif

#if USE_INTERNET
// this method makes a HTTP connection to the server:
static void uploadTemperature(int thisData)
{
  // if there's a successful connection:
  if (client.connect(dataServer, 80))
  {
    Serial.println("#connecting...");
    // send the HTTP PUT request:
    client.print("PUT /v2/feeds/");
    client.print(FEEDID);
    client.println(".csv HTTP/1.1");
    client.println("Host: api.xively.com");
    client.print("X-XivelyApiKey: ");
    client.println(APIKEY);
    client.print("User-Agent: ");
    client.println(USERAGENT);
    client.print("Content-Length: ");

    // calculate the length of the sensor reading in bytes:
    // 8 bytes for "sensor1," + number of digits of the data:
    int thisLength = 8 + getLength(thisData);
    client.println(thisLength);

    // last pieces of the HTTP PUT request:
    client.println("Content-Type: text/csv");
    client.println("Connection: close");
    client.println();

    // here's the actual content of the PUT request:
    client.print("sensor1,");
    client.println(thisData);

  }
  else
  {
    // if you couldn't make a connection:
    Serial.println("#connection failed");
    Serial.println("#");
    Serial.println("#disconnecting.");
    client.stop();
  }
}

int getLength(int someValue) {
  // there's at least one byte:
  int digits = 1;
  // continually divide the value by ten,
  // adding one to the digit count for each
  // time you divide, until you're at 0:
  int dividend = someValue / 10;
  while (dividend > 0) {
    dividend = dividend / 10;
    digits++;
  }
  // return the number of digits:
  return digits;
}
#endif

static void resetLcd()
{
#if USE_LCD
  /* Start the Liquid Crystal Display library */
  lcd.begin (20,4,LCD_5x8DOTS);
  lcd.setBacklightPin(BACKLIGHT_PIN, POSITIVE);
  lcd.setBacklight(HIGH);
  lcdResetTime = millis();
#endif
}

static void reportTemperature(unsigned long time, double target, double current, char *activity)
{
  String logData = "";
  static long previousTarget = -255;
  static long previousTemp   = -255;
  
  logData = String(time) + "," + activity + "," + String(target) + "," + String(current);
  
#if USE_LCD
  lcd.clear();
#endif
  if (0 > current)
  {
//    if ((previousTemp != current) || (previousTarget != target))
    {
      Serial.println("#Error getting temperature");
    }
#if USE_LCD
    lcd.setCursor(0,3);
    lcd.print("Error getting temperature");
#endif
  }
  else
  {
//    if ((previousTemp != current) || (previousTarget != target))
    {
      Serial.println(logData);
    }
 #if USE_LCD
    lcd.setCursor(0,LCD_SET_TEMP_LINE);
    lcd.print("Set:     ");
    lcd.print(target);
    lcd.setCursor(0,LCD_CURRENT_TEMP_LINE);
    lcd.print("Current: ");
    lcd.print(current);
    lcd.setCursor(0,LCD_UPTIME_LINE);
    lcd.print("Uptime:  ");
    lcd.print(millis());
    lcd.setCursor(0,LCD_STATUS_LINE);
    lcd.print(activity);
#endif  
  }
  previousTemp   = current;
  previousTarget = target;

#if USE_SD_CARD  
  logTemperature(logData);
#endif

#if USE_INTERNET
  uploadTemperature(current);
#endif
}

static void setTemperatureRelay(unsigned char HEAT)
{
/*
  Serial.print("#Setting Heat pin ");
  Serial.print(heatRelayPin);
  Serial.print(" to ");
  Serial.println(HEAT);
*/
  pinMode(heatRelayPin,OUTPUT);
  digitalWrite(heatRelayPin,!HEAT);
}

static void getTargetTemperature(double *target)
{
  double potValue = analogRead(TEMPERATURE_POT_PIN);
  double offset   = (potValue /1023.0) * (MAX_SET_TEMPERATURE-MIN_SET_TEMPERATURE);
  *target = MIN_SET_TEMPERATURE 
            + (round(offset * 2.0)/2.0); 
}

void setup()
{
  unsigned int i;
  
  /* Start the Serial interface, 9600 is fast enough and is pretty standard */
  Serial.begin(9600);

  /* Set the potentiometer pin as input */
  pinMode(TEMPERATURE_POT_PIN, INPUT);
  getTargetTemperature(&targetTemp);
  
  /* assume that the current temperature is the target */
  currentTemp = targetTemp;
  
  /* turn off the relays until we are sure of what we should be doing */
  setTemperatureRelay(LOW); 
  
  // Start up the temperature sensor library
  temperatureSensors.begin();
  
  // set the resolution to 10 bit
  temperatureSensors.setResolution(thermometer, 12);

#if USE_LCD
  resetLcd();
#endif

  serialTime = 0;
  /* Iniitalize the PID algorhythm variables */
  windowStartTime = millis();
  //tell the PID to range between 0 and the full window size
  warmingPID.SetOutputLimits(0, WindowSize);
  //turn the PID on
  warmingPID.SetMode(AUTOMATIC);
  //the probe is very slow to respond to change, let the PID know
  warmingPID.SetSampleTime(THERMOMETER_SAMPLE_TIME);
  
#if USE_SD_CARD
  // Set the SD Card libraray Chip Select pin for the SD card to OUTPUT
  pinMode(HARDWARE_SS_PIN, OUTPUT);
  
  // Initialize the SD card
  if (!SD.begin(SHIELD_SS_PIN))
  {
    Serial.println("SD Card begin failure");
  }
  else
  {
    Serial.println("card initialized.");
  }
#endif

#if USE_ETHERNET
  // give the ethernet module time to boot up:
  delay(1000);
  // start the Ethernet connection using a fixed IP address and DNS server:
  if (0 != Ethernet.begin(mac))
  {
    // print the Ethernet board/shield's IP address:
    Serial.print("#My IP address: ");
    for (byte thisByte = 0; thisByte < 4; thisByte++)
    {
      // print the value of each byte of the IP address:
      Serial.print(Ethernet.localIP()[thisByte], DEC);
      Serial.print(".");
    }
    Serial.println();
  }
  else
  {
    Serial.println("#error getting address");
  }
#endif
  wdt_enable (WDTO_8S);  // reset after one second, if no "pat the dog" received
}

void coast()
{
  setTemperatureRelay(LOW);
  previousActivity=currentActivity;
  currentActivity = COASTING_STRING;
}

void heat()
{
  setTemperatureRelay(HIGH);
  previousActivity=currentActivity;
  currentActivity = HEATING_STRING;
}

void loop()
{
  double gap;
  unsigned long time;

  wdt_reset ();
  getTargetTemperature(&targetTemp);
  temperatureSensors.requestTemperatures();
  currentTemp = temperatureSensors.getTempF(thermometer); 

  gap = abs(targetTemp-currentTemp); //distance away from setpoint
  if(gap<1)
  {  //we're close to setpoint, use conservative tuning parameters
    warmingPID.SetTunings(CONSERVATIVE_PID_KP, CONSERVATIVE_PID_KI, CONSERVATIVE_PID_KD);
  }
  else
  {
     //we're far from setpoint, use aggressive tuning parameters
     warmingPID.SetTunings(AGGRESSIVE_PID_KP, AGGRESSIVE_PID_KI, AGGRESSIVE_PID_KD);
  }
  
  warmingPID.Compute();
  
  time = millis(); 
  // This checks for rollover with millis()
  if (time < windowStartTime) {
    windowStartTime = 0;
  }
  
  if ((time - windowStartTime) > WindowSize)
  { //time to shift the Relay Window
    windowStartTime = time;
  }

  /************************************************
   * turn the output pin on/off based on pid output
   ************************************************/    
  if(pidOutput > (time - windowStartTime))
  {
    heat();
  }
  else
  {
    coast();
  }
  
  if (time < lcdResetTime)
  {
    lcdResetTime = 0; 
  }

  if (time > (lcdResetTime + LCD_RESET_PERIOD))
  {
    resetLcd();
  }
  
  // This checks for rollover with millis()
  if (time < serialTime) {
    serialTime = 0;
  }
  
  if ((time > serialTime) || (currentActivity != previousActivity))
  { //time to shift the Relay Window
    serialTime = time + SERIAL_PRINT_PERIOD;
    Serial.print("#time=");Serial.print(time);
    Serial.print(" windowStartTime=");Serial.print(windowStartTime);
    Serial.print(" pidOutput=");Serial.print(pidOutput);
    Serial.print(" Kp=");Serial.print(warmingPID.GetKp());
    Serial.print(" Ki=");Serial.print(warmingPID.GetKi());
    Serial.print(" Kd=");Serial.println(warmingPID.GetKd());
    reportTemperature(time, targetTemp, currentTemp, currentActivity);
  }
//  delay(500);
}
