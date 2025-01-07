#include <WiFi.h>
#include <esp_now.h>
#include <ArduinoJson.h>

String receivedMessage = "";
const char* ssid = "Your_SSID";
const char* password = "Your_PASSWORD";

const char* serverUrl = "http://YOUR_SERVER_IP:8000/store-sensor-data/";

void setup() {
  Serial.begin(9600);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi!");


}

void loop() {
  
}

void onDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  char chunk[len + 1];
  memcpy(chunk, incomingData, len);
  chunk[len] = '\0';

  receivedMessage += String(chunk);

  // Deserialize JSON data
  StaticJsonDocument<300> jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, receivedMessage);

  if (error) {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    // Prepare JSON document to hold data for multiple devices
    StaticJsonDocument<2048> jsonDoc;  
    JsonArray deviceArray = jsonDoc.to<JsonArray>();

    // Device 1 data
    JsonObject device1 = deviceArray.createNestedObject();
    device1["DeviceId"] = jsonDoc["D"];
    device1["Temperature"] = jsonDoc["T"];
    device1["Humidity"] =jsonDoc["H"];
    device1["LDR"] = jsonDoc["L"];
    device1["Voltage"] = jsonDoc["V"];
    JsonObject gps1 = device1.createNestedObject("GPS");
    gps1["Latitude"] = jsonDoc["G1"];
    gps1["Longitude"] = jsonDoc["G2"];
    device1["Timestamp"] = jsonDoc["TS"];


    // Serialize the JSON object
    String jsonData;
    serializeJson(jsonDoc, jsonData);

    Serial.println("Sending data:");
    Serial.println(jsonData);

    // Send the POST request
    int httpResponseCode = http.POST(jsonData);

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println("Response:");
      Serial.println(response);
    } else {
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
    }

    http.end();
  } else {
    Serial.println("WiFi not connected");
  }

  delay(10000);

  // Clear received message buffer
  receivedMessage = "";
}
