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

#if USE_ETHERNET
#include <SPI.h>
#include <Ethernet.h>
#endif

#define numberof(x) (sizeof((x))/sizeof 0[(x)])

#define TEMPERATURE_POT_PIN A0
#define MAX_SET_TEMPERATURE 125.0

#define HARDWARE_SS_PIN 10
#define SHIELD_SS_PIN   4
#define LOG_FILE_NAME   "warmerator.csv"

#define INITIAL_PID_WINDOW_SIZE    5000
#define INITIAL_TARGET_TEMPERATURE 22.222222
#define CRITICALLY_HOT_OFFSET      10
#define CRITICALLY_COOL_OFFSET     INITIAL_TARGET_TEMPERATURE

// Data wire is plugged into pin 13 on the Arduino
#define ONE_WIRE_BUS_PIN 13

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
#define LCD_STATUS_LINE       3
#define HEATING_STRING        "Heating"
#define COOLING_STRING        "Cooling"
#define COASTING_STRING       "Coasting"


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
#endif

// Assign the addresses of your 1-Wire temp sensors.
// See the tutorial on how to obtain these addresses:
// http://www.hacktronics.com/Tutorials/arduino-1-wire-address-finder.html

DeviceAddress thermometer = { 0x28, 0x24, 0xF0, 0xB4, 0x05, 0x00, 0x00, 0x78 };

char heatRelayPin[1] = {5};
char coolRelayPin[1] = {-1};

// Define Temperature control variables
double currentTemp     = INITIAL_TARGET_TEMPERATURE;
double targetTemp      = INITIAL_TARGET_TEMPERATURE;
double pidOutput;
char  *currentActivity = COASTING_STRING;

//Specify the links and initial tuning parameters
PID warmingPID(&currentTemp, &pidOutput, &targetTemp, 2,5,1, DIRECT);

int WindowSize = INITIAL_PID_WINDOW_SIZE;
unsigned long windowStartTime;

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

static void reportTemperature(unsigned long time, double target, double current, char *activity)
{
  String logData = "";
  static double previousTarget = -255;
  static double previousTemp   = -255;
  
  logData = String(time) + "," + activity + "," + String(target) + "," + String(current);
  
#if USE_LCD
  lcd.clear();
#endif
  if (-127.00 == current)
  {
    if ((previousTemp != current) || (previousTarget != target))
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
    lcd.print(DallasTemperature::toFahrenheit(target)); 
    lcd.setCursor(0,LCD_CURRENT_TEMP_LINE);
    lcd.print("Current: ");
    lcd.print(DallasTemperature::toFahrenheit(current));
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

static void setTemperatureRelays(unsigned char HEAT, unsigned char COOL)
{
  unsigned int i;
#if 1
  /* Set the heat relay pins to output low */
  for(i = 0; i < numberof(heatRelayPin); ++i)
  {
    if (0 <= heatRelayPin[i])
    { 
/*
      Serial.print("#Setting Heat pin ");
      Serial.print(heatRelayPin[i]);
      Serial.print(" to ");
      Serial.println(HEAT);
*/
      pinMode(heatRelayPin[i],OUTPUT);
      digitalWrite(heatRelayPin[i],HEAT);
    }
  }
#endif
#if 0    
  /* Set the chill relay pins to output low */
  for(i = 0; i < numberof(coolRelayPin); ++i)
  {
    if (0 <= coolRelayPin[i])
    { 
/* 
      Serial.print("#Setting Cool pin ");
      Serial.print(coolRelayPin[i]);
      Serial.print(" to ");
      Serial.println(COOL);
*/
      pinMode(coolRelayPin[i],OUTPUT);
      digitalWrite(coolRelayPin[i],COOL);
    }
  }
#endif
}

static void getTargetTemperature(double *target)
{
  double potValue = 181.8666; /* set default value to 72 degrees F */  
  potValue = analogRead(TEMPERATURE_POT_PIN);
    
  *target = MAX_SET_TEMPERATURE * (potValue /1023.0); 
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
  setTemperatureRelays(LOW,LOW); 
  
  // Start up the temperature sensor library
  temperatureSensors.begin();
  
  // set the resolution to 10 bit
  temperatureSensors.setResolution(thermometer, 10);

#if USE_LCD
  /* Start the Liquid Crystal Display library */
  lcd.begin (20,4,LCD_5x8DOTS);
  lcd.setBacklightPin(BACKLIGHT_PIN, POSITIVE);
  lcd.setBacklight(HIGH);
#endif

  /* Iniitalize the PID algorhythm variables */
  windowStartTime = millis();
  //tell the PID to range between 0 and the full window size
  warmingPID.SetOutputLimits(0, WindowSize);
  //turn the PID on
  warmingPID.SetMode(AUTOMATIC);
  
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
}

void coast()
{
  setTemperatureRelays(LOW,LOW);
  currentActivity = COASTING_STRING;
}

void heat()
{
  setTemperatureRelays(HIGH,LOW);
  currentActivity = HEATING_STRING;
}

void chill()
{
  setTemperatureRelays(LOW,HIGH);
  currentActivity = COOLING_STRING;  
}

void loop()
{
  unsigned long time;

  getTargetTemperature(&targetTemp);
  temperatureSensors.requestTemperatures();
  currentTemp = temperatureSensors.getTempC(thermometer);
  time = millis();
  reportTemperature(time, targetTemp, currentTemp, currentActivity);
  
  warmingPID.Compute();

  /************************************************
   * turn the output pin on/off based on pid output
   ************************************************/
  time = millis();
  if ((time - windowStartTime) > WindowSize)
  { //time to shift the Relay Window
    windowStartTime += WindowSize;
  }
  
  if(pidOutput > (time - windowStartTime))
  {
    heat();
  }
  else
  {
    coast();
  }
/*  
  if (currentTemp >= (targetTemp+CRITICALLY_HOT_OFFSET))
  {
    chill(); 
  }
  else if (currentTemp <= (targetTemp-CRITICALLY_COOL_OFFSET))
  {
    heat();
  }
*/
  Serial.print("#time = ");Serial.println(time);
  Serial.print("#windowStartTime = ");Serial.println(windowStartTime);
  Serial.print("#pidOutput = ");Serial.println(pidOutput);
}
