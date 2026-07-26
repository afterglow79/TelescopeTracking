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
  double startAz;
  double startAlt;
  const float dpsX = 0.225; // degrees per step of the motor along the X axis
                            // w/ this, 90 degrees is 400 steps
  const float dpsY = 0.0337;// degrees per step of the motor along the Y axis
                            // w/ this, 90 degrees is 1000 steps
  double xStep;
  double yStep;
  
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

void getDSOAltAz(int objNum, int selectedTable){
    // Implementation for getting DSO Alt/Az

    switch (selectedTable) {
        case 0:
          myAstro.selectStarTable(objNum); // select object x in the star table
          break;
        case 1:
          myAstro.selectMessierTable(objNum); // select object x in the Messier table
          break;
        case 2:
          myAstro.selectCaldwellTable(objNum); // select object x in the Caldwell Table
          break;
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
    double objRA = myAstro.getRAdec(); // get the RA of the object
    double objDec = myAstro.getDeclinationDec(); // get the Dec of the

    planet.setRAdec(objRA, objDec); // set RA/Dec of SiderealPlanets to that of the object
    planet.doPrecessFrom2000(); // calculate how the object has moved since 2000
    getPlanetAltAz(); // convert from RA/Dec to AltAz and save to objInfo
}

void getDateTime(){
        char dateDelimiter = '-';
    char timeDelimiter = ':';
    
    int valueCount = 0;
    
    int startIdx = 0;
    int endIdx = latestData.date.indexOf(dateDelimiter);
    while (endIdx >= -1 && valueCount < 3) {
        String value = latestData.date.substring(startIdx, endIdx);
        if (valueCount == 0) datetime.year = value.toInt();
        else if (valueCount == 1) datetime.month = value.toInt();
        else if (valueCount == 2) datetime.day = value.toInt();

        startIdx = endIdx + 1;
        endIdx = latestData.date.indexOf(dateDelimiter, startIdx);
        valueCount++;
    }
    startIdx = 0;
    endIdx = latestData.time.indexOf(timeDelimiter);
    valueCount = 0;
    while (endIdx >= -1 && valueCount < 3) {
        String value = latestData.time.substring(startIdx, endIdx);
        if (valueCount == 0) datetime.hour = value.toInt();
        else if (valueCount == 1) datetime.minute = value.toInt();
        else if (valueCount == 2) datetime.second = value.toInt();

        startIdx = endIdx + 1;
        endIdx = latestData.time.indexOf(timeDelimiter, startIdx);
        valueCount++;
    }
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

    Serial.printf("Parsed -> date: %s\n time: %s\n lat: %f\n lon: %f\n targetCategory: %s\n targetNumber: %s\n init: %s\n initObjectCategory: %s\n initObjectNumber: %s\n name: %s\n\n",
                latestData.date.c_str(),
                latestData.time.c_str(),
                latestData.latitude,
                latestData.longitude,
                latestData.targetCategory.c_str(),
                latestData.targetNumber.c_str(),
                latestData.initialObject.c_str(),
                latestData.initObjectCategory.c_str(),
                latestData.initObjectNumber.c_str(),
                latestData.name.c_str());




    // TODO: kick off tracking/motor logic here now that latestData is populated
    planet.setLatLong(latestData.latitude, latestData.longitude);
    Serial.printf("Set lat/long to %f, %f\n", latestData.latitude, latestData.longitude);

    getDateTime();
    planet.setGMTdate(datetime.year, datetime.month, datetime.day);
    planet.setGMTtime(datetime.hour, datetime.minute, datetime.second);

    Serial.printf("Set date to %04d-%02d-%02d and time to %02d:%02d:%02d\n", datetime.year, datetime.month, datetime.day, datetime.hour, datetime.minute, datetime.second);
    
    if (latestData.initObjectCategory == "Star") {
        int starNum = latestData.initObjectNumber.toInt();
        Serial.printf("Selecting star %d\n", starNum);
        myAstro.selectStarTable(starNum);
        planet.setRAdec(myAstro.getRAdec(), myAstro.getDeclinationDec());
    } else if (latestData.initialObject == "Moon") {
        planet.doMoon();
    } else if (latestData.initialObject == "Jupiter") {
        planet.doJupiter();
    }
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