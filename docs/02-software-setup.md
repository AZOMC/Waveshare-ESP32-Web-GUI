# 02 — Software Setup

This guide covers everything from installing Arduino IDE to getting the dashboard live in your browser.

---

## Step 1 — Install Arduino IDE

Download **Arduino IDE 2.x** from [arduino.cc/en/software](https://www.arduino.cc/en/software) and run the installer with default settings.

---

## Step 2 — Add the ESP32 Board Package

1. Open Arduino IDE → **File → Preferences**
2. Paste the following URL into *Additional boards manager URLs*:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Click **OK**
4. Go to **Tools → Board → Boards Manager**, search for `esp32` by *Espressif Systems*, and click **Install**

---

## Step 3 — Select the Board and Settings

1. **Tools → Board → ESP32 Arduino** → select `Waveshare ESP32-S3-Zero`
2. **Tools → Partition Scheme** → select `Huge APP (4MB No OTA/1MB SPIFFS)` or any scheme with at least 1 MB for LittleFS
3. **Tools → Port** → select the COM port that appears when the board is plugged in via USB-C

---

## Step 4 — Install Required Libraries

Go to **Tools → Manage Libraries** and install each of these:

| Library | Author |
|---|---|
| `PubSubClient` | Nick O'Leary |
| `arduinoWebSockets` | Links2004 |
| `ModbusMaster` | 4-20mA |
| `ArduinoJson` | Benoit Blanchon — install **v6.x** (not v7) |
| `UniversalTelegramBot` | Brian Lough |

The remaining libraries (`LittleFS`, `SD_MMC`, `ETH`, `WebServer`, `WiFiClientSecure`) are bundled with the ESP32 Arduino core — no separate install needed.

---

## Step 5 — Install the LittleFS Upload Plugin

The web files (HTML, CSS, JSON) are stored in the ESP32's flash and uploaded separately from the firmware using a plugin.

1. Download the latest `.jar` file from [github.com/lorol/arduino-esp32littlefs-plugin/releases](https://github.com/lorol/arduino-esp32littlefs-plugin/releases)
2. Find your Arduino sketchbook folder:
   - Windows: `Documents\Arduino`
   - macOS / Linux: `~/Arduino`
3. Create this path and place the `.jar` file inside it:
   ```
   tools/ESP32LittleFS/tool/ESP32LittleFS-x.x.x.jar
   ```
4. **Restart Arduino IDE completely**
5. Verify: press `Ctrl+Shift+P`, search for `Upload LittleFS` — it should appear in the list

---

## Step 6 — Set Your Wi-Fi Credentials

Open `ESP32_test.ino` and update these two lines near the top:

```cpp
const char* ssid     = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
```

> If you are always using Ethernet, Wi-Fi is only used as a fallback — but you still need valid credentials in case Ethernet is unavailable.

---

## Step 7 — Upload the Firmware

1. Connect the board via USB-C
2. Confirm the correct board and COM port are selected under the **Tools** menu
3. Click **Upload** (the arrow button) or press `Ctrl+U`
4. Wait for *Done uploading* in the output panel

> If you see *Failed to connect*, hold the **BOOT** button on the board while clicking Upload, then release it once uploading starts.

---

## Step 8 — Upload the Web Files (LittleFS)

> **Close the Serial Monitor** before this step — it locks the COM port and the upload will fail.

1. Press `Ctrl+Shift+P` in Arduino IDE
2. Search for and run **Upload LittleFS**
3. Wait for *LittleFS Image Uploaded* in the output panel

The entire `data/` folder is now stored in the ESP32's internal flash.

> Firmware upload and LittleFS upload are **two separate operations**. Both must be done on first setup. After that, if you only change `.html` or `.css` files, you only need to repeat the LittleFS upload. If you only change firmware code, you only need to repeat the firmware upload.

---

## Step 9 — Find the IP Address

1. Open **Tools → Serial Monitor** and set the baud rate to **115200**
2. Press the **RESET** button on the board (or unplug and replug USB)
3. Look for a line like:
   ```
   ETH IP: 192.168.1.105
   ```
   or
   ```
   WiFi IP: 192.168.1.105
   ```
4. Note that IP address — you can also find it on your router's connected devices list

---

## Step 10 — Open the Dashboard

1. Make sure your PC or phone is on the **same network** as the ESP32
2. Open a browser and go to `http://<IP>` — for example `http://192.168.1.105`
3. You will be redirected to the login page automatically
4. Log in with: **admin** / **admin123**

The dashboard loads showing live DI/DO states, machine metrics, and ESP32 health.

---

## Next Steps

- [03 — Dashboard](03-dashboard.md) — what each page does
- [04 — Telegram Bot](04-telegram-setup.md) — set up push alerts and remote commands
- [05 — Configuration](05-configuration.md) — change credentials and customise the system
