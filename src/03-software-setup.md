# 03 — Software Setup

## Arduino IDE Setup

### 1. Install Arduino IDE
Download **Arduino IDE 2.x** from https://support.arduino.cc/hc/en-us/articles/360019833020-Download-and-install-Arduino-IDE

### 2. Add the ESP32 Board Package
In Arduino IDE, go to **File → Preferences** and add this URL to "Additional boards manager URLs":
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json

Then go to **Tools → Board → Boards Manager**, search for **esp32** by Espressif Systems, and install version **3.x** or later.

### 3. Select the Board
Go to **Tools → Board → ESP32 Arduino** and select:
```
Waveshare ESP32-S3-Zero
```
or if that exact entry is not available, select **ESP32S3 Dev Module** and configure:
- Flash Mode: **QIO 80MHz**
- Flash Size: **4MB** (or 8MB if your board has it)
- Partition Scheme: **Default 4MB with SPIFFS** → change to **Default 4MB (1.2MB APP / 1.5MB SPIFFS)**
- PSRAM: **OPI PSRAM** (if equipped)
- Upload Speed: **921600**

---

## Required Libraries

Install all of the following via **Tools → Manage Libraries** or by downloading from GitHub:

| Library | Purpose | Source |
|---|---|---|
| `PubSubClient` | MQTT client | Library Manager (Nick O'Leary) |
| `WebSocketsClient` (arduinoWebSockets) | WebSocket transport for cloud MQTT | Library Manager (Links2004) |
| `ModbusMaster` | Modbus RTU master for RS485 | Library Manager (4-20mA) |
| `ArduinoJson` | JSON parsing and generation | Library Manager (Bblanchon), v6.x |
| `UniversalTelegramBot` | Telegram Bot API | Library Manager (Brian Lough) |
| `WiFiClientSecure` | TLS client for Telegram/WSS | Bundled with ESP32 Arduino core |
| `LittleFS` | Internal flash filesystem | Bundled with ESP32 Arduino core |
| `SD_MMC` | SD card driver | Bundled with ESP32 Arduino core |
| `ETH` | Ethernet driver | Bundled with ESP32 Arduino core |
| `WebServer` | HTTP server | Bundled with ESP32 Arduino core |

> **ArduinoJson version:** Use v6.x (e.g. 6.21). Version 7 changes the API and will require code adjustments.

---

## LittleFS Upload Tool

The web interface files (HTML, CSS, JSON, images) are stored in the ESP32's internal flash using LittleFS. They must be uploaded separately from the firmware.

### Install the Plugin
Use the **arduino-littlefs-upload** plugin:
1. Download the latest `.vsix` release from [GitHub: earlephilhower/arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload)
2. In Arduino IDE, go to **Sketch → Add file...** or install via the Extension Manager
3. Alternatively, use the legacy **ESP32 LittleFS Data Upload** plugin for Arduino IDE 1.x

### Prepare the Data Folder
Create a folder named `data` inside your sketch folder:

```
ESP32_UI/
├── ESP32_UI.ino
└── data/
    ├── index.html
    ├── monitoring.html
    ├── mqtt.html
    ├── rs485.html
    ├── sdcard.html
    ├── telegram.html
    ├── login.html
    ├── style.css
    ├── shrdc_logo.png
    ├── msf_logo.png
    ├── config.json
    ├── users.json
    ├── telegram.json
    └── variables.json
```

### Upload LittleFS Data
1. Ensure the correct COM port is selected in Arduino IDE.
2. Close the Serial Monitor.
3. In Arduino IDE: go to **Tools → ESP32 LittleFS Data Upload** (or use the plugin command).
4. Wait for "LittleFS upload complete".

> **Important:** LittleFS upload and firmware upload are two separate operations. Both must be done for the system to work.

---

## Uploading the Firmware

1. Open `ESP32_UI.ino` in Arduino IDE.
2. Configure Wi-Fi credentials in the file:
   ```cpp
   const char* ssid     = "YOUR_SSID";
   const char* password = "YOUR_PASSWORD";
   ```
3. Click **Upload** (Ctrl+U).
4. Open **Serial Monitor** at **115200 baud** to confirm boot messages:
   ```
   Booting ESP32...
   LittleFS OK
   Network: Ethernet, IP: 192.168.x.xxx
   HTTP server started
   ```

---

## Initial Configuration Files

These JSON files live in LittleFS and are read at boot. If they don't exist, the firmware uses safe defaults.

### `users.json`
```json
[
  {"username": "admin",  "password": "admin123"},
  {"username": "user1",  "password": "pass1"}
]
```

### `config.json`
```json
{
  "deviceName": "esp32",
  "inputs":  ["esp32/di1", "esp32/di2", ..., "esp32/di8"],
  "outputs": ["esp32/do1", "esp32/do2", ..., "esp32/do8"],
  "rs485": {
    "baud": 9600, "parity": "None", "stopBits": 1, "dataBits": 8, "addr": "1"
  }
}
```

### `telegram.json`
```json
{
  "botToken": "",
  "chatId": "",
  "enabled": false
}
```

### `variables.json`
Defines available variable names for the Monitoring page's custom efficiency formula autocomplete:
```json
{
  "variables": [
    {"key": "Runtime[s]"},
    {"key": "Downtime[s]"},
    {"key": "Last Cycle[s]"},
    ...
  ]
}
```

---

## Verifying the Setup

After uploading firmware and LittleFS data:

1. Open Serial Monitor (115200 baud) and confirm startup messages.
2. Note the IP address printed (`Network: Ethernet, IP: x.x.x.x` or `Network: WiFi, IP: x.x.x.x`).
3. Open `http://<IP>` in a browser.
4. Log in with `admin` / `admin123`.
5. Verify the Dashboard loads and DI/DO states update.
