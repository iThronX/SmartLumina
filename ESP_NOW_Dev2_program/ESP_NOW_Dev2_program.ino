#include <WiFi.h>
#include <esp_now.h>

// Replace with the MAC address of the other ESP32
uint8_t peerMAC[] = {0x8C, 0x4F, 0x00, 0x10, 0x63, 0xF8};  // Example MAC address

void setup() {
  Serial.begin(9600);
  WiFi.mode(WIFI_STA);

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Register peer
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, peerMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  // Register receive callback
  esp_now_register_recv_cb(onDataRecv);
}

void loop() {
  if (Serial.available() > 0) {
    String message = Serial.readStringUntil('\n');
    sendMessage(message.c_str());
  }
}

void sendMessage(const char* message) {
  esp_err_t result = esp_now_send(peerMAC, (uint8_t *)message, strlen(message));

  if (result == ESP_OK) {
    Serial.println("Message sent successfully");
  } else {
    Serial.print("Error sending message: ");
    Serial.println(result);
  }
}

void onDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  char message[len + 1];
  memcpy(message, incomingData, len);
  message[len] = '\0';

  Serial.print("Received message: ");
  Serial.println(message);
}
// 8C:4F:00:10:63:F8 08:3A:F2:40:5B:6C
