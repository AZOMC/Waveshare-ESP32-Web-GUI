# Waveshare-ESP32-S3-Web-GUI

A full-stack embedded web application running on the Waveshare ESP32-S3-POE-ETH-8DI-8DO board. It provides a browser-based dashboard for real-time monitoring and control of 8 digital inputs, 8 digital outputs, Modbus RS485 devices, MQTT telemetry, SD card data logging, and Telegram bot alerting — all served directly from the ESP32 over Wi-Fi or Ethernet.


## Features

1. Web Dashboard: Live DI/DO status, machine state badge, health metrics, live clock.
2. Monitoring: Per-channel trigger counts, ON-time accumulation, custom efficiency formula.
3. MQTT: Local TCP/TLS or cloud WebSocket/WSS broker; configurable per-channel topics.
4. RS485 Modbus: RTU master; up to 16 configurable registers (coil, discrete, holding, input).
5. SD Card Logging: Periodic CSV logging with configurable interval and time-based log rotation.
6. Telegram Bot: Push alerts for machine state changes and reject milestones; remote control commands.
7. Authentication: Cookie-based session login with multi-user support (`users.json`).
8. Dual Network: Auto-detects Ethernet (PoE) first, falls back to Wi-Fi.
9. NTP Time Sync: Malaysia UTC+8 via `pool.ntp.org`.
10. Persistent Config: All settings stored in LittleFS JSON files; metrics survive reboots.


## Quick Start

### 1. Install Prerequisites
- Arduino IDE 2.x with the ESP32 board package (Espressif, v3.x)
- Required libraries — see [docs/03-software-setup.md]

### 2. Configure Wi-Fi
Open `ESP32_UI.ino` and edit:
```cpp
const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
```

### 3. Upload Firmware
Select Waveshare ESP32-S3-POE (or compatible ESP32-S3) in the Arduino IDE board menu and upload.

### 4. Upload LittleFS Data
Use the Arduino LittleFS Upload tool to upload the `data/` folder (HTML, CSS, JSON files).

### 5. Open the Dashboard
Navigate to `http://<ESP32-IP>/` in any browser. Default credentials:
- Username: `admin`  Password: `admin123`


## Project Structure

```
esp32-dashboard/
├── ESP32_test.ino          # Main Arduino firmware
├── data/                  # LittleFS filesystem (uploaded separately)
│   ├── index.html          # Dashboard page
│   ├── monitoring.html     # Monitoring & counters page
│   ├── mqtt.html           # MQTT configuration page
│   ├── rs485.html          # RS485 configuration page
│   ├── sdcard.html         # SD Card manager page
│   ├── telegram.html       # Telegram bot configuration page
│   ├── login.html          # Login page
│   ├── style.css           # Shared stylesheet
│   ├── shrdc_logo.png      # SHRDC logo (navbar left)
│   ├── msf_logo.png        # MSF logo (navbar right)
│   ├── config.json         # MQTT + device config (auto-created)
│   ├── users.json          # User accounts
│   ├── telegram.json       # Telegram bot credentials
│   └── variables.json      # Monitoring variable definitions
└── docs/
    ├── 01-overview.md
    ├── 02-hardware-setup.md
    ├── 03-software-setup.md
    ├── 04-system-architecture.md
    ├── 05-frontend.md
    ├── 06-backend-esp32.md
    ├── 07-communication.md
    ├── 08-customization.md
    └── 09-troubleshooting.md
```


## Documentation Index

01-overview.md: System purpose, use cases, key components
02-hardware-setup.md: Hardware requirements and wiring
03-software-setup.md: Arduino IDE setup, libraries, upload process
04-system-architecture.md: Overall design, ESP32 as a web server, data flow
05-frontend.md: All HTML/CSS/JSON files explained
06-backend-esp32.md: Firmware structure, all API endpoints
07-communication.md: Frontend ↔ ESP32 request/response flow
08-customization.md: How to extend the system
09-troubleshooting.md: Common issues and fixes


## Board

Waveshare ESP32-S3-POE-ETH-8DI-8DO
- ESP32-S3 dual-core 240 MHz
- 8 × opto-isolated digital inputs
- 8 × relay/digital outputs (via I2C expander at address 0x20)
- Ethernet (PoE capable) + Wi-Fi
- SD card slot (SD_MMC 1-bit)
- RS485 half-duplex port


## License

This project is developed by SHRDC (Selangor Human Resource Development Centre) for the Malaysian Smart Factory 4.0 Department. All Rights Reserved
