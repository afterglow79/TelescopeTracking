#include <Key.h>
#include <Keypad.h>
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

// 160 steps at 20 mm/step to go 36 degrees, that is 160/36 = 4.444 deg per step, grbl can do 4.45 so that is what I will round to. Should be marginal.
// 0.225 deg/step @ 20 mm/step

//In order to make the telescope track, you must first point it at Polaris, then select an object from the menu on the SSD1306 screen.

float currentAngleAz;  // to keep track (roughly) of where we are pointed in the sky along Azimuth, will be updated as software runs
float currentAngleAlt;  // same as above, but along Altitude

float startAz;   // Start angle along Azimuth, will not be updated
float startAlt;  // Start angle along Altitude, will not be updated

float objAngleAz;  // Azimuth of the object we are trying to look at
float objAngleAlt;  // Altitude of the object we are trying to look at
char objAz[6];
char objAlt[6];
float dps = 0.225;  // degrees per step of the motor
                    // w/ this, 90 degrees is 400 steps

char *object;

int menuCheck = 0;

static const int RXPin = 13, TXPin = 12; // set up all the GPS stuff
static const uint32_t GPSBaud = 9600;
int gpsSetCheck = 0;
int year;
int month;
int day;
int hour;
int minute;
int second;

// SSD1306 stuff
SAppMenu menu;
const char *menuItems[] = { // menu for planetary bodies, DSOs takes you to a second menu
  "Mercury",
  "Venus",
  "Moon",
  "Mars",
  "Jupiter",
  "Saturn",
  "Uranus",
  "Neptune",
  "DSOs",

};

const char *dsoMenuItems[] = { //for the various DSOs, will have the ability to input a custom number to pick what to look at once you pick what type of DSO you want to see. 
  "Stars",
  "Messiers",
  "Caldwells",
  "Hershels",
  "NGCs",
  "ICs",
  "Others",
  "Back",
};



// init the keypad, this is from a keypad.h example
const byte ROWS = 4; //four rows
const byte COLS = 4; //four columns

//define the symbols on the buttons of the keypads
char hexaKeys[ROWS][COLS] = { // this can be arbitrary, but I have picked what is on the keypad
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte colPins[ROWS] = {29, 27, 25, 23}; //connect to the row pinouts of the keypad
byte rowPins[COLS] = {37, 35, 33, 31}; //connect to the column pinouts of the keypad

// end of copy/paste from one of the keypad.h examples


//Initialize the various libraries
Keypad keypad = Keypad( makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS); 
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
  ssd1306_printFixed(0, 8, ":3", STYLE_NORMAL); // show screen is on
  Serial.println("Begin");
  
  planet.begin(); // init SiderealPlanets
  planet.setTimeZone(-5); // set timezone
  planet.useAutoDST(); // check for daylight savings time
  myAstro.begin(); // init SiderealObjects
  delay(500);

  ssd1306_createMenu( &menu, menuItems, sizeof(menuItems) / sizeof(char *) ); // create the intial menu
  // ssd1306_showMenu(&menu);


}

void loop() {
  char key = keypad.getKey(); // get the pressed key of the keypad at the start of each loop
  if (key){
    Serial.println(key);
  }
  
  if ((gpsSetCheck != 1)){ // set all the important one-time values once and then never again. Does not account for if the clock rolls over to midnight, I believe
                           // also tells the software where polaris is in the sky, thusly where the telescope is pointed

    while(ss.available() > 0){
      gps.encode(ss.read());

      if(gps.time.isUpdated()){
        // Serial.println("Time updated"); // in case the arduino freezes, see roughly where it froze
        if(gps.time.isValid()){
          // Serial.println("Time valid");
          if(gps.location.isValid()){
            // Serial.println("Location valid");
            if(gps.date.isValid()){
              planet.setLatLong(gps.location.lat(), gps.location.lng()); // set our lat/long in SiderealPlanets
              planet.setGMTtime(gps.time.hour(), gps.time.minute(), gps.time.second()); // set the time in SiderealPlanets
              planet.setGMTdate(gps.date.year(), gps.date.month(), gps.date.day()); // set the date in SiderealPlanets
              Serial.println(gps.location.lat(), 4); // print lat to the 4th decimal
              Serial.println(gps.location.lng(), 4); // print lng to the 4th decimal
              Serial.println();
              myAstro.selectStarTable(49); // set polaris as the selected star

              // double initObjectRA = (myAstro.getRAdec());
              // double initObjectDec = (myAstro.getDeclinationDec());

              // planet.setRAdec(initObjectRA, initObjectDec);
              planet.setRAdec(myAstro.getRAdec(), myAstro.getDeclinationDec()); // set the RA/Dec that SiderealPlanets sees as the same as SiderealObjects
            
              planet.doPrecessFrom2000(); // calculate where Polaris is in our sky at the current date, time, and location

              planet.doRAdec2AltAz(); // convert from RA/Dec to AltAz
              startAlt = planet.getAltitude(); // set the starting altitude of the telescope
              startAz = planet.getAzimuth(); // set starting azimuth of the telescope
              Serial.println(startAlt, 6); // print the start alt of the telescope to serial, to the 6th decimal 
              Serial.println(startAz, 6);  // print the start az of the telscope to serial, to the 6th decimal
              Serial.println();
              Serial.print(gps.date.year()); Serial.print("/"); Serial.print(gps.date.month()); Serial.print("/"); Serial.print(gps.date.day()); // print the date to serial
              Serial.print("  "); 
              Serial.print(gps.time.hour()); Serial.print(":"); Serial.print(gps.time.minute()); Serial.print(":"); Serial.println(gps.time.second()); // print the time to serial
              currentAngleAlt = startAlt; // set the current alt angle to the starting alt angle, for simplicity's sake
              currentAngleAz = startAz; // set the current az angle to the starting az angle, for simlicity's sake
              ssd1306_clearScreen();

              gpsSetCheck = 1; // make sure it does not do this loop again
            }
          }
        }
      }
    }
  }

  while (ss.available() > 0){
    gps.encode(ss.read());

    if (gps.time.isValid() && gps.time.isUpdated()){
      planet.setGMTtime(gps.time.hour(), gps.time.minute(), gps.time.second()); // update the time every loop
      year = gps.date.year(); month = gps.date.month(); day = gps.date.day(); // assign values to the date variables 
                                                                     // (***should*** remain constant so there isn't much of a point putting it in the loop function,
                                                                     // but I'm doing it just to be safe in case you're out at GMT midnight or something)

      hour = gps.time.hour(); minute = gps.time.minute(); second = gps.time.second(); // assign values to the time variables, updates each loop
    }
  }

  if (menuCheck != 1){
    ssd1306_showMenu(&menu); // update the menu every loop, if not explicitly being told to not
  }

  if (menuCheck == 1){
    updateScreenStats(object, year, month, day, hour, minute, second, objAngleAlt, objAngleAz); // update the screen with a bunch of stats, see the function for more info
  }

  if (key == 'C'){ //bring the menu up 1 selection
    ssd1306_clearScreen();
    ssd1306_menuUp(&menu);
    Serial.println(key);
  }

  if (key == 'D'){ // bring the menu down 1 selection
    ssd1306_clearScreen();
    ssd1306_menuDown(&menu);
    Serial.println(key);
  }

  if (key == '0'){ // select current selection
    ssd1306_updateMenu(&menu);
    Serial.println(key);
    switch (ssd1306_menuSelection(&menu)){ // switch statement for each planet, contents should be roughly the same so I have documented only the first case
      case 0:
        // calculate where mercury is in the sky // freezes here for some reason
        planet.doMercury();
        planet.doRAdec2AltAz(); // convert from RA/Dec to AltAz
        Serial.println("Check 2");
        objAngleAz = planet.getAzimuth(); // save az of the planet to a variable for later use
        objAngleAlt = planet.getAltitude(); // save alt of planet to a variable for later use
        
        Serial.print(objAngleAlt); Serial.print("/"); Serial.println(objAngleAz); // print altaz to the serial
        object = "Mercury";
        menuCheck = 1; // make it so that the menu doesn't write itself again
        calculateSteps(currentAngleAlt, objAngleAlt, dps, true); // calculate steps along the Y axis (Alt) that the telescope needs to move, and execute that command
        calculateSteps(currentAngleAz, objAngleAz, dps, false); // calculate steps along the X axis (Az) that the telescope needs to move, and execute that command
        ssd1306_clearScreen();
        break;

      case 1:
        planet.doVenus();
        break;

      case 2:
        planet.doMoon();
        break;

      case 4:
        planet.doJupiter();
        planet.doRAdec2AltAz();
        objAngleAz = planet.getAzimuth();
        objAngleAlt = planet.getAltitude();
        Serial.print(objAngleAlt); Serial.print("/"); Serial.println(objAngleAz);
        ssd1306_clearScreen();
        ssd1306_printFixed(0, 8, "Jupiter", STYLE_NORMAL);
        menuCheck = 1;
        calculateSteps(currentAngleAlt, objAngleAlt, dps, true);
        calculateSteps(currentAngleAz, objAngleAz, dps, false);
        break;

      default:
        break;
    }
  }
  if ((key == '*') && (menuCheck == 1)){ // go back in/to the menu
    ssd1306_clearScreen();
    menuCheck = 0; 
  }
}

void calculateSteps(float current, float desired, float degPerStep, bool isYDir) {
  float stepsToMove = (abs(current - desired) / degPerStep); // Calculate how many steps the motor needs to move given the gear ratio. 
                                                              // Can be negated to move the motr the other direction
  if (isYDir) {  // send movement command for Y along Serial
    Serial.print("Steps to move Y: ");
    Serial.println(stepsToMove);
    writeCommand(stepsToMove, true, true);
  }

  if (!isYDir) {  // send movement command for X along Serial
    Serial.print("Steps to move X: ");
    Serial.println(stepsToMove);
    writeCommand(stepsToMove, false, true);
  }
}

void writeCommand(int steps, bool isY, bool writeToRegularSerial) {  // amount of steps, if it is y direction or not, write to regular serial for debugging
  if (isY) {
    if (writeToRegularSerial) { // write the command of G0 y + amount of steps to regular serial, for debugging
      Serial.print("G0 y");
      Serial.println(steps);
    }

    Serial1.print("G0 y"); // write the command of G0 y + amount of steps to serial1, moves telescope along alt
    Serial1.println(steps);
  }

  if (!isY) {
    if (writeToRegularSerial) { // write the command of G0 x + amount of steps to regular serial, for debugging
      Serial.print("G0 x");
      Serial.println(steps);
    }

    Serial1.print("G0 x"); // write the command of G0 x + amount of steps to regular serial, moves telescope along az
    Serial1.println(steps);
  }
}

void getDSOAltAz(int objNum, int table, double azReturn, double altReturn) { // input a table number and an object number to get out the altaz coordinates
                                                                             // exists to easily get the coordinates of DSOs, which is in of itself needlessly convoluted

  switch (table){ // see printable packet (objects.pdf) for a list of all objects and their respective table/object number
    case 1:
      myAstro.selectStarTable(objNum); // select object x in the star table
      break;
    case 2:
      myAstro.selectMessierTable(objNum); // select object x in the Messier table
      break;
    case 3:
      myAstro.selectCaldwellTable(objNum); // select object x in the Caldwell Table
    case 4:
      myAstro.selectHershel400Table(objNum); // select object x in the Hershel table
      break;
    case 5:
      myAstro.selectNGCTable(objNum); // select object x in the NGC table
      break;
    case 6:
      myAstro.selectICTable(objNum); // select object x in the IC table
      break;
    case 7:
      myAstro.selectOtherObjectsTable(objNum); // select object x in the "Others" table
      break;
  }

  double objRA = myAstro.getRAdec(); // get RA of selected object
  double objDec = myAstro.getDeclinationDec(); // get Dec of selected object

  planet.setRAdec(objRA, objDec); // set RA/Dec of SiderealPlanets to that of the object
  planet.doPrecessFrom2000(); // calculate how the object has moved since 2000, basically where it is in the sky currently
  planet.doRAdec2AltAz(); // convert from RA/Dec to AltAz
  azReturn = planet.getAzimuth(); // save object azimuth to a variable
  altReturn = planet.getAltitude(); // save object altitude to a variable
}

void updateScreenStats(char *selectedObject, int y, int m, int d, int h, int mi, int s, float alt, float az){ // selectedObject is the selected object, 
                                                                                                              //y, m, d, are all the date,
                                                                                                              //h, mi, s, are the time, 
                                                                                                              //and alt/az is the altitude/azimuth
                                                                                                              
  ssd1306_printFixed(0, 8, selectedObject, STYLE_NORMAL); // print the name of whatever object we're looking at to the screen
  ssd1306_printFixed(0, 16, (String(alt) + "/" + String(az)).c_str(), STYLE_NORMAL); // print the alt/az of the object we are looking at (remains static) to the screen
  ssd1306_printFixed(0, 24, (String(h) + ":" + String(mi) + ":" + String(s)).c_str(), STYLE_NORMAL); // print time to the screen
  ssd1306_printFixed(0, 32,  (String(y) + "/" + String(m) + "/" + String(d)).c_str(), STYLE_NORMAL); // print the date to the screen 
}
