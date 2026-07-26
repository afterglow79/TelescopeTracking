#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h> 

//access point settings
const char* AP_SSID = "TelescopeTracker";
const char* AP_PASSWORD = "changeme123";   // must be 8+ chars, or use "" for open network

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

struct telescopeData {
  double azimuth;
  double altitude;
  double xStep;
} telescope;

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
}

void loop() {
  server.handleClient();

  if (latestData.valid) {
    // latestData.valid = false;
  }
}