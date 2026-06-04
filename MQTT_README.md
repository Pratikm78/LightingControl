# LightingControl MQTT API Documentation

This document defines the MQTT interface for the LightingControl device. It provides details on the topic structure and the JSON payloads exchanged between the device and the broker.

## 1. Topic Structure

The device uses a configurable `gate_id` (default: `gate1`) to identify itself.

| Direction | Topic Pattern | Description |
| :--- | :--- | :--- |
| **Device Subscribes** | `access/control/{gate_id}/action` | Receives commands (Control/Config) |
| **Device Publishes** | `access/control/{gate_id}/status` | Reports status (Heartbeat, ACKs, NACKs) |
| **Device Publishes** | `access/control/{gate_id}/info` | Reports telemetry (Boot info, Usage data) |

---

## 2. Outgoing Messages (Device -> Broker)

### 2.1 Boot & Info
Sent immediately after a successful connection (`msgType: "boot"`) and then periodically (`msgType: "info"`) based on the configured `info_interval`.

**Topic:** `access/control/{gate_id}/info`
**Example Payload:**
```json
{
  "msgType": "boot",
  "seqID": 42,
  "timestamp": 1693305600000,
  "uptime_mins": 0,
  "version": "1.0.0", 
  "rssi": -60,
  "systemType": 3,
  "relayStates": [
    {
      "name": "court1",
      "state": false
    },
    {
      "name": "court2",
      "state": true
    },
    {
      "name": "patio",
      "state": false
    }
  ]
}
```

### 2.2 Heartbeat
Sent periodically to indicate the device is alive.

**Topic:** `access/control/{gate_id}/status`
**Example Payload:**
```json
{
  "msgType": "heartbeat",
  "seqID": 43,
  "timestamp": 1693305660000,
  "uptime_mins": 15
}
```

### 2.3 Acknowledge (ACK)
Sent in response to a successful `config` command. Sent in response to a successful config or control command.

**Topic:** `access/control/{gate_id}/status`
**Example Payload:**
```json
{
  "msgType": "ack",
  "seqID": 43,
  "timestamp": 1693305665,
  "actionID": 501
}
```
#### 2.4 Not Acknowledge (NACK)
Sent when an incoming message is malformed or contains an unknown msgType.

**Topic**: access/control/{gate_id}/status 
**Example Payload:**
```json
{
  "msgType": "nack",
  "seqID": 45,
  "timestamp": 1693305670000
}
```
---

## 3. Incoming Messages (Broker -> Device)

### 3.1 Configuration Update
Used to update device settings. These values are persisted to Flash.

**Topic:** `access/control/{gate_id}/action`
**Example Payload:**
```json
{
  "msgType": "config",
  "actionID": 502,
  "hb_interval": 30,
  "info_interval": 120,
  "gate_id": "gate_north"
}
```
*Note: If `gate_id` is updated, the device will automatically unsubscribe from the old action topic and subscribe to the new one.*

### 3.2 Control Command
Used to trigger real-time physical actions on the device (e.g., toggling a relay).

**Topic:** access/control/{gate_id}/action 
**Example Payload:**
```json
{
  "msgType": "control",
  "actionID": 501
}
```