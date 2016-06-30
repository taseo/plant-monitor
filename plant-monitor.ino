#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <dht.h>
#include <SPI.h>
#include <SD.h>

// Pin declarations

#define csPin 4
#define dhtPin 5
#define interruptPin 7
#define soilMositureTransistorPin 8
#define errorPin 13
#define photoresistorPin A0
#define thermistorPin A1
#define soilMoisturePin A2

// Coefficients for calculations

#define thermistorSeries 10000
#define bCoefficient 3950
#define temperatureNormal 25
#define thermistorNominal 10000

// Global variables

int dhtStatus;
int dhtTemperature;
int dhtHumidity;

#define analogueSamples 5

volatile int rollState = 1;

unsigned long logId = 1;
unsigned long lastDhtAccess = 0;
unsigned long lastSoilMositureAccess = 0;
unsigned long lastLogRecordTime = 0;

char clearLCD[17] = "                ";
char dhtError[17];

float soilMoistureAverageReading;

// Objects

LiquidCrystal_I2C lcd(0x3F, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);
dht DHT;
File logFile;

void setup() {
  pinMode(interruptPin, INPUT);
  pinMode(soilMositureTransistorPin, OUTPUT);
  pinMode(csPin, OUTPUT);
  pinMode(errorPin, OUTPUT);

  analogReference(EXTERNAL);

  attachInterrupt(digitalPinToInterrupt(interruptPin), interruptHandler, CHANGE);

  lcd.begin(16, 2);

  readSensorsOnStartup();

  // SD card initialization

  if (!SD.begin(csPin)) {
    digitalWrite(errorPin, HIGH);
    return;
  }

  // Header for log file

  logFile = SD.open ("log.csv", FILE_WRITE);

  if (logFile) {
    char headerBreak[12] = " , , , , , ";
    logFile.println(headerBreak);
    char header[102];
    sprintf(header, "ID, Temperature (%sC), Humudity (%c), Lighting (lx), Thermistor (%sC), Soil Moisture", "\u00B0", 37, "\u00B0");
    logFile.println(header);
    logFile.close();
  }
}

// Reads DHT Tempereature/Humidity and Soil Moisture on startup

void readSensorsOnStartup() {
  dhtStatus = DHT.read11(dhtPin);
  dhtTemperature = DHT.temperature;
  dhtHumidity = DHT.humidity;
  digitalWrite(soilMositureTransistorPin, HIGH);
  delay(20);
  soilMoistureAverageReading = readSensor(soilMoisturePin);
  digitalWrite(soilMositureTransistorPin, LOW);
}

void loop() {

  unsigned long upTime = millis();

  // Reads DHT sensor and stores values in respective variables

  if (upTime - lastDhtAccess > 2000) {
    dhtStatus = DHT.read11(dhtPin);
    dhtTemperature = DHT.temperature;
    dhtHumidity = DHT.humidity;
    lastDhtAccess = upTime;
  }

  // Illuminance calculation (it is rather inaccurate as voltage can fluctuate and sensor isn't accurate)

  float photoresistorAverageReading = readSensor(photoresistorPin);
  photoresistorAverageReading = photoresistorAverageReading * 0.00322265625; // 3.3 V / 1024 = 0.00322265625
  photoresistorAverageReading = 500 / (10 * ((3.3 - photoresistorAverageReading) / photoresistorAverageReading));
  int lux = (int)round(photoresistorAverageReading);

  char lightCondition[12] = "";

  if (lux < 10) {
    strcpy(lightCondition, "pitch black");
  } else if (lux < 50) {
    strcpy(lightCondition, "very dark  ");
  } else if (lux < 200) {
    strcpy(lightCondition, "dark inside");
  } else if (lux < 400) {
    strcpy(lightCondition, "dim inside ");
  } else if (lux < 1000) {
    strcpy(lightCondition, "normal in  ");
  } else if (lux < 5000) {
    strcpy(lightCondition, "bright in  ");
  } else if (lux < 10000) {
    strcpy(lightCondition, "dim outside");
  } else if (lux < 30000) {
    strcpy(lightCondition, "cloudy out ");
  } else {
    strcpy(lightCondition, "direct sun ");
  }

  // Reads thermistor multiple times for accuracy and calculates the average result

  float thermistorAverageReading = readSensor(thermistorPin);

  // Steinhart–Hart equation to calculate temperature

  thermistorAverageReading = ( 1023 / thermistorAverageReading) - 1 ;
  thermistorAverageReading = thermistorSeries / thermistorAverageReading;

  float thermistorTemperature;

  thermistorTemperature = thermistorAverageReading / thermistorNominal;
  thermistorTemperature = log(thermistorTemperature);
  thermistorTemperature /= bCoefficient;
  thermistorTemperature += 1.0 / (temperatureNormal + 273.15);
  thermistorTemperature = 1.0 / thermistorTemperature;
  thermistorTemperature -= 273.15;

  // Thermistor reading from Float to String

  char thermistorStringValue[6];
  int whole = (int)thermistorTemperature;
  int fraction = (int)(thermistorTemperature * 100) % 100;
  sprintf(thermistorStringValue, "%d.%d", whole, fraction);

  // Reads soil moisture once in 15 minutes multiple times for accuracy and calculates the average result

  if (upTime - lastSoilMositureAccess > 900000) {
    digitalWrite(soilMositureTransistorPin, HIGH);
    delay(20);
    soilMoistureAverageReading = readSensor(soilMoisturePin);
    digitalWrite(soilMositureTransistorPin, LOW);
    lastSoilMositureAccess = upTime;
  }

  // Logs data on SD card

  if (upTime - lastLogRecordTime > 60000) {
    char logData[40];
    sprintf(logData, "%lu, %d, %d, %d, %s, %d", logId, dhtTemperature, dhtHumidity, lux, thermistorStringValue, (int)soilMoistureAverageReading);

    logFile = SD.open("log.csv", FILE_WRITE);

    if (logFile) {
      logFile.println(logData);
      logFile.close();
    } else {
      digitalWrite(errorPin, HIGH);
    }

    logId++;
    lastLogRecordTime = upTime;
  }

  // Switch that operates programs menu on lcd (based on the value of rollState variable)

  switch (rollState) {
    case 1:
      displayDhtReading(dhtStatus, dhtTemperature, dhtHumidity);
      delay(500);
      break;
    case 2:
      displayPhotoresistorReading(lux, lightCondition);
      delay(500);
      break;
    case 3:
      displayThermistorAndMoistureReading(thermistorStringValue, soilMoistureAverageReading);
      delay(500);
      break;
  }
}

// Function that handles menu button's interrupt and launches changeRollSate function

void interruptHandler() {
  static unsigned long lastInterruptTime = 0;
  unsigned long interruptTime = millis();

  if (interruptTime - lastInterruptTime > 200) {
    changeRollSate();
  }
  lastInterruptTime = interruptTime;
}

// Function that restricts LCD roll state (possition in the menu) between 1 and 3

int changeRollSate() {
  if (rollState > 2) {
    rollState = 1;
  } else {
    rollState++;
  }
  return rollState;
}

// Function that displays DHT temperature and humidity on LCD

void displayDhtReading(int dhtStatus, int dhtTemperature, int dhtHumidity) {
  switch (dhtStatus) {
    case DHTLIB_OK:
      char temperatureLine[19];
      char humidityLine[19];
      sprintf(temperatureLine, "Temp1: %d%cC       ", dhtTemperature, 223);
      sprintf(humidityLine, "Hum: %d%c        ", dhtHumidity, 37);
      lcd.setCursor(0, 0);
      lcd.print(temperatureLine);
      lcd.setCursor(0, 1);
      lcd.print(humidityLine);
      break;
    case DHTLIB_ERROR_CHECKSUM:
      sprintf(dhtError, "DHT Checksum Err");
      lcd.setCursor(0, 0);
      lcd.print(dhtError);
      lcd.setCursor(0, 1);
      lcd.print(clearLCD);
      break;
    case DHTLIB_ERROR_TIMEOUT:
      sprintf(dhtError, "DHT Timeout Err ");
      lcd.setCursor(0, 0);
      lcd.print(dhtError);
      lcd.setCursor(0, 1);
      lcd.print(clearLCD);
      break;
    default:
      sprintf(dhtError, "DHT Unknown Err ");
      lcd.setCursor(0, 0);
      lcd.print(dhtError);
      lcd.setCursor(0, 1);
      lcd.print(clearLCD);
      break;
  }
}

// Function that displays illuminance on LCD

void displayPhotoresistorReading (int lux, char* lightCondition) {
  char lightFirstLine[17];
  char lightSecondLine[21];
  sprintf(lightFirstLine, "It's %s", lightCondition);
  sprintf(lightSecondLine, "%d lx             ", lux);
  lcd.setCursor(0, 0);
  lcd.print(lightFirstLine);
  lcd.setCursor(0, 1);
  lcd.print(lightSecondLine);
}

// Function that displays temperature from analogue thermistor and soil moisture sensor

void displayThermistorAndMoistureReading (char* thermistorTemperature, int moistureReading) {
  char thermistorLine[19];
  char soilMoistureLine[20];
  sprintf(thermistorLine, "Temp2: %s%cC   ", thermistorTemperature, 223);
  sprintf(soilMoistureLine, "Soil moist: %d   ", moistureReading);
  lcd.setCursor(0, 0);
  lcd.print(thermistorLine);
  lcd.setCursor(0, 1);
  lcd.print(soilMoistureLine);
}

// Function that reads sensor xx times and reutrns the average value

float readSensor(int sensorPin) {
  uint8_t i;
  float averageSensorReading = 0;
  float sensorSampleReadings[analogueSamples];

  for (i = 0; i < analogueSamples; i++) {
    sensorSampleReadings[i] = analogRead(sensorPin);
    delay(10);
  }

  for (i = 0; i < analogueSamples; i++) {
    averageSensorReading += sensorSampleReadings[i];
  }

  return averageSensorReading /= analogueSamples;
}
