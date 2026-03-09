#!/usr/bin/env python3
"""
Raspberry Pi IoT Client
Sends telemetry to the platform and receives commands via MQTT.
"""

import json
import time
import random
import signal
import sys
from typing import Any, Callable, Optional
import paho.mqtt.client as mqtt


class IoTClient:
    def __init__(
        self,
        serial: str,
        mqtt_token: str,
        mqtt_host: str = "localhost",
        mqtt_port: int = 1883,
        project_prefix: str = "ECO",
    ):
        self.serial = serial
        self.mqtt_token = mqtt_token
        self.mqtt_host = mqtt_host
        self.mqtt_port = mqtt_port
        self.project_prefix = project_prefix

        self.running = True
        self.client: Optional[mqtt.Client] = None

        self.telemetry_callbacks: list[Callable[[dict], None]] = []

    def start(self):
        self.client = mqtt.Client(client_id=self.serial)
        self.client.username_pw_set(self.serial, self.mqtt_token)
        self.client.on_connect = self._on_connect
        self.client.on_message = self._on_message
        self.client.on_disconnect = self._on_disconnect

        # LWT: si el cliente se desconecta inesperadamente, el broker envía offline
        lwt_topic = f"{self.project_prefix}/{self.serial}/status"
        self.client.will_set(lwt_topic, "offline", qos=1, retain=True)

        print(f"[IoT] Connecting to {self.mqtt_host}:{self.mqtt_port} as {self.serial}")
        
        try:
            self.client.connect(self.mqtt_host, self.mqtt_port, 60)
        except Exception as e:
            print(f"[IoT] Failed to connect: {e}")
            return

        self.client.loop_start()

        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)

        self._provision()
        
        while self.running:
            self._send_telemetry()
            time.sleep(30)

    def _signal_handler(self, signum, frame):
        print("\n[IoT] Shutting down...")
        self.running = False
        # Enviar offline antes de desconectar
        self._send_status("offline")
        if self.client:
            self.client.loop_stop()
            self.client.disconnect()

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            print(f"[IoT] Connected successfully!")
            topic = f"{self.project_prefix}/{self.serial}/commands"
            client.subscribe(topic)
            print(f"[IoT] Subscribed to {topic}")
            # Subscribe to ack topic
            ack_topic = f"{self.project_prefix}/{self.serial}/commands/ack"
            client.subscribe(ack_topic)
            print(f"[IoT] Subscribed to {ack_topic}")
            # Send online status
            self._send_status("online")
        else:
            print(f"[IoT] Connection failed with code {rc}")

    def _on_message(self, client, userdata, msg):
        try:
            payload = json.loads(msg.payload.decode())
            print(f"[IoT] Received command: {payload}")
            self._handle_command(payload)
        except Exception as e:
            print(f"[IoT] Error parsing command: {e}")

    def _on_disconnect(self, client, userdata, rc):
        print(f"[IoT] Disconnected (rc={rc})")
        # Solo enviar offline si es desconexión intencional (rc=0)
        # Las desconexiones inesperadas las maneja LWT
        if rc == 0:
            self._send_status("offline")

    def _provision(self):
        print("[IoT] Sending provision request...")
        client = mqtt.Client()
        client.username_pw_set(self.serial, self.mqtt_token)
        try:
            client.connect(self.mqtt_host, self.mqtt_port, 60)
            client.publish(f"devices/provision/{self.serial}", "", qos=1)
            client.disconnect()
            print("[IoT] Provision request sent")
        except Exception as e:
            print(f"[IoT] Provision failed: {e}")

    def _send_telemetry(self):
        telemetry = self._generate_telemetry()
        
        topic = f"{self.project_prefix}/{self.serial}/telemetry"
        payload = json.dumps(telemetry)
        
        if self.client and self.client.is_connected():
            result = self.client.publish(topic, payload, qos=1)
            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                print(f"[IoT] Telemetry sent: {telemetry}")
            else:
                print(f"[IoT] Failed to send telemetry: {result.rc}")
        else:
            print("[IoT] Not connected, skipping telemetry")

    def _send_status(self, status: str):
        topic = f"{self.project_prefix}/{self.serial}/status"
        if self.client and self.client.is_connected():
            self.client.publish(topic, status, qos=1, retain=True)
            print(f"[IoT] Status sent: {status}")

    def _generate_telemetry(self) -> dict[str, Any]:
        return {
            "batteryLevel": random.uniform(60, 100),
            "fillLevel": random.uniform(0, 100),
            "isAccepting": random.choice([True, False]),
            "temperature": random.uniform(15, 35),
            "weight": random.uniform(0, 50),
            "signalStrength": random.randint(-90, -30),
            "extra": {
                "humidity": random.uniform(30, 80),
                "pressure": random.uniform(990, 1030),
            },
        }

    def _handle_command(self, payload: dict):
        command = payload.get("command")
        command_payload = payload.get("payload", {})
        
        print(f"[IoT] Processing command: {command} with payload: {command_payload}")
        
        if command == "REBOOT":
            print("[IoT] Rebooting...")
            time.sleep(1)
        elif command == "SET_ACCEPTING":
            accepting = command_payload.get("value", True)
            print(f"[IoT] Setting accepting: {accepting}")
        elif command == "OPEN_LID":
            print("[IoT] Opening lid...")
        elif command == "GET_STATUS":
            print("[IoT] Sending status response...")
            self._send_status_response()
        elif command == "CONSOLE_LOG":
            print(f"[IoT] CONSOLE_LOG received! Payload: {command_payload}")
        else:
            print(f"[IoT] Unknown command: {command}")
            return
        
        # Send acknowledgment
        log_id = payload.get("logId")
        if log_id:
            self._send_ack(log_id, "ACKNOWLEDGED")

    def _send_ack(self, log_id: str, status: str, error: Optional[str] = None):
        topic = f"{self.project_prefix}/{self.serial}/commands/ack"
        ack_payload = {
            "logId": log_id,
            "status": status,
        }
        if error:
            ack_payload["error"] = error
        
        if self.client and self.client.is_connected():
            self.client.publish(topic, json.dumps(ack_payload), qos=1)
            print(f"[IoT] Ack sent: {log_id} -> {status}")

    def _send_status_response(self):
        topic = f"{self.project_prefix}/{self.serial}/status"
        payload = "online"
        if self.client and self.client.is_connected():
            self.client.publish(topic, payload, qos=1)


def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="Raspberry Pi IoT Client")
    parser.add_argument("--serial", required=True, help="Device serial (e.g., ECO-2026-0001)")
    parser.add_argument("--token", required=True, help="MQTT token from platform")
    parser.add_argument("--host", default="localhost", help="MQTT broker host")
    parser.add_argument("--port", type=int, default=1883, help="MQTT broker port")
    parser.add_argument("--prefix", default="ECO", help="Project device prefix")
    
    args = parser.parse_args()
    
    client = IoTClient(
        serial=args.serial,
        mqtt_token=args.token,
        mqtt_host=args.host,
        mqtt_port=args.port,
        project_prefix=args.prefix,
    )
    client.start()


if __name__ == "__main__":
    main()
