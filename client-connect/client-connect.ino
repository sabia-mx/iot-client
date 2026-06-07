/**
 * AwareTrack · client-connect
 * -----------------------------------------------------------------------------
 * Firmware MÍNIMO de provisioning WiFi por SoftAP.
 *
 * Qué hace:
 *  1. PAIR MODE: si no hay credenciales (o si mantienes el botón 3 s), levanta un
 *     SoftAP "AwareTrack-XXXX" y un servidor HTTP en 192.168.4.1. LED parpadea lento.
 *  2. La app se une a ese SoftAP y hace:
 *        GET  /info       -> identidad del equipo (para verificar antes de enviar)
 *        POST /provision  -> {ssid,password,serial,token,mqttHost,mqttPort,prefix,tlsInsecure}
 *     El equipo guarda todo en NVS (Preferences) y se reinicia.
 *  3. STA MODE: arranca, conecta al WiFi de casa y a EMQX (TLS), publica "online".
 *     A partir de aquí, la plataforma marca el device ONLINE.
 *
 * Esto es SÓLO la parte de conexión. Para telemetría real + manejo de comandos,
 * fusiónalo con clients/esp32-client/esp32-client.ino (mismos topics/credenciales).
 *
 * Librerías (Library Manager / PlatformIO):
 *  - ArduinoJson (bblanchon)
 *  - PubSubClient (knolleary)
 * WiFi, WebServer, Preferences, WiFiClientSecure son built-in del core ESP32.
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ============== HARDWARE ==============
#define LED_PIN     LED_BUILTIN   // GPIO2 en la mayoría de devkits
#define BUTTON_PIN  0             // GPIO0 (BOTÓN BOOT) — mantener 3 s = re-provisionar
#define MODEL       "AWARETRACK-001"
#define FW_VERSION  "1.0.0"

// SoftAP: para el piloto usamos un PSK fijo del producto (>= 8 chars WPA2).
// En producción: PSK por-equipo derivado del chip id e impreso en el QR/etiqueta.
#define AP_PSK      "awaretrack"

// ==========================================

Preferences prefs;
WebServer server(80);
WiFiClientSecure wifiClient;
PubSubClient mqtt(wifiClient);

// Config persistida
String cfgSsid, cfgPass, cfgSerial, cfgToken, cfgHost, cfgPrefix;
int    cfgPort = 8883;
bool   cfgTlsInsecure = true;
bool   provisioned = false;

bool provisioningMode = false;
unsigned long lastBlink = 0;
bool ledOn = false;
unsigned long btnPressedAt = 0;

unsigned long lastTelemetry = 0;
const unsigned long TELEMETRY_INTERVAL = 30000;

String chipId() {
  uint64_t mac = ESP.getEfuseMac();
  char buf[9];
  snprintf(buf, sizeof(buf), "%04X%04X",
           (uint16_t)(mac >> 32), (uint16_t)(mac & 0xFFFF));
  return String(buf);
}

// ============== SETUP ==============

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  loadConfig();

  Serial.println("\n=== AwareTrack client-connect ===");
  Serial.print("Chip ID: ");  Serial.println(chipId());
  Serial.print("Provisioned: "); Serial.println(provisioned ? "yes" : "no");

  if (provisioned && cfgSsid.length() > 0) {
    startStation();
  } else {
    startProvisioning();
  }
}

void loop() {
  handleButton();

  if (provisioningMode) {
    server.handleClient();
    blink(500);                       // PAIR MODE: parpadeo lento
    return;
  }

  // STA MODE
  if (WiFi.status() != WL_CONNECTED) {
    blink(150);                       // reconectando
    return;
  }
  if (!mqtt.connected()) {
    blink(150);
    connectMQTT();
  } else {
    digitalWrite(LED_PIN, HIGH);      // ONLINE: fijo
    mqtt.loop();
    unsigned long now = millis();
    if (now - lastTelemetry >= TELEMETRY_INTERVAL) {
      lastTelemetry = now;
      sendTelemetry();
    }
  }
}

// ============== NVS ==============

void loadConfig() {
  prefs.begin("awaretrack", true);    // read-only
  provisioned   = prefs.getBool("provisioned", false);
  cfgSsid       = prefs.getString("ssid", "");
  cfgPass       = prefs.getString("pass", "");
  cfgSerial     = prefs.getString("serial", "");
  cfgToken      = prefs.getString("token", "");
  cfgHost       = prefs.getString("host", "");
  cfgPort       = prefs.getInt("port", 8883);
  cfgPrefix     = prefs.getString("prefix", "");
  cfgTlsInsecure = prefs.getBool("tlsInsecure", true);
  prefs.end();
}

void saveConfig() {
  prefs.begin("awaretrack", false);   // read-write
  prefs.putString("ssid", cfgSsid);
  prefs.putString("pass", cfgPass);
  prefs.putString("serial", cfgSerial);
  prefs.putString("token", cfgToken);
  prefs.putString("host", cfgHost);
  prefs.putInt("port", cfgPort);
  prefs.putString("prefix", cfgPrefix);
  prefs.putBool("tlsInsecure", cfgTlsInsecure);
  prefs.putBool("provisioned", true);
  prefs.end();
}

void clearConfig() {
  prefs.begin("awaretrack", false);
  prefs.clear();
  prefs.end();
}

// ============== PAIR MODE (SoftAP + HTTP) ==============

void startProvisioning() {
  provisioningMode = true;
  String ap = "AwareTrack-" + chipId().substring(4);  // p.ej. AwareTrack-A1B2
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap.c_str(), AP_PSK);
  Serial.print("PAIR MODE · SoftAP: "); Serial.println(ap);
  Serial.print("PSK: "); Serial.println(AP_PSK);
  Serial.print("HTTP: http://"); Serial.println(WiFi.softAPIP());

  server.on("/info", HTTP_GET, handleInfo);
  server.on("/provision", HTTP_POST, handleProvision);
  server.begin();
}

void handleInfo() {
  JsonDocument doc;
  doc["id"] = chipId();
  doc["mac"] = WiFi.macAddress();
  doc["model"] = MODEL;
  doc["fw"] = FW_VERSION;
  doc["provisioned"] = provisioned;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleProvision() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty_body\"}");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad_json\"}");
    return;
  }
  // Campos obligatorios
  const char* req[] = {"ssid", "serial", "token", "mqttHost", "prefix"};
  for (const char* k : req) {
    if (!doc[k].is<const char*>() || String((const char*)doc[k]).isEmpty()) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing_field\"}");
      return;
    }
  }

  cfgSsid       = (const char*)doc["ssid"];
  cfgPass       = doc["password"].is<const char*>() ? (const char*)doc["password"] : "";
  cfgSerial     = (const char*)doc["serial"];
  cfgToken      = (const char*)doc["token"];
  cfgHost       = (const char*)doc["mqttHost"];
  cfgPort       = doc["mqttPort"] | 8883;
  cfgPrefix     = (const char*)doc["prefix"];
  cfgTlsInsecure = doc["tlsInsecure"] | true;

  saveConfig();
  Serial.println("Provisioning recibido. Guardado en NVS. Reiniciando...");

  server.send(200, "application/json", "{\"ok\":true}");
  delay(800);          // deja salir la respuesta
  ESP.restart();       // arranca en STA MODE con las nuevas credenciales
}

// ============== STA MODE ==============

void startStation() {
  provisioningMode = false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
  Serial.print("STA · conectando a "); Serial.println(cfgSsid);

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {  // ~20 s
    delay(500); blink(150); Serial.print("."); tries++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(" WiFi FALLÓ. Mantén el botón 3 s para re-provisionar.");
    return;  // el loop seguirá reintentando vía WiFi auto-reconnect
  }
  Serial.print(" OK. IP: "); Serial.println(WiFi.localIP());

  if (cfgTlsInsecure) wifiClient.setInsecure();  // cert autofirmado (piloto)
  mqtt.setServer(cfgHost.c_str(), cfgPort);
}

void connectMQTT() {
  String statusTopic = cfgPrefix + "/" + cfgSerial + "/status";
  Serial.print("MQTT · conectando a "); Serial.print(cfgHost); Serial.print(":"); Serial.println(cfgPort);

  // LWT: si el equipo cae, el broker publica "offline" en su topic de status
  bool ok = mqtt.connect(
    cfgSerial.c_str(),        // client id
    cfgSerial.c_str(),        // username = serial
    cfgToken.c_str(),         // password = mqttToken
    statusTopic.c_str(), 1, true, "offline");

  if (ok) {
    Serial.println("MQTT conectado · ONLINE");
    mqtt.publish(statusTopic.c_str(), "online", true);
    mqtt.subscribe((cfgPrefix + "/" + cfgSerial + "/commands").c_str());
  } else {
    Serial.print("MQTT falló rc="); Serial.println(mqtt.state());
    delay(3000);
  }
}

void sendTelemetry() {
  // Stub: reemplazar por lecturas reales de sensores.
  JsonDocument doc;
  doc["batteryLevel"] = 90.0;
  doc["fillLevel"] = 40.0;
  doc["signalStrength"] = WiFi.RSSI();
  String out;
  serializeJson(doc, out);
  String topic = cfgPrefix + "/" + cfgSerial + "/telemetry";
  mqtt.publish(topic.c_str(), out.c_str());
  Serial.print("Telemetría -> "); Serial.println(out);
}

// ============== BOTÓN / LED ==============

void handleButton() {
  if (digitalRead(BUTTON_PIN) == LOW) {        // presionado (pull-up)
    if (btnPressedAt == 0) btnPressedAt = millis();
    else if (millis() - btnPressedAt >= 3000) {  // 3 s -> re-provisionar
      Serial.println("Botón 3 s -> entrando a PAIR MODE");
      clearConfig();
      delay(300);
      ESP.restart();
    }
  } else {
    btnPressedAt = 0;
  }
}

void blink(unsigned long periodMs) {
  if (millis() - lastBlink >= periodMs) {
    lastBlink = millis();
    ledOn = !ledOn;
    digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
  }
}
