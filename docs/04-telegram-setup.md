# 04 — Telegram Bot Setup

The ESP32 can send push alerts to your phone and respond to commands over Telegram. This is optional but useful for remote monitoring without opening a browser.

---

## Step 1 — Create a Bot

1. Open Telegram and search for **@BotFather**
2. Send `/newbot`
3. Follow the prompts — give the bot a name and a username (username must end in `bot`)
4. BotFather replies with a **bot token** that looks like:
   ```
   123456789:AbCdefGhiJkLmnoPQRStuvwXyZ
   ```
5. Copy and save this token

---

## Step 2 — Get Your Chat ID

1. In Telegram, search for **@userinfobot**
2. Send any message to it
3. It replies with your **Chat ID** — a number like `5012345678`
4. Copy and save that number

---

## Step 3 — Enter Credentials in the Dashboard

1. In your browser, go to `http://<ESP32-IP>/telegram`
2. Paste your **Bot Token** into the Bot Token field
3. Paste your **Chat ID** into the Chat ID field
4. Toggle **Enable Bot** to on
5. Click **Save & Apply**

The status banner turns green: *"Bot is active and polling for commands every 2.5 seconds."*

---

## Available Commands

Send these commands to your bot in Telegram:

| Command | What it does |
|---|---|
| `/status` | All DI1–DI8 and DO1–DO8 states, IP address, machine state |
| `/metrics` | Runtime, downtime, cycle time, reject count, efficiency |
| `/health` | Chip temperature, free RAM, network info, CPU speed, uptime |
| `/do on N` | Turn Digital Output N on (N = 1 to 8) |
| `/do off N` | Turn Digital Output N off (N = 1 to 8) |
| `/resetmetrics` | Reset all counters and timers to zero |
| `/help` | List all available commands |

---

## Automatic Alerts

The bot sends these automatically without any command:

| Event | Message |
|---|---|
| ESP32 boots up | `🚀 ESP32 Online — IP: x.x.x.x` |
| Machine → RUNNING | `🟢 Machine: RUNNING` |
| Machine → STOPPED | `🔴 Machine: STOPPED` |
| Reject milestone reached | `⚠️ Reject Alert! Total: N` |

The reject alert fires every N rejects — configure the threshold on the `/telegram` page (set to `0` to disable reject alerts).

---

## Pre-filling Credentials (Optional)

Instead of entering credentials through the browser, edit `data/telegram.json` before the LittleFS upload:

```json
{
  "botToken": "YOUR_BOT_TOKEN_HERE",
  "chatId": "YOUR_CHAT_ID_HERE",
  "enabled": true,
  "rejectThreshold": 10
}
```

After editing, re-run the LittleFS Data Upload from Arduino IDE.

---

## Next Step

→ [05 — Configuration](05-configuration.md)
