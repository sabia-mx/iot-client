# ESP32 Client

Cliente IoT para ESP32 (Arduino framework).

## Requisitos

### Arduino IDE

Instala las siguientes librerías desde Library Manager:

- **PubSubClient** by Nick O'Leary
- **ArduinoJson** by Benoit Blanchon

### PlatformIO

El archivo `platformio.ini` ya incluye las dependencias.

```bash
pio run upload
```

## Configuración

1. **Crea un dispositivo en el panel**:
   - Ve a Organizations → Tu Org → Projects → Tu Proyecto
   - Crea un dispositivo
   - Copia el `serial` y el `mqttToken`

2. **Edita `esp32-client.ino`** y configura:

```cpp
// WiFi
const char* WIFI_SSID = "TuRedWiFi";
const char* WIFI_PASSWORD = "TuPassword";

// Device credentials
const char* DEVICE_SERIAL = "ECO-2026-0001";  // Tu serial
const char* MQTT_TOKEN = "tu-mqtt-token-aqui";  // Tu token

// Broker
const char* MQTT_HOST = "192.168.1.100";  // IP de tu broker
```

## Compilar y subir

### Arduino IDE

1. Selecciona tu placa: Tools → Board → ESP32 Dev Module
2. Configura el puerto: Tools → Port
3. Compila y sube: Sketch → Upload

### PlatformIO

```bash
pio run upload
pio device monitor
```

## Topics MQTT

### Enviar telemetría

- **Topic**: `{prefix}/{serial}/telemetry`
- **Payload**:

```json
{
  "batteryLevel": 85.5,
  "fillLevel": 45.0,
  "isAccepting": true,
  "temperature": 22.5,
  "weight": 12.3,
  "signalStrength": -65,
  "extra": {
    "humidity": 55.0
  }
}
```

### Estado del dispositivo

- **Topic**: `{prefix}/{serial}/status`
- **Payload**: `online` o `offline`

### Recepción de comandos

- **Topic**: `{prefix}/{serial}/commands`
- **Payload**:

```json
{
  "command": "SET_ACCEPTING",
  "payload": { "value": false },
  "logId": "uuid"
}
```

### Provision

- **Topic**: `devices/provision/{serial}`
- **Payload**: vacío

## Comandos disponibles

- `SET_ACCEPTING` - Activar/desactivar aceptación
- `OPEN_LID` - Abrir tapa (agrega tu código)
- `REBOOT` - Reiniciar ESP32
- `GET_STATUS` - Solicitar estado

## Credentiales MQTT

- **Client ID**: Serial del dispositivo
- **Username**: Serial del dispositivo
- **Password**: MQTT Token

## Hardware

Conectar sensores típicos:

- **Batería**: Pin ADC
- **Ultrasonido** (fill level): Trig/D echo
- **Temperatura**: DHT22
- **Peso**: Celda de carga + HX711
