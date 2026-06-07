# client-connect · provisioning WiFi por SoftAP

Firmware **mínimo** para configurar el WiFi de un ESP32 **desde la app AwareTrack**, sin
hardcodear credenciales. Diseño completo (incluyendo el lado app): `../../docs/wifi-provisioning-plan.md`.

A diferencia de `../esp32-client/`, aquí el equipo arranca "tonto": recibe WiFi + serial +
mqttToken + broker por aire durante el _pairing_ y los persiste en NVS. Un solo binario sirve
para cualquier device y entorno.

## Cómo funciona

```
Sin creds (de fábrica) o botón 3 s
        │
        ▼
  PAIR MODE  ── SoftAP "AwareTrack-XXXX" (WPA2) + HTTP @ 192.168.4.1  · LED parpadeo lento
        │            GET  /info       → identidad del equipo
        │            POST /provision  → {ssid,password,serial,token,mqttHost,mqttPort,prefix,tlsInsecure}
        │  guarda en NVS → reinicia
        ▼
  STA MODE   ── conecta WiFi de casa · LED parpadeo rápido
        │  conecta EMQX (TLS) y publica "online"
        ▼
  ONLINE     ── LED fijo · telemetría cada 30 s
```

## Protocolo HTTP (durante PAIR MODE)

`GET /info`
```json
{ "id":"A1B2C3D4", "mac":"24:6F:28:A1:B2:C3", "model":"AWARETRACK-001",
  "fw":"1.0.0", "provisioned":false }
```

`POST /provision`
```json
{ "ssid":"MiWiFi", "password":"clave", "serial":"ECO-2026-0001",
  "token":"a1b2c3d4...", "mqttHost":"167.172.141.63", "mqttPort":8883,
  "prefix":"ECO", "tlsInsecure":true }
```
→ `200 {"ok":true}` y el equipo se reinicia. Errores → `400 {"ok":false,"error":"..."}`.

`serial`, `token`, `mqttHost`/`mqttPort` (de `mqttUrl`) y `prefix` los entrega el backend al
crear el device (`POST /projects/:id/devices`). La app sólo añade el `ssid`/`password` del
WiFi de casa que teclea el usuario.

## LED y botón

| Estado | LED | |
|---|---|---|
| PAIR MODE | parpadeo lento (500 ms) | SoftAP arriba, esperando provisioning |
| Conectando | parpadeo rápido (150 ms) | uniéndose a WiFi/MQTT |
| ONLINE | fijo | conectado a EMQX |

Mantén el **botón BOOT (GPIO0) 3 s** para borrar credenciales y volver a PAIR MODE.

## Flashear

PlatformIO:
```bash
pio run -t upload && pio device monitor
```
Arduino IDE: placa "ESP32 Dev Module", instalar **PubSubClient** y **ArduinoJson**, subir.

## Probar el provisioning sin la app (curl)

1. Conéctate desde tu compu al WiFi `AwareTrack-XXXX` (clave `awaretrack`).
2. ```bash
   curl http://192.168.4.1/info
   curl -X POST http://192.168.4.1/provision -H 'Content-Type: application/json' \
     -d '{"ssid":"MiWiFi","password":"clave","serial":"ECO-2026-0001",
          "token":"<mqttToken>","mqttHost":"167.172.141.63","mqttPort":8883,
          "prefix":"ECO","tlsInsecure":true}'
   ```
3. El equipo se reinicia, conecta y aparece **ONLINE** en la plataforma.

## Notas / pendientes

- **Sólo WiFi 2.4 GHz** (limitación del ESP32).
- `tlsInsecure:true` salta la validación del cert (broker autofirmado del piloto). Producción:
  embeber la CA y usar `setCACert()`.
- PSK del SoftAP fijo (`awaretrack`) para el piloto. Producción: PSK por-equipo impreso en el QR.
- Esto cubre **sólo la conexión**. Para telemetría real + comandos, fusiónalo con
  `../esp32-client/esp32-client.ino` (mismos topics y credenciales).
