#include <WiFi.h>
#include <esp_now.h>
#include <ArduinoJson.h>
#include <time.h>

uint8_t receiverMAC[] = {0x8C, 0x4F, 0x00, 0x10, 0x63, 0xF8};  // Example MAC address
const int CHUNK_SIZE = 240;  // Chunk size to ensure we stay within the 250-byte limit

StaticJsonDocument<300> jsonDoc;

void setup() {
  Serial.begin(9600);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);

  // Initialize time
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");  // 19800 seconds offset for IST (UTC+5:30)
  Serial.println("Waiting for time synchronization...");
  while (!time(nullptr)) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("Time synchronized");
}

void loop() {
  // Dummy sensor data
  float temp = 25.0;
  float hum = 60.0;
  int ldrValue = 500;
  float voltage = 3.7;
  float gpsLat = 13.0827;
  float gpsLon = 80.2707;

  // Get current time
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char timestamp[20];
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

  // Create JSON object with encrypted keys
  jsonDoc["D"] = "00001";  // DeviceId
  jsonDoc["T"] = temp;     // Temperature
  jsonDoc["H"] = hum;      // Humidity
  jsonDoc["L"] = ldrValue; // LDR
  jsonDoc["V"] = voltage;  // Voltage
  jsonDoc["G1"] = gpsLat;  // GPS Latitude
  jsonDoc["G2"] = gpsLon;  // GPS Longitude
  jsonDoc["TS"] = timestamp; // Timestamp

  // Serialize JSON to string
  String jsonData;
  serializeJson(jsonDoc, jsonData);

  // Send JSON data in chunks
  sendLargeMessage(jsonData);

  delay(10000);  // Wait for 10 seconds before next reading
}

void sendLargeMessage(String message) {
  int messageLength = message.length();
  int chunks = (messageLength + CHUNK_SIZE - 1) / CHUNK_SIZE;

  for (int i = 0; i < chunks; i++) {
    int start = i * CHUNK_SIZE;
    int end = min(start + CHUNK_SIZE, messageLength);
    String chunk = message.substring(start, end);

    esp_err_t result = esp_now_send(receiverMAC, (uint8_t *)chunk.c_str(), chunk.length());

    if (result == ESP_OK) {
      Serial.print("Data sent ");
      Serial.println(i + 1);
    } else {
      Serial.print("Error sending data ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.println(result);
    }

    delay(100);  // Short delay between chunks
  }
}

void onDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  char message[len + 1];
  memcpy(message, incomingData, len);
  message[len] = '\0';

  Serial.print("Received message: ");
  Serial.println(message);
}
