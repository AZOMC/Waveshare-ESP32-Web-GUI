# 03 — Dashboard Pages

Once logged in, navigate between pages using the top navigation bar.

---

## `/` — Dashboard

The main overview page. Shows everything at a glance:

- **Live clock** — updates every second in the browser
- **Machine state** — RUNNING (green), STOPPED (red), or UNKNOWN (grey)
- **Network info** — current IP address and whether the board is on Ethernet or Wi-Fi
- **Process metrics** — Runtime, Downtime, Cycle Time, Reject Count, Efficiency
- **ESP32 health** — chip temperature, free RAM, Wi-Fi RSSI (or "Ethernet"), CPU speed, uptime
- **Digital Inputs (DI1–DI8)** — live ON/OFF badges, updated every 500 ms
- **Digital Outputs (DO1–DO8)** — live state badges with **Toggle** buttons
- **DO Logic Rules** — Automatically drive outputs based on input state conditions (AND/OR logic)

Clicking a **Toggle** button immediately flips that output. The badge updates on the next poll (within 500 ms).

**Efficiency** defaults to `Runtime ÷ (Runtime + Downtime) × 100%`. You can override this with a custom formula — save one in the custom efficiency section and it will be used here automatically.

### DO Logic Rules 

Automatically control outputs based on input states — no PLC required. Each rule defines conditions based on DI states and drives a DO output when those conditions are met.

**How it works:**
- Each rule targets one **Digital Output** (DO1–DO8)
- The rule has one or more **conditions** on DI states (ON or OFF)
- Conditions are chained with **AND** or **OR** logic
- When the overall condition evaluates to true, the output turns ON; when false, it turns OFF
- Rules are re-evaluated automatically every time any DI state changes

**Example rule:**
```
IF DI2 = ON  AND  DI3 = OFF  →  DO1 = ON
```
This turns on DO1 (e.g. a green indicator light) whenever the Start button (DI2) is pressed and the Reset button (DI3) is not active.

Up to **16 rules** can be defined, each with up to **8 conditions**.

> **Note:** Rules run automatically and will override manual toggles from the dashboard whenever a DI state changes. To stop a rule from controlling an output, disable it with the toggle or delete it.

Click **Add Rule**, configure the output, add conditions, then **Save Rules**. Rules take effect immediately.

---

## `/iot` — IoT Config

All connectivity and automation configuration lives on this single page, organised into three sections: **MQTT**, and **RS485 / Modbus**.

---

### MQTT

Connect the ESP32 to an MQTT broker to publish DI/DO state changes and RS485 register values, and receive remote DO control commands.

**Connection mode:**

| Mode | Use when |
|---|---|
| **Local TCP** | Connecting to a broker on your LAN (e.g. Mosquitto on port 1883) |
| **Local TLS** | Same as above but with TLS encryption (port 8883) |
| **Cloud WS** | Connecting to a cloud broker over WebSocket (e.g. HiveMQ) |
| **Cloud WSS** | Cloud broker over secure WebSocket — port 443, path `/mqtt` |

**DI/DO Topics:**

Each DI and DO channel has two fields:
- **Topic** — the MQTT topic string for that channel
- **Heartbeat (ms)** — if set to a value greater than 0, the ESP32 will publish the current state on that topic at this interval in milliseconds, even if nothing changed. Set to `0` for on-change-only publishing.

> Example: set DI1 topic to `factory/di1` and heartbeat to `5000` — the ESP32 will publish `"1"` or `"0"` to `factory/di1` every 5 seconds, and also immediately on any state change.

Changes take effect immediately after clicking **Save** — no reboot required.

---

### RS485 / Modbus

Configure the RS485 serial port and define up to **16 Modbus registers** to poll from connected slave devices (PLCs, VFDs, energy meters).

**Port settings:** baud rate, parity, stop bits, data bits, slave address, poll interval (ms), publish interval (ms)

- **Poll interval** — how often (in ms) the next register in the round-robin is polled. Minimum 50 ms.
- **Publish interval** — how often (in ms) a register's value is published to its MQTT topic, independent of the poll rate.

**Register table (per register):**

| Field | Description |
|---|---|
| Name | Label shown in the UI |
| Function Code | 1 = Coil, 2 = Discrete Input, 3 = Holding Register, 4 = Input Register |
| Address | Modbus register address (0-based) |
| Data Type | UINT16, INT16, FLOAT32 (2 registers), or Scaled |
| Scale | Multiplier applied when data type is Scaled |
| MQTT Topic | Topic to publish this register's value to (leave blank to skip MQTT for this register) |
| Writable | Enables a **Write** button to send a value to Holding/Coil registers |
| Enabled | Whether this register is actively polled |

Live values and OK/Error status are shown in the table, updated as polls come in.

Click **Add Register**, fill in the fields, then **Save Registers**. Polling begins immediately — no reboot needed. A green/red indicator shows whether the device is responding.

---

## `/sdcard` — SD Card Manager

- **Card status** — mounted/unmounted, card type, total/used/free space
- **Auto logging** — enable periodic CSV row writing; set the interval (5–3600 s) and file rotation period
- **Manual snapshot** — write one CSV row immediately
- **File browser** — list all files on the SD card with Download, Preview, and Delete actions

Log files are named `log_YYYYMMDD_HHmm.csv` (e.g. `log_20240115_0900.csv`).

Each row in the CSV contains:

```
millis, machine_state, runtime_ms, downtime_ms, cycle_ms, rejects, efficiency_pct,
trigger_count_di1, trigger_count_di2, ..., trigger_count_di8
```

> The last 8 columns are cumulative **trigger counts** (rising edge counts) for each DI channel, not the current ON/OFF state.

---

## `/telegram` — Telegram Bot

Configure the Telegram bot for push alerts and remote commands. See [04 — Telegram Setup](04-telegram-setup.md) for full setup instructions.

- Shows bot status: inactive / active / failed
- Bot token and Chat ID entry fields
- Reject alert threshold (send an alert every N rejects; set to 0 to disable)
- **Send Test Message** button to verify the bot is working

---

## Next Step

→ [04 — Telegram Setup](04-telegram-setup.md)
