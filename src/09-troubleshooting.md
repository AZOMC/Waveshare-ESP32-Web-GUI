# 09 — Troubleshooting

## Firmware Upload Issues

### `No device found on port COMx`
- Check the USB cable — some cables are charge-only and do not carry data.
- Try a different USB port on your PC.
- Press and hold the **BOOT** button on the ESP32 board, then click Upload in Arduino IDE. Release BOOT once uploading starts.
- Check Windows Device Manager / macOS System Information for the correct COM port.

### `A fatal error occurred: Failed to connect to ESP32-S3`
- Same as above. Also try reducing the upload speed: **Tools → Upload Speed → 460800**.
- If the board has a "USB CDC On Boot" option, set it to **Enabled** if you are connecting via the native USB port.

### Compilation error: `'LittleFS' was not declared`
- The LittleFS header requires the ESP32 Arduino core v2.0+ with LittleFS support.
- In `Arduino IDE → Tools → Partition Scheme`, ensure a scheme with SPIFFS/LittleFS is selected (not "No OTA (2MB APP)").

### Compilation error: `'StaticJsonDocument' is not a member of 'ArduinoJson'`
- You have ArduinoJson v7 installed. This code requires **v6.x**.
- Go to Library Manager and downgrade to the latest 6.x release.

---

## LittleFS Upload Issues

### LittleFS upload plugin not appearing in Tools menu
- Arduino IDE 2.x uses a different plugin mechanism. Install the `.vsix` plugin for Arduino IDE 2 from the [arduino-littlefs-upload releases page](https://github.com/earlephilhower/arduino-littlefs-upload/releases).
- Ensure the sketch folder is named the same as the `.ino` file (e.g. folder `ESP32_test/` containing `ESP32_test.ino`).
- The `data/` folder must be directly inside the sketch folder.

### `LittleFS mount failed!` in Serial Monitor
- The LittleFS data was not uploaded or the partition scheme does not allocate space for it.
- Re-upload LittleFS data using the upload tool.
- Ensure the partition scheme includes a SPIFFS/LittleFS partition (check `Tools → Partition Scheme`).

### Pages load but show no styling or broken layout
- The `style.css` file was not uploaded to LittleFS, or was uploaded but the upload was incomplete.
- Re-upload the LittleFS data partition.
- Open the browser developer console (F12) and check for 404 errors on `/style.css`.

### Page shows "Not Found"
- The HTML file for that page (e.g. `monitoring.html`) is missing from LittleFS.
- Verify all files are present in the `data/` folder, then re-upload.

---

## Network / WiFi Issues

### ESP32 never connects to WiFi
- Verify the SSID and password constants in the firmware are correct (they are hardcoded).
- The ESP32-S3 supports **2.4 GHz only** — ensure your access point is broadcasting on 2.4 GHz.
- Move the board closer to the router during setup to rule out weak signal.
- Check the Serial Monitor (115200 baud) for the dot progress: `..............` followed by `No network` if it times out.

### Ethernet works but WiFi does not (or vice versa)
- This is normal and expected — Ethernet takes priority.
- If Ethernet is connected and gets an IP, `eth_connected = true` and the board uses ETH for all communication including Telegram.
- If you want to force WiFi only, disconnect the Ethernet cable.

### Board connects to network but dashboard is unreachable
- Find the IP from Serial Monitor: `Network: WiFi, IP: 192.168.x.x`
- Ensure your browser device is on the same subnet.
- Check that no firewall is blocking port 80.
- Try `ping <IP>` from your PC to verify basic connectivity.

### IP address changes after reboot
- The ESP32 gets a DHCP-assigned IP. Configure a **static DHCP lease** on your router (bind the board's MAC address to a fixed IP). The MAC address is printed in the Serial Monitor at boot.

---

## Data Not Updating in the Browser

### All metrics show `--` or `?`
- The browser is successfully loading the HTML page but the API calls are failing.
- Open the browser developer console (F12 → Network tab) and look for failed `fetch()` calls.
- A `401` error means the session cookie expired or was lost — log out and log back in.
- A `500` error indicates a firmware crash or handler bug — check Serial Monitor.

### DI/DO states are frozen (not updating)
- The `/status` endpoint is polled every 500 ms. If it stops updating, the ESP32 web server may be blocked.
- Heavy RS485 polling or a blocking Telegram HTTPS call can temporarily stall the web server loop.
- Check if the RS485 slave is responding — a non-responsive slave causes Modbus timeouts that block the rs485Task, but the mutex should prevent them from blocking the web server.
- Reduce the number of enabled RS485 registers or increase the poll interval if needed.

### Metrics reset to zero after reboot unexpectedly
- Metrics are saved to `metrics.json` every 5 minutes. If the board loses power before a save cycle, up to 5 minutes of data is lost.
- To reduce this window, decrease the save interval in the main loop:
  ```cpp
  if (now - lastMetricsSaveMs >= 300000UL) {  // change 300000 to a smaller value
  ```
- Note: more frequent saves write to LittleFS more often, which increases flash wear.

---

## MQTT Issues

### MQTT badge shows "Disconnected" persistently
- Verify the broker host, port, and protocol in the MQTT Config page.
- For cloud brokers (HiveMQ, etc.) use **WSS** mode, port **443**, path `/mqtt`.
- For local Mosquitto, use **TCP** mode, port **1883** (or 8883 for TLS).
- Check that the broker is reachable from the ESP32's network.
- Open the Serial Monitor to see MQTT reconnect attempts.

### DI state changes are not arriving at the broker
- Check that the DI topic fields are non-empty in the MQTT Config page.
- Verify the ESP32 is actually connected (green badge on MQTT Config page).
- Use an MQTT client (e.g. MQTT Explorer) to subscribe to `#` and confirm messages are arriving.

### Publishing to a DO topic does not toggle the output
- Confirm the output topic configured in the UI matches exactly what you are publishing to (case-sensitive).
- The payload must be exactly `"1"` (to turn on) or `"0"` (to turn off) — no spaces or trailing characters.
- Check `mqttCallback()` in the firmware if you suspect a topic-matching issue.

### Cloud MQTT (WSS) connects but disconnects frequently
- Cloud WebSocket MQTT connections are more sensitive to network latency.
- The keep-alive is set to 15 seconds in the firmware. If your broker's keep-alive is shorter, increase it on the broker side or reduce the firmware value:
  ```cpp
  mqtt.setKeepAlive(15);  // reduce if needed
  ```
- Some cloud brokers require authentication (username/password). The current firmware does not pass MQTT credentials — you would need to add them to `mqtt.connect()`:
  ```cpp
  mqtt.connect(deviceName, "mqttUser", "mqttPass");
  ```

---

## RS485 / Modbus Issues

### All registers show "Error" status
- Verify the slave address matches the physical device configuration.
- Check baud rate, parity, stop bits, and data bits in the RS485 Config page — they must match the slave device exactly.
- Verify wiring: A(D+) to A(D+), B(D-) to B(D-), GND to GND.
- Check the DE pin (GPIO 21) is wired correctly and the direction control is working.
- Use a USB-to-RS485 adapter and a Modbus scanner tool (e.g. Modbus Poll) on a PC to verify the slave device responds independently.

### Some registers read correctly, others fail
- Each register is polled in round-robin every 500 ms. A slow or overloaded slave may time out on some reads.
- Try increasing the inter-poll delay in `rs485Task`:
  ```cpp
  vTaskDelay(pdMS_TO_TICKS(500));  // increase to 1000 or more
  ```
- Check that the function code and address are correct for the failing registers.

### FLOAT32 register value looks wrong
- FLOAT32 uses two consecutive 16-bit registers. Verify the byte order. The firmware assumes big-endian (high word first):
  ```cpp
  uint16_t hi = modbusNode.getResponseBuffer(0);
  uint16_t lo = modbusNode.getResponseBuffer(1);
  uint32_t raw = ((uint32_t)hi << 16) | lo;
  ```
  If your device uses little-endian (low word first), swap `hi` and `lo`.

---

## SD Card Issues

### SD card shows "Not Mounted"
- Ensure the SD card is inserted before powering the board.
- The card must be **FAT32** formatted. exFAT is not supported by `SD_MMC` in 1-bit mode.
- Verify `NET_SCS` (GPIO 16) is driven HIGH before `SD_MMC.begin()` — this is done in `setup()` and prevents the Ethernet chip from interfering with the SD bus.
- Try a different SD card (some cards are not compatible with 1-bit MMC mode).

### Log files are not being created
- Check that SD is mounted (green status on SD Card page).
- Verify "Auto Logging" is enabled on the SD Card page.
- NTP time sync is required for correct log filenames. If NTP fails, the fallback filename format is `log_boot_<N>h.csv`. Check that `pool.ntp.org` is reachable from the ESP32's network.

### Log file content looks wrong or truncated
- The preview via the web UI is capped at 16 KB. Download the file to see the full content.
- If a row appears mid-write during a power cut, the last line may be malformed — this is normal for a power-loss scenario with no journaling.

---

## Telegram Issues

### Bot is configured but "failed to initialise" shown
- The bot token is incorrect. Double-check it against the token provided by @BotFather.
- Ensure the token is pasted without trailing spaces.
- The `WiFiClientSecure` connection uses `setInsecure()` — this should work for most networks. If your network has SSL inspection/interception, the HTTPS connection to the Telegram API may fail.

### Bot is active but sends no messages
- Verify the Chat ID is correct. Get your Chat ID from `@userinfobot` on Telegram.
- The outgoing message queue holds a maximum of 6 messages. If alerts fire faster than the 2.5-second poll cycle can drain the queue, older messages are dropped.
- Check that the board has network connectivity at the time the alert fires.

### Telegram commands get "Unauthorized" reply
- Another person or bot tested the bot using a different chat ID.
- The ESP32 rejects messages from chat IDs that do not match the configured `chatId`.
- Confirm your Chat ID on the Telegram Config page.

---

## General Debugging Tips

- **Always open Serial Monitor at 115200 baud** during setup and testing. The firmware prints boot progress, IP address, MQTT connection attempts, Modbus errors, and SD mount results.
- **Use the browser developer console** (F12) to inspect failed `fetch()` calls, see exact error codes, and view JSON responses.
- **Free heap** on the ESP32 Health panel should remain above ~80 KB during normal operation. If it drops below 20 KB, there may be a memory leak or the JSON documents are too large for the available stack.
- **Restart to clear transient issues** — the ESP32 sends a Telegram message on every boot, so you will know when it comes back online.
