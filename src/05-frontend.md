# 05 — Frontend

## Overview

The frontend is a plain HTML/CSS/JavaScript multi-page application (MPA). There is no JavaScript framework, no build pipeline, and no package manager. Every page is a self-contained `.html` file served directly from LittleFS. Pages share a single `style.css` stylesheet and a common navigation bar structure.

All dynamic data is fetched from the ESP32's REST API using the browser's native `fetch()` API inside `setInterval()` polling loops.

---

## File-by-File Reference

### `style.css`

Shared stylesheet for all pages. Key sections:

| CSS Class / Selector | Purpose |
|---|---|
| `.topnav` | Fixed top navigation bar with logos and page links |
| `.nav-link`, `.nav-link.active` | Navigation links; active page is highlighted |
| `.btn-logout` | Red logout button in the top-right corner |
| `.container` | Main page content wrapper with padding |
| `.metric-card`, `.mc-label`, `.mc-value` | Dashboard metric cards |
| `.mc-value.green/.amber/.red` | Colour-coded metric values |
| `.io-wrapper`, `.io-box`, `.io-item` | Digital IO layout (inputs/outputs side by side) |
| `.di-status.on/.off`, `.do-status.on/.off` | Coloured ON/OFF badge spans |
| `.led-circle`, `.led-circle.active` | LED indicator bubbles on Monitoring page |
| `.mon-table` | DI trigger counter table |
| `.sd-file-table` | SD card file browser table |
| `.tg-section`, `.tg-row` | Telegram config form layout |
| `.mqtt-conn-badge.connected/.disconnected` | MQTT status badge |
| `.login-box` | Centered login card |
| `.site-footer` | Footer bar at the bottom of each page |

Colour coding conventions used throughout:
- **Green** → healthy / on / running
- **Amber** → warning / marginal
- **Red** → error / off / stopped

---

### `login.html`

**Purpose:** Authentication gate. Displayed at `/login`.

**How it works:**
- Simple HTML form with `method="POST" action="/login"`.
- On submission, the browser POST's `username` and `password` to the ESP32.
- If credentials are correct, ESP32 sets a `session=<token>` cookie and redirects to `/`.
- If credentials are wrong, ESP32 redirects to `/login?error=1` and a small JavaScript block shows the error message div:
  ```javascript
  if (new URLSearchParams(window.location.search).get('error') === '1') {
    document.getElementById('errorMsg').style.display = 'block';
  }
  ```
- The error div is `display:none` by default so it doesn't flash on a clean load.

---

### `index.html` — Dashboard

**Purpose:** Main overview page served at `/`.

**Sections:**
1. **Live clock** — updated every second using `setInterval(updateClock, 1000)`. Runs entirely client-side; does not call the ESP32.
2. **IP / Network / Machine State row** — populated from `/status`. Machine state badge changes colour and label (RUNNING → green, STOPPED → red, UNKNOWN → grey).
3. **Process Metrics panel** — Runtime, Downtime, Cycle Time, Rejects, Efficiency. Updated every 1000 ms from `/getMetrics`.
4. **ESP32 Health panel** — Internal temperature, free heap, WiFi RSSI or "Ethernet", CPU frequency, uptime. Updated every 2000 ms from `/getHealth`.
5. **Digital Inputs / Outputs** — 8 × DI badges and 8 × DO badges with Toggle buttons. Updated every 500 ms from `/status`.

**Efficiency calculation (`computeDashEfficiency`):**
The dashboard checks `localStorage` for a saved custom formula (set on the Monitoring page). If found, it evaluates the formula client-side using the latest metrics. If not found, it falls back to the default formula:

```
Efficiency = Runtime / (Runtime + Downtime) × 100%
```

The formula evaluator (`evalCustomFormula`) substitutes named variables (`Runtime[s]`, `Downtime[s]`, etc.) in the formula string with their current numeric values, then evaluates the resulting arithmetic expression using `Function('"use strict";return(' + expr + ')')()`. It only permits digits, operators, and parentheses to prevent injection.

**Output toggling:**
Each DO Toggle button calls:
```javascript
function toggle(ch) {
  fetch('/toggle?ch=' + ch);
}
```
The ESP32 flips the output and returns `"1"` or `"0"`. The next status poll (within 500 ms) updates the displayed state.

---

### `monitoring.html` — Monitoring & Counters

**Purpose:** Detailed per-channel monitoring, served at `/monitoring`.

**Sections:**

1. **Live DI LED indicators** — Eight coloured LED bubbles built dynamically from `DI_LABELS` array. DI7 (Metal Detect) turns a distinct colour (`.metal`) when active.

2. **System Metrics summary** — Same runtime/downtime/cycle/rejects/efficiency summary as the dashboard but in a horizontal card row.

3. **DI Trigger Counter table** — Built dynamically from `DI_LABELS`. For each channel shows: live ON/OFF badge, total trigger count, total ON time (formatted as HH:MM:SS), and a per-channel Reset button.

4. **Real-Time Calculator** — Pick two metric values (Runtime, Downtime, Rejects, Cycle Time, or a custom number) and an operation (+, -, ×, /). The result is computed client-side from the latest polled metrics.

5. **Custom Efficiency Formula** — A text input where engineers can type an arithmetic formula using variable names from `variables.json`. Features:
   - **Autocomplete dropdown** — as you type, matching variable names from `variables.json` appear. Clicking one inserts it.
   - **Live preview** — the formula is evaluated with current metrics and the result is shown below the input.
   - **Save / Clear** — saved to `localStorage` under the key `customEffFormula`. Once saved, it overrides the default efficiency formula on both the Dashboard and Monitoring pages.
   - **SD logging sync** — every time metrics are fetched, the computed efficiency is POST'd to `/setCustomEff` so the ESP32 can include the correct value in SD log rows.

**Variable names available in formulae** (from `variables.json`):

| Variable key | Meaning |
|---|---|
| `Runtime[s]` | Total machine runtime in seconds |
| `Downtime[s]` | Total machine downtime in seconds |
| `Last Cycle[s]` | Most recent DI4→DI6 cycle time in seconds |
| `Rejects` | Total metal-detect trigger count |
| `Pos Start[s]` | Total ON time for DI4 in seconds |
| `Pos Middle[s]` | Total ON time for DI5 in seconds |
| `Pos End[s]` | Total ON time for DI6 in seconds |
| `Metal Detect[s]` | Total ON time for DI7 in seconds |
| `Emergency Count` | Trigger count for DI1 |
| `Start Seq Count` | Trigger count for DI2 |
| `Reset Seq Count` | Trigger count for DI3 |
| `Pos Start Count` | Trigger count for DI4 |
| `Pos Middle Count` | Trigger count for DI5 |
| `Pos End Count` | Trigger count for DI6 |

**Example formula:**
```
Runtime[s] / (Runtime[s] + Downtime[s]) * 100
```

---

### `mqtt.html` — MQTT Configuration

**Purpose:** Configure the MQTT broker connection and per-channel topics. Served at `/mqtt`.

**Sections:**

1. **Status banner** — Shows the active host, protocol/port, and a connected/disconnected badge. Updated every 5 seconds by `pollStatus()` which calls `/getConfig`. The banner is deliberately separated from the form so that polling never overwrites what the user is typing.

2. **Connection Mode selector** — Toggle between **Local** (TCP/TLS, direct broker IP) and **Cloud** (WebSocket/WSS, cloud broker host). Choosing a mode shows the relevant settings block.

   - Local: Protocol (TCP/TLS), Broker IP, Port
   - Cloud: Protocol (WS/WSS), Host/URL, Port, WebSocket Path

3. **DI/DO Topic inputs** — 8 input topic fields (what the ESP32 publishes when a DI changes state) and 8 output topic fields (what the ESP32 subscribes to for receiving DO control commands).

**Key design detail — `formLoaded` guard:**
The page loads config once on startup (`loadConfig()`) to populate the form fields. The status polling function (`pollStatus()`) only ever updates the connection badge — it never touches the form fields. This prevents the inputs from being reset while the user is editing them.

**Save flow:**
```javascript
// On form submit:
1. Read all mode/protocol/host/port/path fields
2. POST to /save with FormData
3. On success, call pollStatus() after 1.5 s to check reconnection result
```

---

### `rs485.html` — RS485 Configuration

**Purpose:** Configure serial port parameters and define the Modbus register table. Served at `/rs485`.

**Sections:**

1. **Port Settings** — Baud rate, parity, stop bits, data bits, slave address. Saved via POST to `/saveRS`. The ESP32 re-initialises the UART immediately on save.

2. **Register Table** — Up to 16 configurable Modbus registers. Each row defines:

   | Field | Options |
   |---|---|
   | Name | Free-text label for the register |
   | Function Code | 1=Coil, 2=Discrete Input, 3=Holding, 4=Input Register |
   | Address | Modbus register address (0-based) |
   | Data Type | UINT16, INT16, FLOAT32 (2 registers), Scaled |
   | Scale Factor | Multiplier applied when data type is Scaled |
   | Writable | Enables a "Write" button in the UI for Holding/Coil registers |
   | Enabled | Whether this register is actively polled |

3. **Live Values** — The page polls `/getRS485Regs` every 2 seconds to display the last read value and OK/Error status for each register.

---

### `sdcard.html` — SD Card Manager

**Purpose:** Monitor SD card status, configure data logging, and manage files. Served at `/sdcard`.

**Sections:**

1. **SD Card Status** — Polls `/sdInfo` every 10 seconds. Shows mounted status, card type (SDSC/SDHC/MMC), total/used/free space in MB.

2. **Data Logging Settings:**
   - **Auto Logging toggle** — Enable/disable periodic CSV row writing. POST to `/sdLogConfig`.
   - **Interval** — How often (in seconds, min 5 / max 3600) a CSV row is written.
   - **Rotation Period** — How often (in seconds) a new log file is created. Default 3600 s = 1 hour.
   - **Current Log File** — Shows the active filename (e.g. `log_20240115_0900.csv`).
   - **Manual Snapshot** — POST to `/sdSnapshot` to write one row immediately.

3. **File Browser** — Fetches the file list from `/sdFiles` and renders a table. Each row has Download, Preview, and Delete actions:
   - **Download** → navigates to `/sdDownload?file=<name>` which serves the file with `Content-Disposition: attachment`.
   - **Preview** → calls `/sdRead?file=<name>` (max 16 KB returned), opens result in a new tab as `<pre>` text.
   - **Delete** → POST to `/sdDelete?file=<name>`.
   - **Clear All Old Logs** → POST to `/sdClearAllLogs`, which deletes all files except the currently active log.

---

### `telegram.html` — Telegram Bot Configuration

**Purpose:** Configure and test the Telegram bot integration. Served at `/telegram`.

**Sections:**

1. **Status banner** — Shows bot state: no token / disabled / active / failed. Polled every 10 seconds from `/getTelegramConfig`.

2. **Credentials** — Bot Token (stored password-style; existing token shown masked as `XXXXXXXX••••••••`), Chat ID, and Enabled toggle. Leaving the token field blank preserves the existing token.

3. **Push Alert Settings** — Reject threshold (send an alert every N rejects; 0 to disable). The list of automatic alert types is displayed for reference (machine state changes, reject milestones, ESP32 boot).

4. **Bot Commands Reference** — A table listing all supported Telegram commands (`/status`, `/metrics`, `/health`, `/do on N`, `/do off N`, `/resetmetrics`, `/help`).

5. **Actions** — Save & Apply (POST to `/saveTelegramConfig`) and Send Test Message (POST to `/telegramTest`). The test endpoint writes an SD snapshot, saves all configs, and queues a test message before responding.

---

### `variables.json`

Used by the Monitoring page's autocomplete to know which variable names are valid in custom efficiency formulae. Loaded by the browser via `fetch('/variables.json')`.

```json
{
  "variables": [
    { "key": "Runtime[s]" },
    { "key": "Downtime[s]" },
    ...
  ]
}
```

To add a new variable name to the autocomplete, add a `{"key": "..."}` entry here and ensure the corresponding mapping exists in `getDashVarValues()` in `index.html` / `monitoring.html`.

---

## How to Modify the UI

### Change Colours
Edit the relevant CSS class in `style.css`. For example, to change the "running" badge colour:
```css
#machineStateBadge.running {
  background: #dcfce7;
  color: #16a34a;
  border-color: #bbf7d0;
}
```

### Add a New Page
1. Create `data/newpage.html` with the standard nav bar structure.
2. Add a nav link `<a href="/newpage" class="nav-link">New Page</a>` in **all** existing HTML files' `<nav>` sections.
3. Add a route in `ESP32_test.ino` inside `setup()`:
   ```cpp
   server.on("/newpage", HTTP_GET, []() {
     if (!requireAuth()) return;
     serveStatic("/newpage.html", "text/html", true);
   });
   ```
4. Re-upload LittleFS data and re-flash firmware.

### Change DI Channel Labels
In `monitoring.html`, edit the `DI_LABELS` array:
```javascript
const DI_LABELS = [
  ['DI1', 'Emergency'],      // change 'Emergency' to your label
  ['DI2', 'Start Seq.'],
  ...
];
```

### Adjust Poll Intervals
Find the relevant `setInterval()` call in the appropriate HTML file and change the millisecond value. For example, to poll status every 200 ms instead of 500 ms:
```javascript
setInterval(fetchStatus, 200);  // was 500
```

Be cautious — very fast polling on a busy network or during heavy RS485 activity can cause the ESP32 web server to miss requests.
