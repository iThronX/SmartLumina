#include <WiFi.h>
#include <esp_now.h>
#include <ArduinoJson.h>

String receivedMessage = "";

void setup() {
  Serial.begin(9600);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
}

void loop() {
  // Nothing to do here
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

  // Extract data with encrypted keys
  const char* deviceId = jsonDoc["D"];
  float temp = jsonDoc["T"];
  float hum = jsonDoc["H"];
  int ldrValue = jsonDoc["L"];
  float voltage = jsonDoc["V"];
  float gpsLat = jsonDoc["G1"];
  float gpsLon = jsonDoc["G2"];
  const char* timestamp = jsonDoc["TS"];

  // Print data in structured format
  Serial.println("Received Data:");
  Serial.print("Device ID: ");
  Serial.println(deviceId);
  Serial.print("Temperature: ");
  Serial.print(temp);
  Serial.println(" °C");
  Serial.print("Humidity: ");
  Serial.print(hum);
  Serial.println(" %");
  Serial.print("LDR: ");
  Serial.println(ldrValue);
  Serial.print("Voltage: ");
  Serial.print(voltage);
  Serial.println(" V");
  Serial.print("GPS Latitude: ");
  Serial.println(gpsLat);
  Serial.print("GPS Longitude: ");
  Serial.println(gpsLon);
  Serial.print("Timestamp: ");
  Serial.println(timestamp);

  // Clear received message buffer
  receivedMessage = "";
}
