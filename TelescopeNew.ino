#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <SiderealObjects.h>
#include <SiderealObjectsTables.h>
#include <font6x8.h>
#include <nano_engine.h>
#include <nano_gfx.h>
#include <nano_gfx_types.h>
#include <sprite_pool.h>
#include <ssd1306.h>
#include <ssd1306_16bit.h>
#include <ssd1306_1bit.h>
#include <ssd1306_8bit.h>
#include <ssd1306_console.h>
#include <ssd1306_fonts.h>
#include <ssd1306_generic.h>
#include <ssd1306_uart.h>
#include <SiderealPlanets.h>

//In order to make the telescope track, you must first point it at Polaris, then select from the drop down list on the SSD1306 screen.
//You get no fancy UI on the computer end because fuck you I said so.

float currentAngleX;  // to keep track (roughly) of where we are pointed in the sky along Azimuth, will be updated as software runs
float currentAngleY;  // same as above, but along Altitude

float startAz;   // Start angle along Azimuth, will not be updated
float startAlt;  // Start angle along Altitude, will not be updated

float objAngleX;  // Azimuth of the object we are trying to look at
float objAngleY;  // Altitude of the object we are trying to look at

float dps = 0.18;  // assuming a 1:10 ratio and a motor that moves 1.8 deg/step, deg/step should be 0.18


// TODO; ADD GPS STUFF
static const int RXPin = 11, TXPin = 10;
static const uint32_t GPSBaud = 9600;
int gpsSetCheck = 0;

// SSD1306 stuff
SAppMenu menu;
const char *menuItems[] = {
  "Mercury"
  "Venus"
  "Moon"
  "Mars"
  "Jupiter"
  "Saturn"
  "Uranus"
  "Neptune"
};

//set names for various libraries
SiderealObjects myAstro;
SiderealPlanets planet;
TinyGPSPlus gps;
SoftwareSerial ss(RXPin, TXPin);

void setup() {
  Serial.begin(9600);   // begin regular serial for debugging
  Serial1.begin(9600);  // serial for transmissions
  ss.begin(GPSBaud);

  ssd1306_128x64_i2c_init();                  // init ssd1306
  ssd1306_setFixedFont(ssd1306xled_font6x8);  // set oled font
  ssd1306_clearScreen();                      // ensure the screen is clear
  ssd1306_printFixed(0, 8, ":3", STYLE_NORMAL);
  Serial.println("");
  //init SiderealPlanets/SiderealObjects
  planet.begin();
  planet.setTimeZone(-5);
  planet.useAutoDST();
  myAstro.begin();
  myAstro.selectStarTable(49);

  double initObjectRA = (myAstro.getRAdec());
  double initObjectDec = (myAstro.getDeclinationDec());

  planet.setRAdec(initObjectRA, initObjectDec);
  planet.doPrecessFrom2000();

  planet.doRAdec2AltAz();
  startAlt = planet.getAltitude();
  startAz = planet.getAzimuth();
  Serial.println(startAlt);
  Serial.println(startAz);

  //init angles


  //calculateSteps(startAngleY, objAngleY, dps, true); // calculate amount of steps to move telescope along alt
  //calculateSteps(startAngleX, objAngleX, dps, false); // calculate amount of steps to move telscope along az


  // ssd1306_createMenu( &menu, , uint8_t count);
  // switch (ssd1306_menuSelection(&menu))
}

void loop() {
  if (gpsSetCheck != 1){ // set all the important one-time values once and then never again. Does not account for if the clock rolls over to midnight, I believe
    while(ss.available() > 0){
      gps.encode(ss.read());

      if(gps.time.isUpdated()){
        //Serial.println("updated");
        if(gps.time.isValid()){
          //Serial.println("Time valid");
          if(gps.location.isValid()){
            //
            if(gps.date.isValid()){
            planet.setLatLong(gps.location.lat(), gps.location.lng());
            planet.setGMTtime(gps.time.hour(), gps.time.minute(), gps.time.second());
            planet.setGMTdate(gps.date.year(), gps.date.month(), gps.date.day());
            Serial.println(gps.time.value());
            gpsSetCheck = 1;
            }
          }
        }
      }
    }
  }
  while (ss.available() > 0){
    gps.encode(ss.read());
    // update time every loop
    if (gps.time.isValid() && gps.time.isUpdated()){
      planet.setGMTtime(gps.time.hour(), gps.time.minute(), gps.time.second());
      Serial.print("TIME = "); Serial.print(gps.time.hour()); Serial.print(":"); Serial.print(gps.time.minute()); Serial.print(":"); Serial.println(gps.time.second());
    }
  }
  

  // add GPS stuff and calculateSteps here once we get it work properly
}

void calculateSteps(float current, float desired, float degPerStep, bool isYDir) {
  int stepsToMove = (abs(current - desired) / degPerStep);

  if (isYDir) {  // send movement command for Y
    Serial.print("Steps to move Y: ");
    Serial.println(stepsToMove);
    writeCommand(stepsToMove, true, true);
  }

  if (!isYDir) {  // send movement command for X
    Serial.print("Steps to move X: ");
    Serial.println(stepsToMove);
    writeCommand(stepsToMove, false, true);
  }
}

void writeCommand(int steps, bool isY, bool writeToRegularSerial) {  // amount of steps, if it is y direction or not, write to regular serial for debugging
  if (isY) {
    if (writeToRegularSerial) {
      Serial.print("G0 y");
      Serial.println(steps);
    }

    Serial1.print("G0 y");
    Serial1.println(steps);
  }

  if (!isY) {
    if (writeToRegularSerial) {
      Serial.print("G0 x");
      Serial.println(steps);
    }

    Serial1.print("G0 x");
    Serial1.println(steps);
  }
}

void getDSOAltAz(int objNum, int table, double azReturn, double altReturn) { // input a table number and an object number to get out the altaz coordinates
  switch (table){ // see packet printed for a list of all objects and their respective table/object number
    case 1:
      myAstro.selectStarTable(objNum);
      break;
    case 2:
      myAstro.selectMessierTable(objNum);
      break;
    case 3:
      myAstro.selectCaldwellTable(objNum);
    case 4:
      myAstro.selectHershel400Table(objNum);
      break;
    case 5:
      myAstro.selectNGCTable(objNum);
      break;
    case 6:
      myAstro.selectICTable(objNum);
      break;
    case 7:
      myAstro.selectOtherObjectsTable(objNum);
      break;
  }

  double objRA = myAstro.getRAdec();
  double objDec = myAstro.getDeclinationDec();

  planet.setRAdec(objRA, objDec);
  planet.doPrecessFrom2000();
  planet.doRAdec2AltAz();
  azReturn = planet.getAzimuth();
  altReturn = planet.getAltitude();
}