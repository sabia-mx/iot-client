# Raspberry Pi Client

Cliente IoT en Python para Raspberry Pi (o cualquier dispositivo con Python 3).

## Requisitos

```bash
pip install -r requirements.txt
```

## Configuración

1. **Crea un dispositivo en el panel**:
   - Ve a Organizations → Tu Org → Projects → Tu Proyecto
   - Crea un dispositivo
   - Copia el `serial` y el `mqttToken`

2. **Configura el cliente**:

```bash
python client.py \
  --serial ECO-2026-0001 \
  --token tu-mqtt-token-aqui \
  --host 192.168.1.100 \
  --port 1883 \
  --prefix ECO
```

O edita el código directamente para hardcodear los valores.

## Uso

```bash
python client.py --serial ECO-2026-0001 --token TU_TOKEN --host TU_BROKER_IP
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
    "humidity": 55.0,
    "pressure": 1013.25
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
  "logId": "uuid-del-log"
}
```

### Provision (primer contacto)

- **Topic**: `devices/provision/{serial}`
- **Payload**: vacío

## Comandos disponibles

Desde el panel puedes enviar:

- `SET_ACCEPTING` - Activar/desactivar aceptación
- `OPEN_LID` - Abrir tapa
- `REBOOT` - Reiniciar dispositivo
- `GET_STATUS` - Solicitar estado

## Credentiales MQTT

- **Username**: Serial del dispositivo (ej: `ECO-2026-0001`)
- **Password**: MQTT Token del dispositivo (generado al crear el dispositivo)



4d48db2c6fc6c861d25f5007e7f6f3f05a3ab2eaff1fd6092db491ff5c18f390

ECOB-2026-0002


python client.py \
  --serial ECOB-2026-0002 \
  --token 4d48db2c6fc6c861d25f5007e7f6f3f05a3ab2eaff1fd6092db491ff5c18f390 \
  --host 192.168.68.101 \
  --port 1883 \
  --prefix ECOB