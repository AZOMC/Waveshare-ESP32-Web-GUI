# 06 — Troubleshooting

---

## Firmware Upload

**`No device found on port COMx`**
- Use a different USB cable — many cables are charge-only and carry no data
- Try a different USB port on your PC
- Check Windows Device Manager or macOS System Information for the correct COM port

**`Failed to connect to ESP32-S3`**
- Hold the **BOOT** button on the board while clicking Upload; release it once uploading starts
- Try a slower upload speed: **Tools → Upload Speed → 460800**

**Compilation error: `'LittleFS' was not declared`**
- Ensure the ESP32 Arduino core v2.0+ is installed
- In **Tools → Partition Scheme**, select a scheme that includes a LittleFS/SPIFFS partition

**Compilation error: `'StaticJsonDocument' is not a member of 'ArduinoJson'`**
- You have ArduinoJson v7 installed — this project requires **v6.x**
- Open Library Manager and downgrade to the latest 6.x release

---

## LittleFS Upload

**Upload LittleFS option not appearing in Arduino IDE**
- The `.jar` plugin must be placed in `tools/ESP32LittleFS/tool/` inside your Arduino sketchbook folder
- Restart Arduino IDE completely after placing the file
- The `data/` folder must be directly inside the sketch folder (same level as the `.ino` file)

**`LittleFS mount failed!` in Serial Monitor**
- The LittleFS data was never uploaded, or the partition scheme has no LittleFS partition
- Re-upload using the LittleFS tool and verify the partition scheme in the Tools menu

**Pages load but look broken or unstyled**
- `style.css` is missing or was not uploaded — re-upload the full `data/` folder
- Open the browser developer console (F12 → Network tab) and check for 404 errors

**Page shows "Not Found"**
- The HTML file for that page is missing from LittleFS
- Verify all required files are in the `data/` folder and re-upload

---

## Network

**ESP32 never connects to Wi-Fi**
- The ESP32-S3 supports **2.4 GHz only** — ensure your router is broadcasting on 2.4 GHz
- Double-check the SSID and password constants in the firmware
- Move the board closer to the router during initial setup
- Check Serial Monitor for timeout: `No network`

**Dashboard is unreachable after the board connects**
- Confirm your PC or phone is on the **same subnet** as the ESP32
- Check that no firewall is blocking port 80
- Try `ping <IP>` from your PC to verify basic connectivity

**IP address changes after every reboot**
- The board uses DHCP — configure a **static DHCP lease** on your router by binding the board's MAC address to a fixed IP (the MAC is printed in Serial Monitor at boot)

---

## Dashboard / Data

**All metrics show `--` or stop updating**
- Open the browser developer console (F12 → Network tab) and look for failed fetch requests
- A `401` error means the session cookie expired — log out and log back in
- A `500` error indicates a firmware issue — check Serial Monitor for details

**DI/DO states frozen**
- The ESP32 web server may be temporarily stalled by a slow RS485 slave or a Telegram HTTPS call
- Check if the RS485 device is responding — a non-responsive slave causes Modbus timeouts
- Reduce the number of enabled RS485 registers or increase the poll interval on the IoT Config page

**Metrics reset to zero after reboot**
- Metrics are saved to `metrics.json` every 5 minutes — up to 5 minutes of data can be lost after a power cut. This is expected behaviour.

---

## MQTT

**MQTT badge shows "Disconnected" permanently**
- For cloud brokers: use **Cloud WSS** mode, port **443**, path `/mqtt`
- For local Mosquitto: use **Local TCP** mode, port **1883**
- Check Serial Monitor for reconnect attempt messages

**DI state changes not arriving at the broker**
- Check that the DI topic fields are filled in on the IoT Config page
- Confirm the ESP32 shows as connected (green badge)
- Use an MQTT client (e.g. MQTT Explorer) and subscribe to `#` to see all arriving messages

**Publishing to a DO topic does not toggle the output**
- Topic names are case-sensitive — confirm the topic matches exactly
- Payload must be exactly `"1"` (on) or `"0"` (off) — no spaces or extra characters

**Cloud WSS disconnects frequently**
- The keep-alive is set to 15 seconds. If your broker's keep-alive is shorter, lower the firmware value:
  ```cpp
  mqtt.setKeepAlive(15);  // reduce if needed
  ```
- Some cloud brokers require a username/password. Add credentials to `mqtt.connect()` in `reconnectMQTT()` if needed.

---

## RS485 / Modbus

**All registers show "Error"**
- Verify slave address, baud rate, parity, stop bits, and data bits match the physical device exactly
- Check wiring: A(D+) to A(D+), B(D-) to B(D-)
- Test the slave device independently with a PC and a USB-to-RS485 adapter to confirm it responds

**Some registers read correctly, others fail**
- A slow slave may time out on some reads — increase the poll interval on the IoT Config page
- Verify the function code and register address are correct for the failing registers

**FLOAT32 value looks wrong**
- The firmware assumes **big-endian** byte order (high word first). If your device uses little-endian, swap `hi` and `lo` in the FLOAT32 handling code in `pollRS485Reg()`

**RS485 device shows as disconnected even though registers read OK**
- The connectivity check polls the first enabled register every 2 seconds. If that register has an error, the device is marked disconnected even if other registers are OK. Make sure the first enabled register in the list is a reliable one.

---

## DO Logic Rules

**A rule is not activating the output**
- Confirm the rule is toggled **Enabled** on the IoT Config page
- Check the DI channel indices are correct (DI1 = index 0 in the firmware, but labeled DI1 in the UI)
- The rule re-evaluates on every DI change. If no DI is changing, the rule will not fire.

**Output keeps switching on and off unexpectedly**
- A DO logic rule is likely overriding manual toggles. Rules re-evaluate on every DI state change. Disable or delete the rule for that output if you want manual control only.

---

## SD Card

**SD card shows "Not Mounted"**
- The card must be inserted **before** powering the board
- The card must be **FAT32** formatted — exFAT is not supported
- Try a different SD card; some are not compatible with 1-bit MMC mode

**Log files not being created**
- Verify SD is mounted and "Auto Logging" is enabled on the `/sdcard` page
- NTP sync is required for correct filenames. Ensure `pool.ntp.org` is reachable from the board's network. If NTP fails, files are named with a fallback format.

---

## Telegram

**Bot says "failed to initialise"**
- Double-check the bot token — copy it carefully from BotFather with no trailing spaces
- Verify the token has not been revoked (send `/token` to @BotFather to check)

**Bot is active but I receive no messages**
- Verify the Chat ID is correct — get it from @userinfobot
- The outgoing queue holds up to 6 messages. If alerts fire faster than the 2.5-second poll cycle, older messages are dropped.

**Commands get "Unauthorized" reply**
- The ESP32 only accepts messages from the configured Chat ID
- Confirm your Chat ID on the `/telegram` page

---

## General Tips

- **Always open Serial Monitor at 115200 baud** during setup — the board prints its IP address, MQTT connection status, Modbus errors, and SD mount result at boot
- **Use the browser developer console (F12)** to see failed fetch requests and exact HTTP error codes
- **Free heap** shown on the Dashboard health panel should stay above ~80 KB during normal operation — if it drops below 20 KB, restart the board
- **Restart to clear transient issues** — the board sends a Telegram message on every boot so you know when it comes back online
