/**
 * AwareTrack · client POC (provisioning DOS FASES + NAT + MQTT + 1 sensor fake)
 * -----------------------------------------------------------------------------
 * Evolución poc-2 -> poc-3: el provisioning pasa de UNA fase a DOS FASES con NAT.
 * El teléfono se une al SoftAP del ESP y, una vez la STA tiene internet, el ESP
 * activa NAPT para que el teléfono navegue A TRAVÉS del ESP (sin datos móviles)
 * y pueda crear los recursos en el backend. Luego el teléfono manda las
 * credenciales MQTT y el ESP empieza a publicar telemetría (sensor FAKE).
 *
 * Doc: ../../docs/poc-expo-esp32-wifi.md
 *
 * Flujo:
 *  - Arranca: si hay WiFi+MQTT en NVS -> conecta WiFi + MQTT directo, SIN AP.
 *             si no -> REPOSO (LED apagado). BOOT 5 s para MODO AP.
 *  - Botón BOOT: 5 s -> MODO AP (pairing) · 10 s -> FACTORY RESET (olvida todo).
 *  - MODO AP (AP+STA): el SoftAP ofrece DNS (8.8.8.8) por DHCP desde el arranque.
 *      FASE 1 (WiFi + NAT):
 *        GET  /info    -> identidad + fase actual
 *        GET  /scan    -> redes 2.4GHz visibles (select de la app)
 *        POST /wifi    -> { ssid, password }  -> conecta STA. Al obtener IP:
 *                         WiFi.AP.enableNAPT(true) -> el teléfono ya tiene internet.
 *        GET  /status  -> { wifi, ip, nat, mqtt, serial }  (fase1 ok: wifi && nat)
 *      FASE 2 (MQTT):
 *        POST /device  -> { serial,token,mqttHost,mqttPort,prefix,tlsInsecure,
 *                           sensorKey,sensorMin,sensorMax,intervalMs } -> conecta MQTT.
 *        GET  /status  -> ... (fase2 ok: mqtt==true)
 *  - ~30 s después de que MQTT conecta (solo en MODO AP) se apaga el SoftAP
 *    (softAPdisconnect) manteniendo STA+MQTT vivos. El AP solo se reabre con botón.
 *
 * LED (GPIO2):
 *  - APAGADO        = reposo
 *  - parpadeo rápido (150ms) = MODO AP
 *  - parpadeo lento  (500ms) = conectando (WiFi o MQTT)
 *  - FIJO           = WiFi + MQTT OK
 *
 * Librerías extra: ArduinoJson + PubSubClient. WiFi/WebServer/Preferences son del core.
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include "esp_netif.h"   // DNS por DHCP en el SoftAP (esp_netif_dhcps_option, etc.)

// ============== HARDWARE ==============
const int  LED_PIN     = 2;
const int  BUTTON_PIN  = 0;
const char* AP_SSID    = "ESP32-Setup";
const char* FW_VERSION = "poc-3";
const char* MODEL      = "AWARETRACK-001";
const unsigned long LONG_PRESS_MS    = 5000;
const unsigned long FACTORY_RESET_MS = 10000;
const unsigned long AP_TEARDOWN_MS   = 30000;   // apaga el AP 30 s tras conectar MQTT

// DEBUG: en 1 = olvida TODO en cada arranque (re-probar provisioning sin borrar flash).
#define DEBUG_FORGET_WIFI 0
// ======================================

Preferences prefs;
WebServer server(80);
WiFiClientSecure tlsClient;
PubSubClient mqtt(tlsClient);

// WiFi
String staSsid, staPass;
// MQTT / device (vienen del backend vía la app)
String cfgSerial, cfgToken, cfgHost, cfgPrefix;
int    cfgPort = 8883;
bool   cfgTlsInsecure = true;
bool   hasMqttCfg = false;
// Sensor (fake por ahora)
String sensorKey = "valor";
int    sensorMin = 1, sensorMax = 100;
unsigned long intervalMs = 10000;

bool apMode = false;
bool natEnabled = false;          // NAPT activo (AP enruta a internet vía STA)
unsigned long mqttUpAt = 0;       // millis del instante en que MQTT conectó (para teardown AP)
bool apTorndown = false;          // el AP ya se apagó tras conectar MQTT

// botón
unsigned long btnDownAt = 0;
bool btnHandled = false;
// LED
unsigned long lastBlink = 0;
bool ledOn = false;
// conexión WiFi controlada
bool pendingConnect = false;
bool connecting = false;
unsigned long connectStartedAt = 0;
// MQTT timing
unsigned long lastMqttTry = 0;
unsigned long lastPublish = 0;

// forward declarations
void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
void loadConfig();
void saveWifi();
void saveDevice();
void clearConfig();
void startStation();
void enterApMode();
void configureApDns();
void enableNatIfApMode();
void maybeTeardownAp();
void handleInfo();
void handleScan();
void handleWifi();
void handleDevice();
void handleStatus();
void maybeConnect();
void mqttLoop();
bool mqttConnect();
void publishTelemetry();
void handleButton();
void factoryReset();
void updateLed();
void scanSerial();
const char* phaseStr();
const char* authModeStr(wifi_auth_mode_t m);
const char* reasonHint(uint8_t r);

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  randomSeed(esp_random());

  WiFi.onEvent(onWifiEvent);
  loadConfig();

#if DEBUG_FORGET_WIFI
  clearConfig();
  staSsid = ""; staPass = ""; cfgSerial = ""; hasMqttCfg = false;
  Serial.println("[debug] DEBUG_FORGET_WIFI: NVS borrada al arrancar.");
#endif

  server.on("/info",   HTTP_GET,  handleInfo);
  server.on("/scan",   HTTP_GET,  handleScan);
  server.on("/wifi",   HTTP_POST, handleWifi);
  server.on("/device", HTTP_POST, handleDevice);
  server.on("/status", HTTP_GET,  handleStatus);

  Serial.printf("\n=== AwareTrack client %s (%s) ===\n", FW_VERSION, MODEL);
  if (staSsid.length() > 0) {
    // Arranque con NVS: conecta WiFi (+ MQTT si lo hay) directo, SIN abrir AP.
    Serial.print("Config guardada. SSID: "); Serial.println(staSsid);
    if (hasMqttCfg) Serial.println("MQTT en NVS: se conectará tras obtener IP.");
    startStation();
  } else {
    Serial.println("En reposo. Manten BOOT 5 s para MODO AP.");
  }
}

void loop() {
  handleButton();
  if (apMode) server.handleClient();
  maybeConnect();
  mqttLoop();
  maybeTeardownAp();
  updateLed();
}

// ============== NVS ==============

void loadConfig() {
  prefs.begin("pocwifi", true);
  staSsid        = prefs.getString("ssid", "");
  staPass        = prefs.getString("pass", "");
  cfgSerial      = prefs.getString("serial", "");
  cfgToken       = prefs.getString("token", "");
  cfgHost        = prefs.getString("host", "");
  cfgPort        = prefs.getInt("port", 8883);
  cfgPrefix      = prefs.getString("prefix", "");
  cfgTlsInsecure = prefs.getBool("tlsInsec", true);
  sensorKey      = prefs.getString("sKey", "valor");
  sensorMin      = prefs.getInt("sMin", 1);
  sensorMax      = prefs.getInt("sMax", 100);
  intervalMs     = prefs.getUInt("interval", 10000);
  prefs.end();
  hasMqttCfg = cfgSerial.length() && cfgToken.length() && cfgHost.length();
}

// Fase 1: solo credenciales WiFi.
void saveWifi() {
  prefs.begin("pocwifi", false);
  prefs.putString("ssid", staSsid);
  prefs.putString("pass", staPass);
  prefs.end();
}

// Fase 2: credenciales MQTT/device + sensor.
void saveDevice() {
  prefs.begin("pocwifi", false);
  prefs.putString("serial", cfgSerial);
  prefs.putString("token", cfgToken);
  prefs.putString("host", cfgHost);
  prefs.putInt("port", cfgPort);
  prefs.putString("prefix", cfgPrefix);
  prefs.putBool("tlsInsec", cfgTlsInsecure);
  prefs.putString("sKey", sensorKey);
  prefs.putInt("sMin", sensorMin);
  prefs.putInt("sMax", sensorMax);
  prefs.putUInt("interval", intervalMs);
  prefs.end();
}

void clearConfig() {
  prefs.begin("pocwifi", false);
  prefs.clear();
  prefs.end();
}

// ============== MODOS ==============

void startStation() {
  apMode = false;
  WiFi.mode(WIFI_STA);
  connecting = true;
  connectStartedAt = millis();
  WiFi.begin(staSsid.c_str(), staPass.c_str());
  Serial.print("Conectando a "); Serial.println(staSsid);
}

void enterApMode() {
  apMode = true;
  apTorndown = false;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID);
  configureApDns();               // DNS por DHCP ANTES de que el teléfono se una
  server.begin();
  Serial.print("MODO AP · \""); Serial.print(AP_SSID);
  Serial.print("\" en http://"); Serial.println(WiFi.softAPIP());
  scanSerial();
}

// El DHCP del SoftAP debe ofrecer un DNS (8.8.8.8) para que el teléfono resuelva
// dominios una vez activo el NAT. Hay que reiniciar el DHCPS para aplicar la opción.
void configureApDns() {
  esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!ap) {
    Serial.println("[dns] no se pudo obtener el netif del AP");
    return;
  }
  esp_netif_dns_info_t dns = {};
  dns.ip.type = ESP_IPADDR_TYPE_V4;
  dns.ip.u_addr.ip4.addr = esp_ip4addr_aton("8.8.8.8");

  esp_netif_dhcps_stop(ap);
  esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
  uint8_t offer = 2;   // DHCPS_OFFER_DNS: ofrecer el DNS configurado a los clientes
  esp_err_t e1 = esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET,
                                        ESP_NETIF_DOMAIN_NAME_SERVER,
                                        &offer, sizeof(offer));
  esp_err_t e2 = esp_netif_dhcps_start(ap);
  Serial.printf("[dns] DHCPS ofrece DNS 8.8.8.8 (opt=0x%x start=0x%x)\n", e1, e2);
}

// NAPT: activar cuando la STA obtiene IP, solo en MODO AP.
void enableNatIfApMode() {
  if (!apMode) return;
  bool ok = WiFi.AP.enableNAPT(true);
  natEnabled = ok;
  Serial.printf("[nat] enableNAPT(true) -> %s\n", ok ? "OK (AP enruta a internet)" : "FALLO");
}

// ~30 s después de que MQTT conecta, apaga el SoftAP (solo en MODO AP),
// manteniendo STA+MQTT vivos. No reinicia. El AP solo se reabre con el botón.
void maybeTeardownAp() {
  if (!apMode || apTorndown || mqttUpAt == 0) return;
  if (!mqtt.connected()) return;
  if (millis() - mqttUpAt < AP_TEARDOWN_MS) return;

  Serial.println("[ap] 30 s tras MQTT -> apagando SoftAP (STA+MQTT siguen vivos)");
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apMode = false;
  natEnabled = false;     // ya no hay AP que enrute
  apTorndown = true;
}

// ============== HTTP ==============

void handleInfo() {
  JsonDocument d;
  d["model"] = MODEL;
  d["fw"] = FW_VERSION;
  d["mac"] = WiFi.macAddress();
  d["phase"] = phaseStr();
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

// Devuelve las redes 2.4GHz visibles (deduplicadas por SSID, mejor RSSI) para el select.
void handleScan() {
  int n = WiFi.scanNetworks();
  JsonDocument d;
  JsonArray arr = d.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool dup = false;
    for (JsonObject o : arr) {
      if (ssid == o["ssid"].as<const char*>()) { dup = true; break; }
    }
    if (dup) continue;
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = ssid;
    o["rssi"] = WiFi.RSSI(i);
    o["channel"] = WiFi.channel(i);
    o["auth"] = authModeStr(WiFi.encryptionType(i));
    o["open"] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

// FASE 1: guarda WiFi en NVS y conecta la STA (un solo begin controlado).
// No toca MQTT. Al obtener IP (GOT_IP) se activa el NAT.
void handleWifi() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty\"}");
    return;
  }
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad_json\"}");
    return;
  }
  if (!d["ssid"].is<const char*>() || String((const char*)d["ssid"]).isEmpty()) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing_ssid\"}");
    return;
  }

  staSsid = (const char*)d["ssid"];
  staPass = d["password"].is<const char*>() ? (const char*)d["password"] : "";

  saveWifi();
  pendingConnect = true;   // loop() dispara UN solo WiFi.begin (sin bucle)

  server.send(200, "application/json", "{\"ok\":true}");
}

// FASE 2: guarda credenciales MQTT/device + sensor en NVS y conecta MQTT.
void handleDevice() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty\"}");
    return;
  }
  JsonDocument d;
  if (deserializeJson(d, server.arg("plain"))) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"bad_json\"}");
    return;
  }
  if (!d["serial"].is<const char*>() || String((const char*)d["serial"]).isEmpty()) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing_serial\"}");
    return;
  }

  cfgSerial = (const char*)d["serial"];
  cfgToken  = d["token"].is<const char*>()    ? (const char*)d["token"]    : "";
  cfgHost   = d["mqttHost"].is<const char*>() ? (const char*)d["mqttHost"] : "";
  cfgPort   = d["mqttPort"] | 8883;
  cfgPrefix = d["prefix"].is<const char*>()   ? (const char*)d["prefix"]   : "";
  cfgTlsInsecure = d["tlsInsecure"] | true;

  // Sensor (fake)
  sensorKey  = d["sensorKey"].is<const char*>() ? (const char*)d["sensorKey"] : "valor";
  sensorMin  = d["sensorMin"] | 1;
  sensorMax  = d["sensorMax"] | 100;
  intervalMs = d["intervalMs"] | 10000;

  hasMqttCfg = cfgSerial.length() && cfgToken.length() && cfgHost.length();
  saveDevice();
  lastMqttTry = 0;   // que mqttLoop intente conectar ya en el siguiente ciclo

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleStatus() {
  JsonDocument d;
  bool wifi = WiFi.status() == WL_CONNECTED;
  d["wifi"]   = wifi;
  d["ip"]     = wifi ? WiFi.localIP().toString() : "";
  d["nat"]    = natEnabled;
  d["mqtt"]   = mqtt.connected();
  d["serial"] = cfgSerial;
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

// ============== CONEXIÓN WiFi (controlada) ==============

void maybeConnect() {
  if (!pendingConnect) return;
  if (WiFi.status() == WL_CONNECTED) { pendingConnect = false; return; }
  // Si /wifi se llama varias veces durante un intento en curso, NO re-begin en bucle.
  if (connecting && millis() - connectStartedAt < 20000) { pendingConnect = false; return; }

  pendingConnect = false;
  connecting = true;
  connectStartedAt = millis();
  Serial.print("Conectando a: "); Serial.println(staSsid);
  WiFi.disconnect(true);
  delay(50);
  WiFi.begin(staSsid.c_str(), staPass.c_str());
}

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[wifi] asociado, pidiendo IP...");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      connecting = false;
      Serial.print("[wifi] CONECTADO. IP: "); Serial.println(WiFi.localIP());
      enableNatIfApMode();   // FASE 1 lista: el teléfono ya tiene internet vía ESP
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      connecting = false;
      natEnabled = false;
      Serial.print("[wifi] desconectado. reason=");
      Serial.print(info.wifi_sta_disconnected.reason);
      Serial.println(reasonHint(info.wifi_sta_disconnected.reason));
      break;
    default: break;
  }
}

// ============== MQTT ==============

void mqttLoop() {
  if (WiFi.status() != WL_CONNECTED || !hasMqttCfg) return;

  if (!mqtt.connected()) {
    if (millis() - lastMqttTry < 3000) return;
    lastMqttTry = millis();
    mqttConnect();
    return;
  }
  mqtt.loop();
  if (millis() - lastPublish >= intervalMs) {
    lastPublish = millis();
    publishTelemetry();
  }
}

bool mqttConnect() {
  if (cfgTlsInsecure) tlsClient.setInsecure();   // cert autofirmado (piloto)
  mqtt.setServer(cfgHost.c_str(), cfgPort);

  String statusTopic = cfgPrefix + "/" + cfgSerial + "/status";
  Serial.printf("[mqtt] conectando a %s:%d como %s ...\n", cfgHost.c_str(), cfgPort, cfgSerial.c_str());

  // LWT: si el ESP cae, el broker publica "offline" (retained) en el topic de status
  bool ok = mqtt.connect(cfgSerial.c_str(), cfgSerial.c_str(), cfgToken.c_str(),
                         statusTopic.c_str(), 1, true, "offline");
  if (ok) {
    Serial.println("[mqtt] CONECTADO. publicando online.");
    mqtt.publish(statusTopic.c_str(), "online", true);
    String cmdTopic = cfgPrefix + "/" + cfgSerial + "/commands";
    mqtt.subscribe(cmdTopic.c_str());
    if (mqttUpAt == 0) mqttUpAt = millis();   // arranca el cronómetro de teardown del AP
    publishTelemetry();   // primer dato inmediato
  } else {
    Serial.printf("[mqtt] fallo rc=%d (revisa token/host)\n", mqtt.state());
  }
  return ok;
}

void publishTelemetry() {
  int val = random(sensorMin, sensorMax + 1);   // FAKE: aleatorio min..max
  JsonDocument d;
  d["extra"][sensorKey] = val;                   // tableros leen extra.<key>
  String out; serializeJson(d, out);
  String topic = cfgPrefix + "/" + cfgSerial + "/telemetry";
  mqtt.publish(topic.c_str(), out.c_str());
  Serial.printf("[mqtt] -> %s %s\n", topic.c_str(), out.c_str());
}

// ============== BOTÓN / LED ==============

void handleButton() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (btnDownAt == 0) { btnDownAt = millis(); btnHandled = false; }
    unsigned long held = millis() - btnDownAt;

    if (held >= FACTORY_RESET_MS) {
      Serial.println("Long-press 10 s -> FACTORY RESET");
      factoryReset();
    } else if (!btnHandled && held >= LONG_PRESS_MS) {
      btnHandled = true;
      Serial.println("Long-press 5 s -> MODO AP");
      if (!apMode) enterApMode();
    }
  } else {
    btnDownAt = 0;
  }
}

void factoryReset() {
  clearConfig();
  for (int i = 0; i < 10; i++) { digitalWrite(LED_PIN, i % 2); delay(150); }
  digitalWrite(LED_PIN, LOW);
  Serial.println("[factory] NVS borrada. Reiniciando...");
  delay(300);
  ESP.restart();
}

void updateLed() {
  bool wifi = WiFi.status() == WL_CONNECTED;
  if (wifi && (!hasMqttCfg || mqtt.connected())) {   // todo OK -> fijo
    digitalWrite(LED_PIN, HIGH);
    return;
  }
  if (!apMode && staSsid.length() == 0) {            // reposo -> apagado
    digitalWrite(LED_PIN, LOW);
    return;
  }
  unsigned long period = apMode ? 150 : 500;         // AP rápido / conectando lento
  if (millis() - lastBlink >= period) {
    lastBlink = millis();
    ledOn = !ledOn;
    digitalWrite(LED_PIN, ledOn ? HIGH : LOW);
  }
}

// ============== DIAGNÓSTICO ==============

void scanSerial() {
  Serial.println("[scan] redes 2.4GHz visibles:");
  int n = WiFi.scanNetworks();
  if (n <= 0) { Serial.println("  (ninguna)"); return; }
  for (int i = 0; i < n; i++) {
    Serial.printf("  ch%2d  %4ddBm  %-10s  %s\n",
                  WiFi.channel(i), WiFi.RSSI(i),
                  authModeStr(WiFi.encryptionType(i)), WiFi.SSID(i).c_str());
  }
  WiFi.scanDelete();
}

// Fase actual para /info: idle (reposo) | wifi (STA up, sin MQTT) |
// online (STA up + NAT activo) | mqtt (MQTT conectado).
const char* phaseStr() {
  if (mqtt.connected()) return "mqtt";
  bool wifi = WiFi.status() == WL_CONNECTED;
  if (wifi && natEnabled) return "online";
  if (wifi) return "wifi";
  return "idle";
}

const char* reasonHint(uint8_t r) {
  switch (r) {
    case 201: return " (NO_AP_FOUND: SSID no visible en 2.4GHz -> 5GHz/typo)";
    case 15:  return " (4WAY_HANDSHAKE_TIMEOUT: contrasena incorrecta o WPA3)";
    case 2:   return " (AUTH_EXPIRE)";
    case 3:   return " (AUTH_LEAVE)";
    case 205: return " (CONNECTION_FAIL)";
    default:  return "";
  }
}

const char* authModeStr(wifi_auth_mode_t m) {
  switch (m) {
    case WIFI_AUTH_OPEN:          return "OPEN";
    case WIFI_AUTH_WEP:           return "WEP";
    case WIFI_AUTH_WPA_PSK:       return "WPA";
    case WIFI_AUTH_WPA2_PSK:      return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:  return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:      return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default:                      return "?";
  }
}
