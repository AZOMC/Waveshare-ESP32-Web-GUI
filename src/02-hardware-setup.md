# 02 — Hardware Setup

## Required Hardware

| Item | Details |
|---|---|
| **Waveshare ESP32-S3-POE-ETH-8DI-8DO** | Main controller board |
| **MicroSD card** | FAT32 formatted; Class 10 recommended |
| **Ethernet cable** | Cat5e/Cat6; PoE switch or injector if using PoE |
| **RS485 slave device** *(optional)* | Any Modbus RTU device (PLC, VFD, energy meter) |
| **5–48 V DC power supply** | Per board specification; or PoE from the Ethernet port |
| **Sensors / switches** | For digital inputs DI1–DI8 |
| **Relays / indicators** | Connected to digital outputs DO1–DO8 |

---

## Pin Assignments (from firmware)

### Digital Inputs
The firmware maps the 8 digital input channels to the following ESP32 GPIO pins:

```cpp
const int inputPins[8] = {4, 5, 6, 7, 8, 9, 10, 11};
```

All inputs are configured as `INPUT_PULLUP`. The board's opto-isolators invert the signal, so the firmware applies channel-specific logic inversion:

```cpp
// DI1 (index 0) is inverted relative to the rest:
int raw = (i == 0) ? !digitalRead(inputPins[i]) : digitalRead(inputPins[i]);
```

> **Note:** DI1 (GPIO 4) is treated as an Emergency input and has inverted logic. When DI1 goes LOW (circuit open / inactive), the machine state toggles between RUNNING and STOPPED.

Logical state after debounce:
- `logical = 1` → input is **active/ON** (sensor triggered)
- `logical = 0` → input is **inactive/OFF**

### Digital Outputs
Outputs are controlled via an I2C GPIO expander (PCF8574 compatible) at address **0x20**:

```cpp
#define EXIO_ADDR 0x20
Wire.begin(42, 41);  // SDA=42, SCL=41
```

The `outputState` byte is written to the expander after each change:
```cpp
Wire.beginTransmission(EXIO_ADDR);
Wire.write(outputState);
Wire.endTransmission();
```

Bit 0 of `outputState` = DO1, bit 7 = DO8.

### RS485 / Modbus
```cpp
#define RS485_RX  18   // UART RX
#define RS485_TX  17   // UART TX
#define RS485_DE  21   // Direction-Enable (DE/RE combined)
```

The DE pin is driven HIGH before transmission and LOW after, enabling half-duplex operation:
```cpp
void rs485PreTx()  { digitalWrite(RS485_DE, HIGH); }
void rs485PostTx() { digitalWrite(RS485_DE, LOW);  }
```

### SD Card
The SD card uses SD_MMC in 1-bit mode:
```cpp
#define SD_CLK  48
#define SD_CMD  47
#define SD_D0   45
#define NET_SCS 16    // Ethernet SCS — must be HIGH during SD use
```

`NET_SCS` (GPIO 16) is tied to the Ethernet chip select. It is driven HIGH before `SD_MMC.begin()` to avoid bus conflicts.

### I2C
```cpp
Wire.begin(42, 41);  // SDA=42, SCL=41
```

---

## Wiring Notes

### Digital Inputs
The board accepts dry-contact or NPN open-collector sensors on DI1–DI8. The opto-isolator handles voltage isolation. Connect sensor COM to the board GND terminal and NO/NC to the DI terminal. Wiring orientation depends on sensor type — refer to the Waveshare board schematic.

### RS485
For Modbus RTU, connect:
- **A (D+)** terminal on the board → A terminal on the slave device
- **B (D-)** terminal on the board → B terminal on the slave device
- **GND** → GND (if isolated ground is not used)

Terminate with a 120 Ω resistor across A–B at the far end of the cable for runs longer than ~10 m.

### Network
The board supports both Ethernet and Wi-Fi simultaneously. Ethernet takes priority. The firmware tries Ethernet first (`ETH.begin()`), then Wi-Fi (`WiFi.begin(ssid, password)`), with a 15-second timeout. If both fail, the server still starts but without network access.

### Power
The board can be powered via:
- The DC barrel connector / terminal block
- PoE from the Ethernet port (802.3af)

---

## Debounce

All digital inputs are debounced in firmware with a 10 ms window:

```cpp
#define DEBOUNCE_MS 10
```

This prevents false trigger counts from noisy contacts. The DI scan runs every 5 ms inside the main loop.
