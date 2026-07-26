#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h> 
#include <SiderealPlanets.h>
#include <SiderealObjects.h>

//TODO keypad control via phone

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
} miscInfo;

struct objData{
    float objAngleAz;
    float objAngleAlt;
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
  
} telescope;

void writeCommand(float stepsX, float stepsY, bool writeToRegularSerial) { // writes the amount of steps to move in both direcetions in standard G-code
    if (writeToRegularSerial){ // write to regular serial for debugging
        Serial.print("G0 x");
        Serial.print(stepsX);
        Serial.print(" y");
        Serial.println(stepsY);
    }
    Serial.print("G0 x");
    Serial.print(stepsX);
    Serial.print(" y");
    Serial.println(stepsY);
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

void getPlanetAltAz(){
    planet.doRAdec2AltAz(); // convert from RA/Dec to AltAz
    objInfo.objAngleAz = planet.getAzimuth();
    objInfo.objAngleAlt = planet.getAltitude();

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

void getBodyInfo(){
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

    Serial.printf("Targeted object is %s # %d\n", latestData.name.c_str(), num);
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

    latestData.date          = doc["date"].as<String>();
    latestData.time          = doc["time"].as<String>();
    latestData.latitude      = doc["latitude"].as<double>();
    latestData.longitude     = doc["longitude"].as<double>();
    latestData.initialObject = doc["initialObject"].as<String>();
    latestData.name          = doc["name"].as<String>();
    latestData.targetCategory     = doc["targetCategory"].as<String>();
    latestData.targetNumber       = doc["targetNumber"].as<String>();
    latestData.initialObject      = doc["initialObject"].as<String>();
    latestData.initObjectCategory = doc["initObjectCategory"].as<String>();
    latestData.initObjectNumber   = doc["initObjectNumber"].as<String>();
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




    // TODO: kick off tracking/motor logic here now that latestData is populated
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

void handleNotFound() {
  Serial.printf("Unmatched request: %s %s\n", 
                (server.method() == HTTP_GET) ? "GET" : "POST", 
                server.uri().c_str());
  server.send(404, "text/plain", "Not found");
}

void setup() {
  Serial.begin(115200);
  delay(5000);
  Serial.println("Starting up...");

  
  Serial.println("Starting AP...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  WiFi.setTxPower(WIFI_POWER_8_5dBm); 


  Serial.print("AP IP address: ");
  Serial.println(WiFi.softAPIP());  // should print 192.168.10.1

  server.on("/", HTTP_POST, handleTrackingPost);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("HTTP server started");

  planet.begin();
  myAstro.begin();
  planet.setTimeZone(-5); // set timezone
  planet.useAutoDST(); // check for daylight savings time
  delay(500);
  startUpTest();
  Serial.println("Setup complete");
}

void loop() {
  server.handleClient();

  if (latestData.valid) {
    // latestData.valid = false;
  }
}