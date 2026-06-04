# Datos de Conexión para Desarrollador de Hardware

## Lo que necesitas saber

Solo necesitas **3 datos** para conectar tu placa IoT a la plataforma:

1. **Serial** del dispositivo (identificador único)
2. **MQTT Token** (password secreto)
3. **MQTT URL** (broker - viene automáticamente del panel)

Todo lo demás (topics, formato JSON, comandos) es estándar y está documentado abajo.

---

## 1. Broker MQTT (Automático)

**¡Buenas noticias!** La URL del broker MQTT se genera automáticamente cuando el administrador crea el dispositivo en el panel. No necesitas saberla de antemano.

### Lo que te dará el administrador:

| Dato | Ejemplo (Producción) | Ejemplo (Desarrollo) |
|------|----------------------|----------------------|
| **MQTT URL** | `mqtts://167.172.141.63:8883` | `mqtt://localhost:1883` |

### Formato completo:

```
Producción: mqtts://167.172.141.63:8883
Desarrollo: mqtt://localhost:1883
```

**El administrador verá esto automáticamente al crear el dispositivo:**

```json
{
  "serial": "ECO-2026-0001",
  "token": "a1b2c3d4...",
  "mqttUrl": "mqtts://167.172.141.63:8883"
}
```

---

## 2. Credenciales del Dispositivo

Cada dispositivo necesita un **Serial** y un **MQTT Token**. Además, el administrador te dará la **MQTT URL** automáticamente.

Estos te los da el administrador desde el panel web. Deben ser configurados en tu firmware:

```cpp
// Ejemplo ESP32
const char* DEVICE_SERIAL = "ECO-2026-0001";      // Identificador único
const char* MQTT_TOKEN = "a1b2c3d4e5f6...";       // Password/Token secreto
const char* PROJECT_PREFIX = "ECO";                // Prefijo del proyecto
// MQTT URL (automático del panel):
// mqtts://167.172.141.63:8883
```

### Autenticación MQTT

| Campo | Valor |
|-------|-------|
| **Username** | El Serial del dispositivo (ej: `ECO-2026-0001`) |
| **Password** | El MQTT Token (ej: `a1b2c3d4e5f6...`) |

---

## 3. Topics MQTT

Todos los topics siguen el patrón:

```
{prefijo}/{serial}/{tipo}
```

### Topics que la placa debe PUBLICAR (enviar):

| Topic | Payload | Descripción | Frecuencia |
|-------|---------|-------------|------------|
| `{prefix}/{serial}/telemetry` | JSON con datos del sensor | Telemetría de los sensores | Cada 30 segundos |
| `{prefix}/{serial}/status` | `online` o `offline` | Estado de conexión | Al conectar/desconectar |
| `devices/provision/{serial}` | vacío `{""}` | Primer contacto (una vez al inicio) | Solo al arrancar |

### Topics que la placa debe SUSCRIBIRSE (recibir):

| Topic | Payload | Descripción |
|-------|---------|-------------|
| `{prefix}/{serial}/commands` | JSON con comando | Comandos desde el panel web |
| `{prefix}/{serial}/commands/ack` | JSON con ack | Confirmaciones de comandos |

### Ejemplo con Serial = `ECO-2026-0001` y Prefix = `ECO`:

```
Publicar:
  ECO/ECO-2026-0001/telemetry    → {"batteryLevel":85.5,...}
  ECO/ECO-2026-0001/status       → online
  devices/provision/ECO-2026-0001 → ""

Suscribir:
  ECO/ECO-2026-0001/commands     ← {"command":"SET_ACCEPTING","payload":{...}}
  ECO/ECO-2026-0001/commands/ack ← {"logId":"...","status":"ACKNOWLEDGED"}
```

---

## 4. Formato de Telemetría (JSON)

Este es el JSON que debe enviar tu placa cada 30 segundos:

```json
{
  "batteryLevel": 85.5,
  "fillLevel": 45.0,
  "isAccepting": true,
  "temperature": 22.5,
  "weight": 12.3,
  "signalStrength": -65,
  "extra": {
    "humidity": 55.0,
    "pressure": 1013.25
  }
}
```

### Campos:

| Campo | Tipo | Requerido | Descripción |
|-------|------|-----------|-------------|
| `batteryLevel` | float (0-100) | No | % de batería |
| `fillLevel` | float (0-100) | No | % de llenado del contenedor |
| `isAccepting` | boolean | No | ¿Acepta input? (botellas, etc.) |
| `temperature` | float (°C) | No | Temperatura |
| `weight` | float (kg) | No | Peso |
| `signalStrength` | int (dBm) | No | Fuerza de señal WiFi |
| `extra` | object | No | Cualquier dato adicional que quieras enviar |

**Nota**: Todos los campos son opcionales. Puedes enviar solo los que tu placa tenga sensores.

---

## 5. Formato de Comandos (JSON entrante)

Tu placa recibirá comandos en el topic `commands`. Ejemplo:

```json
{
  "command": "SET_ACCEPTING",
  "payload": {
    "value": false
  },
  "logId": "550e8400-e29b-41d4-a716-446655440000"
}
```

### Comandos disponibles:

| Comando | Payload | Acción |
|---------|---------|--------|
| `REBOOT` | — | Reiniciar placa |
| `SET_ACCEPTING` | `{"value": true/false}` | Activar/desactivar aceptación |
| `OPEN_LID` | — | Abrir tapa |
| `GET_STATUS` | — | Enviar estado actual |
| `CONSOLE_LOG` | `{"message":"..."}` | Log remoto |

### Respuesta (ACK):

Después de ejecutar un comando, tu placa debe responder:

```json
{
  "logId": "550e8400-e29b-41d4-a716-446655440000",
  "status": "ACKNOWLEDGED"
}
```

Si falla:
```json
{
  "logId": "550e8400-e29b-41d4-a716-446655440000",
  "status": "FAILED",
  "error": "Motivo del error"
}
```

Publicar en: `{prefix}/{serial}/commands/ack`

---

## 6. LWT (Last Will and Testament)

Configura LWT al conectarte al broker:

```
Topic: {prefix}/{serial}/status
Message: offline
QoS: 1
Retain: true
```

Así si tu placa se desconecta inesperadamente, el broker publica automáticamente `offline`.

Y cuando te conectes correctamente, publica:

```
Topic: {prefix}/{serial}/status
Message: online
QoS: 1
Retain: true
```

---

## 7. Flujo de Conexión Completo

```
1. Conectar WiFi
   ↓
2. Conectar al Broker MQTT
   - Username: Serial del dispositivo
   - Password: MQTT Token
   - Configurar LWT: {prefix}/{serial}/status → "offline"
   ↓
3. Suscribirse a:
   - {prefix}/{serial}/commands
   - {prefix}/{serial}/commands/ack
   ↓
4. Enviar provision:
   - Topic: devices/provision/{serial}
   - Payload: ""
   ↓
5. Enviar status online:
   - Topic: {prefix}/{serial}/status
   - Payload: "online"
   ↓
6. Loop cada 30 segundos:
   - Leer sensores
   - Publicar telemetry
   - Verificar comandos entrantes
   - Responder con ACK
   ↓
7. Al desconectar (graceful):
   - Publicar status offline
   - Desconectar MQTT
```

---

## 8. Ejemplo de Código (Arduino/ESP32)

```cpp
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ============ CONFIGURACIÓN (te la da el admin) ============
const char* WIFI_SSID = "TuWiFi";
const char* WIFI_PASSWORD = "TuPassword";
const char* DEVICE_SERIAL = "ECO-2026-0001";  // ← Te lo dan
const char* MQTT_TOKEN = "a1b2c3d4e5f6";      // ← Te lo dan
const char* PROJECT_PREFIX = "ECO";            // ← Te lo dan
const char* MQTT_HOST = "167.172.141.63"; // ← Producción
const int MQTT_PORT = 8883;                     // ← 1883 para local

// Topics
String telemetryTopic = String(PROJECT_PREFIX) + "/" + DEVICE_SERIAL + "/telemetry";
String statusTopic = String(PROJECT_PREFIX) + "/" + DEVICE_SERIAL + "/status";
String commandsTopic = String(PROJECT_PREFIX) + "/" + DEVICE_SERIAL + "/commands";
String ackTopic = String(PROJECT_PREFIX) + "/" + DEVICE_SERIAL + "/commands/ack";
String provisionTopic = "devices/provision/" + String(DEVICE_SERIAL);

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

void setup() {
  Serial.begin(115200);
  connectWiFi();
  
  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(onMessage);
  connectMQTT();
  
  // Enviar provision
  mqttClient.publish(provisionTopic.c_str(), "");
  
  // Enviar online
  mqttClient.publish(statusTopic.c_str(), "online", true);
}

void loop() {
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();
  
  // Cada 30 segundos enviar telemetría
  static unsigned long lastSend = 0;
  if (millis() - lastSend > 30000) {
    lastSend = millis();
    sendTelemetry();
  }
}

void connectMQTT() {
  mqttClient.connect(
    DEVICE_SERIAL,      // clientID
    DEVICE_SERIAL,      // username = serial
    MQTT_TOKEN,         // password = token
    statusTopic.c_str(), // willTopic
    1,                   // willQoS
    true,                // willRetain
    "offline"            // willMessage
  );
  
  mqttClient.subscribe(commandsTopic.c_str());
}

void sendTelemetry() {
  StaticJsonDocument<512> doc;
  doc["batteryLevel"] = readBattery();
  doc["fillLevel"] = readFillLevel();
  doc["isAccepting"] = true;
  doc["temperature"] = readTemp();
  doc["weight"] = readWeight();
  doc["signalStrength"] = WiFi.RSSI();
  
  char buffer[512];
  serializeJson(doc, buffer);
  mqttClient.publish(telemetryTopic.c_str(), buffer);
}

void onMessage(char* topic, byte* payload, unsigned int length) {
  // Parsear JSON del comando
  // Ejecutar comando
  // Enviar ACK a ackTopic
}
```

---

## 9. Checklist para el Desarrollador

Antes de entregar la placa, asegúrate de:

- [ ] Conectar WiFi correctamente
- [ ] Conectar al broker MQTT con TLS (puerto 8883)
- [ ] Usar Serial como username y Token como password
- [ ] Configurar LWT (Last Will) para detectar desconexiones
- [ ] Suscribirse al topic de comandos
- [ ] Enviar provision al arrancar
- [ ] Enviar status "online" al conectar
- [ ] Enviar telemetría cada 30 segundos (mínimo)
- [ ] Responder ACK a cada comando recibido
- [ ] Enviar status "offline" al desconectar limpiamente

---

## 10. Datos que necesitas del Admin

Pide al administrador del sistema **3 datos** (la URL del broker viene automática):

| # | Dato | Ejemplo | Descripción |
|---|------|---------|-------------|
| 1 | **Serial** | `ECO-2026-0001` | Identificador único del dispositivo |
| 2 | **MQTT Token** | `a1b2c3d4e5f6...` | Password secreto |
| 3 | **MQTT URL** | `mqtts://167.172.141.63:8883` | Broker (automático del panel) |

**Nota:** El administrador ve la URL del broker automáticamente al crear el dispositivo. Si estás en desarrollo local, usa `mqtt://localhost:1883`.

---

## Preguntas Frecuentes

**¿Puedo usar cualquier librería MQTT?**
Sí, cualquier cliente MQTT funciona. En Arduino se usa `PubSubClient`, en Python `paho-mqtt`, en Node.js `mqtt`.

**¿Qué pasa si mi placa pierde conexión?**
El LWT enviará automáticamente "offline" al topic de status. Cuando se reconecte, envía "online" de nuevo.

**¿Puedo enviar telemetría más rápido que cada 30 segundos?**
Sí, puedes enviar cada 5-10 segundos si necesitas datos en tiempo real. Pero considera el consumo de batería y datos móviles.

**¿Necesito TLS/SSL?**
En producción (EMQX en DigitalOcean) sí es obligatorio. En desarrollo local no.

**¿Qué pasa si envío un campo que no está en la lista?**
Puedes enviar campos adicionales dentro de `extra`. Los campos principales son los estándar pero no son estrictamente obligatorios.

---

## Contacto

Si tienes dudas técnicas sobre la conexión MQTT, contacta al equipo de backend.
Si necesitas crear nuevos dispositivos o ver tokens, contacta al administrador del panel.