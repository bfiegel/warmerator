
// This Arduino sketch reads DS18B20 "1-Wire" digital temperature sensors.
// as derived from the Tutorial:
// http://www.hacktronics.com/Tutorials/arduino-1-wire-tutorial.html
//
// then it uses that temperature information to control a relay in order to warm/cool the monitor space.
//
// Various UI elements are displayed on an LCD and control of the SET temperature is through a potentiometer
#include <OneWire.h>
#include <DallasTemperature.h>

#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>
#include <PID_v1.h>

#define RelayPin 6

#define INITIAL_PID_WINDOW_SIZE 5000
#define INITIAL_TARGET_TEMPERATURE 22.222222
#define CRITICALLY_HOT_OFFSET 10
#define CRITICALLY_COOL_OFFSET INITIAL_TARGET_TEMPERATURE
#define HEATING_STRING "Heating"
#define COOLING_STRING "Cooling"
#define COASTING_STRING  "Coasting"
#define MAX_SET_TEMPERATURE 125.0
#define SET_TEMP_LINE 0
#define CURRENT_TEMP_LINE 1
#define STATUS_LINE 3
#define numberof(x) ((x)[0]/sizeof((x)[0]))

// Data wire is plugged into pin 9 on the Arduino
#define ONE_WIRE_BUS 13

#define I2C_ADDR 0x27
#define BACKLIGHT_PIN 3
#define En_pin 2
#define Rw_pin 1
#define Rs_pin 0
#define D4_pin 4
#define D5_pin 5
#define D6_pin 6
#define D7_pin 7

#define TEMPERATURE_POT_PIN A0

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature temperatureSensors(&oneWire);

// UI LCD
LiquidCrystal_I2C lcd(I2C_ADDR,En_pin,Rw_pin,Rs_pin, D4_pin,D5_pin,D6_pin,D7_pin);

// Assign the addresses of your 1-Wire temp sensors.
// See the tutorial on how to obtain these addresses:
// http://www.hacktronics.com/Tutorials/arduino-1-wire-address-finder.html

DeviceAddress thermometer = { 0x28, 0x24, 0xF0, 0xB4, 0x05, 0x00, 0x00, 0x78 };

unsigned char heatRelayPin[1] = {7};//4,5,6,7};
unsigned char chillRelayPin[0] = {};

// Define Temperature control variables
double currentTemp     = INITIAL_TARGET_TEMPERATURE;
double targetTemp      = INITIAL_TARGET_TEMPERATURE;
double pidOutput;
char  *currentActivity = COASTING_STRING;
int    tempSetPotPin   = TEMPERATURE_POT_PIN;

//Specify the links and initial tuning parameters
PID warmingPID(&currentTemp, &pidOutput, &targetTemp, 2,5,1, DIRECT);

int WindowSize = INITIAL_PID_WINDOW_SIZE;
unsigned long windowStartTime;

static void printTemperature(double target, double current, char *activity)
{
  static double previousTarget = -255;
  static double previousTemp = -255;
  
  lcd.clear();
  if (-127.00 == current)
  {
    if ((previousTemp != current) || (previousTarget != target))
    {
      Serial.println("Error getting temperature");
      Serial.println("\n\r");
    }
    lcd.setCursor(0,3);
    lcd.print("Error getting temperature");
  }
  else
  {
    if ((previousTemp != current) || (previousTarget != target))
    {
      Serial.print("Target is: C: ");
      Serial.print(target);
      Serial.print(" Temperature is: C:");
      Serial.print(current);
      Serial.print(" F: ");
      Serial.print(DallasTemperature::toFahrenheit(current));
      Serial.print(" Activity: ");
      Serial.println(activity);
      Serial.println("\n\r");
    }
    lcd.setCursor(0,SET_TEMP_LINE);
    lcd.print("Set:     ");
    lcd.print(DallasTemperature::toFahrenheit(target)); 
    lcd.setCursor(0,CURRENT_TEMP_LINE);
    lcd.print("Current: ");
    lcd.print(DallasTemperature::toFahrenheit(current));
    lcd.setCursor(0,STATUS_LINE);
    lcd.print(activity);  
  }
  previousTemp   = current;
  previousTarget = target;
}

static void setTemperatureRelays(unsigned char HEAT, unsigned char CHILL)
{
  unsigned int i;
  /* Set the heat relay pins to output low */
  for(i = 0; i < numberof(heatRelayPin); i++)
  {
    pinMode(heatRelayPin[i],OUTPUT);
    digitalWrite(heatRelayPin[i],HEAT);
  }
    
  /* Set the chill relay pins to output low */
  for(i = 0; i < numberof(chillRelayPin); i++)
  {
    pinMode(chillRelayPin[i],OUTPUT);
    digitalWrite(chillRelayPin[i],CHILL);
  }
}

static void getTargetTemperature(double *target)
{
  double potValue;  
  potValue = analogRead(tempSetPotPin);
    
  *target = MAX_SET_TEMPERATURE * (potValue /1023.0); 
}

void setup()
{
  unsigned int i;
  
  /* Start the Serial interface, 9600 is fast enough and is pretty standard */
  Serial.begin(9600);

  /* Set the potentiometer pin as input */
  pinMode(tempSetPotPin, INPUT);
  getTargetTemperature(&targetTemp);
  
  /* assume that the current temperature is the target */
  currentTemp = targetTemp;
  
  /* turn off the relays until we are sure of what we should be doing */
  setTemperatureRelays(LOW,LOW); 
  
  // Start up the temperature sensor library
  temperatureSensors.begin();
  
  // set the resolution to 10 bit
  temperatureSensors.setResolution(thermometer, 10);
  
  /* Start the Liquid Crystal Display library */
  lcd.begin (20,4,LCD_5x8DOTS);
  lcd.setBacklightPin(BACKLIGHT_PIN, POSITIVE);

  /* Iniitalize the PID algorhythm variables */
  windowStartTime = millis();
  //tell the PID to range between 0 and the full window size
  warmingPID.SetOutputLimits(0, WindowSize);
  //turn the PID on
  warmingPID.SetMode(AUTOMATIC);
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
  lcd.setBacklight(HIGH);
  lcd.home();
  lcd.setBacklight(HIGH);
  
  getTargetTemperature(&targetTemp);
//  delay(2000);
//  Serial.print("Getting temperatures...\n\r");
  temperatureSensors.requestTemperatures();
  currentTemp = temperatureSensors.getTempC(thermometer);
  
  printTemperature(targetTemp, currentTemp, currentActivity);
  
  warmingPID.Compute();

  /************************************************
   * turn the output pin on/off based on pid output
   ************************************************/
  if ((millis() - windowStartTime) > WindowSize)
  { //time to shift the Relay Window
    windowStartTime += WindowSize;
  }
  
  if(pidOutput < (millis() - windowStartTime))
  {
    heat();
  }
  else
  {
    coast();
  }
  
  if (currentTemp >= (targetTemp+CRITICALLY_HOT_OFFSET))
  {
    chill(); 
  }
  else if (currentTemp <= (targetTemp-CRITICALLY_COOL_OFFSET))
  {
    heat();
  }
}
