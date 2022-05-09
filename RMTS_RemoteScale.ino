#include <Wire.h>
#include <EEPROM.h> //Needed to record user settings
#include "FastNAU7802.h" //Modified Spartkfun Lib
/*
Arduino/Sparkfun provided NAU7802 works fine, too, but is a bit slower in my tests. Use FastNAU lib from the controllers SD card instead
*/

NAU7802 myScale; //Create instance of the NAU7802 class

//EEPROM locations
uint16_t LOC_CAL_FAC =   0x20; //Float, 4 bytes of EEPROM

//#define ARM_PWR    PA11
//#define INT_SCALE  PB5
#define RELAY      PA0
#define LED        PC13   //STM32 onBoard LED PC13

bool stop = true;
bool uncal = true;
unsigned long startMillis;

//Uncomment for use with stm32duino updated libs (2.0.0) and check "Tools/U(S)ART Support: Enable (no generic Serial)" 
HardwareSerial Serial1(PA10, PA9); //PC USB-Serial adapter
HardwareSerial Serial2(PA3, PA2);  //Scale interface
//In the sketch below, rename "Serial1." to "Serial2." and than "Serial." to "Serial1."

void setup() {
  Serial1.begin(115200);   //usb serial pc interface
  Serial1.println(F("Rocket Motor Test Stand"));
  Serial1.println(F("Remote Scale Service Interface"));
  // F("String") keeps it in programm flash instead of RAM

  Serial2.begin(115200);  //remote interface

  //  //Check if igition voltage is present
  //  pinMode(ARM_PWR, INPUT_PULLDOWN);
  //  //Controller has to be in disarmed mode
  //  while (digitalRead(ARM_PWR) == HIGH) {
  //    Serial1.println(F("ERROR! Controller is armed!"));
  //    delay(1000);  //while(1);
  //  }

  //pinMode(INT_SCALE, INPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(LED, OUTPUT);
  digitalWrite(RELAY, LOW);   //relay off
  digitalWrite(LED, HIGH);   //LED off

  Wire.begin();
  Wire.setClock(400000);

  if (myScale.begin(Wire) == false) {
    Serial1.println(F("I2C Bus error"));
    while (1);
  }
  Serial1.println(F("NAU7802 detected"));

  myScale.setGain(NAU7802_GAIN_128); //Gain can be set to 1, 2, 4, 8, 16, 32, 64, or 128.
  myScale.setSampleRate(NAU7802_SPS_320); //Increase to max sample rate
  myScale.calibrateAFE(); //Re-cal analog front end when we change gain, sample rate, or channel
  //samplerate 321,128Hz calculated over 20min, log runtime milliseconds for exact dates

  readSystemSettings(); //Load calibrationFactor from EEPROM

  float calFac = myScale.getCalibrationFactor();
  //check if the calibration factor is legit
  if (calFac != 0 & calFac > -5000 & calFac < 5000) {
    Serial1.print(F("Restored calibration factor: "));
    Serial1.println(calFac, 2);
    uncal = false;
  } else {
    Serial1.println(F("Calibration invalid"));
    myScale.setCalibrationFactor(-1.45);  //default
  }

  myScale.calculateZeroOffset(64);

  Serial1.println(F("ready"));
  Serial1.println(F("t - tare scale"));
  Serial1.println(F("c - calibrate scale"));
  Serial1.println(F("f - start"));
  Serial1.println(F("s - stop"));
}

void loop() {

  //Sampling and data transmission
  if (myScale.available() == true)
  {
    int currentReading = myScale.getWeight();
    String dataOut = (String)(currentReading);
    dataOut += 'x';   //end marker
    Serial2.println(dataOut);
    if(!stop)Serial1.println(currentReading);
  }

  //Ignition auto-off timer
  if (startMillis + 5000 <= millis()) {
    digitalWrite(RELAY, LOW);
  }

  //PC interface
  if (Serial1.available())
  {
    pcCom();
  }

  //remote controller
  if (Serial2.available())
  {
    controllerCom();
  }

}//end of main loop

//------------- other functions ----------------
void peformStart() {
  myScale.calculateZeroOffset(64);  //tare scale at start
  stop = false;
  startMillis = millis();
  digitalWrite(LED, LOW);    //LED on
  digitalWrite(RELAY, HIGH); //relay on
}

void peformStop() {
  digitalWrite(RELAY, LOW); //relay off
  digitalWrite(LED, HIGH);  //LED off
  stop = true;
}

//Serial Com to PC
void pcCom() {
  byte incoming = Serial1.read();

  if (incoming == 't' & stop) {
    myScale.calculateZeroOffset(64);
    Serial1.println(myScale.getZeroOffset());
  }
  else if (incoming == 'c' & stop)
  {
    calibrateScale();
  }
  else if (incoming == 's') //stop recording
  {
    peformStop();
  }
  else if (incoming == 'f') //start fire and record
  {
    peformStart();
  }

  //Clear the input buffer
  while (Serial1.available()) Serial1.read();
}

//Serial Com to Controller
void controllerCom() {
  byte incoming = Serial2.read();

  if (incoming == 't' & stop)
  {
    myScale.calculateZeroOffset(64);
  }
  else if (incoming == 'c' & stop)
  {
    //wait for the incoming data bytes...
    delay(500);
    if (Serial2.available() <= 2  ) delay(500);

    float weightOnScale = Serial2.parseFloat();
    //Tell the library how much weight is currently on it
    myScale.calculateCalibrationFactor(weightOnScale, 64);

    float calFac = myScale.getCalibrationFactor();
    //check if the calibration factor is legit
    if (calFac != 0 & calFac > -5000 & calFac < 5000) {
      Serial2.println(calFac, 2);  //send calFac to Controller
      saveSystemSettings(); //save to EEPROM
      uncal = false;
    } else Serial2.println(0.0); //error
  }
  else if (incoming == 'd')    //get one value
  {
    while (myScale.available() == false);

    int currentReading = myScale.getWeight();
    String dataOut = (String)(currentReading);
    dataOut += 'f';   //end marker
    Serial2.println(dataOut);

  }
  else if (incoming == 'b')    //booted up response
  {
    if (uncal) Serial2.println('u');
    else Serial2.println('r');
  }
  else if (incoming == 's')       //stop recording
  {
    peformStop();
  }
  else if (incoming == 'f')       //start fire and record
  {
    peformStart();
  }

  //Clear the input buffer
  while (Serial2.available()) Serial2.read();
}

//USB calibration routine
void calibrateScale() {
  Serial1.println(F("Setup scale with no weight on it. Press a key when ready."));
  while (Serial1.available()) Serial1.read(); //Clear anything in RX buffer
  while (Serial1.available() == 0) delay(10); //Wait for user to press key
  myScale.calculateZeroOffset(64); //Zero or Tare the scale. Average over 64 readings.
  Serial1.print(F("New zero offset: "));
  Serial1.println(myScale.getZeroOffset());

  Serial1.println(F("Place known weight on scale. Enter the weight (format: '100.0'):"));
  while (Serial1.available()) Serial1.read(); //Clear anything in RX buffer
  while (Serial1.available() == 0) delay(10); //Wait for user to press key
  //Read user input
  float weightOnScale = Serial1.parseFloat();
  Serial1.println();

  myScale.calculateCalibrationFactor(weightOnScale, 64); //Tell the library how much weight is currently on it
  float calFac = myScale.getCalibrationFactor();

  if (calFac != 0 & calFac > -5000 & calFac < 5000) {
    Serial1.print(F("New cal factor: "));
    Serial1.println(calFac, 2);
    Serial1.print(F("New Scale Reading: "));
    Serial1.println(myScale.getWeight(), 1);
    uncal = false;
    saveSystemSettings(); //save to EEPROM
  } else Serial1.println(F("Calibration failed"));
}


//---------------- EEPROM ------------------------
void readSystemSettings() {
  float calFac;
  EEPROM_readAnything(LOC_CAL_FAC, calFac);
  myScale.setCalibrationFactor(calFac);
}

void saveSystemSettings() {
  EEPROM_writeAnything(LOC_CAL_FAC, myScale.getCalibrationFactor());
}

//EEPROM workaround from https://playground.arduino.cc/Code/EEPROMWriteAnything/
template <class T> int EEPROM_writeAnything(int ee, const T& value) {
  const byte* p = (const byte*)(const void*)&value;
  unsigned int i;
  for (i = 0; i < sizeof(value); i++)
    EEPROM.update(ee++, *p++);
  return i;
}

template <class T> int EEPROM_readAnything(int ee, T& value) {
  byte* p = (byte*)(void*)&value;
  unsigned int i;
  for (i = 0; i < sizeof(value); i++)
    *p++ = EEPROM.read(ee++);
  return i;
}
