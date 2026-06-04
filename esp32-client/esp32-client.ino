/**
 * ESP32 IoT Client
 * Sends telemetry to the platform and receives commands via MQTT.
 * 
 * Required libraries (via Arduino IDE Library Manager or PlatformIO):
 * - PubSubClient (by Nick O'Leary)
 * - WiFi (built-in)
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <functional>

// ============== CONFIGURATION ==============
// Update these with your values

// WiFi credentials
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Device credentials (from platform)
const char* DEVICE_SERIAL = "ECO-2026-0001";  // Your device serial
const char* MQTT_TOKEN = "your-mqtt-token-here";  // Get from platform

// MQTT Broker
const char* MQTT_HOST = "167.172.141.63";  // EMQX en DigitalOcean
const int MQTT_PORT = 8883;                 // TLS

// Project prefix (from platform)
const char* PROJECT_PREFIX = "ECO";

// ==========================================

// Topic templates
#define TELEMETRY_TOPIC PROJECT_PREFIX "/" DEVICE_SERIAL "/telemetry"
#define STATUS_TOPIC PROJECT_PREFIX "/" DEVICE_SERIAL "/status"
#define COMMANDS_TOPIC PROJECT_PREFIX "/" DEVICE_SERIAL "/commands"
#define COMMANDS_ACK_TOPIC PROJECT_PREFIX "/" DEVICE_SERIAL "/commands/ack"
#define PROVISION_TOPIC "devices/provision/" DEVICE_SERIAL

// WiFi and MQTT clients
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

// Timing
unsigned long lastTelemetry = 0;
const unsigned long TELEMETRY_INTERVAL = 30000;  // 30 seconds

// Simulated sensor values (replace with real sensor reads)
float batteryLevel = 85.0;
float fillLevel = 45.0;
bool isAccepting = true;
float temperature = 22.5;
float weight = 12.3;
int signalStrength = -65;

// Callback for commands
void (*commandCallback)(const char* command, const JsonObject& payload, const char* logId) = nullptr;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("=== ESP32 IoT Client ===");
  Serial.print("Device Serial: ");
  Serial.println(DEVICE_SERIAL);
  Serial.print("MQTT Broker: ");
  Serial.println(MQTT_HOST);

  // Connect to WiFi
  connectWiFi();

  // Piloto: no validar el certificado del broker (cert autofirmado).
  // Producción: reemplazar por wifiClient.setCACert(EMQX_CA_PEM);
  wifiClient.setInsecure();

  // Setup MQTT
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  // Register command handler
  registerCommandHandler(handleCommand);

  // Send provision request
  sendProvision();

  Serial.println("Setup complete!");
}

void loop() {
  if (!mqttClient.connected()) {
    connectMQTT();
  }

  mqttClient.loop();

  // Send telemetry periodically
  unsigned long now = millis();
  if (now - lastTelemetry >= TELEMETRY_INTERVAL) {
    lastTelemetry = now;
    sendTelemetry();
  }

  // Update simulated sensors (replace with real reads)
  updateSensors();
}

// ============== WIFI ==============

void connectWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" Failed!");
  }
}

// ============== MQTT ==============

void connectMQTT() {
  Serial.print("Connecting to MQTT...");
  
  // LWT: si el ESP se desconecta inesperadamente, el broker envía "offline"
  // connect(clientID, username, password, willTopic, willQos, willRetain, willMessage)
  bool lwtSuccess = mqttClient.connect(
    DEVICE_SERIAL,           // client ID
    DEVICE_SERIAL,           // username  
    MQTT_TOKEN,              // password
    STATUS_TOPIC,            // will topic
    1,                       // will QoS
    true,                    // will retain
    "offline"                // will message
  );
  
  if (lwtSuccess) {
    Serial.println(" Connected!");
    
    // Subscribe to commands
    mqttClient.subscribe(COMMANDS_TOPIC);
    Serial.print("Subscribed to: ");
    Serial.println(COMMANDS_TOPIC);
    
    // Subscribe to commands/ack (for receiving acks from platform)
    mqttClient.subscribe(COMMANDS_ACK_TOPIC);
    Serial.print("Subscribed to: ");
    Serial.println(COMMANDS_ACK_TOPIC);
    
    // Send online status
    mqttClient.publish(STATUS_TOPIC, "online");
  } else {
    Serial.print(" Failed! Error: ");
    Serial.println(mqttClient.state());
    delay(5000);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");

  // Convert payload to string
  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println(message);

  // Parse JSON
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }

  JsonObject root = doc.as<JsonObject>();
  
  // Check if it's an acknowledgment from the platform
  if (root.containsKey("status")) {
    const char* status = root["status"];
    const char* logId = root["logId"];
    Serial.print("Ack received: ");
    Serial.print(logId);
    Serial.print(" -> ");
    Serial.println(status);
    return;
  }
  
  if (root.containsKey("command")) {
    const char* command = root["command"];
    JsonObject cmdPayload = root["payload"];
    const char* logId = root["logId"] | "";
    
    Serial.print("Command: ");
    Serial.println(command);
    Serial.print("LogId: ");
    Serial.println(logId);
    
    if (commandCallback) {
      commandCallback(command, cmdPayload, logId);
    }
  }
}

// ============== TELEMETRY ==============

void sendTelemetry() {
  StaticJsonDocument<512> doc;
  
  doc["batteryLevel"] = batteryLevel;
  doc["fillLevel"] = fillLevel;
  doc["isAccepting"] = isAccepting;
  doc["temperature"] = temperature;
  doc["weight"] = weight;
  doc["signalStrength"] = signalStrength;
  
  // Extra data (optional)
  doc["extra"]["humidity"] = 55.0;
  doc["extra"]["pressure"] = 1013.25;

  char buffer[512];
  serializeJson(doc, buffer);

  Serial.print("Sending telemetry: ");
  Serial.println(buffer);
  
  if (mqttClient.publish(TELEMETRY_TOPIC, buffer)) {
    Serial.println("Telemetry sent successfully!");
  } else {
    Serial.println("Telemetry send failed!");
  }
}

void sendProvision() {
  Serial.println("Sending provision request...");
  
  if (mqttClient.publish(PROVISION_TOPIC, "")) {
    Serial.println("Provision request sent!");
  } else {
    Serial.println("Provision request failed!");
  }
}

// ============== COMMANDS ==============

void registerCommandHandler(void (*handler)(const char*, const JsonObject&, const char*)) {
  commandCallback = handler;
}

void sendAck(const char* logId, const char* status, const char* error = nullptr) {
  if (strlen(logId) == 0) {
    return;
  }
  
  StaticJsonDocument<256> doc;
  doc["logId"] = logId;
  doc["status"] = status;
  
  if (error != nullptr) {
    doc["error"] = error;
  }
  
  char buffer[256];
  serializeJson(doc, buffer);
  
  Serial.print("Sending ack: ");
  Serial.println(buffer);
  
  mqttClient.publish(COMMANDS_ACK_TOPIC, buffer);
}

void handleCommand(const char* command, const JsonObject& payload, const char* logId) {
  Serial.print("Processing command: ");
  Serial.println(command);

  bool success = true;
  const char* errorMsg = nullptr;

  if (strcmp(command, "REBOOT") == 0) {
    Serial.println("Rebooting...");
    sendAck(logId, "ACKNOWLEDGED");
    delay(500);
    ESP.restart();
  }
  else if (strcmp(command, "POWEROFF") == 0) {
    // El ESP32 no se "apaga": entra en deep sleep indefinido (consumo mínimo).
    // Se despierta con un reset físico o un pin EXT (ver docs de esp_sleep).
    Serial.println("Powering off (deep sleep)...");
    sendAck(logId, "ACKNOWLEDGED");
    delay(500);
    ESP.deepSleep(0);  // 0 = sin temporizador → duerme hasta reset
  }
  else if (strcmp(command, "SET_ACCEPTING") == 0) {
    if (payload.containsKey("value")) {
      isAccepting = payload["value"].as<bool>();
      Serial.print("isAccepting set to: ");
      Serial.println(isAccepting);
    } else {
      success = false;
      errorMsg = "Missing 'value' parameter";
    }
  }
  else if (strcmp(command, "OPEN_LID") == 0) {
    Serial.println("Opening lid...");
    // Add your servo/actuator code here
  }
  else if (strcmp(command, "GET_STATUS") == 0) {
    Serial.println("Sending status response...");
    mqttClient.publish(STATUS_TOPIC, "online");
  }
  else if (strcmp(command, "CONSOLE_LOG") == 0) {
    Serial.println("CONSOLE_LOG received!");
    if (payload.containsKey("message")) {
      Serial.print("Message: ");
      Serial.println(payload["message"].as<const char*>());
    }
    if (payload.containsKey("level")) {
      Serial.print("Level: ");
      Serial.println(payload["level"].as<const char*>());
    }
  }
  else {
    Serial.print("Unknown command: ");
    Serial.println(command);
    success = false;
    errorMsg = "Unknown command";
  }
  
  // Send acknowledgment
  if (strlen(logId) > 0) {
    if (success) {
      sendAck(logId, "ACKNOWLEDGED");
    } else {
      sendAck(logId, "FAILED", errorMsg);
    }
  }
}

// ============== SENSORS ==============

void updateSensors() {
  // Replace these with actual sensor readings
  // Example:
  // batteryLevel = battery.readPercentage();
  // fillLevel = ultrasonic.readDistance();
  // temperature = dht.readTemperature();
  
  // For demo, we simulate small changes
  batteryLevel = constrain(batteryLevel - 0.01, 0, 100);
  fillLevel = constrain(fillLevel + random(-5, 6), 0, 100);
  temperature = temperature + random(-1, 2) * 0.1;
  weight = weight + random(-1, 2) * 0.1;
  
  // WiFi signal strength
  signalStrength = WiFi.RSSI();
}
