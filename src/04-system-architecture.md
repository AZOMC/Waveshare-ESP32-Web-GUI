# 04 — System Architecture

## Big Picture

The ESP32 is the single hardware node. It runs three concurrent execution contexts:

```
┌───────────────────────────────────────────────────────────┐
│                     ESP32-S3 (Dual Core)                  │
│                                                           │
│  Core 1 (Arduino loop())                                  │
│  ┌──────────────────────────────────────────────────────┐ │
│  │  WebServer.handleClient()  ← HTTP GET/POST requests  │ │
│  │  DI scan (every 5 ms)      ← debounce + metrics      │ │
│  │  MQTT loop / reconnect     ← PubSubClient            │ │
│  │  WebSocket loop            ← cloud MQTT transport    │ │
│  │  Runtime/Downtime accrual  ← every 100 ms            │ │
│  │  SD log write              ← periodic interval       │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                           │
│  Core 0 — FreeRTOS Tasks                                  │
│  ┌─────────────────────┐  ┌────────────────────────────┐  │
│  │  telegramTask       │  │  rs485Task                 │  │
│  │  Poll bot every 2.5s│  │  Round-robin Modbus polls  │  │
│  │  Send queued alerts │  │  every 500 ms/register     │  │
│  └─────────────────────┘  └────────────────────────────┘  │
│                                                           │
│  LittleFS (internal flash)    SD Card (SD_MMC)            │
│  ┌─────────────────────────┐  ┌──────────────────────┐    │
│  │ HTML / CSS / JS / JSON  │  │ CSV log files        │    │
│  │ config.json             │  │ log_YYYYMMDD_HHmm.csv│    │
│  │ users.json              │  └──────────────────────┘    │
│  │ telegram.json           │                              │
│  │ metrics.json            │                              │
│  │ rs485_regs.json         │                              │
│  └─────────────────────────┘                              │
└───────────────────────────────────────────────────────────┘
         │ HTTP/80          │ MQTT              │ RS485
         ▼                  ▼                   ▼
    Browser UI         MQTT Broker         Modbus slave
    (any device)   (local or cloud)      (PLC, VFD, meter)
                          │
                          ▼
                    Telegram API
                  (via HTTPS/443)
```

---

## ESP32 as a Web Server

The `WebServer` library runs on port 80 in the Arduino main loop. Routes are registered in `setup()` and dispatched inside `loop()` via `server.handleClient()`.

All routes that serve web pages or API data require authentication. The `requireAuth()` function checks the `Cookie: session=<token>` header against the single active token stored in RAM (`activeToken`). Only one session is active at a time.

Static files (HTML, CSS, PNG) are served from LittleFS using `server.streamFile()`. Dynamic API responses are built as JSON strings or plain text and sent with `server.send()`.

```
Browser request                       ESP32
─────────────────────────────────────────────────────────
GET /              ──────────────►  requireAuth()
                                   LittleFS.open("/index.html")
                   ◄──────────────  streamFile() → HTML

GET /status        ──────────────►  requireAuth()
                                   build JSON from live state
                   ◄──────────────  send(200, "application/json", buf)

POST /save         ──────────────►  requireAuth()
                                   parse form args
                                   saveConfig()
                                   applyMqttTransport()
                   ◄──────────────  send(200, "text/plain", "OK")
```

---

## Frontend ↔ Backend Interaction

The web UI is a traditional multi-page application (MPA). Each page is a separate HTML file served from LittleFS. There is **no JavaScript build step, no React, no bundler** — everything runs as plain HTML+JS in the browser.

Data flows as follows:

1. **Browser loads a page** → ESP32 streams the HTML file from LittleFS.
2. **JavaScript on the page** starts polling loops using `setInterval()` + `fetch()`.
3. **Each `fetch()` call** goes to a REST-like endpoint (e.g. `/status`, `/getMetrics`).
4. **ESP32 handles the request** synchronously in the web server handler, builds a JSON response, and returns it.
5. **Browser JS parses the JSON** and updates the DOM (text content, CSS classes for colour coding).

Poll intervals vary by page and data type:

| Data | Interval | Endpoint |
|---|---|---|
| DI/DO state | 500 ms | `/status` |
| Process metrics | 1000 ms | `/getMetrics` |
| ESP32 health | 2000 ms | `/getHealth` |
| SD card info | 10000 ms | `/sdInfo` |
| Telegram status | 10000 ms | `/getTelegramConfig` |
| MQTT status | 5000 ms | `/getConfig` |
| Monitoring counters | 800 ms | `/getMetrics` |

---

## Configuration Persistence

All user-configurable settings are stored as JSON files in LittleFS. They are loaded at boot with `load*()` functions and written on save with `save*()` functions.

| File | Contents | Loaded by |
|---|---|---|
| `config.json` | MQTT broker, topics, RS485 serial settings | `loadConfig()` |
| `users.json` | Username/password pairs (up to 10) | `loadUsers()` |
| `telegram.json` | Bot token, chat ID, enabled flag, reject threshold | `loadTelegramConfig()` |
| `metrics.json` | Counters, runtime/downtime, machine state, trigger counts | `loadMetrics()` |
| `rs485_regs.json` | Modbus register definitions | `loadRS485Regs()` |

Metrics are auto-saved every **5 minutes** in the main loop, and also on demand (e.g. after a `/resetMetrics` call or Telegram `/resetmetrics` command).

---

## FreeRTOS Tasks

Two tasks run on Core 0 to keep time-sensitive or blocking operations off the web server loop:

### `telegramTask`
- Polls the Telegram Bot API every 2.5 seconds.
- Drains an outgoing message queue (`tgOutQueue`) first — messages are enqueued via `tgSend()` from the main loop (machine state changes, reject alerts, boot notification).
- Calls `handleTelegramMessage()` for each incoming command.
- Uses `WiFiClientSecure` with `setInsecure()` (no certificate validation — acceptable for bot polling; production deployments should pin the certificate).

### `rs485Task`
- Waits 3 seconds at startup to let the network settle.
- Iterates through enabled Modbus registers in round-robin fashion, one register every 500 ms.
- Uses `rs485Mutex` (a FreeRTOS `SemaphoreHandle_t`) to serialise access to the RS485 bus with the web handler (`handleWriteRS485Reg`).

### Thread Safety
- All output changes go through `setOutput()`, which takes `ioMutex` before modifying `outputState` or writing to I2C.
- RS485 reads and writes share `rs485Mutex`.
- Telegram outgoing messages use a FreeRTOS queue — thread-safe by design.

---

## MQTT Transport Architecture

The firmware supports two MQTT connection modes, selected per configuration:

**Local mode** (TCP or TLS):
```
ESP32 → PubSubClient → WiFiClient (TCP) or WiFiClientSecure (TLS) → Broker
```

**Cloud mode** (WebSocket or WSS):
```
ESP32 → PubSubClient → WsClientBridge → WebSocketsClient (WS/WSS) → Broker
```

`WsClientBridge` is a custom class that implements the Arduino `Client` interface on top of `WebSocketsClient`. This lets `PubSubClient` (which expects a `Client&`) work transparently over a WebSocket connection. Incoming WebSocket binary frames are pushed into a circular buffer and read by `PubSubClient` as if they came from a TCP socket.

When the broker settings change (via `/save`), `applyMqttTransport()` disconnects the old transport, switches the client, and reconnects.
