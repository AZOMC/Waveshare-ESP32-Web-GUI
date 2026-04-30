# 07 — Communication

This document covers how the browser frontend and the ESP32 backend exchange data, how MQTT connects the ESP32 to external brokers, and how the Telegram bot communicates with the outside world.

---

## 1. Browser ↔ ESP32 (HTTP REST)

### Protocol
All browser-to-ESP32 communication uses plain **HTTP/1.1 over port 80**. There is no WebSocket between the browser and ESP32 — all data exchange is request-response polling using the browser's native `fetch()` API.

### Authentication Flow

```
Browser                              ESP32
  │                                    │
  │ POST /login                        │
  │ body: username=admin&password=...  │
  │ ──────────────────────────────────►│
  │                                    │ checkCredentials() → true
  │                                    │ generateToken() → "a3f9..."
  │ 302 Redirect → /                   │
  │ Set-Cookie: session=a3f9...        │
  │◄───────────────────────────────────│
  │                                    │
  │ GET /                              │
  │ Cookie: session=a3f9...            │
  │ ──────────────────────────────────►│
  │                                    │ isAuthenticated() → true
  │ 200 OK — index.html                │
  │◄───────────────────────────────────│
```

Every subsequent request from the browser automatically includes the `Cookie: session=<token>` header (managed by the browser). The ESP32 validates it against `activeToken` in RAM before serving any protected resource.

If the cookie is missing or invalid, the ESP32 returns `302 → /login`.

---

### Polling Flow (Dashboard example)

Once the Dashboard page is loaded, three independent polling loops run:

```
Browser (index.html)                 ESP32
  │                                    │
  │◄── setInterval(fetchStatus, 500ms) │
  │                                    │
  │ GET /status                        │
  │ Cookie: session=...                │
  │ ──────────────────────────────────►│
  │                                    │ read diCurrentlyOn[], outputState,
  │                                    │ machineStateIdx, IP
  │ 200 OK                             │
  │ {"inputs":[0,0,0,1,0,0,0,0],       │
  │  "outputs":[0,1,0,0,0,0,0,0],      │
  │  "ip":"192.168.1.105",             │
  │  "network":"Ethernet",             │
  │  "machineState":"RUNNING"}         │
  │◄───────────────────────────────────│
  │                                    │
  │ JS updates DOM badges              │
```

This same pattern applies to all other polling endpoints. Each endpoint is completely stateless on the ESP32 side — it reads the current global state and returns it; no session state is stored per endpoint.

---

### Form Submit Flow (Save MQTT Config)

```
Browser (mqtt.html)                  ESP32
  │                                    │
  │ POST /save                         │
  │ Content-Type: application/x-www-form-urlencoded
  │ body: mqttMode=cloud&mqttProto=wss │
  │       &mqttHost=broker.hivemq.com  │
  │       &mqttPort=443&mqttPath=/mqtt │
  │       &in0=esp32/di1&...           │
  │ ──────────────────────────────────►│
  │                                    │ parse args → update mqttCfg
  │                                    │ saveConfig() → config.json
  │                                    │ applyMqttTransport() (reconnect)
  │ 200 OK "OK"                        │
  │◄───────────────────────────────────│
  │                                    │
  │ alert("Saved! ESP32 will reconnect.")
```

---

### Data Formats

All API responses use **JSON** (`application/json`). All form submissions use **`application/x-www-form-urlencoded`** (standard HTML form encoding). There is no multipart or JSON body in POST requests.

ESP32 response headers always include:
- `Cache-Control: no-cache` for live data endpoints (status, metrics, health)
- `Cache-Control: public, max-age=86400` for static assets (CSS, PNG)
- No `Content-Length` for streamed files (uses chunked transfer)

---

### Complete Request/Response Reference

| Method | Endpoint | Request body | Response body |
|---|---|---|---|
| GET | `/status` | — | JSON: inputs, outputs, ip, network, machineState |
| GET | `/getMetrics` | — | JSON: triggers, onTimes, diState, cycleTime, rejects, runtime, downtime |
| GET | `/getHealth` | — | JSON: temp, freeHeap, rssi, cpuFreq, uptime |
| GET | `/getConfig` | — | JSON: MQTT settings + topics |
| GET | `/getRS485` | — | JSON: baud, parity, stopBits, dataBits, addr |
| GET | `/getRS485Regs` | — | JSON array of register definitions + live values |
| GET | `/sdInfo` | — | JSON: SD status + log settings |
| GET | `/sdFiles` | — | JSON array: name, size, lastWrite |
| GET | `/sdRead?file=X` | — | Plain text (max 16 KB) |
| GET | `/sdDownload?file=X` | — | Binary file stream (attachment) |
| GET | `/getTelegramConfig` | — | JSON: Telegram settings (token masked) |
| GET | `/variables.json` | — | JSON: variable list for formula autocomplete |
| GET | `/toggle?ch=N` | — | Plain text: "1" or "0" |
| POST | `/login` | `username=&password=` | 302 redirect |
| POST | `/logout` | — | 302 redirect |
| POST | `/save` | MQTT + topic fields | "OK" |
| POST | `/saveRS` | RS485 serial settings | "OK" |
| POST | `/saveRS485Regs` | JSON array | "OK" |
| POST | `/writeRS485Reg` | `idx=&value=` | "OK" or error |
| POST | `/resetMetrics` | `target=` | "OK" |
| POST | `/sdSnapshot` | — | "Snapshot written" |
| POST | `/sdLogConfig` | `interval=&enabled=&rotateInterval=` | "OK" |
| POST | `/sdDelete?file=X` | — | "Deleted" |
| POST | `/sdClearLog` | — | "Log cleared" |
| POST | `/sdClearAllLogs` | — | "Deleted N file(s)" |
| POST | `/saveTelegramConfig` | Token + chatId + settings | "OK" |
| POST | `/telegramTest` | — | "Queued - check Telegram in ~3 seconds" |
| POST | `/setCustomEff` | `eff=<float>` | "OK" |

---

## 2. ESP32 ↔ MQTT Broker

### Topic Structure

**Published by ESP32** (when a DI changes state):
```
esp32/di1  →  "1" or "0"
esp32/di2  →  "1" or "0"
...
esp32/di8  →  "1" or "0"
```

**Subscribed by ESP32** (to receive DO control commands):
```
esp32/do1  ←  "1" (turn ON) or "0" (turn OFF)
esp32/do2  ←  ...
...
esp32/do8  ←  ...
```

**Subscribed by ESP32** (special topics):
```
esp32/machine/state  ←  "RUNNING" or "STOPPED"
esp32/sd/cmd         ←  JSON: {"cmd":"getInfo"} or {"cmd":"snapshot"}
```

> All topic strings are configurable from the MQTT Config page. The defaults shown above come from `config.json`.

### Publish Flow (DI state change)

```
DI4 sensor triggers (rising edge)
  → loop() detects logical=1, diCurrentlyOn[3] was 0
  → triggerCount[3]++
  → if MQTT connected:
      mqtt.publish("esp32/di4", "1")
  → MQTT broker receives message
  → Any subscriber (Node-RED, SCADA, etc.) receives "1" on "esp32/di4"
```

### Subscribe Flow (DO control via MQTT)

```
External system publishes "1" to "esp32/do2"
  → MQTT broker forwards to ESP32
  → mqttCallback("esp32/do2", "1", 1)
  → matches outputConfig[1].topic
  → setOutput(1, true)
  → I2C: write updated outputState to expander
  → DO2 relay activates
```

### MQTT Connection Modes

**Local TCP:**
```
ESP32 → WiFiClient → TCP:1883 → Mosquitto (LAN)
```

**Local TLS:**
```
ESP32 → WiFiClientSecure (insecure mode) → TCP:8883 → TLS broker (LAN)
```

**Cloud WebSocket (WS):**
```
ESP32 → WsClientBridge → WebSocketsClient → WS:80/mqtt → HiveMQ / cloud
```

**Cloud WebSocket Secure (WSS):**
```
ESP32 → WsClientBridge → WebSocketsClient (SSL) → WSS:443/mqtt → HiveMQ / cloud
```

Reconnect is attempted every **5 seconds** if the MQTT client is disconnected.

---

## 3. ESP32 ↔ Telegram

The Telegram integration uses HTTPS polling (long-poll style via `getUpdates`), not webhooks.

### Outgoing Messages (ESP32 → Telegram)

Outgoing messages are queued via a FreeRTOS queue (`tgOutQueue`) to decouple the main loop from the blocking HTTPS call:

```
Main loop / DI scan
  → rejectCount reaches threshold
  → tgSend("⚠️ Reject Alert! Total: 50")
  → xQueueSend(tgOutQueue, message, 0)      ← non-blocking, drops if queue full

telegramTask (Core 0, every 2.5 s)
  → xQueueReceive(tgOutQueue, outMsg, 0)    ← check for pending outgoing
  → if found: tgBot.sendMessage(chatId, outMsg, "Markdown")
  → if not:   tgBot.getUpdates(lastId + 1)  ← check for incoming commands
```

**Automatic alert triggers:**

| Event | Message |
|---|---|
| ESP32 boot | `🚀 ESP32 Online\nIP: 192.168.1.x` |
| Machine → RUNNING | `🟢 Machine: RUNNING` |
| Machine → STOPPED | `🔴 Machine: STOPPED` |
| Reject milestone | `⚠️ Reject Alert! Total: N` |

### Incoming Commands (Telegram → ESP32)

```
User sends "/status" in Telegram chat
  ↓
Telegram API queues it
  ↓
telegramTask calls getUpdates()
  ↓
handleTelegramMessage() checks chat_id
  ↓
buildStatusMsg() → constructs Markdown string
  ↓
tgBot.sendMessage(chatId, statusMsg, "Markdown")
  ↓
User sees formatted status message in Telegram
```

**Security:** The ESP32 checks `m.chat_id` against the configured `chatId`. Messages from other chat IDs receive an "Unauthorized" reply and are ignored.

---

## 4. MQTT SD Card Remote Commands

The ESP32 subscribes to `esp32/sd/cmd` for remote SD operations over MQTT:

```json
// Trigger SD info publish
{"cmd": "getInfo"}

// Trigger an immediate log row write
{"cmd": "snapshot"}
```

This allows a Node-RED flow or SCADA system to remotely trigger data capture without going through the web UI.
