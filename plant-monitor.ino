#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <dht.h>
#include <SPI.h>
#include <SD.h>

// Pin declarations

#define csPin 4
#define dhtPin 5
#define interruptPin 7
#define photoresistorPin A0
#define thermistorPin A1
#define soilMoisturePin A2

// Coefficients for calculations

#define thermistorSeries 3950
#define bcoefficient 3950
#define temperatureNormal 25
#define thermistorNominal 3950

// Global variables

#define thermistorSamples 5
volatile int rollState = 1;
int thermistorSampleReadings[thermistorSamples];
long logId = 1;
static unsigned long lastLogRecordTime = 0;

// Objects

LiquidCrystal_I2C lcd(0x3F, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);
dht DHT;

void setup() {
  pinMode(interruptPin, INPUT);
  pinMode(csPin, OUTPUT);

  analogReference(EXTERNAL);

  attachInterrupt(digitalPinToInterrupt(interruptPin), interruptHandler, CHANGE);

  lcd.begin(16, 2);

  // SD Card initialization

  if (!SD.begin(csPin)) {
    lcd.setCursor(0, 0);
    lcd.print("SD Card Failure");
    delay(5000);
    return;
  }

  lcd.setCursor(0, 0);
  lcd.print("SD Card is Ready");

  // Header for Log file

  File logFile = SD.open ("log.csv", FILE_WRITE);
  if (logFile) {
    logFile.println(" , , , , , ");
    String header = "ID, Temperature (Celsius), Humudity (%), Lighting (lx), Thermistor (Celsius), Soil Moisture";
    logFile.println(header);
    logFile.close();
  }
}

void loop() {

  // Reads DHT sensor and stores values in respective variables

  int dhtStatus = DHT.read11(dhtPin);
  float dhtTemperature = DHT.temperature;
  float dhtHumidity = DHT.humidity;

  // Illuminance calculation (it is rather inaccurate as, for example, the voltage can fluctuate

  int photoresistorReading = analogRead(photoresistorPin);
  float vout = photoresistorReading * 0.00322265625; // 3.3 V / 1024 = 0.00322265625
  float lux = 500 / (10 * ((3.3 - vout) / vout));

  String lightCondition;

  if (lux < 10) {
    lightCondition = "pitch black";
  } else if (lux < 50) {
    lightCondition = "very dark";
  } else if (lux < 200) {
    lightCondition = "dark in";
  } else if (lux < 400) {
    lightCondition = "dim in";
  } else if (lux < 1000) {
    lightCondition = "normal in";
  } else if (lux < 5000) {
    lightCondition = "bright in";
  } else if (lux < 10000) {
    lightCondition = "dim out";
  } else if (lux < 30000) {
    lightCondition = "cloudy out";
  } else {
    lightCondition = "direct sun";
  }

  // Reads thermistor multiples times for accuracy and calculates the average result

  uint8_t i;
  float thermistorAverageReading;

  for (i = 0; i < thermistorSamples; i++) {
    thermistorSampleReadings[i] = analogRead(thermistorPin);
    delay(10);
  }

  thermistorAverageReading = 0;
  for (i = 0; i < thermistorSamples; i++) {
    thermistorAverageReading += thermistorSampleReadings[i];
  }

  thermistorAverageReading /= thermistorSamples;

  thermistorAverageReading = ( 1023 / thermistorAverageReading) - 1 ;
  thermistorAverageReading = thermistorSeries / thermistorAverageReading;

  float steinhart;

  steinhart = thermistorAverageReading / thermistorNominal;     // (R/Ro)
  steinhart = log(steinhart);                  // ln(R/Ro)
  steinhart /= bcoefficient;                   // 1/B * ln(R/Ro)
  steinhart += 1.0 / (temperatureNormal + 273.15); // + (1/To)
  steinhart = 1.0 / steinhart;                 // Invert
  steinhart -= 273.15;                         // convert to C

  // Reads soil moisture (TODO: add relay to minimize damage on sensor from oxidizing when current flows constantly)

  int moistureReading = analogRead(soilMoisturePin);

  // Logs data to SD card

  long upTime = millis();

  if (upTime - lastLogRecordTime > 1000) {
    String logData = String(logId) + "," + String(dhtTemperature) + "," + String(dhtHumidity) + "," + String(lux) + "," + String(steinhart) + "," + String(moistureReading);
    File logFile = SD.open("log.csv", FILE_WRITE);

    if (logFile) {
      logFile.println(logData);
      logFile.close();
    } else {
      lcd.setCursor(0, 0);
      lcd.print("SD Card Failure");
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
      displayThermistorAndMoistureReading(steinhart, moistureReading);
      delay(500);
      break;
    default:
      lcd.setCursor(0, 0);
      lcd.print("Unknown Error   ");
      lcd.setCursor(0, 1);
      lcd.print(rollState);
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

void displayDhtReading(int dhtStatus, float dhtTemperature, float dhtHumidity) {

  switch (dhtStatus) {
    case DHTLIB_OK:
      lcd.setCursor(0, 0);
      lcd.print("Temp: ");
      lcd.print(dhtTemperature, 1);
      lcd.print(" ");
      lcd.print((char)223);
      lcd.print("C    ");
      lcd.setCursor(0, 1);
      lcd.print("Hum: ");
      lcd.print(dhtHumidity, 1);
      lcd.print(" %     ");
      break;
    case DHTLIB_ERROR_CHECKSUM:
      lcd.setCursor(0, 0);
      lcd.print("DHT Checksum Err ");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      break;
    case DHTLIB_ERROR_TIMEOUT:
      lcd.setCursor(0, 0);
      lcd.print("DHT Timeout Err ");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      break;
    default:
      lcd.setCursor(0, 0);
      lcd.print("DHT Unknown Err ");
      lcd.setCursor(0, 1);
      lcd.print("                ");
      break;
  }
}

// Function that displays illuminance on LCD

void displayPhotoresistorReading (int lux, String lightCondition) {
  lcd.setCursor(0, 0);
  lcd.print("It's " + lightCondition + "        ");
  lcd.setCursor(0, 1);
  lcd.print("Lux: " + String(lux) + " lx        ");
}

// Function that displays temperature from analogue thermistor and soil moisture sensor

void displayThermistorAndMoistureReading (float steinhart, int moistureReading) {
  lcd.setCursor(0, 0);
  lcd.print("Therm: ");
  lcd.print(steinhart);
  lcd.print(" ");
  lcd.print((char)223);
  lcd.print("C  ");
  lcd.setCursor(0, 1);
  lcd.print("Soil moist: ");
  lcd.print(moistureReading);
  lcd.print("   ");
}
