# ESP32 Industrial Dashboard

A browser-based monitoring and control dashboard running entirely on a **Waveshare ESP32-S3-POE-ETH-8DI-8DO** board. No app, no cloud, no install — open the dashboard from any browser on your local network.

![Board](https://img.shields.io/badge/Board-Waveshare%20ESP32--S3--POE--ETH--8DI--8DO-blue)
![Platform](https://img.shields.io/badge/Platform-Arduino%20IDE%202.x-green)
![License](https://img.shields.io/badge/License-SHRDC%20%2F%20MSF-orange)

---

## What It Does

| Feature | Description |
|---|---|
| **Live Dashboard** | Monitor 8 digital inputs and control 8 digital outputs from any browser |
| **Machine Metrics** | Track runtime, downtime, cycle time, reject count, and efficiency |
| **DO Logic Rules** | Automatically drive outputs based on input state conditions (AND/OR logic) |
| **MQTT** | Publish DI/DO states and RS485 values to local or cloud broker; per-topic heartbeat intervals |
| **RS485 Modbus** | Poll up to 16 registers from PLCs, VFDs, or energy meters; publish each to its own MQTT topic |
| **SD Card Logging** | Auto-write timestamped CSV logs with configurable interval and file rotation |
| **Telegram Bot** | Receive push alerts and send control commands from your phone |
| **Multi-user Login** | Cookie-based authentication with configurable user accounts |
| **Dual Network** | Ethernet (PoE) with automatic Wi-Fi fallback |

---

## Quick Start

1. [Set up hardware](docs/01-hardware-setup.md) — connect power, Ethernet, and sensors
2. [Set up software](docs/02-software-setup.md) — install Arduino IDE, libraries, and the LittleFS plugin
3. Flash firmware + upload web files — full steps in the software setup guide
4. Open `http://<ESP32-IP>` in any browser
5. Log in with **admin** / **admin123**

---

## Pages

| URL | Page |
|---|---|
| `/` | Dashboard — live DI/DO states, machine metrics, ESP32 health |
| `/iot` | IoT Config — MQTT broker, RS485/Modbus registers, DO logic rules |
| `/sdcard` | SD Card — log settings, file browser |
| `/telegram` | Telegram Bot — credentials, alerts, command reference |

---

## Documentation

| Doc | What it covers |
|---|---|
| [01 — Hardware Setup](docs/01-hardware-setup.md) | Required hardware, wiring, pin assignments |
| [02 — Software Setup](docs/02-software-setup.md) | Arduino IDE, libraries, firmware upload, LittleFS upload |
| [03 — Dashboard](docs/03-dashboard.md) | Dashboard page and IoT Config page walkthrough |
| [04 — Telegram Bot](docs/04-telegram-setup.md) | Creating a bot, entering credentials, available commands |
| [05 — Configuration](docs/05-configuration.md) | Config files, changing credentials, common customisations |
| [06 — Troubleshooting](docs/06-troubleshooting.md) | Common issues and fixes |

---

## Project Structure

```
ESP32_test/
├── ESP32_test.ino          ← Arduino firmware (flash this first)
└── data/                   ← Web files (upload separately via LittleFS tool)
    ├── index.html          ← Dashboard
    ├── iot.html            ← IoT Config (MQTT, RS485, DO Logic Rules)
    ├── sdcard.html         ← SD Card manager
    ├── telegram.html       ← Telegram bot configuration
    ├── login.html          ← Login page
    ├── style.css           ← Shared stylesheet
    ├── config.json         ← MQTT, RS485, and device settings
    ├── users.json          ← Login accounts
    ├── telegram.json       ← Telegram bot credentials
    ├── do_rules.json       ← DO logic rule definitions
    ├── variables.json      ← Monitoring variable definitions
    ├── shrdc_logo.png
    └── msf_logo.png
```

---

## Default Login

| Username | Password |
|---|---|
| `admin` | `admin123` |
| `user1` | `pass1` |

Change these by editing `data/users.json` before uploading. See [Configuration](docs/05-configuration.md).

---

## Board

**Waveshare ESP32-S3-POE-ETH-8DI-8DO**
- ESP32-S3 dual-core 240 MHz
- 8 × opto-isolated digital inputs (GPIO 4–11), active-LOW
- 8 × relay/digital outputs via I2C expander (PCF8574 at 0x20), active-LOW
- Ethernet with PoE + Wi-Fi 2.4 GHz
- MicroSD card slot (SD_MMC 1-bit)
- RS485 half-duplex port

---

## Credits

Developed by **SHRDC** (Selangor Human Resource Development Centre) for the **Malaysian Smart Factory 4.0** Department.  
All Rights Reserved.
