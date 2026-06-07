# poc-wifi · POC de provisioning WiFi (solo WiFi)

Firmware para validar el canal **app ↔ ESP32** por el AP del equipo. Sin MQTT, sin token,
sin mapeo de puertos — solo enviar el WiFi de casa y confirmar que conecta.

Diseño + pantalla Expo Go + pasos completos: **`../../docs/poc-expo-esp32-wifi.md`**.

## Flashear

PlatformIO:
```bash
cd clients/poc-wifi
pio run -t upload && pio device monitor
```
Arduino IDE: placa **ESP32 Dev Module**, instalar **ArduinoJson**, subir. Monitor a 115200.

## Cómo usarlo

1. Al arrancar queda en **reposo** (LED apagado). Botón **BOOT**:
   - **5 s** → entra a **MODO AP** (parpadeo rápido). Sirve para el primer pairing y para re-parear.
   - **10 s** → **factory reset**: olvida el WiFi guardado y reinicia de fábrica (parpadeo rápido ~1.5 s y reinicia).
2. LED:
   - **apagado** = en reposo, esperando el botón
   - parpadeo **rápido** = MODO AP, esperando credenciales
   - parpadeo **lento** = conectando al WiFi de casa
   - **fijo** = conectado
3. Une el teléfono/compu a la red **`ESP32-Setup`** (abierta) → `192.168.4.1`.

## Endpoints (en MODO AP)

| Método | Ruta | Qué hace |
|---|---|---|
| GET | `/info` | `{model, fw, mac, connected}` |
| POST | `/provision` | body `{"ssid":"...","password":"..."}` → guarda en NVS + intenta conectar |
| GET | `/status` | `{connected, ssid, ip}` — para el polling de la app |

## Probar sin app (navegador + curl)

```bash
# unido a ESP32-Setup:
curl http://192.168.4.1/info
curl -X POST http://192.168.4.1/provision -H 'Content-Type: application/json' \
  -d '{"ssid":"MiWiFi","password":"claveDelWifi"}'
curl http://192.168.4.1/status     # repetir hasta connected:true + ip del router
```

Éxito = `/status` devuelve `connected:true` con una **IP del rango de tu router** (no
`192.168.4.x`) y el LED queda fijo.

## Notas

- **Solo WiFi 2.4 GHz** (el ESP32 no ve 5 GHz).
- En Android, si el `fetch` falla con el WiFi sin internet, **apaga datos móviles** durante
  la prueba.
- Al asociarse a tu red, el AP puede cambiar de canal y el teléfono se cae 2-3 s del
  `ESP32-Setup`; reconecta solo (por eso el polling reintenta).
- Siguiente paso (firmware completo con NVS + MQTT + puertos): `../client-connect/`.
