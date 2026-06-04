# Guía de Conexión de Dispositivos IoT

## Índice
1. [Requisitos Previos](#requisitos-previos)
2. [Arquitectura de Comunicación](#arquitectura-de-comunicación)
3. [Crear Dispositivo en el Panel](#crear-dispositivo-en-el-panel)
4. [Configurar MQTT Broker](#configurar-mqtt-broker)
5. [Conectar Raspberry Pi](#conectar-raspberry-pi)
6. [Conectar ESP32](#conectar-esp32)
7. [Formato de Telemetría](#formato-de-telemetría)
8. [Comandos Disponibles](#comandos-disponibles)
9. [Troubleshooting](#troubleshooting)

---

## Requisitos Previos

- **Panel de control** desplegado y accesible (local o producción)
- **Backend** corriendo con base de datos migrada y seed ejecutado
- **Broker MQTT** disponible (EMQX local o EMQX en DigitalOcean)
- Credenciales de **SuperAdmin** o **Admin** con acceso para crear dispositivos

---

## Arquitectura de Comunicación

```
┌─────────────────┐      MQTT      ┌──────────────┐
│   Dispositivo   │ ◄────────────► │    Broker    │
│  (ESP32 / RPi)  │   1883/8883   │  (EMQX/Hive) │
└─────────────────┘               └──────┬───────┘
                                         │
                                         │ HTTP
                                         ▼
                                  ┌──────────────┐
                                  │   Backend    │
                                  │   (NestJS)   │
                                  └──────────────┘
                                         │
                                         │ REST
                                         ▼
                                  ┌──────────────┐
                                  │    Panel     │
                                  │   (Next.js)  │
                                  └──────────────┘
```

**Flujo:**
1. Dispositivo se conecta al Broker MQTT con credenciales
2. Backend recibe telemetría desde el broker
3. Usuario ve datos en tiempo real en el panel
4. Usuario envía comandos desde el panel → backend → broker → dispositivo

---

## Crear Dispositivo en el Panel

### 1. Acceder al Panel
- **Producción**: `https://iot-panel.vercel.app`
- **Local**: `http://localhost:3000`

### 2. Login
Usa las credenciales de seed:
- **Email**: `admin@iot-platform.dev`
- **Password**: `admin1234`

### 3. Crear Proyecto (si no existe)
1. Ve a `/control/projects`
2. Click **"Crear Proyecto"**
3. Completa:
   - **Nombre**: Ecobotes Piloto
   - **Prefijo de Dispositivo**: `ECO` (importante para los topics MQTT)
4. Guardar

### 4. Crear Ubicación (opcional)
1. Ve a `/control/locations`
2. Click **"Crear Ubicación"**
3. Completa:
   - **Nombre**: Plaza Central
   - **Dirección**: Av. Principal 100
   - **Coordenadas**: Lat/Lng
4. Guardar

### 5. Crear Dispositivo
1. Ve a `/control/devices`
2. Click **"Crear Dispositivo"**
3. Completa:
   - **Proyecto**: Ecobotes Piloto
   - **Ubicación**: Plaza Central (opcional)
   - **Nombre**: Ecobote 01 (opcional, descriptivo)
   - **Tipo**: STANDALONE
4. **Guardar** → El sistema genera automáticamente:
   - **Serial**: `ECO-2026-0001`
   - **MQTT Token**: `a1b2c3d4e5f6...` (32 bytes hex)

⚠️ **IMPORTANTE**: Copia el **Serial** y el **MQTT Token** inmediatamente. El token solo se muestra una vez.

---

## Configurar MQTT Broker

### Opción A: Desarrollo Local (Docker)

```bash
# En la raíz del proyecto
docker-compose up -d emqx

# Verificar que EMQX está corriendo
docker logs iot_emqx

# Dashboard EMQX: http://localhost:18083
# Usuario: admin / Password: admin
```

**Config del dispositivo:**
- **Host**: `localhost` (o IP de tu máquina si el device está en otra red)
- **Port**: `1883` (TCP sin TLS)
- **Serial**: `ECO-2026-0001`
- **Token**: `a1b2c3d4e5f6...`

### Opción B: Producción (EMQX en DigitalOcean)

```bash
# URL de conexión segura
mqtts://167.172.141.63:8883
```

**Config del dispositivo:**
- **Host**: `167.172.141.63`
- **Port**: `8883` (TLS requerido)
- **Serial**: `ECO-2026-0001`
- **Token**: `a1b2c3d4e5f6...`

⚠️ **NOTA**: El certificado del broker EMQX es autofirmado. En el piloto los clientes se conectan en modo inseguro: en ESP32 usa `WiFiClientSecure` con `setInsecure()`, y en la Raspberry pasa `--tls --insecure`.

---

## Conectar Raspberry Pi

### 1. Instalar Dependencias

```bash
cd clients/rasp-client
pip install -r requirements.txt
```

### 2. Ejecutar Cliente

```bash
# Local (con EMQX en Docker)
python client.py \
  --serial ECO-2026-0001 \
  --token a1b2c3d4e5f6... \
  --host localhost \
  --port 1883 \
  --prefix ECO

# Producción (con EMQX en DigitalOcean)
python client.py \
  --serial ECO-2026-0001 \
  --token a1b2c3d4e5f6... \
  --host 167.172.141.63 \
  --port 8883 \
  --prefix ECO \
  --tls --insecure
```

### 3. Verificar Conexión

Deberías ver en consola:
```
[IoT] Connecting to localhost:1883 as ECO-2026-0001
[IoT] Connected successfully!
[IoT] Subscribed to ECO/ECO-2026-0001/commands
[IoT] Subscribed to ECO/ECO-2026-0001/commands/ack
[IoT] Status sent: online
[IoT] Telemetry sent: {"batteryLevel": 85.5, "fillLevel": 45.0, ...}
```

### 4. Personalizar Sensores

Edita `client.py` y reemplaza `_generate_telemetry()`:

```python
def _generate_telemetry(self) -> dict[str, Any]:
    # Ejemplo: leer sensor de distancia HC-SR04
    distance = read_ultrasonic_sensor()
    fill_level = calculate_fill_percentage(distance)
    
    return {
        "batteryLevel": read_battery_level(),
        "fillLevel": fill_level,
        "isAccepting": fill_level < 90,
        "temperature": read_temperature_sensor(),
        "weight": read_load_cell(),
        "signalStrength": read_wifi_rssi(),
        "extra": {
            "humidity": read_humidity_sensor(),
            "doorOpen": read_door_sensor(),
        },
    }
```

---

## Conectar ESP32

### 1. Instalar Librerías (Arduino IDE)

- **PubSubClient** by Nick O'Leary
- **ArduinoJson** by Benoit Blanchon
- **WiFi** (built-in ESP32)

### 2. Configurar Código

Edita `esp32-client/esp32-client.ino`:

```cpp
// ============== CONFIGURATION ==============

// WiFi credentials
const char* WIFI_SSID = "TuWiFi";
const char* WIFI_PASSWORD = "TuPassword";

// Device credentials (from platform)
const char* DEVICE_SERIAL = "ECO-2026-0001";
const char* MQTT_TOKEN = "a1b2c3d4e5f6...";

// MQTT Broker
const char* MQTT_HOST = "localhost";  // O IP de tu máquina
const int MQTT_PORT = 1883;

// Project prefix (from platform)
const char* PROJECT_PREFIX = "ECO";
```

### 3. Para producción EMQX (TLS)

Si usas EMQX en DigitalOcean en producción, necesitas `WiFiClientSecure`:

```cpp
#include <WiFiClientSecure.h>

WiFiClientSecure wifiSecureClient;
PubSubClient mqttClient(wifiSecureClient);

void setup() {
  // ... setup WiFi ...
  
  // Configurar TLS (opcional: validar certificado)
  wifiSecureClient.setInsecure();  // Solo para desarrollo
  
  mqttClient.setServer(MQTT_HOST, 8883);  // Puerto TLS
  // ... resto del setup ...
}
```

### 4. Subir Código

```bash
# Con PlatformIO
pio run --target upload

# O con Arduino IDE
# Tools → Board → ESP32 Dev Module
# Tools → Port → /dev/cu.usbserial-XXXX
# Click Upload
```

### 5. Verificar Conexión

Abre el Serial Monitor (115200 baud):
```
=== ESP32 IoT Client ===
Device Serial: ECO-2026-0001
MQTT Broker: 192.168.1.100
Connecting to WiFi..... Connected!
IP Address: 192.168.1.50
Sending provision request...
Provision request sent!
Connecting to MQTT... Connected!
Subscribed to: ECO/ECO-2026-0001/commands
Sending telemetry: {"batteryLevel":85,"fillLevel":45,...}
Telemetry sent successfully!
```

### 6. Personalizar Sensores

Reemplaza `updateSensors()` con lecturas reales:

```cpp
#include <DHT.h>

#define DHT_PIN 4
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

void updateSensors() {
  // Leer sensor de temperatura/humedad
  temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  
  // Leer sensor de distancia (ultrasonico)
  float distance = readUltrasonicDistance();
  fillLevel = map(distance, 0, 100, 0, 100);
  
  // Leer nivel de batería
  batteryLevel = readBatteryPercentage();
  
  // Señal WiFi
  signalStrength = WiFi.RSSI();
  
  // Guardar en extra
  extraHumidity = humidity;
}
```

---

## Formato de Telemetría

### Campos Estándar

| Campo | Tipo | Descripción | Ejemplo |
|-------|------|-------------|---------|
| `batteryLevel` | float | % de batería (0-100) | `85.5` |
| `fillLevel` | float | % de llenado (0-100) | `45.0` |
| `isAccepting` | boolean | Acepta input | `true` |
| `temperature` | float | Temperatura en °C | `22.5` |
| `weight` | float | Peso en kg | `12.3` |
| `signalStrength` | int | dBm de señal | `-65` |
| `extra` | object | Datos adicionales | `{ "humidity": 55 }` |

### Ejemplo JSON

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
    "pressure": 1013.25,
    "doorOpen": false
  }
}
```

---

## Comandos Disponibles

### Enviados desde el Panel

| Comando | Payload | Descripción |
|---------|---------|-------------|
| `REBOOT` | — | Reinicia el dispositivo |
| `SET_ACCEPTING` | `{ "value": true/false }` | Activa/desactiva aceptación |
| `OPEN_LID` | — | Abre la tapa (servo) |
| `GET_STATUS` | — | Solicita estado actual |
| `CONSOLE_LOG` | `{ "message": "...", "level": "info" }` | Log remoto |

### Flujo de Comando

```
Panel (Usuario) → Backend → MQTT Broker → Dispositivo
                                                      │
                                                      ▼
Panel ← Backend ← MQTT Broker ←── ACK ─── Dispositivo
```

### Estructura del Comando

```json
{
  "command": "SET_ACCEPTING",
  "payload": { "value": false },
  "logId": "550e8400-e29b-41d4-a716-446655440000"
}
```

### ACK del Dispositivo

```json
{
  "logId": "550e8400-e29b-41d4-a716-446655440000",
  "status": "ACKNOWLEDGED"
}
```

---

## Troubleshooting

### ❌ "Connection refused"

**Causa**: Broker no accesible

**Solución**:
```bash
# Verificar que EMQX está corriendo
docker ps | grep emqx

# Verificar puertos abiertos
netstat -an | grep 1883

# Si está en otra máquina, usa la IP correcta
python client.py --host 192.168.1.100 ...
```

### ❌ "Authentication failed"

**Causa**: Serial o token incorrecto

**Solución**:
- Verifica que el serial coincide exactamente (mayúsculas)
- Verifica que el token es el correcto (32 bytes hex)
- Si rotaste el token, usa el nuevo

### ❌ "MQTT error" / "Offline"

**Causa**: Backend no puede conectar al broker

**Solución**:
```bash
# Local: asegúrate que el .env apunta a localhost
MQTT_URL=mqtt://localhost:1883

# Si usas Docker para el backend pero no para EMQX:
# El backend en Docker necesita la IP de tu máquina host
MQTT_URL=mqtt://192.168.68.101:1883
```

### ❌ No aparece telemetría en el panel

**Causa**: Topic incorrecto o dispositivo no existe en DB

**Verificar**:
1. El prefijo coincide: `ECO/ECO-2026-0001/telemetry`
2. El serial existe en la base de datos
3. El dispositivo pertenece al proyecto correcto

### ❌ Comandos no llegan al dispositivo

**Causa**: Dispositivo no suscrito al topic de comandos

**Verificar**:
- El dispositivo debe estar conectado (status = online)
- El topic de suscripción es correcto: `{prefix}/{serial}/commands`

---

## Configuraciones por Entorno

### Desarrollo Local

| Servicio | URL/Host | Puerto |
|----------|----------|--------|
| Panel | http://localhost:3000 | 3000 |
| Backend | http://localhost:3001 | 3001 |
| MQTT Broker | localhost | 1883 |
| PostgreSQL | localhost | 5433 |
| Redis | localhost | 6379 |

### Producción (Render + Vercel)

| Servicio | URL/Host | Puerto |
|----------|----------|--------|
| Panel | https://iot-panel.vercel.app | 443 |
| Backend | https://iot-backend-n77w.onrender.com | 443 |
| MQTT Broker | 167.172.141.63 | 8883 |

---

## Próximos Pasos

- [ ] Implementar Deep Sleep en ESP32 para ahorro de energía
- [ ] Agregar OTA (Over-The-Air) updates
- [ ] Implementar cacheo local de telemetría cuando no hay conexión
- [ ] Crear librería reutilizable para nuevas plataformas
- [ ] Agregar más sensores (GPS, acelerómetro, etc.)

---

## Soporte

Si tienes problemas:
1. Revisa los logs del dispositivo
2. Revisa los logs del backend (Render Dashboard → Logs)
3. Verifica la conexión MQTT con un cliente de prueba (MQTT Explorer, mosquitto_sub)
4. Consulta la documentación de EMQX según tu broker