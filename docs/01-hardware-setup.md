# 01 — Hardware Setup

## What You Need

| Item | Notes |
|---|---|
| **Waveshare ESP32-S3-POE-ETH-8DI-8DO** | Main board (Any similar specs board will also work) |
| **USB-C cable** | Must be a data cable — charge-only cables won't work for flashing |
| **Power supply** | 7–36 V DC via terminal block, or PoE from the Ethernet port |
| **MicroSD card** *(optional)* | FAT32 formatted, Class 10 recommended |
| **Ethernet cable** *(optional)* | Cat5e/Cat6; use a PoE switch or injector if powering via Ethernet |
| **Sensors / switches** *(optional)* | Dry-contact or NPN open-collector, connected to DI1–DI8 |
| **Relays / indicators** *(optional)* | Connected to DO1–DO8 |
| **RS485 slave device** *(optional)* | Any Modbus RTU device — PLC, VFD, energy meter |

---

## Pin Reference

### Digital Inputs (DI1–DI8) *(Can be changed)*

| Channel | GPIO | Default role |
|---|---|---|
| DI1 | 4 | Emergency (toggles machine state RUNNING/STOPPED) |
| DI2 | 5 | Start Sequence (forces RUNNING) |
| DI3 | 6 | Reset Sequence (forces STOPPED) |
| DI4 | 7 | Position Start (cycle timer start) |
| DI5 | 8 | Position Middle |
| DI6 | 9 | Position End (cycle timer stop) |
| DI7 | 10 | Metal Detect (reject counter) |
| DI8 | 11 | Spare |

All inputs are opto-isolated and active-LOW (LOW = input ON). Connect sensor COM to board GND and signal wire to the DI terminal.

> **Machine state logic:** DI1 going active toggles between RUNNING and STOPPED. DI2 going active forces RUNNING. DI3 going active forces STOPPED. This can be changed in firmware — see [Configuration](05-configuration.md).

### Digital Outputs (DO1–DO8)

Driven via an I2C GPIO expander (PCF8574 at address **0x20**).  
I2C: **SDA = GPIO 42**, **SCL = GPIO 41**

Outputs can be toggled manually from the dashboard, controlled via MQTT, or driven automatically by **DO Logic Rules** configured on the IoT Config page.

### RS485 / Modbus

| Signal | GPIO |
|---|---|
| TX (A / D+) | 17 |
| RX (B / D-) | 18 |
| DE/RE (direction control) | 21 |

Connect A(D+) on the board to A(D+) on the slave, and B(D-) to B(D-). For cable runs longer than ~10 m, add a 120 Ω termination resistor across A–B at the far end.

### SD Card

Uses SD_MMC in 1-bit mode on GPIO 45 / 47 / 48. The card must be **FAT32 formatted** and inserted before powering the board.

---

## Power Options

- **PoE** — plug into a PoE-capable switch or injector via Ethernet
- **DC terminal** — connect a 7–36 V DC supply to the power terminal block on the board

---

## Network

The board tries **Ethernet first**, then falls back to **Wi-Fi**. If Ethernet gets a link, it is used for everything including Telegram. Wi-Fi credentials are set in the firmware before flashing — see [Software Setup](02-software-setup.md).

---

## Next Step

→ [02 — Software Setup](02-software-setup.md)
