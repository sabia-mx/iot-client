# IoT Clients

Ejemplos de clientes para conectar dispositivos a la plataforma IoT.

## Estructura

```
clients/
├── rasp-client/     # Cliente Python para Raspberry Pi
├── esp32-client/    # Cliente Arduino para ESP32
└── README.md        # Este archivo
```

## Quick Start

### 1. Crea un dispositivo en el panel

1. Inicia sesión como admin
2. Ve a Organizations → Tu Org → Projects → Tu Proyecto
3. Crea un dispositivo
4. Copia el **Serial** y el **MQTT Token**

### 2. Configura el cliente

Edita el código del cliente con:

- Serial del dispositivo
- MQTT Token
- IP del broker MQTT
- Prefijo del proyecto

### 3. Ejecuta el cliente

**Raspberry Pi:**

```bash
pip install -r requirements.txt
python client.py --serial SERIAL --token TOKEN --host BROKER_IP
```

**ESP32:**

```bash
# Arduino IDE o PlatformIO
pio run upload
```

## Flujo de comunicación

```
┌─────────────┐     MQTT      ┌─────────────┐
│  Dispositivo │ ◄──────────► │   Broker    │
│  (Client)    │   1883      │  (Mosquitto)│
└─────────────┘              └──────┬──────┘
                                    │
                                    │ HTTP
                                    ▼
                             ┌─────────────┐
                             │  Plataforma │
                             │   (NestJS)  │
                             └─────────────┘
```

### Telemetría (Dispositivo → Plataforma)

```json
// Topic: {prefix}/{serial}/telemetry
{
  "batteryLevel": 85.5,
  "fillLevel": 45.0,
  "isAccepting": true,
  "temperature": 22.5,
  "weight": 12.3,
  "signalStrength": -65,
  "extra": { ... }
}
```

### Comandos (Plataforma → Dispositivo)

```json
// Topic: {prefix}/{serial}/commands
{
  "command": "SET_ACCEPTING",
  "payload": { "value": false },
  "logId": "uuid"
}
```

## Autenticación MQTT

- **Username**: Serial del dispositivo
- **Password**: MQTT Token

## Topics disponibles

| Dirección | Topic                         | Descripción                 |
| --------- | ----------------------------- | --------------------------- |
| →         | `{prefix}/{serial}/telemetry` | Telemetría del dispositivo  |
| →         | `{prefix}/{serial}/status`    | Estado (online/offline)     |
| ←         | `{prefix}/{serial}/commands`  | Comandos desde el panel     |
| →         | `devices/provision/{serial}`  | Primer contacto (provision) |

## Próximos pasos

- Agregar sensores reales
- Implementar comandos específicos del hardware
- Configurar deep sleep para ahorro de energía
- Agregar OTA updates

---

## ¿Vale la pena crear una librería?

Analicemos los pros y contras de crear una librería reusable en Python/Arduino que abstraiga la lógica del cliente.

### Similitudes entre clientes

| Funcionalidad            | Python       | Arduino/ESP32        |
| ------------------------ | ------------ | -------------------- |
| Conexión MQTT            | ✅ paho-mqtt | ✅ PubSubClient      |
| Publicar telemetría      | ✅           | ✅                   |
| Suscribir a comandos     | ✅           | ✅                   |
| Enviar acknowledgment    | ✅           | ✅                   |
| Recibir ack del platform | ✅           | ✅                   |
| Provisioning             | ✅           | ✅                   |
| Reconnection logic       | ✅ manual    | ✅ automático        |
| Callback de comandos     | ✅ funciones | ✅ function pointers |

### Código duplicado actual (~80% similar conceptualmente)

1. **Lógica de conexión MQTT** - muy similar, solo cambia la librería
2. **Formateo de topics** - mismo patrón: `{prefix}/{serial}/{tipo}`
3. **Manejo de comandos** - mismo flujo: parsear → ejecutar → responder
4. **Telemetry** - mismo JSON structure
5. **Ack system** - mismo protocolo

### Opciones

#### Opción 1: No crear librería (estado actual)

**Pros:**

- Código específico por plataforma (óptimo para cada caso)
- No hay dependencias adicionales
- Fácil de modificar para casos específicos

**Contras:**

- Mantener dos codebases similares
- Cambios requieren aplicarse en ambos lugares
- Mayor curva de aprendizaje para nuevos desarrolladores

#### Opción 2: Crear librería Python + bindings Arduino

```
iot-client/
├── python/          # iot-client-py
│   ├── setup.py
│   └── src/iotclient/
├── arduino/         # iot-client-arduino
│   ├── src/
│   └── library.properties
└── docs/
```

**Pros:**

- DRY: un solo lugar para lógica de negocio
- APIs consistentes entre plataformas
- Testing centralizado
- Nueva plataforma = nuevo binding

**Contras:**

- Abstracción puede limitar funcionalidades específicas
- Mantener la librería itself
- Overhead de configuración para usuarios simples
- Arduino tiene limitaciones de memoria

### Recomendación

**Por ahora: NO crear librería.**

Razones:

1. **Simplicidad actual**: Ambos clientes son relativamente simples (~200-350 líneas)
2. **Diferencias de plataforma**: Python y C++ son muy diferentes; abstracción completa requeriría interfaces complejas
3. **Caso de uso controlado**: Solo hay 2 plataformas (RPi + ESP32), no un ecosistema abierto
4. **Maturity**: Primero validar que el sistema funciona bien en producción

**En el futuro**, si:

- Se agregan más plataformas (Python MicroPython, Raspberry Pico W, otros microcontroladores)
- Los clientes crecen en complejidad (manejo offline, caching, etc.)
- Se necesita compartir lógica de negocio (parsers, validations)

...entonces reconsiderar con una arquitectura más robusta.

### Cómo proceder ahora

Por ahora, mantener ambos clientes sincronizados manualmente:

- Si agregas un comando en Python, agregarlo en ESP32
- Si cambias el protocolo, actualizar ambos READMEs
- Documentar cambios importantes aquí
