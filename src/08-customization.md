# 08 — Customisation Guide

## Change Wi-Fi Credentials

Open `ESP32_test.ino` and find the network constants near the top:

```cpp
const char* ssid     = "staffshrdc";
const char* password = "w0rkCo0p2017@$hrDc";
```

Replace with your network credentials, then re-flash the firmware. If an Ethernet cable is connected, the board will use Ethernet regardless and Wi-Fi serves as a fallback.

---

## Change User Accounts

Edit `data/users.json` before uploading to LittleFS. Up to 10 accounts are supported:

```json
[
  {"username": "admin",    "password": "admin123"},
  {"username": "engineer", "password": "securepass"},
  {"username": "operator", "password": "readonly1"}
]
```

> **Security note:** Passwords are stored and compared in plain text. This is acceptable for a closed LAN deployment. For a public-facing system, implement password hashing before storing credentials.

After editing, re-upload the LittleFS data partition.

---

## Change DI Channel Descriptions

DI labels are defined in two places. For **Monitoring page** labels:

In `data/monitoring.html`, edit the `DI_LABELS` array:
```javascript
const DI_LABELS = [
  ['DI1', 'Emergency'],      // ← Change second element
  ['DI2', 'Start Seq.'],
  ['DI3', 'Reset Seq.'],
  ['DI4', 'Pos Start'],
  ['DI5', 'Pos Middle'],
  ['DI6', 'Pos End'],
  ['DI7', 'Metal Detect'],
  ['DI8', '--']
];
```

For **Dashboard** DI labels, these are not currently shown (only DI1–DI8 identifiers appear in `index.html`). If you want descriptions there too, add a similar array to `index.html` and update the IO list rendering.

After editing, re-upload LittleFS data. No firmware change is required.

---

## Change Machine State Logic

The machine state is currently driven by DI1, DI2, and DI3 via `autoUpdateMachineState()`:

```cpp
void autoUpdateMachineState(int ch, int state) {
  if (ch == 0 && state == 0) { setMachineState(machineStateIdx == 1 ? 2 : 1); return; }  // DI1 off = toggle
  if (ch == 1 && state == 1) { setMachineState(1); return; }  // DI2 on = RUNNING
  if (ch == 2 && state == 1) { setMachineState(2); return; }  // DI3 on = STOPPED
}
```

To change this logic — for example, to use DI5 as the running signal:

```cpp
void autoUpdateMachineState(int ch, int state) {
  if (ch == 4 && state == 1) { setMachineState(1); return; }  // DI5 on = RUNNING
  if (ch == 4 && state == 0) { setMachineState(2); return; }  // DI5 off = STOPPED
}
```

`ch` is zero-indexed (DI1 = ch 0, DI5 = ch 4). Re-flash the firmware after changes.

---

## Change Cycle Time Measurement

Currently, cycle time is measured from the DI4 rising edge to the DI6 rising edge:

```cpp
if (i == 3) { cycleStartMs = now; cycleActive = true; }            // DI4 triggers
if (i == 5 && cycleActive) { lastCycleTimeMs = now - cycleStartMs; // DI6 triggers
                              cycleActive = false; }
```

To measure a different pair (e.g. DI2 → DI3):
```cpp
if (i == 1) { cycleStartMs = now; cycleActive = true; }   // DI2
if (i == 2 && cycleActive) { lastCycleTimeMs = now - cycleStartMs; cycleActive = false; } // DI3
```

Re-flash after changes.

---

## Change Reject Counter Trigger

The reject counter increments when DI7 (index 6) triggers:

```cpp
if (i == 6) {
  rejectCount++;
  // Telegram alert logic
}
```

To use a different channel (e.g. DI8, index 7):
```cpp
if (i == 7) {
  rejectCount++;
}
```

---

## Add a New Sensor / Metric

If you want to track a new calculated metric (e.g. OEE = Efficiency × Quality × Availability):

### Step 1: Define the formula in the UI

On the Monitoring page, use the Custom Efficiency Formula box:
```
Runtime[s] / (Runtime[s] + Downtime[s]) * 100
```

For more complex derived metrics that require new inputs, proceed to Step 2.

### Step 2: Add a new variable to `variables.json`

```json
{
  "variables": [
    { "key": "Runtime[s]" },
    { "key": "Downtime[s]" },
    { "key": "MyNewMetric" }
  ]
}
```

### Step 3: Map the variable in the formula evaluator

In both `index.html` and `monitoring.html`, find `getDashVarValues()`:

```javascript
function getDashVarValues(m) {
  return {
    'Runtime[s]':    m.runtime  / 1000,
    'Downtime[s]':   m.downtime / 1000,
    'MyNewMetric':   m.triggers[7]  // ← add your mapping here
  };
}
```

### Step 4: Ensure the ESP32 includes it in `/getMetrics`

If your new metric comes from existing fields (triggers, onTimes, runtime, downtime, cycleTime, rejects), no backend change is needed — they are all already included in the `/getMetrics` response.

If you need a truly new counter, add it to the firmware:

```cpp
// Add a global variable
unsigned long myNewCount = 0;

// Increment in loop() on the appropriate DI channel
if (i == 7 && logical == 1) myNewCount++;

// Include in handleGetMetrics()
doc["myNew"] = myNewCount;
```

Then update `getDashVarValues()` to use `m.myNew`.

---

## Add a New API Endpoint

### Step 1: Write the handler function

```cpp
void handleMyEndpoint() {
  if (!requireAuth()) return;
  // Read query args
  String param = server.arg("param");
  // Build response
  StaticJsonDocument<64> doc;
  doc["result"] = param.toInt() * 2;
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}
```

### Step 2: Register the route in `setup()`

```cpp
server.on("/myEndpoint", HTTP_GET, handleMyEndpoint);
// or for POST:
server.on("/myEndpoint", HTTP_POST, handleMyEndpoint);
```

### Step 3: Call it from the frontend

```javascript
const data = await (await fetch('/myEndpoint?param=5')).json();
console.log(data.result); // 10
```

Re-flash firmware after adding new endpoints. No LittleFS re-upload needed unless you also changed HTML files.

---

## Add a New Modbus Register

The easiest way is through the **RS485 Config page** in the web UI:

1. Navigate to `/rs485`.
2. In the Register Table section, click "Add Register".
3. Fill in: Name, Function Code, Address, Data Type, Scale (if needed), Writable flag, Enabled checkbox.
4. Click "Save Registers".

The settings are saved to `rs485_regs.json` in LittleFS and take effect immediately. The `rs485Task` starts polling the new register within its next cycle.

**Programmatically** (pre-loading registers in firmware), edit `loadRS485Regs()`:

```cpp
void loadRS485Regs() {
  // ...existing file-load code...

  // If no file exists, load default registers:
  if (rs485RegCount == 0) {
    // Add a default register
    RS485Register& r = rs485Regs[rs485RegCount];
    strlcpy(r.name, "Voltage", sizeof(r.name));
    r.fc    = 4;      // Input register
    r.addr  = 0;
    r.dt    = 3;      // Scaled
    r.scale = 0.1f;
    r.wr    = false;
    r.en    = true;
    rs485RegCount++;
  }
}
```

---

## Change MQTT Topics

Navigate to the MQTT Config page in the web UI (`/mqtt`), update the DI/DO topic fields, and click Save. Topics take effect immediately without rebooting.

To change the built-in special topics (machine state, SD commands), edit the constants in the firmware:

```cpp
#define MACHINE_STATE_TOPIC "esp32/machine/state"
#define SD_CMD_TOPIC        "esp32/sd/cmd"
```

Re-flash after changes.

---

## Change SD Log Interval and Rotation

These can be changed live from the SD Card page in the web UI without any code changes. If you want different compile-time defaults, change the initial values in the firmware:

```cpp
int           sdLogIntervalSec     = 60;    // rows written every 60 seconds
unsigned long logRotateIntervalSec = 3600;  // new file every 1 hour
```

---

## Change the SD Log CSV Format

The CSV header and row are written by two functions. To add or remove columns:

**Header** (in `ensureLogHeader()`):
```cpp
f.println(F("millis,state,runtime_ms,downtime_ms,cycle_ms,rejects,eff_pct,"
            "di1,di2,di3,di4,di5,di6,di7,di8"));
```

**Row** (in `sdLogRow()`):
```cpp
// Add a new column for 'myNewCount':
f.print(","); f.print(myNewCount);
```

Make sure the number of columns in the header and each data row match. Re-flash firmware after changes.

---

## Change the Telegram Reject Alert Threshold

This can be changed live from the Telegram Config page in the web UI — no code change needed. Enter a number in the "Reject Alert Every N rejects" field and click Save.

To set a default in the firmware:
```cpp
tgConfig.rejectThreshold = 10;  // change default here
```

---

## Add a New Telegram Bot Command

In the firmware, find `handleTelegramMessage()` and add a new `else if` branch:

```cpp
else if (text == "/mycommand") {
  String reply = "Current DI1 count: " + String(triggerCount[0]);
  tgBot.sendMessage(m.chat_id, reply, "");
}
```

Also update the `/help` response to list the new command. Re-flash firmware after changes.

---

## Change the NTP Timezone

The current timezone is UTC+8 (Malaysia):

```cpp
configTime(8*3600, 0, "pool.ntp.org", "time.cloudflare.com");
```

Change `8*3600` to your UTC offset in seconds. For example, UTC+5:30 (India):
```cpp
configTime(5*3600 + 30*60, 0, "pool.ntp.org", "time.cloudflare.com");
```

For regions with daylight saving time, use the third argument:
```cpp
configTime(0, 3600, "pool.ntp.org");  // UTC with 1h DST offset
```

---

## Increase the Maximum Number of Modbus Registers

The current limit is 16:
```cpp
#define MAX_RS485_REGS 16
```

Increase this and the `StaticJsonDocument` size in `saveRS485Regs()` and `loadRS485Regs()` accordingly:

```cpp
#define MAX_RS485_REGS 32

// In loadRS485Regs():
StaticJsonDocument<8192> doc;  // was 4096

// In saveRS485Regs():
StaticJsonDocument<8192> doc;
```

Monitor free heap after the change — each register adds ~64 bytes of RAM usage.
