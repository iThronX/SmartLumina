#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <time.h>
#include <vector>

// ------------------------------------------------------
// Configuration structure
// ------------------------------------------------------
struct SystemConfig {
  String deviceMode;            // "SENDER", "RECEIVER", or "GATEWAY"
  String lampId;                // Unique lamp identifier
  String ssid;                  // WiFi SSID (for RECEIVER/GATEWAY modes)
  String password;              // WiFi password
  String serverURL;             // Server endpoint
  String apiKey;                // API key for server
  uint8_t peerMAC[6];           // Peer MAC address for ESP-NOW
  bool bufferDiscardWhenFull;   // Buffer management policy
  bool useWiFi;                 // Enable WiFi functionality
  bool autoUpload;              // Auto upload to server
  unsigned long uploadInterval; // Upload interval in milliseconds
} config;

// ------------------------------------------------------
// Data structures
// ------------------------------------------------------
struct SensorData {
  String deviceId;
  String lampId;
  float voltage1;
  float voltage2;
  int ldrValue;
  bool pirDetection;
  float temperature;
  float humidity;
  float latitude;
  float longitude;
  bool gpsValid;
  bool mainPowerStatus;
  bool backupPowerStatus;
  bool dummyMode;
  unsigned long timestamp;
  int rssi;
};

typedef struct {
  char jsonData[200];
  uint8_t chunkId;
  uint8_t totalChunks;
  bool isComplete;
  char lampId[16];
} DataChunk;

typedef struct {
  char command[32];
  bool activate;
  char targetLampId[16];
} CommandMessage;

// ------------------------------------------------------
// Pin definitions
// ------------------------------------------------------
#define VOLTAGE_SENSOR_1_PIN 32
#define VOLTAGE_SENSOR_2_PIN 33
#define LDR_SENSOR_PIN         35
#define PIR_SENSOR_PIN         25
#define DHT_SENSOR_PIN         26
#define DHT_TYPE               DHT11
#define GPS_RX_PIN             16
#define GPS_TX_PIN             17
#define MAIN_POWER_PIN         4
#define BACKUP_POWER_PIN       2
#define STATUS_LED_PIN         2

// ------------------------------------------------------
// Global variables and state
// ------------------------------------------------------
DHT dht(DHT_SENSOR_PIN, DHT_TYPE);
std::vector<SensorData> dataBuffer;
SensorData localSensorData;
String receivedJsonBuffer = "";
String gpsData = "";

// GPS variables
float latitude = 0.0;
float longitude = 0.0;
bool gpsValid = false;

// System state flags
bool systemConfigured = false;
bool dummyMode = false;
bool sensorsConnected = true;
bool wifiConnected = false;

// Timing variables
unsigned long lastSensorRead = 0;
unsigned long lastDataSend = 0;
unsigned long lastUpload = 0;
unsigned long lastWiFiCheck = 0;

// Statistics
int receivedPacketsCount = 0;
int sentPacketsCount = 0;
int serverUploadCount = 0;
int serverErrorCount = 0;

// Constants
const int MAX_BUFFER_SIZE = 50;
const unsigned long SENSOR_READ_INTERVAL = 2000;
const unsigned long DATA_SEND_INTERVAL = 2000;
const unsigned long WIFI_CHECK_INTERVAL = 30000;
const size_t MAX_CHUNK_SIZE = 190;

// NTP configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;
const int daylightOffset_sec = 0;

// Global flag for command acknowledgement via ESP-NOW
bool lastCmdWasCommand = false;

// ------------------------------------------------------
// Function Prototypes
// ------------------------------------------------------
void initializeDefaultConfig();
void processCommand(String commandRaw);
void printHelp();
void setDeviceMode(String mode);
void setLampId(String lampId);
void setWiFiCredentials(String credentials);
void setServerURL(String url);
void setApiKey(String key);
void setPeerMAC(String macAddress);
void setBufferPolicy(String policy);
void setUploadInterval(String interval);
void configureComplete();
void initializeWiFi();
void initializeESPNow();
void checkWiFiConnection();
void readLocalSensors();
void readRealSensors();
void generateDummyData();
void readGPS();
void parseGPS(String data);
float convertToDecimalDegrees(String coord, char direction);
void addToBuffer(const SensorData& data);
void sendData();
String createJsonData();
void sendSingleChunk(String jsonData);
void sendMultipleChunks(String jsonData);
void uploadToServer();
void updateStatusLED();
void checkSensorConnections();
void clearBuffer();
void discardBuffer();
void resetConfiguration();
void sendCommandToPeer(String params);
void printSystemStatus();
void printSensorStatus();
void printBufferStatus();
void printNetworkStatus();
void printConfiguration();
void factoryReset();
void rebootDevice();

// ESP-NOW callback functions
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len);
void processReceivedChunk(const DataChunk& chunk);
void processCompleteJson(const String& jsonData, const String& sourceLampId);
void processReceivedCommand(const CommandMessage& cmd);

// ------------------------------------------------------
// SETUP and LOOP
// ------------------------------------------------------
void setup() {
  Serial.begin(9600);
  Serial2.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  
  // Initialize pins
  pinMode(VOLTAGE_SENSOR_1_PIN, INPUT);
  pinMode(VOLTAGE_SENSOR_2_PIN, INPUT);
  pinMode(LDR_SENSOR_PIN, INPUT);
  pinMode(PIR_SENSOR_PIN, INPUT);
  pinMode(MAIN_POWER_PIN, INPUT_PULLUP);
  pinMode(BACKUP_POWER_PIN, INPUT_PULLUP);
  pinMode(STATUS_LED_PIN, OUTPUT);
  
  // Initialize DHT sensor
  dht.begin();
  
  // Load default configuration
  initializeDefaultConfig();
  
  Serial.println("ESP32 Smart Lamp System Ready");
  Serial.println("Type 'HELP' for configuration commands");
}

void loop() {
  // Handle serial commands without altering the case of parameters
  if (Serial.available()) {
    String commandRaw = Serial.readString();
    commandRaw.trim();
    processCommand(commandRaw);
  }
  
  // Only run main functionality if system is configured
  if (systemConfigured) {
    // Read sensors periodically
    if (millis() - lastSensorRead > SENSOR_READ_INTERVAL) {
      readLocalSensors();
      addToBuffer(localSensorData);
      lastSensorRead = millis();
    }
    
    // Send data (for SENDER mode)
    if (config.deviceMode == "SENDER" && millis() - lastDataSend > DATA_SEND_INTERVAL) {
      sendData();
      lastDataSend = millis();
    }
    
    // Upload to server (for GATEWAY mode)
    if (config.deviceMode == "GATEWAY" && config.autoUpload &&
        millis() - lastUpload > config.uploadInterval) {
      uploadToServer();
      lastUpload = millis();
    }
    
    // Check WiFi connection periodically
    if (config.useWiFi && millis() - lastWiFiCheck > WIFI_CHECK_INTERVAL) {
      checkWiFiConnection();
      lastWiFiCheck = millis();
    }
    
    // Read GPS data
    readGPS();
    
    // Update status LED
    updateStatusLED();
  }
  
  delay(100);
}

// ------------------------------------------------------
// Configuration and Command Functions
// ------------------------------------------------------
void initializeDefaultConfig() {
  config.deviceMode = "UNCONFIGURED";
  config.lampId = "LAMP_" + WiFi.macAddress();
  config.lampId.replace(":", "");
  config.ssid = "";
  config.password = "";
  config.serverURL = "http://your-server.com/api/sensor-data";
  config.apiKey = "";
  config.bufferDiscardWhenFull = true;
  config.useWiFi = false;
  config.autoUpload = false;
  config.uploadInterval = 10000;
  
  // Default peer MAC (needs to be configured)
  for (int i = 0; i < 6; i++) {
    config.peerMAC[i] = 0x00;
  }

  localSensorData.deviceId = WiFi.macAddress();
  localSensorData.deviceId.replace(":", "");
  localSensorData.lampId = config.lampId;
}

// Process the incoming command without converting parameters to uppercase
void processCommand(String commandRaw) {
  commandRaw.trim();
  int spaceIndex = commandRaw.indexOf(' ');
  String keyword, arguments;
  if (spaceIndex == -1) {
    keyword = commandRaw;
    arguments = "";
  } else {
    keyword = commandRaw.substring(0, spaceIndex);
    arguments = commandRaw.substring(spaceIndex + 1);
  }
  
  // Use uppercase only for comparing the command keyword
  String keywordUpper = keyword;
  keywordUpper.toUpperCase();
  
  if (keywordUpper == "HELP") {
    printHelp();
  } else if (keywordUpper == "SET_MODE") {
    setDeviceMode(arguments);
  } else if (keywordUpper == "SET_LAMP_ID") {
    setLampId(arguments);
  } else if (keywordUpper == "SET_WIFI") {
    setWiFiCredentials(arguments);
  } else if (keywordUpper == "SET_SERVER") {
    setServerURL(arguments);
  } else if (keywordUpper == "SET_API_KEY") {
    setApiKey(arguments);
  } else if (keywordUpper == "SET_PEER_MAC") {
    setPeerMAC(arguments);
  } else if (keywordUpper == "SET_BUFFER_POLICY") {
    setBufferPolicy(arguments);
  } else if (keywordUpper == "SET_UPLOAD_INTERVAL") {
    setUploadInterval(arguments);
  } else if (keywordUpper == "CONFIGURE_COMPLETE") {
    configureComplete();
  } else if (keywordUpper == "STATUS") {
    printSystemStatus();
  } else if (keywordUpper == "SENSOR_STATUS") {
    printSensorStatus();
  } else if (keywordUpper == "BUFFER_STATUS") {
    printBufferStatus();
  } else if (keywordUpper == "NETWORK_STATUS") {
    printNetworkStatus();
  } else if (keywordUpper == "DUMMY_MODE") {
    dummyMode = true;
    Serial.println("Dummy mode activated");
  } else if (keywordUpper == "NORMAL_MODE") {
    dummyMode = false;
    Serial.println("Normal mode activated");
  } else if (keywordUpper == "CLEAR_BUFFER") {
    clearBuffer();
  } else if (keywordUpper == "DISCARD_BUFFER") {
    discardBuffer();
  } else if (keywordUpper == "UPLOAD_NOW") {
    uploadToServer();
  } else if (keywordUpper == "CHECK_SENSORS") {
    checkSensorConnections();
  } else if (keywordUpper == "RESET_CONFIG") {
    resetConfiguration();
  } else if (keywordUpper == "FACTORY_RESET") {
    factoryReset();
  } else if (keywordUpper == "REBOOT") {
    rebootDevice();
  } else if (keywordUpper == "GET_MAC") {
  Serial.println("Device MAC Address: " + getMyMAC());
  } else if (keywordUpper == "SET_AUTO_UPLOAD") {
    String value = arguments;
    value.trim();
    value.toUpperCase();
    if (value == "ON" || value == "TRUE" || value == "1") {
      config.autoUpload = true;
      Serial.println("Auto upload enabled");
    } else if (value == "OFF" || value == "FALSE" || value == "0") {
      config.autoUpload = false;
      Serial.println("Auto upload disabled");
    } else {
      Serial.println("Invalid value for SET_AUTO_UPLOAD. Use ON/OFF, TRUE/FALSE, or 1/0");
    }
  } else if (keywordUpper == "SET_WIFI_MODE") {
    String value = arguments;
    value.trim();
    value.toUpperCase();
    if (value == "ON" || value == "TRUE" || value == "1") {
      config.useWiFi = true;
      Serial.println("WiFi mode enabled");
    } else if (value == "OFF" || value == "FALSE" || value == "0") {
      config.useWiFi = false;
      Serial.println("WiFi mode disabled");
    } else {
      Serial.println("Invalid value for SET_WIFI_MODE. Use ON/OFF, TRUE/FALSE, or 1/0");
    }
  } else if (keywordUpper == "SEND_CMD") {
    sendCommandToPeer(arguments);
  } else {
    Serial.println("Unknown command. Type 'HELP' for available commands.");
  }
}

void printHelp() {
  Serial.println("=== ESP32 Smart Lamp System Commands ===");
  Serial.println("Configuration Commands:");
  Serial.println("  SET_MODE <SENDER|RECEIVER|GATEWAY>");
  Serial.println("  SET_LAMP_ID <lamp_id>");
  Serial.println("  SET_WIFI <ssid> <password>   (Enclose SSID in quotes if it has spaces)");
  Serial.println("  SET_SERVER <url>");
  Serial.println("  SET_API_KEY <key>");
  Serial.println("  SET_PEER_MAC <mac_address>");
  Serial.println("  SET_BUFFER_POLICY <DISCARD|MAINTAIN>");
  Serial.println("  SET_UPLOAD_INTERVAL <milliseconds>");
  Serial.println("  CONFIGURE_COMPLETE");
  Serial.println();
  Serial.println("Operation Commands:");
  Serial.println("  STATUS            - Show system status");
  Serial.println("  SENSOR_STATUS     - Show sensor readings");
  Serial.println("  BUFFER_STATUS     - Show buffer information");
  Serial.println("  NETWORK_STATUS    - Show network information");
  Serial.println("  DUMMY_MODE        - Enable dummy data");
  Serial.println("  NORMAL_MODE       - Enable real sensor data");
  Serial.println("  CLEAR_BUFFER      - Clear data buffer");
  Serial.println("  DISCARD_BUFFER    - Force discard buffer");
  Serial.println("  UPLOAD_NOW        - Force server upload");
  Serial.println("  CHECK_SENSORS     - Check sensor connections");
  Serial.println("  RESET_CONFIG      - Reset configuration");
  Serial.println("  FACTORY_RESET     - Erase all settings to defaults");
  Serial.println("  REBOOT            - Restart the device");
  Serial.println("  SET_AUTO_UPLOAD <ON/OFF>");
  Serial.println("  SET_WIFI_MODE   <ON/OFF>");
  Serial.println("  SEND_CMD <target_lamp_id> <command>");
  Serial.println("========================================");
}

void setDeviceMode(String mode) {
  mode.trim();
  mode.toUpperCase();
  if (mode == "SENDER" || mode == "RECEIVER" || mode == "GATEWAY") {
    config.deviceMode = mode;
    if (mode == "RECEIVER" || mode == "GATEWAY") {
      config.useWiFi = true;
    }
    if (mode == "GATEWAY") {
      config.autoUpload = true;
    }
    Serial.println("Device mode set to: " + mode);
  } else {
    Serial.println("Invalid mode. Use: SENDER, RECEIVER, or GATEWAY");
  }
}

String getMyMAC() {
  uint8_t mac[6];
  // Get the MAC address for the station interface
  esp_wifi_get_mac(WIFI_IF_STA, mac);
  char macStr[18];
  sprintf(macStr, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
          mac[3], mac[4], mac[5]);
  return String(macStr);
}

void setLampId(String lampId) {
  config.lampId = lampId;
  localSensorData.lampId = lampId;
  Serial.println("Lamp ID set to: " + lampId);
}

// Note: This function now preserves the case of the SSID and password.
void setWiFiCredentials(String credentials) {
  credentials.trim();
  // If the SSID is enclosed in quotes, extract it even if it contains spaces.
  if (credentials.charAt(0) == '\"') {
    int endQuote = credentials.indexOf("\"", 1);
    if (endQuote > 0) {
      config.ssid = credentials.substring(1, endQuote);
      config.password = credentials.substring(endQuote + 1);
      config.password.trim();
      Serial.println("WiFi credentials configured. SSID: \"" + config.ssid + "\", Password: \"" + config.password + "\"");
    } else {
      Serial.println("Invalid format. Use: SET_WIFI \"SSID with spaces\" password");
      return;
    }
  } else {
    int spaceIndex = credentials.indexOf(' ');
    if (spaceIndex > 0) {
      config.ssid = credentials.substring(0, spaceIndex);
      config.password = credentials.substring(spaceIndex + 1);
      Serial.println("WiFi credentials configured. SSID: " + config.ssid + ", Password: " + config.password);
    } else {
      Serial.println("Invalid format. Use: SET_WIFI <ssid> <password>");
      return;
    }
  }
  
  // Attempt connection immediately
  initializeWiFi();
  if (wifiConnected) {
    Serial.println("WiFi connected. IP address: " + WiFi.localIP().toString());
  } else {
    Serial.println("WiFi connection failed.");
  }
}

void setServerURL(String url) {
  config.serverURL = url;
  Serial.println("Server URL set to: " + url);
}

void setApiKey(String key) {
  config.apiKey = key;
  Serial.println("API key configured");
}

void setPeerMAC(String macAddress) {
  // Parse MAC address string (format: AA:BB:CC:DD:EE:FF)
  macAddress.replace(":", "");
  if (macAddress.length() == 12) {
    for (int i = 0; i < 6; i++) {
      String byteString = macAddress.substring(i * 2, (i * 2) + 2);
      config.peerMAC[i] = (uint8_t)strtol(byteString.c_str(), NULL, 16);
    }
    Serial.println("Peer MAC address configured");
  } else {
    Serial.println("Invalid MAC format. Use: AA:BB:CC:DD:EE:FF");
  }
}

void setBufferPolicy(String policy) {
  policy.trim();
  policy.toUpperCase();
  if (policy == "DISCARD") {
    config.bufferDiscardWhenFull = true;
    Serial.println("Buffer policy: DISCARD when full");
  } else if (policy == "MAINTAIN") {
    config.bufferDiscardWhenFull = false;
    Serial.println("Buffer policy: MAINTAIN (overwrite oldest)");
  } else {
    Serial.println("Invalid policy. Use: DISCARD or MAINTAIN");
  }
}

void setUploadInterval(String interval) {
  unsigned long intervalMs = interval.toInt();
  if (intervalMs >= 1000) {
    config.uploadInterval = intervalMs;
    Serial.println("Upload interval set to: " + String(intervalMs) + "ms");
  } else {
    Serial.println("Invalid interval. Minimum 1000ms");
  }
}

void configureComplete() {
  if (config.deviceMode == "UNCONFIGURED") {
    Serial.println("Error: Device mode not configured");
    return;
  }
  
  // Initialize WiFi if needed
  if (config.useWiFi && config.ssid.length() > 0) {
    initializeWiFi();
  }
  
  // Initialize ESP-NOW
  initializeESPNow();
  
  // Configure time if WiFi is available
  if (wifiConnected) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }
  
  systemConfigured = true;
  Serial.println("Configuration complete. System started in " + config.deviceMode + " mode.");
}

void initializeWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(config.ssid.c_str(), config.password.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }
  
  wifiConnected = (WiFi.status() == WL_CONNECTED);
}

void initializeESPNow() {
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW initialization failed");
    return;
  }
  
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);
  
  // Add peer if configured
  bool hasPeer = false;
  for (int i = 0; i < 6; i++) {
    if (config.peerMAC[i] != 0x00) {
      hasPeer = true;
      break;
    }
  }
  
  if (hasPeer) {
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, config.peerMAC, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    WiFi.disconnect();
    WiFi.begin(config.ssid.c_str(), config.password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 10) {
      delay(500);
      attempts++;
    }
    
    wifiConnected = (WiFi.status() == WL_CONNECTED);
  } else {
    wifiConnected = true;
  }
}

void readLocalSensors() {
  if (dummyMode) {
    generateDummyData();
  } else {
    readRealSensors();
  }
  
  localSensorData.mainPowerStatus = digitalRead(MAIN_POWER_PIN);
  localSensorData.backupPowerStatus = digitalRead(BACKUP_POWER_PIN);
  localSensorData.latitude = latitude;
  localSensorData.longitude = longitude;
  localSensorData.gpsValid = gpsValid;
  localSensorData.dummyMode = dummyMode;
  localSensorData.rssi = wifiConnected ? WiFi.RSSI() : 0;
  
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    localSensorData.timestamp = mktime(&timeinfo);
  } else {
    localSensorData.timestamp = millis();
  }
}

void readRealSensors() {
  localSensorData.voltage1 = (analogRead(VOLTAGE_SENSOR_1_PIN) * 5.0) / 4095.0;
  localSensorData.voltage2 = (analogRead(VOLTAGE_SENSOR_2_PIN) * 5.0) / 4095.0;
  localSensorData.ldrValue = analogRead(LDR_SENSOR_PIN);
  localSensorData.pirDetection = digitalRead(PIR_SENSOR_PIN);
  localSensorData.temperature = dht.readTemperature();
  localSensorData.humidity = dht.readHumidity();
  
  if (isnan(localSensorData.temperature)) localSensorData.temperature = 25.0;
  if (isnan(localSensorData.humidity)) localSensorData.humidity = 50.0;
}

void generateDummyData() {
  localSensorData.voltage1 = 3.3 + (random(-50, 50) / 100.0);
  localSensorData.voltage2 = 5.0 + (random(-30, 30) / 100.0);
  localSensorData.ldrValue = random(100, 3000);
  localSensorData.pirDetection = random(0, 10) > 7;
  localSensorData.temperature = 25.0 + random(-5, 15);
  localSensorData.humidity = 50.0 + random(-20, 30);
}

void readGPS() {
  while (Serial2.available()) {
    char c = Serial2.read();
    gpsData += c;
    
    if (c == '\n') {
      parseGPS(gpsData);
      gpsData = "";
    }
  }
}

void parseGPS(String data) {
  if (data.startsWith("$GPGGA")) {
    int commaCount = 0;
    String field = "";
    String latStr = "";
    String lonStr = "";
    char latDir = 'N';
    char lonDir = 'E';
    
    for (int i = 0; i < data.length(); i++) {
      if (data[i] == ',') {
        commaCount++;
        if (commaCount == 2 && field.length() > 0) {
          latStr = field;
        } else if (commaCount == 3 && field.length() > 0) {
          latDir = field[0];
        } else if (commaCount == 4 && field.length() > 0) {
          lonStr = field;
        } else if (commaCount == 5 && field.length() > 0) {
          lonDir = field[0];
        } else if (commaCount == 6) {
          if (field.toInt() > 0) {
            gpsValid = true;
            if (latStr.length() > 0) {
              latitude = convertToDecimalDegrees(latStr, latDir);
            }
            if (lonStr.length() > 0) {
              longitude = convertToDecimalDegrees(lonStr, lonDir);
            }
          } else {
            gpsValid = false;
          }
          break;
        }
        field = "";
      } else {
        field += data[i];
      }
    }
  }
}

float convertToDecimalDegrees(String coord, char direction) {
  float degrees = coord.substring(0, coord.indexOf('.') - 2).toFloat();
  float minutes = coord.substring(coord.indexOf('.') - 2).toFloat();
  float decimal = degrees + (minutes / 60.0);
  
  if (direction == 'S' || direction == 'W') {
    decimal = -decimal;
  }
  
  return decimal;
}

void addToBuffer(const SensorData& data) {
  if (dataBuffer.size() >= MAX_BUFFER_SIZE) {
    if (config.bufferDiscardWhenFull) {
      return; // Discard new data
    } else {
      dataBuffer.erase(dataBuffer.begin()); // Remove oldest
    }
  }
  
  dataBuffer.push_back(data);
}

void sendData() {
  String jsonData = createJsonData();
  
  if (jsonData.length() <= MAX_CHUNK_SIZE) {
    sendSingleChunk(jsonData);
  } else {
    sendMultipleChunks(jsonData);
  }
}

String createJsonData() {
  StaticJsonDocument<450> doc;
  
  doc["id"] = localSensorData.lampId;
  doc["ts"] = localSensorData.timestamp;
  doc["dm"] = localSensorData.dummyMode;
  doc["v1"] = round(localSensorData.voltage1 * 100) / 100.0;
  doc["v2"] = round(localSensorData.voltage2 * 100) / 100.0;
  doc["ldr"] = localSensorData.ldrValue;
  doc["pir"] = localSensorData.pirDetection;
  doc["tmp"] = round(localSensorData.temperature * 10) / 10.0;
  doc["hum"] = round(localSensorData.humidity * 10) / 10.0;
  
  if (localSensorData.gpsValid) {
    doc["lat"] = localSensorData.latitude;
    doc["lng"] = localSensorData.longitude;
    doc["gps"] = 1;
  } else {
    doc["gps"] = 0;
  }
  
  doc["mp"] = localSensorData.mainPowerStatus;
  doc["bp"] = localSensorData.backupPowerStatus;
  
  String jsonString;
  serializeJson(doc, jsonString);
  return jsonString;
}

void sendSingleChunk(String jsonData) {
  DataChunk chunk;
  jsonData.toCharArray(chunk.jsonData, sizeof(chunk.jsonData));
  chunk.chunkId = 0;
  chunk.totalChunks = 1;
  chunk.isComplete = true;
  config.lampId.toCharArray(chunk.lampId, sizeof(chunk.lampId));
  
  esp_now_send(config.peerMAC, (uint8_t*)&chunk, sizeof(chunk));
}

void sendMultipleChunks(String jsonData) {
  int totalChunks = (jsonData.length() + MAX_CHUNK_SIZE - 1) / MAX_CHUNK_SIZE;
  
  for (int i = 0; i < totalChunks; i++) {
    DataChunk chunk;
    
    int startPos = i * MAX_CHUNK_SIZE;
    int endPos = min((int)jsonData.length(), startPos + (int)MAX_CHUNK_SIZE);
    String chunkData = jsonData.substring(startPos, endPos);
    
    chunkData.toCharArray(chunk.jsonData, sizeof(chunk.jsonData));
    chunk.chunkId = i;
    chunk.totalChunks = totalChunks;
    chunk.isComplete = (i == totalChunks - 1);
    config.lampId.toCharArray(chunk.lampId, sizeof(chunk.lampId));
    
    esp_now_send(config.peerMAC, (uint8_t*)&chunk, sizeof(chunk));
    delay(50);
  }
}

void uploadToServer() {
  if (!wifiConnected || dataBuffer.empty() || config.deviceMode != "GATEWAY") {
    return;
  }
  
  HTTPClient http;
  http.begin(config.serverURL.c_str());
  http.addHeader("Content-Type", "application/json");
  
  if (config.apiKey.length() > 0) {
    http.addHeader("Authorization", "Bearer " + config.apiKey);
  }
  
  DynamicJsonDocument doc(8192);
  doc["gateway_id"] = WiFi.macAddress();
  doc["upload_timestamp"] = millis();
  doc["data_count"] = dataBuffer.size();
  
  JsonArray dataArray = doc.createNestedArray("sensor_data");
  
  for (const auto& data : dataBuffer) {
    JsonObject obj = dataArray.createNestedObject();
    obj["device_id"] = data.deviceId;
    obj["lamp_id"] = data.lampId;
    obj["timestamp"] = data.timestamp;
    obj["dummy_mode"] = data.dummyMode;
    obj["voltage1"] = round(data.voltage1 * 100) / 100.0;
    obj["voltage2"] = round(data.voltage2 * 100) / 100.0;
    obj["ldr_value"] = data.ldrValue;
    obj["pir_detection"] = data.pirDetection;
    obj["temperature"] = round(data.temperature * 10) / 10.0;
    obj["humidity"] = round(data.humidity * 10) / 10.0;
    obj["main_power"] = data.mainPowerStatus;
    obj["backup_power"] = data.backupPowerStatus;
    obj["rssi"] = data.rssi;
    
    if (data.gpsValid) {
      obj["latitude"] = data.latitude;
      obj["longitude"] = data.longitude;
      obj["gps_valid"] = true;
    } else {
      obj["gps_valid"] = false;
    }
  }
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode == 200 || httpResponseCode == 201) {
    serverUploadCount++;
    dataBuffer.clear();
  } else {
    serverErrorCount++;
  }
  
  http.end();
}

void updateStatusLED() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  
  unsigned long currentTime = millis();
  
  if (!systemConfigured) {
    // Fast blink - not configured
    if (currentTime - lastBlink > 200) {
      ledState = !ledState;
      digitalWrite(STATUS_LED_PIN, ledState);
      lastBlink = currentTime;
    }
  } else if (config.useWiFi && !wifiConnected) {
    // Slow blink - WiFi disconnected
    if (currentTime - lastBlink > 1000) {
      ledState = !ledState;
      digitalWrite(STATUS_LED_PIN, ledState);
      lastBlink = currentTime;
    }
  } else if (dataBuffer.size() >= MAX_BUFFER_SIZE * 0.8) {
    // Medium blink - buffer nearly full
    if (currentTime - lastBlink > 500) {
      ledState = !ledState;
      digitalWrite(STATUS_LED_PIN, ledState);
      lastBlink = currentTime;
    }
  } else {
    // Solid on - everything OK
    digitalWrite(STATUS_LED_PIN, HIGH);
  }
}

void checkSensorConnections() {
  bool allSensorsOk = true;
  
  float testTemp = dht.readTemperature();
  if (isnan(testTemp)) {
    allSensorsOk = false;
  }
  
  int volt1 = analogRead(VOLTAGE_SENSOR_1_PIN);
  int volt2 = analogRead(VOLTAGE_SENSOR_2_PIN);
  if (volt1 == 0 && volt2 == 0) {
    allSensorsOk = false;
  }
  
  int ldrTest = analogRead(LDR_SENSOR_PIN);
  if (ldrTest == 0 || ldrTest == 4095) {
    allSensorsOk = false;
  }
  
  sensorsConnected = allSensorsOk;
  
  if (!sensorsConnected && !dummyMode) {
    dummyMode = true;
  }
  
  Serial.print("Sensor connectivity: ");
  Serial.println(sensorsConnected ? "OK" : "ISSUES DETECTED");
}

void clearBuffer() {
  dataBuffer.clear();
  Serial.println("Buffer cleared");
}

void discardBuffer() {
  if (dummyMode) {
    dataBuffer.clear();
    Serial.println("Buffer discarded (dummy mode)");
  } else {
    Serial.println("Buffer discard only allowed in dummy mode");
  }
}

void resetConfiguration() {
  systemConfigured = false;
  initializeDefaultConfig();
  Serial.println("Configuration reset. Reconfigure the system.");
}

void sendCommandToPeer(String params) {
  int spaceIndex = params.indexOf(' ');
  if (spaceIndex > 0) {
    String targetLampId = params.substring(0, spaceIndex);
    String command = params.substring(spaceIndex + 1);
    
    CommandMessage cmd;
    targetLampId.toCharArray(cmd.targetLampId, sizeof(cmd.targetLampId));
    command.toCharArray(cmd.command, sizeof(cmd.command));
    cmd.activate = true;
    
    lastCmdWasCommand = true;
    
    esp_now_send(config.peerMAC, (uint8_t*)&cmd, sizeof(cmd));
    Serial.println("Command sent to " + targetLampId);
  } else {
    Serial.println("Invalid format. Use: SEND_CMD <target_lamp_id> <command>");
  }
}

// ------------------------------------------------------
// Status Printing Functions
// ------------------------------------------------------
void printSystemStatus() {
  Serial.println("=== SYSTEM STATUS ===");
  Serial.print("Device Mode: "); Serial.println(config.deviceMode);
  Serial.print("Lamp ID: "); Serial.println(config.lampId);
  Serial.print("System Configured: "); Serial.println(systemConfigured ? "YES" : "NO");
  Serial.print("WiFi Enabled: "); Serial.println(config.useWiFi ? "YES" : "NO");
  if (config.useWiFi) {
    Serial.print("WiFi Connected: "); Serial.println(wifiConnected ? "YES" : "NO");
    Serial.print("SSID: "); Serial.println(config.ssid);
    if (wifiConnected) {
      Serial.print("IP Address: "); Serial.println(WiFi.localIP());
    }
  }
  Serial.print("Auto Upload: "); Serial.println(config.autoUpload ? "YES" : "NO");
  Serial.print("Upload Interval: "); Serial.print(config.uploadInterval); Serial.println("ms");
  Serial.print("Buffer Policy: "); Serial.println(config.bufferDiscardWhenFull ? "DISCARD" : "MAINTAIN");
  Serial.print("Dummy Mode: "); Serial.println(dummyMode ? "ACTIVE" : "INACTIVE");
  Serial.print("Sensors Connected: "); Serial.println(sensorsConnected ? "YES" : "NO");
  
  // Print peer MAC
  Serial.print("Peer MAC: ");
  for (int i = 0; i < 6; i++) {
    if (config.peerMAC[i] < 0x10) Serial.print("0");
    Serial.print(config.peerMAC[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  
  Serial.println("=====================");
}

void printSensorStatus() {
  Serial.println("=== SENSOR STATUS ===");
  readLocalSensors();
  
  Serial.print("Voltage 1: "); Serial.print(localSensorData.voltage1); Serial.println("V");
  Serial.print("Voltage 2: "); Serial.print(localSensorData.voltage2); Serial.println("V");
  Serial.print("LDR Value: "); Serial.println(localSensorData.ldrValue);
  Serial.print("PIR Detection: "); Serial.println(localSensorData.pirDetection ? "DETECTED" : "NOT DETECTED");
  Serial.print("Temperature: "); Serial.print(localSensorData.temperature); Serial.println("°C");
  Serial.print("Humidity: "); Serial.print(localSensorData.humidity); Serial.println("%");
  Serial.print("Main Power: "); Serial.println(localSensorData.mainPowerStatus ? "ON" : "OFF");
  Serial.print("Backup Power: "); Serial.println(localSensorData.backupPowerStatus ? "ON" : "OFF");
  
  if (localSensorData.gpsValid) {
    Serial.print("GPS Latitude: "); Serial.println(localSensorData.latitude, 6);
    Serial.print("GPS Longitude: "); Serial.println(localSensorData.longitude, 6);
    Serial.println("GPS Status: VALID");
  } else {
    Serial.println("GPS Status: INVALID");
  }
  
  if (wifiConnected) {
    Serial.print("WiFi RSSI: "); Serial.print(localSensorData.rssi); Serial.println("dBm");
  }
  
  Serial.print("Data Mode: "); Serial.println(localSensorData.dummyMode ? "DUMMY" : "REAL");
  Serial.print("Timestamp: "); Serial.println(localSensorData.timestamp);
  Serial.println("====================");
}

void printBufferStatus() {
  Serial.println("=== BUFFER STATUS ===");
  Serial.print("Buffer Size: "); Serial.print(dataBuffer.size()); 
  Serial.print("/"); Serial.println(MAX_BUFFER_SIZE);
  Serial.print("Buffer Usage: "); 
  Serial.print((dataBuffer.size() * 100) / MAX_BUFFER_SIZE); Serial.println("%");
  Serial.print("Buffer Policy: "); Serial.println(config.bufferDiscardWhenFull ? "DISCARD_WHEN_FULL" : "MAINTAIN_OVERWRITE");
  Serial.print("Oldest Entry: ");
  if (!dataBuffer.empty()) {
    Serial.println(dataBuffer.front().timestamp);
  } else {
    Serial.println("EMPTY");
  }
  Serial.print("Newest Entry: ");
  if (!dataBuffer.empty()) {
    Serial.println(dataBuffer.back().timestamp);
  } else {
    Serial.println("EMPTY");
  }
  Serial.println("====================");
}

void printNetworkStatus() {
  Serial.println("=== NETWORK STATUS ===");
  Serial.print("ESP-NOW Status: "); Serial.println("INITIALIZED");
  Serial.print("MAC Address: "); Serial.println(WiFi.macAddress());
  
  if (config.useWiFi) {
    Serial.print("WiFi Mode: "); Serial.println("ENABLED");
    Serial.print("WiFi Status: "); Serial.println(wifiConnected ? "CONNECTED" : "DISCONNECTED");
    if (wifiConnected) {
      Serial.print("IP Address: "); Serial.println(WiFi.localIP());
      Serial.print("Gateway: "); Serial.println(WiFi.gatewayIP());
      Serial.print("DNS: "); Serial.println(WiFi.dnsIP());
      Serial.print("Signal Strength: "); Serial.print(WiFi.RSSI()); Serial.println("dBm");
    }
  } else {
    Serial.println("WiFi Mode: DISABLED");
  }
  
  Serial.print("Server URL: "); Serial.println(config.serverURL);
  Serial.print("API Key: "); Serial.println(config.apiKey.length() > 0 ? "CONFIGURED" : "NOT_CONFIGURED");
  
  // Statistics
  Serial.println("--- Network Statistics ---");
  Serial.print("Packets Sent: "); Serial.println(sentPacketsCount);
  Serial.print("Packets Received: "); Serial.println(receivedPacketsCount);
  Serial.print("Server Uploads: "); Serial.println(serverUploadCount);
  Serial.print("Server Errors: "); Serial.println(serverErrorCount);
  Serial.println("=========================");
}

void printConfiguration() {
  Serial.println("=== CURRENT CONFIGURATION ===");
  Serial.print("Device Mode: "); Serial.println(config.deviceMode);
  Serial.print("Lamp ID: "); Serial.println(config.lampId);
  Serial.print("WiFi SSID: "); Serial.println(config.ssid);
  Serial.print("Server URL: "); Serial.println(config.serverURL);
  Serial.print("Buffer Policy: "); Serial.println(config.bufferDiscardWhenFull ? "DISCARD" : "MAINTAIN");
  Serial.print("Upload Interval: "); Serial.print(config.uploadInterval); Serial.println("ms");
  Serial.print("Auto Upload: "); Serial.println(config.autoUpload ? "ENABLED" : "DISABLED");
  Serial.print("Use WiFi: "); Serial.println(config.useWiFi ? "ENABLED" : "DISABLED");
  
  Serial.print("Peer MAC: ");
  for (int i = 0; i < 6; i++) {
    if (config.peerMAC[i] < 0x10) Serial.print("0");
    Serial.print(config.peerMAC[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
  Serial.println("=============================");
}

void factoryReset() {
  // Clear all configuration
  systemConfigured = false;
  wifiConnected = false;
  dataBuffer.clear();
  
  // Reset statistics
  receivedPacketsCount = 0;
  sentPacketsCount = 0;
  serverUploadCount = 0;
  serverErrorCount = 0;
  
  // Reinitialize default config
  initializeDefaultConfig();
  
  // Disconnect WiFi
  WiFi.disconnect();
  
  Serial.println("Factory reset completed. Please reconfigure the device.");
}

void rebootDevice() {
  Serial.println("Rebooting device in 3 seconds...");
  delay(3000);
  ESP.restart();
}

// ------------------------------------------------------
// ESP-NOW Callback Functions
// ------------------------------------------------------
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    sentPacketsCount++;
    if (lastCmdWasCommand) {
      Serial.println("Command acknowledged by peer");
      lastCmdWasCommand = false;
    }
  } else {
    if (lastCmdWasCommand) {
      Serial.println("Command failed to send");
      lastCmdWasCommand = false;
    }
  }
}

// Updated onDataReceived with new signature
void onDataReceived(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  receivedPacketsCount++;

  if (len == sizeof(DataChunk)) {
    DataChunk *chunk = (DataChunk*)incomingData;
    processReceivedChunk(*chunk);
  }
  else if (len == sizeof(CommandMessage)) {
    CommandMessage *cmd = (CommandMessage*)incomingData;
    String commandStr = String(cmd->command);
    commandStr.toUpperCase();

    // If we receive an ACK command, print acknowledgment.
    if (commandStr == "CMD_ACK") {
      Serial.println("Received command execution ACK from peer");
    }
    else {
      // Process the received command as usual.
      processReceivedCommand(*cmd);
      // Then send an ACK back to the sender.
      CommandMessage ack;
      strcpy(ack.command, "CMD_ACK");
      ack.activate = true;
      // Optionally, you can return your lamp id (or other identifier)
      strcpy(ack.targetLampId, config.lampId.c_str());
      // Send the ACK directly to the sender's MAC address obtained from the info struct.
      esp_now_send(info->src_addr, (uint8_t *)&ack, sizeof(ack));
    }
  }
}


void processReceivedChunk(const DataChunk& chunk) {
  static String reassemblyBuffer = "";
  static int expectedChunks = 0;
  static int receivedChunks = 0;
  
  if (chunk.chunkId == 0) {
    // First chunk - reset reassembly
    reassemblyBuffer = "";
    expectedChunks = chunk.totalChunks;
    receivedChunks = 0;
  }
  
  // Add chunk data to buffer
  reassemblyBuffer += String(chunk.jsonData);
  receivedChunks++;
  
  // Check if we have all chunks
  if (receivedChunks == expectedChunks || chunk.isComplete) {
    processCompleteJson(reassemblyBuffer, String(chunk.lampId));
    reassemblyBuffer = "";
    receivedChunks = 0;
    expectedChunks = 0;
  }
}

void processCompleteJson(const String& jsonData, const String& sourceLampId) {
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, jsonData);
  
  if (error) {
    return; // Invalid JSON data
  }
  
  // Convert JSON to SensorData structure
  SensorData receivedData;
  receivedData.deviceId = WiFi.macAddress(); // Receiver's MAC
  receivedData.lampId = sourceLampId;
  receivedData.timestamp = doc["ts"] | millis();
  receivedData.dummyMode = doc["dm"] | false;
  receivedData.voltage1 = doc["v1"] | 0.0;
  receivedData.voltage2 = doc["v2"] | 0.0;
  receivedData.ldrValue = doc["ldr"] | 0;
  receivedData.pirDetection = doc["pir"] | false;
  receivedData.temperature = doc["tmp"] | 0.0;
  receivedData.humidity = doc["hum"] | 0.0;
  receivedData.mainPowerStatus = doc["mp"] | false;
  receivedData.backupPowerStatus = doc["bp"] | false;
  receivedData.rssi = WiFi.RSSI();
  
  if (doc["gps"] == 1) {
    receivedData.latitude = doc["lat"] | 0.0;
    receivedData.longitude = doc["lng"] | 0.0;
    receivedData.gpsValid = true;
  } else {
    receivedData.latitude = 0.0;
    receivedData.longitude = 0.0;
    receivedData.gpsValid = false;
  }
  
  // Add to buffer
  addToBuffer(receivedData);
}

void processReceivedCommand(const CommandMessage& cmd) {
  // Check if command is for this device or for ALL
  if (String(cmd.targetLampId) == config.lampId || String(cmd.targetLampId) == "ALL") {
    String command = String(cmd.command);
    command.toUpperCase();
    
    if (command == "DUMMY_MODE" && cmd.activate) {
      dummyMode = true;
    } else if (command == "NORMAL_MODE" && cmd.activate) {
      dummyMode = false;
    } else if (command == "CLEAR_BUFFER" && cmd.activate) {
      dataBuffer.clear();
    } else if (command == "UPLOAD_NOW" && cmd.activate) {
      uploadToServer();
    } else if (command == "CHECK_SENSORS" && cmd.activate) {
      checkSensorConnections();
    }
  }
}
