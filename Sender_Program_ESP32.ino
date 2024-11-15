#include <WiFi.h>
#include <DHT.h>
#include <ArduinoJson.h>

// WiFi credentials
const char* ssid = "Airtel_iThronX HQ";
const char* password = "SVS@2004";

// DHT11 setup
#define DHTPIN 15  // GPIO pin where the DHT11 is connected
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// JSON buffer
StaticJsonDocument<300> jsonDoc;

// WiFi server
WiFiClient client;
WiFiServer server(80);

void setup() {
  Serial.begin(9600);  // Set baud rate to 9600
  dht.begin();

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  server.begin();
}

void loop() {
  // Read temperature and humidity
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  // Dummy values for other sensors
  int ldrValue = 500;  // Example LDR sensor value
  float voltage = 3.7; // Example voltage value
  float gpsLat = 13.0827;  // Example GPS latitude
  float gpsLon = 80.2707;  // Example GPS longitude

  // Check if any reads failed and exit early
  if (isnan(temp) || isnan(hum)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  // Create JSON object
  jsonDoc["DeviceId"] = "00001";
  jsonDoc["temperature"] = temp;
  jsonDoc["humidity"] = hum;
  jsonDoc["LDR"] = ldrValue;
  jsonDoc["voltage"] = voltage;
  jsonDoc["GPS"]["latitude"] = gpsLat;
  jsonDoc["GPS"]["longitude"] = gpsLon;

  // Serialize JSON to string
  String jsonData;
  serializeJson(jsonDoc, jsonData);

  // Calculate and print the size of the JSON data
  int jsonDataSize = jsonData.length();
  Serial.print("JSON Data Created: ");
  Serial.println(jsonData);
  Serial.print("JSON Data Size: ");
  Serial.print(jsonDataSize);
  Serial.println(" bytes");

  // Send JSON data over WiFi
  client = server.available();
  if (client) {
    client.println(jsonData);
    client.stop();
  }

  delay(5000);  // Wait for 5 seconds before next reading
}

