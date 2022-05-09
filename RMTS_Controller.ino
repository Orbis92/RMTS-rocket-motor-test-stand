//#define SHOW_MS   //instead of recorded samples

#include <SD.h>     
/*
Had to comment out all "Serial." lines form the SdFile.h in "C:\Program Files (x86)\Arduino\libraries\SD\src\utility\SdFile.h" to work with stm32duino, I guess (REPLACE "Serial." WITH "//Serial.")
*/ 

#include <LiquidCrystal.h>
//LiquidCrystal(rs, rw, enable, d4, d5, d6, d7)   //in use
LiquidCrystal lcd(PC13, PC14, PC15, PB0, PB1, PB10, PB11);     //(12, 11, 5, 4, 3, 2);

#define LED_ARM    PC13  //PB15
#define BTN_FIRE   PB12
#define BTN_OK     PB13
#define BTN_ARM    PB14

bool debug = true;  //save millis to log
unsigned long debounceOk;
unsigned long debounceArm;
unsigned long debounceFire;
unsigned long currentMillis;
unsigned long recordStart = 0;
unsigned long recordMillis = 0;
unsigned long timerLcd;
unsigned long scaleTimeout;
unsigned long samples = 0;
bool btnOk = false;
bool btnFire = false;
bool btnArm = false;
bool uncal = true;
bool stop = true;
bool fileOpen = false;
bool captDone = false;
File dataFile;
String fileName = "";
String scaleStr = "";
String dataString = "";
int lastSample;
int maxSample;

HardwareSerial Serial1(PA10, PA9); //PC USB-Serial adapter
HardwareSerial Serial2(PA3, PA2);  //Scale interface

void setup() {
  Serial1.begin(115200);
  delay(10);
  Serial1.println(F("Rocket Motor Test Stand"));
  Serial1.println(F("Controller v0.1"));
  //F("String") stores in program flash instead of RAM

  lcd.begin(16, 2);  //LCD with 16x2 Chars
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Rocket Motor"));
  lcd.setCursor(0, 1);
  lcd.print(F("Test Stand 0.1"));
  delay(1000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("RMTS starting:"));

  pinMode(LED_ARM, OUTPUT);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_FIRE, INPUT_PULLUP);
  pinMode(BTN_ARM, INPUT_PULLDOWN);

  //SD card init
  lcd.setCursor(0, 1);
  if (SD.begin(PA4) == false) {
    const String strCardFailed = F("Card failed!");
    Serial1.println(strCardFailed);
    lcd.print(strCardFailed);
    while (1);//wait forever
  }
  const String strCardOK = F("Card ok");
  Serial1.println(strCardOK);
  lcd.print(strCardOK);

  //COM port to scale init
  Serial2.begin(115200);
  Serial2.println('s');
  delay(100);
  //Clear the input buffer
  while (Serial2.available()) Serial2.read();

  lcd.setCursor(0, 1);
  Serial2.println('b');  //check for scale
  delay(500);            //wait for boot and anwser
  if (Serial2.available()) {
    byte scaleIn = Serial2.read();
    if (scaleIn == 'r') {
      const String strScaleOK = F("Scale ok");
      Serial1.println(strScaleOK);
      lcd.print(strScaleOK);
      uncal = false;
    } else if (scaleIn == 'u') {
      lcd.setCursor(0, 0);
      lcd.print(F("Calibrate scale "));
      lcd.setCursor(0, 1);
      lcd.print(F("via USB first!"));
      while (1);
    }
  } else {
    const String strScaleFailed = F("Scale failed!");
    Serial1.println(strScaleFailed);
    lcd.print(strScaleFailed);
    while (1);  //wait forever
  }
  delay(500);

  //Clear the input buffers
  while (Serial1.available()) Serial1.read();
  while (Serial2.available()) Serial2.read();

  //final bootup steps
  createNewLog();
  printHelp();
  lcd.clear();

  //interrupts
  attachInterrupt(BTN_ARM, ISRarm, CHANGE);
  attachInterrupt(BTN_FIRE, ISRfire, CHANGE);
  attachInterrupt(BTN_OK, ISRok, FALLING);

  scaleTimeout = millis(); //prevent timeout at startup
}

void loop() {
  currentMillis = millis();

  //capture the scale serial input
  captDone = false;
  if (Serial2.available()) {
    char scaleIn = Serial2.read();
    if (scaleIn == 'x') {   //scale sends 'x' at the end of a value
      captDone = true;
    }  //if ASCII char is a number (see ASCII table)
    else if (scaleIn >= 43 & scaleIn <=  57) {
      scaleStr += scaleIn;
    }

    //if capture complete
    if (captDone) {
      //Log to SD if recording
      if (!stop & fileOpen) {
        recordMillis = currentMillis - recordStart;
        if (debug) {
          dataString += (String)recordMillis;
          dataString += ",";
        }
        dataString += scaleStr;
        dataFile.println(dataString);
        Serial1.println(dataString);
        samples++;
        dataString = "";
      }
      lastSample = scaleStr.toInt();
      scaleTimeout = currentMillis;
      scaleStr = "";
      //Save the max value for LCD
      if (maxSample < lastSample) maxSample = lastSample;
    }
  }//end of record

  //Display
  if (timerLcd + 100 <=  currentMillis)
  {
    timerLcd = currentMillis;
    updateLCD();
  }

  //check last time scale send a legit value
  if (scaleTimeout + 500 <= currentMillis) {
    String errStr = F("CONNECTION LOST!");
    if (fileOpen) dataFile.println(errStr);
    Serial1.println(errStr);
    performStop();
    lcd.setCursor(0, 0);
    lcd.print(errStr);
    while (true); //wait forever
  }

  //Button logic
  if (btnOk) {
    btnOk = false;
    if (stop) {
      if (fileOpen == false) {
        //dumpSDtoSerial();
        createNewLog();
      } else Serial2.println('t');  //tare
    } else performStop();
  }

  //USB Serial COM
  if (Serial1.available()) serialPC();

} // end of main loop


//-------------------------------------------------------
//--------------- other functions -----------------------
void performFire() {
  stop = false;
  recordStart = currentMillis;
  scaleTimeout = currentMillis;
  Serial2.println('f');
}

void performStop() {
  Serial2.println('s');
  stop = true;
  dataFile.close();
  fileOpen = false;
}

//---------------- LCD routine --------------------------
void updateLCD() {
  lcd.clear();
  lcd.setCursor(0, 0);
  if (fileOpen) {
    lcd.print(fileName);
  } else {
    lcd.print(F("Log closed"));
  }

  lcd.setCursor(11, 0);
  if (!btnArm & samples == 0) lcd.print(F(" SAFE"));
  else if (btnArm & samples == 0) lcd.print(F("ARMED"));
  else if (btnArm & !stop) lcd.print(F(" *REC"));
  else if (stop & samples > 0) lcd.print(F(" STOP"));

  //lcd.setCursor(0, 1);
  //lcd.print((String)recordMillis + "ms");
  lcd.setCursor(0, 1);
  if (fileOpen) lcd.print((String)lastSample + "g");
  else lcd.print((String)maxSample + "g max ");

#ifdef SHOW_MS
  //get number of chars on LCD
  String str = (String)recordMS;
  byte pos = 16 - str.length();
  lcd.setCursor(pos, 1);
  lcd.print(str);
#else
  //get number of chars on LCD
  String str = (String)samples;
  byte pos = 16 - str.length();
  lcd.setCursor(pos, 1);
  lcd.print(str);
#endif
}

//---------------- Button interrupts --------------------
void ISRok() {
  if (debounceOk + 200 <= currentMillis ) {
    debounceOk = currentMillis;
    btnOk = true;
  }
}

void ISRarm() {
  delay(5); //debounce button
  btnArm = digitalRead(BTN_ARM);
  if (!btnArm & !stop) performStop();
}

void ISRfire() {
  if (btnArm & fileOpen & stop) performFire();
}

//----------- PC COM port -------------------------------
void serialPC() {
  byte incoming = Serial1.read();

  if (incoming == 't' & stop) {    //Tare the scale
    Serial2.println('t');
    Serial1.println(F("Scale tared"));
  }
  else if (incoming == 'n' & stop) //new run
  {
    Serial1.println(F("Reset test stand..."));
    createNewLog();
  }
  else if (incoming == 'p' & stop) //print last log to serial
  {
    if (!fileOpen) {
      dumpSDtoSerial();
    } else {
      Serial1.print(F("Still recording ? Could not open "));
      Serial1.println(fileName);
    }
  }
  else if (incoming == 'd' & stop) //debug on/off
  {
    debug ^= 1;
    Serial1.print(F("Debug : "));
    Serial1.println(debug);
    createNewLog();  //otherwise the millis header is missing in the log
  }
  else if (incoming == 'f') //start recording
  {
    if (fileOpen) performFire();
    else Serial1.println(F("Create new log first ('n')"));
  }
  else if (incoming == 's') //stop
  {
    performStop();
  }
  else if (incoming == 'i') //captured samples
  {
    Serial1.println(samples);
  }
  else printHelp();  //if (incoming == 'h' | incoming == '?' | incoming == 'H') //print help at unknown commands

  //Clear the input buffer (line feed, car. return)
  while (Serial1.available()) Serial1.read();
}

//----------- last log to serial ------------------------
void dumpSDtoSerial() {
  dataFile = SD.open(fileName);
  if (dataFile) {

    //print to the lcd
    lcd.setCursor(0, 0);
    lcd.print(F("Print log.."));

    //skip the first line, look for "valu{e}"
    char in = 0;
    while (in != 'e') {
      if (dataFile.available()) in = dataFile.read();
    }

    //just print the hole log
    while (dataFile.available()) {
      Serial1.write(dataFile.read());
    }
    dataFile.close();
  }
  else {
    Serial1.print(F("error opening "));
    Serial1.println(fileName);
  }

  scaleTimeout = millis(); //prevent scale timeout after long prints
}

//------------------ create a new Log -------------------
void createNewLog() {
  dataFile.close();  //just in case
  unsigned short fileIndex = 0;
  fileName = "log" + String(fileIndex) + ".txt";

  //Check for and count up the existing logs
  while (SD.exists(fileName)) {
    fileIndex++;
    fileName = "log" + String(fileIndex) + ".txt";
  }

  //Write the labels to the csv
  dataFile = SD.open(fileName, FILE_WRITE);
  if (dataFile) {
    Serial1.print(fileName); Serial1.println(F(" ready"));
    fileOpen = true;
    if (debug) dataString = F("millis,value");
    else dataString = F("value");
    dataFile.println(dataString);
    dataString = "";
    samples = 0;
    recordMillis = 0;
    maxSample = 0;
  } else {
    Serial1.print(F("error creating ")); Serial1.println(fileName);
  }

  scaleTimeout = millis(); //prevent scale timeout after longer routines
}

//--------------- print serial commands -----------------
void printHelp() {
  Serial1.println(F("h - this help list"));
  Serial1.println(F("t - tare scale"));
  Serial1.println(F("f - start / fire"));
  Serial1.println(F("s - stop"));
  Serial1.println(F("n - create new log"));
  Serial1.println(F("p - write open log to serial"));
  Serial1.println(F("i - samples taken"));
  Serial1.println(F("d - debug mode on/off"));
}
