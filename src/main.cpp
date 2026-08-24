#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h> 
#include <SiderealPlanets.h>
#include <SiderealObjects.h>

// SEE GITHUB FOR LICENSING INFORMATION || https://github.com/afterglow79/TelescopeTracking

// This is a program that will make your telescope point at any given object in the sky. See objects.pdf in the github repo for the non-planetary bodies that are supported
// You will need an ESP32C3 Super Mini with this code loaded onto it, an Arduino Uno with GRBL loaded onto it, a CNC Shield V3, and two stepper motors of whatever power is needed to drive your telescope vertically and horizontally
// You will also need the app, which can be downloaded from TODO FILL IN
// there are some variables that may need to be changed, for example dps[x/y], which should be the degrees per step of your motor combined with your gear.
// I will make a full list up here later.


// For the x motor, 160 steps at 20 mm/step to go 36 degrees, that is 160/36 = 4.444 deg per step, grbl can do 4.45 so that is what I will round to. Should be marginal.
// ^^ I don't know what I was trying to say here, 36/160 is 0.225 deg/step so I assume this number is right
// 0.225 deg/step @ 20 mm/step

// For the y motor, 1000 steps @ 20 mm/step to go 90 degrees, that is 1000/90 = 11.11..., grbl can do 11.1, so I will round to that. May be slightly more than marginal
// ^^ same as above, 
//In order to make the telescope track, you must first point it at whatever object you have selected, then select an object from the menu on the SSD1306 screen.

//note to self cnc shield v3 wiring should go: RED BLUE GREEN BLACK | y ORANGE GREEN YELLOW BLUE | x 

//access point settings
const char* AP_SSID = "TelescopeTracker";
const char* AP_PASSWORD = "changeme123";   // must be 8+ chars, or use "" for open network

SiderealObjects myAstro;
SiderealPlanets planet;

// Fixed IP so it matches the app's hardcoded "http://192.168.10.1"
IPAddress local_IP(192, 168, 10, 1);
IPAddress gateway(192, 168, 10, 1);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);

unsigned long lastFollowMillisX = 0; // for tracking when to move the motors
unsigned long lastFollowMillisY = 0; // for tracking when to move the motors

// phone data
struct TrackingData {
  String date;
  String time;
  double latitude;
  double longitude;
  String targetCategory;
  String targetNumber;
  String initialObject;
  String initObjectCategory;
  String initObjectNumber;
  String name;
  bool valid = false;
} latestData;

struct datetime{
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
} datetime;

struct miscData{
    float stepsTakenAlt;
    float stepsTakenAz;
    int starNum;
    bool isFollowingSky;
} miscInfo;

struct objData{
    double objAngleAz;
    double objAngleAlt;
} objInfo;

struct telescopeData {
  double azimuth;
  double altitude;
  double startAz = -999; // give it some random start number that is unacheviable in a typical run
  double startAlt = -999; // or else it will assign startAz and startAlt every loop, which will make it so that it always thinks its pointing at the selected start object
  const float dpsX = 0.225; // degrees per step of the motor along the X axis
                            // w/ this, 90 degrees is 400 steps
  const float dpsY = 0.0337;// degrees per step of the motor along the Y axis
                            // w/ this, 90 degrees is 1000 steps
  double xStep; // amount of steps to move along the X axis, calculated by the calculateSteps function
  double yStep; // amount of steps to move along the Y axis, calculated by the calculateSteps function
  double secondsPerStepX; // how long to wait per step when following earth's rotation.
  double secondsPerStepY; // how long to wait per step when following earth's rotation.
  bool negateX = false; // if true, the X axis will be negated when moving. This will only apply to the sky tracking, not other kind of movement.
  bool negateY = false; // if true, the Y axis will be negated when moving. This will only apply to the sky tracking, not other kind of movement.
} telescope;

void writeCommand(float stepsX, float stepsY, bool writeToRegularSerial) { // writes the amount of steps to move in both direcetions in standard G-code
    if (writeToRegularSerial){ // write to regular serial for debugging
        Serial.print("G0 x");
        Serial.print(stepsX);
        Serial.print(" y");
        Serial.println(stepsY);
    }
    Serial1.print("G0 x");
    Serial1.print(stepsX);
    Serial1.print(" y");
    Serial1.println(stepsY);
}

float calculateSteps(float currentPos, float desiredPos, bool isX) { // calculates the amount of steps to move along a given axis, given the current position and desired position
    //debloated now, and directionless in terms of the numbers
    float stepsToMove;
    if (isX) {
        stepsToMove = ((currentPos-desiredPos) / telescope.dpsX); // Calculate how many steps the motor needs to move given the gear ratio. 
                                                                  // Can be negated to move the motor the other direction
        miscInfo.stepsTakenAz = miscInfo.stepsTakenAz + stepsToMove;
    }
    else {
        stepsToMove = ((currentPos-desiredPos) / telescope.dpsY);
        miscInfo.stepsTakenAlt = miscInfo.stepsTakenAlt + stepsToMove;
    }
    

    return stepsToMove;
}

float computeSecondsPerStep(float degreesPerStep) { // calculates how long to wait per step when following earth's rotation
    float value = degreesPerStep * 239.3; // degrees per step * (24 hours / 360 degrees) * 60 minutes/hour * 60 seconds/minute
    return value;
}

void getPlanetAltAz(){
    planet.doPrecessFrom2000(); // precess from J2000 to current date/time
    planet.doRAdec2AltAz(); // convert from RA/Dec to AltAz
    objInfo.objAngleAz = planet.getAzimuth();
    objInfo.objAngleAlt = planet.getAltitude();

}

void moveRel(float stepsX, float stepsY) { // move the telescope relative to its current position, given the amount of steps to move in both directions
    writeCommand(stepsX + miscInfo.stepsTakenAz, stepsY + miscInfo.stepsTakenAlt, true);
    miscInfo.stepsTakenAz = miscInfo.stepsTakenAz + stepsX;
    miscInfo.stepsTakenAlt = miscInfo.stepsTakenAlt + stepsY;
}

void startUpTest(){
    Serial.println("Starting control test");
    writeCommand(100, 100, true);
    delay(100);
    writeCommand(0, 0, true);
}

void getDateTime(){
  int first  = latestData.date.indexOf('/');
  int second = latestData.date.indexOf('/', first + 1);
  datetime.year  = latestData.date.substring(0, first).toInt();
  datetime.month = latestData.date.substring(first + 1, second).toInt();
  datetime.day   = latestData.date.substring(second + 1).toInt();

  first  = latestData.time.indexOf(':');
  second = latestData.time.indexOf(':', first + 1);

  datetime.hour   = latestData.time.substring(0, first).toInt();
  datetime.minute = latestData.time.substring(first + 1, second).toInt();
  datetime.second = latestData.time.substring(second + 1).toInt();
}

void getDateRegular(String dateString){
  int first  = dateString.indexOf('/');
  int second = dateString.indexOf('/', first + 1);
  datetime.year  = dateString.substring(0, first).toInt();
  datetime.month = dateString.substring(first + 1, second).toInt();
  datetime.day   = dateString.substring(second + 1).toInt();
  //Serial.println("Parsed date: " + String(datetime.year) + "/" + String(datetime.month) + "/" + String(datetime.day));
}

void getTimeRegular(String timeString){
  int first  = timeString.indexOf(':');
  int second = timeString.indexOf(':', first + 1);
  datetime.hour   = timeString.substring(0, first).toInt();
  datetime.minute = timeString.substring(first + 1, second).toInt();
  datetime.second = timeString.substring(second + 1).toInt();
  //Serial.println("Parsed time: " + String(datetime.hour) + ":" + String(datetime.minute) + ":" + String(datetime.second));
}

void getBodyInfo(){
  planet.setGMTtime(datetime.hour, datetime.minute, datetime.second);
  int num = latestData.targetNumber.toInt();

    if (latestData.targetCategory == "Star") {
        myAstro.selectStarTable(num);
        planet.setRAdec(myAstro.getRAdec(), myAstro.getDeclinationDec());

    } else if (latestData.targetCategory == "Messier") {
        myAstro.selectMessierTable(num);
        planet.setRAdec(myAstro.getRAdec(), myAstro.getDeclinationDec());

    } else if (latestData.targetCategory == "Caldwell") {
        myAstro.selectCaldwellTable(num);
        planet.setRAdec(myAstro.getRAdec(), myAstro.getDeclinationDec());

    } else if (latestData.targetCategory == "Herschel400") {
        myAstro.selectHershel400Table(num);
        planet.setRAdec(myAstro.getRAdec(), myAstro.getDeclinationDec());

    } else if (latestData.targetCategory == "NGC") {
        myAstro.selectNGCTable(num);
        planet.setRAdec(myAstro.getRAdec(), myAstro.getDeclinationDec());

    } else if (latestData.targetCategory == "IC") {
        myAstro.selectICTable(num);
        planet.setRAdec(myAstro.getRAdec(), myAstro.getDeclinationDec());

    } else if (latestData.targetCategory == "Other") {
        myAstro.selectOtherObjectsTable(num);
        planet.setRAdec(myAstro.getRAdec(), myAstro.getDeclinationDec());

    } else if (latestData.targetCategory == "Planet"){
        if(latestData.name == "Mercury"){
          planet.doMercury();
        } else if(latestData.name == "Venus"){
          planet.doVenus();
        } else if(latestData.name == "Moon"){
          planet.doMoon();
        } else if(latestData.name == "Mars"){
          planet.doMars();
        } else if (latestData.name == "Jupiter"){
          planet.doJupiter();
        } else if(latestData.name == "Saturn"){
          planet.doSaturn();
        } else if(latestData.name == "Uranus"){
          planet.doUranus();
        } else if(latestData.name == "Neptune"){
          planet.doNeptune();
        } else {
          Serial.printf("Unknown planet name: %s\n", latestData.name.c_str());
        }
    }
      else {
        Serial.printf("Unknown targetCategory: %s\n", latestData.targetCategory.c_str());
    }
    getPlanetAltAz();

    Serial.printf("Targeted object is %s #%d\n", latestData.name.c_str(), num);
    Serial.printf("Target RA/Dec: %f, %f\n", planet.getRAdec(), planet.getDeclinationDec());
    Serial.printf("Target Alt/Az: %f, %f\n", objInfo.objAngleAlt, objInfo.objAngleAz);    
}

void handleTrackingPost() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"no body\"}");
      return;
    }

    String body = server.arg("plain");
    Serial.println("Received JSON:");
    Serial.println(body);

    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
      Serial.print("JSON parse failed: ");
      Serial.println(err.c_str());
      server.send(400, "application/json", "{\"error\":\"bad json\"}");
      return;
    }

    latestData.date = doc["date"].as<String>();
    latestData.time = doc["time"].as<String>();
    latestData.latitude = doc["latitude"].as<double>();
    latestData.longitude = doc["longitude"].as<double>();
    latestData.initialObject = doc["initialObject"].as<String>();
    latestData.name = doc["name"].as<String>();
    latestData.targetCategory = doc["targetCategory"].as<String>();
    latestData.targetNumber = doc["targetNumber"].as<String>();
    latestData.initialObject = doc["initialObject"].as<String>();
    latestData.initObjectCategory = doc["initObjectCategory"].as<String>();
    latestData.initObjectNumber = doc["initObjectNumber"].as<String>();
    latestData.valid = true;

    Serial.printf("Parsed: \ndate: %s\n time: %s\n lat: %f\n lon: %f\n targetName: %s\n targetCategory: %s\n targetNumber: %s\n init: %s\n initObjectCategory: %s\n initObjectNumber: %s\n\n",
                latestData.date.c_str(),
                latestData.time.c_str(),
                latestData.latitude,
                latestData.longitude,
                latestData.name.c_str(),
                latestData.targetCategory.c_str(),
                latestData.targetNumber.c_str(),
                latestData.initialObject.c_str(),
                latestData.initObjectCategory.c_str(),
                latestData.initObjectNumber.c_str());



    planet.setLatLong(latestData.latitude, latestData.longitude);
    Serial.printf("Set lat/long to %f, %f\n", latestData.latitude, latestData.longitude);

    getDateTime();
    planet.setGMTdate(datetime.year, datetime.month, datetime.day);
    planet.setGMTtime(datetime.hour, datetime.minute, datetime.second);

    Serial.printf("Set date to %04d/%02d/%02d and time to %02d:%02d:%02d\n", datetime.year, datetime.month, datetime.day, datetime.hour, datetime.minute, datetime.second);
    
    if(telescope.startAz == -999 && telescope.startAlt == -999) { // if the telescope has not been assigned a starting Alt/Az, assign it now
      if (latestData.initObjectCategory == "Star") {
          int starNum = latestData.initObjectNumber.toInt();
          Serial.printf("Selecting star %d\n", starNum);
          myAstro.selectStarTable(starNum);
          planet.setRAdec(myAstro.getRAdec(), myAstro.getDeclinationDec());
      } else if (latestData.initialObject == "Moon") {
          planet.doMoon();
          Serial.println("Selecting Moon");
      } else if (latestData.initialObject == "Jupiter") {
          planet.doJupiter();
          Serial.println("Selecting Jupiter");
      }
      planet.doPrecessFrom2000();
      planet.doRAdec2AltAz();
      telescope.startAlt = planet.getAltitude();
      telescope.startAz = planet.getAzimuth();
      Serial.printf("Starting Alt/Az: %f, %f\n", telescope.startAlt, telescope.startAz);
      Serial.printf("Starting RA/Dec: %f, %f\n", planet.getRAdec(), planet.getDeclinationDec());

    } else {
      Serial.printf("Telescope already has starting Alt/Az: %f, %f, skipping\n", telescope.startAlt, telescope.startAz);
    }

    
    getBodyInfo();
    telescope.xStep = calculateSteps(telescope.startAz, objInfo.objAngleAz, true);
    telescope.yStep = calculateSteps(telescope.startAlt, objInfo.objAngleAlt, false);
    Serial.printf("Calculated steps to move: X: %f, Y: %f\n", telescope.xStep, telescope.yStep);
    writeCommand(telescope.xStep, telescope.yStep, true);
    

    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleMovePost() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"no body\"}");
      return;
    }

    String body = server.arg("plain");
    Serial.println("Received JSON:");
    Serial.println(body);

    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err) {
      Serial.print("JSON parse failed: ");
      Serial.println(err.c_str());
      server.send(400, "application/json", "{\"error\":\"bad json\"}");
      return;
    }

    float moveX = doc["moveX"].as<float>();
    float moveY = doc["moveY"].as<float>();
    String isDegrees = doc["isDegrees"].as<String>();

    Serial.printf("Parsed move command: moveX: %f, moveY: %f, isDegrees: %s\n", moveX, moveY, isDegrees == "true" ? "true" : "false");
    if(isDegrees == "true"){
      moveX = moveX * telescope.dpsX; // convert degrees to steps
      moveY = moveY * telescope.dpsY; // convert degrees to steps
    }
    moveRel(moveX, moveY);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleFollowPost(){
  if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"no body\"}");
      return;
  }

  String body = server.arg("plain");
  Serial.println("Received JSON:");
  Serial.println(body);

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    server.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }

  String follow = doc["follow"].as<String>();

  if (follow == "true"){
    miscInfo.isFollowingSky = true;
  } else {
    miscInfo.isFollowingSky = false;
  }
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleRegularUpdatePost(){
   if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"no body\"}");
      return;
  }

  String body = server.arg("plain");
  Serial.println("Received JSON:");
  Serial.println(body);

  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    Serial.print("JSON parse failed: ");
    Serial.println(err.c_str());
    server.send(400, "application/json", "{\"error\":\"bad json\"}");
    return;
  }
  String time = doc["time"].as<String>();
  String date = doc["date"].as<String>();
  getTimeRegular(time);
  getDateRegular(date);
  server.send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleNotFound() {
  Serial.printf("Unmatched request: %s %s\n", 
                (server.method() == HTTP_GET) ? "GET" : "POST", 
                server.uri().c_str());
  server.send(404, "text/plain", "Not found");
}

void updateTrackingIntervals() { // unused for now
    // 1. Convert positions to radians
    double lat =  latestData.latitude * M_PI / 180.0;
    double alt =  objInfo.objAngleAlt * M_PI / 180.0;
    double az  =  objInfo.objAngleAz * M_PI / 180.0;

    // 2. Earth rotation rate (0.00417807 deg/sec)
    const double OMEGA = 0.00417807; 

    // 3. Zenith Safety: Prevent tan(90) from crashing Azimuth
    if (objInfo.objAngleAlt > 88.0) alt = 88.0 * M_PI / 180.0;

    // 4. Calculate change rates (degrees per second)
    // Assuming X is Azimuth and Y is Altitude
    double degPerSecX = OMEGA * (sin(lat) - tan(alt) * cos(lat) * cos(az)); // Azimuth
    double degPerSecY = OMEGA * cos(lat) * sin(az);                        // Altitude

    // 5. Determine directions and update driver pins/variables
    // Replace 'setMotorDirectionX' with your code to set the physical DIR pins
    if (degPerSecX >= 0) {
        telescope.negateX = false; // Set direction for X axis
    } else {
        telescope.negateX = true;  // Set direction for X axis
    }

    if (degPerSecY >= 0) {
        telescope.negateY = false; // Set direction for Y axis
    } else {
        telescope.negateY = true;  // Set direction for Y axis
    }

    // 6. Calculate seconds per step (ignore direction sign using abs)
    degPerSecX = std::abs(degPerSecX);
    degPerSecY = std::abs(degPerSecY);

    // 7. Avoid division by zero if an axis temporarily stops moving
    if (degPerSecX > 0.000001) {
        telescope.secondsPerStepX = telescope.dpsX / degPerSecX;
    } else {
        telescope.secondsPerStepX = 999999; // Set safely high so it doesn't step
    }

    if (degPerSecY > 0.000001) {
        telescope.secondsPerStepY = telescope.dpsY / degPerSecY;
    } else {
        telescope.secondsPerStepY = 999999; // Set safely high so it doesn't step
    }
}

void updateLocationForFollow() {
  planet.setGMTtime(datetime.hour, datetime.minute, datetime.second);
  getBodyInfo();
  float xStep = calculateSteps(telescope.startAz, objInfo.objAngleAz, true);
  float yStep = calculateSteps(telescope.startAlt, objInfo.objAngleAlt, false);
  moveRel(xStep, yStep);
}

void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("Starting up...");
  Serial1.begin(9600, SERIAL_8N1, 20, 21); //RX, TX
  while(!Serial1) {
    delay(100);
    Serial.println("Waiting for Serial1 to be ready...");
  }
  Serial.println("Starting AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  WiFi.setTxPower(WIFI_POWER_8_5dBm); 


  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());  // should print 192.168.10.1

  server.on("/track", HTTP_POST, handleTrackingPost);
  server.on("/move", HTTP_POST, handleMovePost);
  server.on("/follow", HTTP_POST, handleFollowPost);
  server.on("/regularupdate", HTTP_POST, handleRegularUpdatePost);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("HTTP server started");

  planet.begin();
  myAstro.begin();
  planet.setTimeZone(-5); // set timezone
  planet.useAutoDST(); // check for daylight savings time
  delay(500);
  startUpTest();

  telescope.secondsPerStepX = computeSecondsPerStep(telescope.dpsX);
  telescope.secondsPerStepY = computeSecondsPerStep(telescope.dpsY);

  Serial.print("Seconds per step X: "); Serial.println(telescope.secondsPerStepX);
  Serial.print("Seconds per step Y: "); Serial.println(telescope.secondsPerStepY);
  Serial.println("Setup complete");
}

void loop() {
  server.handleClient();

  unsigned long currentMillis = millis();
  
  static unsigned long lastRateUpdate = 0;
  if (currentMillis - lastRateUpdate >= 2000) {
    if (miscInfo.isFollowingSky){
      updateLocationForFollow();
      lastRateUpdate = currentMillis;
    }
  }
}