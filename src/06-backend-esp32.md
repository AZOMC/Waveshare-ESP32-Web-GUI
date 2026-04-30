# 06 — Backend (ESP32 Firmware)

## Code Structure

The entire firmware is a single `.ino` file (`ESP32_test.ino`). It is organised into clearly commented sections:

```
ESP32_test.ino
├── Includes & class definitions      (WsClientBridge)
├── Global constants & variables      (pins, structs, state)
├── Config persistence                (loadConfig, saveConfig, loadUsers, etc.)
├── Telegram bot                      (loadTelegramConfig, tgSend, telegramTask)
├── I2C Output expander               (writeOutputs, setOutput)
├── Machine state                     (setMachineState, autoUpdateMachineState)
├── SD card                           (initLogFile, sdLogRow, publishSDInfo)
├── RS485 / Modbus                    (loadRS485Regs, saveRS485Regs, initRS485,
│                                      pollRS485Reg, rs485Task)
├── User management                   (loadUsers, checkCredentials)
├── Authentication                    (generateToken, isAuthenticated, requireAuth)
├── Static file serving               (serveStatic)
├── Login / Logout handlers           (handleLogin, handleLogout)
├── MQTT transport                    (applyMqttTransport, wsEventHandler,
│                                      mqttCallback, reconnectMQTT)
├── API handlers                      (handleToggle, handleStatus, handleGetMetrics, ...)
├── Network event handler             (WiFiEvent)
├── setup()
└── loop()
```

---

## Network Initialisation (`setup()`)

```cpp
WiFi.onEvent(WiFiEvent);   // register Ethernet event callback
ETH.begin();               // start Ethernet (PoE)
WiFi.begin(ssid, password);
WiFi.setSleep(false);

// Wait up to 15 seconds for either interface
while (!eth_connected && WiFi.status() != WL_CONNECTED) {
  if (millis() - cs > 15000) break;
  delay(200);
}
```

The `WiFiEvent` callback sets `eth_connected = true` when the `ARDUINO_EVENT_ETH_GOT_IP` event fires. If Ethernet gets an IP first, WiFi is still connected in the background but ETH is preferred for all address reporting and Telegram network checks.

**NTP:** After network is up, time is synchronised:
```cpp
configTime(8*3600, 0, "pool.ntp.org", "time.cloudflare.com");
```
Malaysia is UTC+8. NTP is used for SD card log filenames.

---

## Authentication

Authentication uses a simple single-token cookie system:

1. User submits the login form → ESP32 checks credentials against `users.json`.
2. If valid, a 32-character random hex token is generated and stored in `activeToken`.
3. The token is sent as a `Set-Cookie: session=<token>; Path=/` header.
4. Subsequent requests from the browser automatically include `Cookie: session=<token>`.
5. `isAuthenticated()` reads the Cookie header and compares against `activeToken`.
6. Only **one session** is active at a time. A new login invalidates the previous session.
7. Logout clears `activeToken` and sets `Max-Age=0` on the cookie.

```cpp
bool requireAuth() {
  if (!isAuthenticated()) {
    server.sendHeader("Location", "/login");
    server.send(302);
    return false;
  }
  return true;
}
```

Every protected handler starts with `if (!requireAuth()) return;`.

---

## Digital Input Processing (`loop()`)

The DI scan runs every **5 ms**:

```cpp
if (now - lastDiScanMs < 5) return;
lastDiScanMs = now;

for (int i = 0; i < 8; i++) {
  int raw = (i == 0) ? !digitalRead(inputPins[i]) : digitalRead(inputPins[i]);
  // debounce
  if (raw != lastRawState[i]) { lastRawState[i] = raw; debounceTimer[i] = now; }
  if ((now - debounceTimer[i]) >= DEBOUNCE_MS) {
    int logical = (raw == LOW) ? 1 : 0;
    if (logical != (int)diCurrentlyOn[i]) {
      // rising edge:
      //   increment triggerCount[i]
      //   if DI4: start cycle timer
      //   if DI6: record cycle time
      //   if DI7: increment rejectCount, maybe send Telegram alert
      // falling edge:
      //   accumulate totalOnTimeMs[i]
      // always:
      //   autoUpdateMachineState(i, logical)
      //   publish to MQTT if topic configured
    }
  }
}
```

**Machine state auto-update logic (`autoUpdateMachineState`):**
- DI1 goes LOW (off) → toggle between RUNNING and STOPPED
- DI2 goes HIGH (on) → force RUNNING
- DI3 goes HIGH (on) → force STOPPED

**Cycle time measurement:**
- DI4 rising edge → record `cycleStartMs`, set `cycleActive = true`
- DI6 rising edge (while `cycleActive`) → `lastCycleTimeMs = now - cycleStartMs`

---

## Metrics Accumulation (`loop()`)

Every **100 ms**, elapsed time is added to either `runtimeMs` or `downtimeMs` depending on the current machine state:

```cpp
if (now - lastMetricAccMs >= 100) {
  unsigned long elapsed = now - lastMetricTickMs;
  if      (machineStateIdx == 1) runtimeMs  += elapsed;
  else if (machineStateIdx == 2) downtimeMs += elapsed;
  lastMetricTickMs = now;
  lastMetricAccMs  = now;
}
```

Metrics are auto-saved to `metrics.json` every **5 minutes** (300,000 ms), so they survive a reboot.

---

## All API Endpoints

### Authentication

| Endpoint | Method | Auth Required | Description |
|---|---|---|---|
| `/login` | GET | No | Serve login page |
| `/login` | POST | No | Submit credentials; sets session cookie |
| `/logout` | GET | Yes (implicit) | Clear session, redirect to login |

---

### Dashboard & Status

#### `GET /status`
Returns current DI/DO state, IP address, network type, and machine state.

**Response:**
```json
{
  "inputs":  [0, 0, 0, 1, 0, 0, 0, 0],
  "outputs": [0, 1, 0, 0, 0, 0, 0, 0],
  "ip":      "192.168.1.105",
  "network": "Ethernet",
  "machineState": "RUNNING"
}
```
- `inputs[i]`: 1 if DI(i+1) is currently active, 0 otherwise
- `outputs[i]`: 1 if DO(i+1) is currently ON, 0 otherwise

---

#### `GET /getMetrics`
Returns all process metric counters.

**Response:**
```json
{
  "triggers":  [5, 3, 2, 10, 10, 9, 0, 0],
  "onTimes":   [120000, 30000, 5000, 95000, 88000, 82000, 0, 0],
  "diState":   [0, 0, 0, 1, 0, 0, 0, 0],
  "cycleTime": 8540,
  "rejects":   0,
  "runtime":   3600000,
  "downtime":  540000
}
```
- `triggers[i]`: total rising-edge count for DI(i+1)
- `onTimes[i]`: total accumulated ON time in milliseconds for DI(i+1) (includes current ON interval if active)
- `diState[i]`: current logical state (1=ON, 0=OFF)
- `cycleTime`: last measured DI4→DI6 cycle time in milliseconds
- `runtime`, `downtime`: in milliseconds

---

#### `GET /getHealth`
Returns ESP32 system health metrics.

**Response:**
```json
{
  "temp":    45.2,
  "freeHeap": 187432,
  "rssi":    0,
  "cpuFreq": 240,
  "uptime":  7234567
}
```
- `temp`: internal chip temperature in °C (from `temperatureRead()`)
- `freeHeap`: free heap RAM in bytes
- `rssi`: WiFi signal in dBm; **0 means Ethernet is active**
- `uptime`: milliseconds since boot (`millis()`)

---

#### `GET /toggle?ch=<n>`
Toggle digital output channel `n` (0–7). Requires auth.

**Response:** `"1"` (now ON) or `"0"` (now OFF), plain text.

---

#### `POST /resetMetrics`
Reset one or more metric groups.

**Body (form-encoded):** `target=<value>`

| `target` value | Effect |
|---|---|
| `all` | Reset all trigger counts, ON times, runtime, downtime, cycle time, rejects |
| `di0` … `di7` | Reset trigger count and ON time for that channel only |
| `rejects` | Reset reject count only |
| `runtime` | Reset runtime and downtime accumulators |
| `cycle` | Reset last cycle time |

**Response:** `"OK"` (200)

---

### MQTT Configuration

#### `GET /getConfig`
Returns current MQTT configuration and connection status.

**Response:**
```json
{
  "deviceName":    "esp32",
  "mqttMode":      "cloud",
  "mqttProto":     "wss",
  "mqttHost":      "broker.hivemq.com",
  "mqttPort":      443,
  "mqttPath":      "/mqtt",
  "mqttConnected": true,
  "inputs":  ["esp32/di1", ..., "esp32/di8"],
  "outputs": ["esp32/do1", ..., "esp32/do8"]
}
```

---

#### `POST /save`
Save MQTT broker settings and topic assignments. Triggers MQTT reconnection if broker details changed.

**Body (form-encoded):** `mqttMode`, `mqttProto`, `mqttHost`, `mqttPort`, `mqttPath`, `in0`…`in7`, `out0`…`out7`

**Response:** `"OK"` (200)

---

### RS485 / Modbus

#### `GET /getRS485`
Returns current RS485 serial port configuration.

**Response:**
```json
{
  "baud": 9600,
  "parity": "None",
  "stopBits": 1,
  "dataBits": 8,
  "addr": "1"
}
```

---

#### `POST /saveRS`
Save RS485 serial settings and immediately re-initialise the UART.

**Body (form-encoded):** `baud`, `parity`, `stopBits`, `dataBits`, `addr`

**Response:** `"OK"` (200)

---

#### `GET /getRS485Regs`
Returns the full Modbus register table with last-read values.

**Response (array):**
```json
[
  {
    "name": "Voltage",
    "fc": 4,
    "addr": 0,
    "dt": 3,
    "scale": 0.1,
    "wr": false,
    "en": true,
    "lastVal": 231.4,
    "lastOk": true
  }
]
```

---

#### `POST /saveRS485Regs`
Save the entire register table (replaces all existing entries).

**Body:** JSON array with the same structure as returned by `/getRS485Regs` (without `lastVal`/`lastOk`).

**Response:** `"OK"` (200)

---

#### `POST /writeRS485Reg`
Write a value to a writable Modbus register.

**Body (form-encoded):** `idx=<register_index>&value=<numeric_value>`

- For FC1 (coil): writes 1 or 0 using `writeSingleCoil`.
- For FC3 FLOAT32: writes two holding registers.
- For all others: writes a single holding register with `writeSingleRegister`.

**Response:** `"OK"` (200) or `"Modbus error 0xXX"` (500) or `"RS485 busy"` (503)

---

### SD Card

#### `GET /sdInfo`
Returns SD card status and logging configuration.

**Response:**
```json
{
  "mounted": true,
  "cardType": "SDHC",
  "totalMB": 7630,
  "usedMB": 12,
  "freeMB": 7618,
  "logEnabled": true,
  "logIntervalSec": 60,
  "logRotateIntervalSec": 3600,
  "currentLogFile": "log_20240115_0900.csv"
}
```

---

#### `GET /sdFiles`
Returns a JSON array of all files on the SD card root.

**Response:**
```json
[
  {"name": "log_20240115_0900.csv", "size": 4096, "lastWrite": "2024-01-15 09:30"}
]
```

---

#### `GET /sdRead?file=<name>`
Returns the first 16 KB of a file as plain text.

---

#### `GET /sdDownload?file=<name>`
Streams the full file with `Content-Disposition: attachment` for browser download.

---

#### `POST /sdDelete?file=<name>`
Deletes the specified file. Returns `"Deleted"` (200) or `"Delete failed"` (500).

---

#### `POST /sdSnapshot`
Writes one CSV row to the current log file immediately. Returns `"Snapshot written"` (200).

---

#### `GET|POST /sdLogConfig`
- **GET** → returns `{"interval":60,"enabled":true,"rotateInterval":3600}`
- **POST** → body: `interval=<n>&enabled=<0|1>&rotateInterval=<n>`; validates ranges and saves.

---

#### `POST /sdClearLog`
Clears the current log file and starts a fresh one.

---

#### `POST /sdClearAllLogs`
Deletes all files on the SD card **except** the currently active log file.
**Response:** `"Deleted N file(s)"` (200)

---

### Telegram

#### `GET /getTelegramConfig`
Returns bot configuration. The token is masked for security.

**Response:**
```json
{
  "hasToken": true,
  "tokenMasked": "8636692X••••••••",
  "chatId": "5078734373",
  "enabled": true,
  "rejectThreshold": 10,
  "botReady": true
}
```

---

#### `POST /saveTelegramConfig`
Save Telegram credentials and re-initialise the bot.

**Body (form-encoded):** `botToken` (blank = keep current), `chatId`, `enabled`, `rejectThreshold`

**Response:** `"OK"` (200)

---

#### `POST /telegramTest`
Saves all configs, writes an SD snapshot, and queues a test Telegram message.

**Response:** `"Queued - check Telegram in ~3 seconds"` (200)

---

### Monitoring (Custom Efficiency)

#### `POST /setCustomEff`
Updates the server-side custom efficiency value used in SD log rows.

**Body (form-encoded):** `eff=<float>` (send `-1` to disable)

This endpoint is called automatically by the Monitoring page every poll cycle. It is not intended for direct human use.

---

### Static Assets

| Route | File served |
|---|---|
| `/style.css` | `style.css` from LittleFS (cached 24 h) |
| `/shrdc_logo.png` | `shrdc_logo.png` from LittleFS (cached 24 h) |
| `/msf_logo.png` | `msf_logo.png` from LittleFS (cached 24 h) |
| `/variables.json` | `variables.json` from LittleFS |

---

## SD Card Log Format

Each CSV row written by `sdLogRow()` contains:

```
millis, state, runtime_ms, downtime_ms, cycle_ms, rejects, eff_pct,
di1, di2, di3, di4, di5, di6, di7, di8
```

- `state`: machine state string (RUNNING / STOPPED / UNKNOWN)
- `eff_pct`: custom efficiency if set via `/setCustomEff`, otherwise computed as `runtime / (runtime + downtime) × 100`
- `di1`…`di8`: current logical state (0 or 1)

Log filenames follow the pattern: `log_YYYYMMDD_HHmm.csv` (e.g. `log_20240115_0900.csv`), generated from NTP time at the moment the file is created.
