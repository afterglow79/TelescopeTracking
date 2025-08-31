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


// SEE GITHUB FOR LICENSING INFORMATION || https://github.com/afterglow79/TelescopeTracking

// This is a program that will make your telescope point at any given object in the sky. See objects.pdf in the github repo for the non-planetary bodies that are supported
// REQUIRES an SSD1306 screen, a NEO-6M GPS unit (I have not tested if other units will work, but they may), a 4x4 keypad, 
// an Arduino Mega with this code loaded onto it, an Arduino Uno with GRBL loaded onto it, a CNC Shield V3, and two stepper motors of whatever power is needed to drive your telescope vertically and horizontally
// there are some variables that may need to be changed, for example dps, which should be the degrees per step of your motor combined with your gear.
// I will make a full list up here later.

// For the x motor, 160 steps at 20 mm/step to go 36 degrees, that is 160/36 = 4.444 deg per step, grbl can do 4.45 so that is what I will round to. Should be marginal.
// 0.225 deg/step @ 20 mm/step

// For the y motor, 1000 steps @ 20 mm/step to go 90 degrees, that is 1000/90 = 11.11..., grbl can do 11.1, so I will round to that. May be slightly more than marginal

//In order to make the telescope track, you must first point it at whatever object you have selected, then select an object from the menu on the SSD1306 screen.

//note to self cnc shield v3 wiring should go: RED BLUE GREEN BLACK

float currentAngleAz;  // to keep track (roughly) of where we are pointed in the sky along Azimuth, will be updated as software runs
float currentAngleAlt;  // same as above, but along Altitude

float stepsTakenAlt;// used for calculating keypad movement
float stepsTakenAz; // used for calculating keypad movement


float startAz;   // Start angle along Azimuth, will not be updated
float startAlt;  // Start angle along Altitude, will not be updated

float objAngleAz;  // Azimuth of the object we are trying to look at
float objAngleAlt;  // Altitude of the object we are trying to look at
char objAz[6];
char objAlt[6];
float dpsX = 0.225;  // degrees per step of the motor along the X axis
                    // w/ this, 90 degrees is 400 steps
float dpsY = 0.0337; // degrees per step of the motor along the Y axis
                     // w/ this, 90 degrees is 1000 steps
int starNum;
bool waitingToStart = true;

char *object; // to print to scrteen what object we are looking at

int menuCheck = 0; // to not draw a menu over stat screens
bool isDSOMenu = false; // to check if we are in the DSO menu or not
bool dsoInput = false;

static const int RXPin = 13, TXPin = 12; // set up all the GPS stuff
static const uint32_t GPSBaud = 9600; // this is arbitrary
int gpsSetCheck = 0; // to prevent the code from looping into the chunk that sets the star to polaris and does the calculations for that star every loop

// for keeping track of date and time
int year;
int month;
int day;
int hour;
int minute;
int second;

// SSD1306 stuff
SAppMenu menu;
SAppMenu dsoMenu;
SAppMenu startPicker;
int dsoTable;
bool dsoCheck;

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

const char *startOptions[] = {
  "Polaris",
  "Castor", // shinjiro no way
  "Pollux",
  "Vega",
  "The Moon"
};
// end SSD1306 stuff


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

void writeCommand(float stepsX, float stepsY, bool writeToRegularSerial) {  // amount of steps, if it is y direction or not, write to regular serial for debugging
  if (writeToRegularSerial) { // write the command of G0 y + amount of steps to regular serial, for debugging
    Serial.print("G0 x"); Serial.print(stepsX); Serial.print(" y"); Serial.println(stepsY);
  }
  Serial1.print("G0 x"); Serial1.print(stepsX); Serial1.print(" y"); Serial1.println(stepsY);
}

void calculateSteps(float currentX, float desiredX, float currentY, float desiredY, float degPerStepX, float degPerStepY) {
  float stepsToMoveX = ((desiredX-currentX) / degPerStepX); // Calculate how many steps the motor needs to move given the gear ratio. 
                                                              // Can be negated to move the motor the other direction
  float stepsToMoveY = ((desiredY-currentY) / degPerStepY);
  Serial.print("Steps to move X: "); Serial.print(stepsToMoveX); Serial.print("  Steps to move Y: "); Serial.println(stepsToMoveY);
  stepsTakenAlt = stepsTakenAlt + stepsToMoveY;
  stepsTakenAz = stepsTakenAz + stepsToMoveX;
  writeCommand(stepsToMoveX, stepsToMoveY, true);

  }

static void smartDelayMovement(unsigned long ms, char isX, int amount, int amount2, float degreesPerSec, float dps2){
  unsigned long start = millis();
  do
  {
    if(isX == 'x'){ // move along x
      stepsTakenAz = stepsTakenAz + amount;
      currentAngleAz = (stepsTakenAz / degreesPerSec) + startAz; 
      Serial1.print("G01 x");
      Serial1.print(stepsTakenAz);
      Serial1.println("");
      
    }
    else if(isX == 'y'){ // move along y
      stepsTakenAlt = stepsTakenAlt + amount;
      currentAngleAlt = (stepsTakenAlt / degreesPerSec) + startAlt;
      Serial1.print("G01 y");
      Serial1.print(stepsTakenAlt);
      Serial1.println("");
    }
    else if(isX == 'z'){ // move along both x and y
      stepsTakenAlt = stepsTakenAlt + amount;
      currentAngleAlt = (stepsTakenAlt / degreesPerSec) + startAlt;
      stepsTakenAz = stepsTakenAz + amount2;
      currentAngleAz = (stepsTakenAz / dps2) + startAz; 

      Serial1.print("G0 x");
      Serial1.print(stepsTakenAz);
      Serial1.print(" y");
      Serial1.println(stepsTakenAlt);

    }
  } while (millis() - start < ms);
  Serial.println(currentAngleAlt); Serial.println(currentAngleAz);
  Serial.println("");
  Serial.println(stepsTakenAlt); Serial.println(stepsTakenAz);
}

void getPlanetAltAz(){

  planet.doRAdec2AltAz(); // convert from RA/Dec to AltAz
  objAngleAz = planet.getAzimuth(); // save az of the planet to a variable for later use
  objAngleAlt = planet.getAltitude(); // save alt of planet to a variable for later use 

  Serial.print(objAngleAlt); Serial.print("/"); Serial.println(objAngleAz); // print altaz to the serial

  calculateSteps(currentAngleAz, objAngleAz, currentAngleAlt, objAngleAlt, dpsX, dpsY); // calculate steps along both axis that the telescope needs to move, and execute that command


}

void startUpTest(){
  Serial.println("Starting control test");
  writeCommand(100, 100, true);
  delay(100);
  writeCommand(0, 0, true);
}

void keypadMovement(){
  float speed = 50;
  bool isFinished = false;
  while(!isFinished){
    char key = keypad.getKey();
    if(key){
      Serial.println(key);
    }
    if(key == '0'){
      isFinished = true;
    }

    if (key == 'A'){
      speed = 500;
    }
    if (key == 'B'){
      speed = 100;
    }
    if (key == 'C'){
      speed = 50;
    }
    if (key == 'D'){
      speed = 10;
    }
    
    if (key == '1'){
      smartDelayMovement(speed, 'z', 10, -1, dpsY, dpsX);
    }
    if (key == '2'){
      smartDelayMovement(speed, 'y', 10, 0, dpsY, 0);
    }
    if (key == '3'){
      smartDelayMovement(speed, 'z', 10, 1, dpsY, dpsX);
    }
    if (key == '4'){
      smartDelayMovement(speed, 'x', -1, 0, dpsX, 0);
    }
    if (key == '5'){
      //todo implement regular tracking or auto home
    }
    if (key == '6'){
      smartDelayMovement(speed, 'x', 1, 0, dpsX, 0);
    }
    if (key == '7'){
      smartDelayMovement(speed, 'z', -10, -1, dpsY, dpsX);
    }
    if (key == '8'){
      smartDelayMovement(speed, 'y', -10, 0, dpsY, 0);
    }
    if (key == '9'){
      smartDelayMovement(speed, 'z', -10, 1, dpsY, dpsX);
    }
  }
  Serial.println("Exiting keypad movement...");
}

void updateScreenStats(char *selectedObject, int y, int m, int d, int h, int mi, int s, float alt, float az, bool isDSO){ // selectedObject is the selected object, 
                                                                                                              // y, m, d, are all the date,
                                                                                                              // h, mi, s, are the time, 
                                                                                                              // and alt/az is the altitude/azimuth
                                                                                               
  ssd1306_printFixed(0, 8, selectedObject, STYLE_NORMAL); // print the name of whatever object we're looking at to the screen
  ssd1306_printFixed(0, 16, (String(alt, 3) + "/" + String(az, 3)).c_str(), STYLE_NORMAL); // print the alt/az of the object we are looking at (remains static) to the screen
  ssd1306_printFixed(0, 24, (String(h) + ":" + String(mi) + ":" + String(s) + "  time in GMT").c_str(), STYLE_NORMAL); // print time to the screen
  ssd1306_printFixed(0, 32,  (String(y) + "/" + String(m) + "/" + String(d) + "  GMT date").c_str(), STYLE_NORMAL); // print the date to the screen
  
  if (isDSO){
    switch (dsoTable){ // print the type of object at the bottom of screen if DSO
      case 1:
        ssd1306_printFixed(0, 40, "Star", STYLE_NORMAL);
        break;

      case 2:
        ssd1306_printFixed(0, 40, "Messier", STYLE_NORMAL);
        break;
      
      case 3:
        ssd1306_printFixed(0, 40, "Caldwell", STYLE_NORMAL);
        break;

      case 4:
        ssd1306_printFixed(0, 40, "Hershel 400", STYLE_NORMAL);
        break;

      case 5:
        ssd1306_printFixed(0, 40, "NGC", STYLE_NORMAL);
        break;

      case 6:
        ssd1306_printFixed(0, 40, "IC", STYLE_NORMAL);
        break;

      case 7:
        ssd1306_printFixed(0, 40, "Other", STYLE_NORMAL);
        break;

      default:
        break;

    }
    Serial.println("c");
  }
}

void getDSOAltAz(int objNum, int table) { // input a table number and an object number to get out the altaz coordinates
                                                                           // exists to easily get the coordinates of DSOs, which is in of itself needlessly convoluted
                                                                           // code freezes here, idk why
  dsoTable = table;
  Serial.print("BEFORE SWITCH    "); // any print in this function is for debugging
  switch (table){ // see printable packet (objects.pdf) for a list of all objects and their respective table/object number
    case 0:
      myAstro.selectStarTable(objNum); // select object x in the star table
      break;
    case 1:
      myAstro.selectMessierTable(objNum); // select object x in the Messier table
      break;
    case 2:
      myAstro.selectCaldwellTable(objNum); // select object x in the Caldwell Table
    case 3:
      myAstro.selectHershel400Table(objNum); // select object x in the Hershel table
      break;
    case 4:
      myAstro.selectNGCTable(objNum); // select object x in the NGC table
      break;
    case 5:
      myAstro.selectICTable(objNum); // select object x in the IC table
      break;
    case 6:
      myAstro.selectOtherObjectsTable(objNum); // select object x in the "Others" table
      break;
  }
  Serial.print("AFTER SWITCH     ");
  double objRA = myAstro.getRAdec(); // get RA of selected object
  double objDec = myAstro.getDeclinationDec(); // get Dec of selected object


  planet.setRAdec(objRA, objDec); // set RA/Dec of SiderealPlanets to that of the object
  Serial.print("RADEC SET     ");
  planet.doPrecessFrom2000(); // calculate how the object has moved since 2000, basically where it is in the sky currently
  Serial.print("PRECESS DONE     ");
  planet.doRAdec2AltAz(); // convert from RA/Dec to AltAz
  Serial.print("RADEC TO ALTAZ DONE     ");
  objAngleAz = planet.getAzimuth(); // save object azimuth to a variable
  objAngleAlt = planet.getAltitude(); // save object altitude to a variable
  dsoCheck = true;
  Serial.print("OBJ ALTAZ IS:     "); Serial.print(objAngleAlt); Serial.print("/"); Serial.print(objAngleAz); Serial.print("     ");
  Serial.println("DSO ALTAZ SET... END OF FUNCTION.... CALCULATING STEPS");
  calculateSteps(currentAngleAz, objAngleAz, currentAngleAlt, currentAngleAz, dpsX, dpsY);
}

void inputDSO(int selectedTable){

  dsoInput = true;
  String str;
  int count;
  menuCheck = 1;
  int obj;

  while (dsoInput){
    char key = keypad.getKey(); // get key pressed

    if (key){
      Serial.println(key);
      if ((key != '*') && (key != '#') && (key != 'A') && (key != 'B') && (key != 'C') && (key != 'D')){ //only allow number inputs
        str += key; // update string with number pressed
        Serial.println(str);
        delay(100);
        ssd1306_clearScreen();
        count++;
        ssd1306_printFixed(((128 - (6 * count))/2), 32, str.c_str(), STYLE_NORMAL); // show on screen what number has been types
        obj = atoi(str.c_str());
      }

      if (key == '#'){
        ssd1306_clearScreen();
        Serial.print(obj); Serial.println("     in input func"); // second print is for debugging
        getDSOAltAz(obj, selectedTable); // see function // code freezes here, strangely
        dsoInput == false; // break out of loop
        object = obj; // update object name with the number input
        dsoCheck = 1; // make it true so we loop

      }
    }
  }
}

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
  ssd1306_createMenu( &dsoMenu, dsoMenuItems, sizeof(dsoMenuItems) / sizeof(char *) ); // create DSO menu
  ssd1306_createMenu( &startPicker, startOptions, sizeof(startOptions) / sizeof(char *));


  while(waitingToStart){ // allow the user to pick a star to point at
    char key = keypad.getKey();
    ssd1306_showMenu( &startPicker );
    if (key){
      Serial.println(key);
      if (key == 'D'){
        ssd1306_menuDown( &startPicker ); //bring menu up
      }
      if (key == 'C'){
        ssd1306_menuUp( &startPicker ); // bring menu down
      }
      if (key == '0'){ // select star
        ssd1306_updateMenu( &startPicker);
        waitingToStart = false;
        
        switch (ssd1306_menuSelection( &startPicker)){
          case 0:
            starNum = 49;
            break;
          case 1:
            starNum = 188;
            break;
          case 2:
            starNum = 196;
            break;
          case 3:
            starNum = 492;
            break;
          case 4:
            starNum = -1;
            break;
        }
      }
    }
  }
  Serial.println(starNum);
  ssd1306_clearScreen();
  startUpTest();
  Serial.println("Looping now");
}

void loop() {
  char key = keypad.getKey(); // get the pressed key of the keypad at the start of each loop
  
  if (key){
    Serial.println(key);
  }
  
  
  if ((gpsSetCheck != 1)){ // set all the important one-time values once and then never again. Does not account for if the clock rolls over to midnight, I believe
                           // also tells the software where polaris is in the sky, thusly where the telescope is pointed
    Serial.print("GPS SET = FALSE     ");
    while(ss.available() > 0){
      Serial.print("SOFTWARE SERIAL AVAILABLE    ");
      gps.encode(ss.read());
      Serial.print("GPS READ     ");
      if(gps.time.isUpdated()){
        Serial.print("GPS TIME UPDATED     ");
        // Serial.println("Time updated"); // in case the arduino freezes, see roughly where it froze
        if(gps.time.isValid()){
          Serial.print("GPS TIME IS VALID     ");
          // Serial.println("Time valid");
          if(gps.location.isValid()){
            Serial.print("GPS LOCATION IS VALID     ");
            // Serial.println("Location valid");
            if(gps.date.isValid()){
              Serial.println("GPS DATE IS VALID");
              planet.setLatLong(gps.location.lat(), gps.location.lng()); // set our lat/long in SiderealPlanets
              planet.setGMTtime(gps.time.hour(), gps.time.minute(), gps.time.second()); // set the time in SiderealPlanets
              planet.setGMTdate(gps.date.year(), gps.date.month(), gps.date.day()); // set the date in SiderealPlanets
              Serial.println(gps.location.lat(), 4); // print lat to the 4th decimal
              Serial.println(gps.location.lng(), 4); // print lng to the 4th decimal
              Serial.println();
              if (starNum != -1){ // if you didn't select the moon as the start point, do this
                Serial.println(starNum);
                myAstro.selectStarTable(starNum); // set the star selected earlier as the selected star

                // double initObjectRA = (myAstro.getRAdec());
                // double initObjectDec = (myAstro.getDeclinationDec());

                // planet.setRAdec(initObjectRA, initObjectDec);
                planet.setRAdec(myAstro.getRAdec(), myAstro.getDeclinationDec()); // set the RA/Dec that SiderealPlanets sees as the same as SiderealObjects
              
                planet.doPrecessFrom2000(); // calculate where the selected object is in our sky at the current date, time, and location
              }

              else{planet.doMoon(); Serial.println("moon");} // if you selected moon, print the moon's coordinates
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

  if (menuCheck != 1 and isDSOMenu == false){
    ssd1306_showMenu(&menu); // update the menu every loop, if not explicitly being told to not
  }

  if (menuCheck != 1 && isDSOMenu){
    ssd1306_showMenu(&dsoMenu);
  }

  if (menuCheck == 1 || dsoCheck){ // code freezes here if in DSO menu
    updateScreenStats(object, year, month, day, hour, minute, second, objAngleAlt, objAngleAz, dsoCheck); // update the screen with a bunch of stats, see the function for more info
  }

  if (key == 'C'){ //bring the menu up 1 selection
    ssd1306_clearScreen();
    ssd1306_menuUp(&dsoMenu); ssd1306_menuUp(&menu);
    Serial.println(key);
  }

  if (key == 'D'){ // bring the menu down 1 selection
    ssd1306_clearScreen();
    ssd1306_menuDown(&dsoMenu); ssd1306_menuDown(&menu);
    Serial.println(key);
  }

  if (key == '0' && (!isDSOMenu && (menuCheck != 1))){ // select current selection
    ssd1306_updateMenu(&menu);
    Serial.println(key);
    switch (ssd1306_menuSelection(&menu)){ // switch statement for each planet, contents should be roughly the same so I have documented only the first case

      case 0: // for some reason, this can crash the program sometimes and make everything freeze.
              // I'm not sure why, but it does happen
        planet.doMercury(); // calculate where mercury is in the sky
        getPlanetAltAz(); // see function
        ssd1306_clearScreen();
        object = "Mercury"; // set object name
                            // yes, this does raise the error:
                            // "ISO C++ forbids converting a string constant to 'char*"
                            // but the code runs, so I'm not worried about it

        menuCheck = 1; // make it so that the menu doesn't write itself again
        break;

      case 1:
        planet.doVenus();
        getPlanetAltAz();
        ssd1306_clearScreen();
        object = "Venus";
        menuCheck = 1;
        break;

      case 2:
        planet.doMoon();
        getPlanetAltAz();
        ssd1306_clearScreen();
        object = "The Moon";
        menuCheck = 1;
        break;
      
      case 3:
        planet.doMars();
        getPlanetAltAz();
        ssd1306_clearScreen();
        object = "Mars";
        menuCheck = 1;
        break;

      case 4:
        planet.doJupiter();
        getPlanetAltAz();
        ssd1306_clearScreen();
        object = "Jupiter";
        menuCheck = 1;
        break;

      case 5:
        planet.doSaturn();
        getPlanetAltAz();
        ssd1306_clearScreen();
        object = "Saturn";
        menuCheck = 1;
        break;

      case 6:
        planet.doUranus();
        getPlanetAltAz();
        ssd1306_clearScreen();
        object = "Uranus";
        menuCheck = 1;
        break;

      case 7:
        planet.doNeptune();
        getPlanetAltAz();
        ssd1306_clearScreen();
        object = "Neptune";
        menuCheck = 1;
        break;

      case 8: 
        isDSOMenu = true;
        ssd1306_clearScreen();
        ssd1306_showMenu(&dsoMenu);
        break;

      default:
        break;
    }

  }

  if (key == '0' && isDSOMenu){
    ssd1306_updateMenu( &dsoMenu );
    Serial.println(key);
    
    switch (ssd1306_menuSelection( &dsoMenu )){

      case 0:
        menuCheck = 1;
        inputDSO(0);
        ssd1306_clearScreen();
        
        break;

      case 1:
        menuCheck = 1;
        inputDSO(1);
        ssd1306_clearScreen();
        
        break;
      
      case 2:
        inputDSO(2);
                
        ssd1306_clearScreen();
        menuCheck = 1;
        break;
        
      case 3:
        inputDSO(3);
                
        ssd1306_clearScreen();
        menuCheck = 1;
        break;
         
      case 4:
        inputDSO(4);
                
        ssd1306_clearScreen();
        menuCheck = 1;
        break;
         
      case 5:
        inputDSO(5);
                
        ssd1306_clearScreen();
        menuCheck = 1;
        break;
        
      case 6:
        inputDSO(6);
                
        ssd1306_clearScreen();
        menuCheck = 1;
        break;
        
        
    }
  }

  if ((key == '*') && ((menuCheck == 1) || (isDSOMenu == true))){ // go back in/to the menu
    ssd1306_clearScreen();
    
    if (!dsoCheck){
      menuCheck = 0; 
    }
    
    if (dsoCheck){
      isDSOMenu = false;
      }
  }
  
  if (key == '5'){ // untested so far
    menuCheck = 2;
    keypadMovement();
  }
}


