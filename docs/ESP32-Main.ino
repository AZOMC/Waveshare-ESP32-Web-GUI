// ============================================================
//  ESP32 Dashboard — with Modbus RS485 register management
//  Waveshare ESP32-S3-POE-ETH-8DI-8DO
// ============================================================

#include <WiFi.h>
#include <ETH.h>
#include <Wire.h>
#include <WebServer.h>
#include <WebSocketsClient.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>          // ← RS485 Modbus
#include <HardwareSerial.h>        // ← RS485 UART

// ─── WebSocket → Client bridge (cloud WS/WSS MQTT only) ──────────────────────
#define WSBRIDGE_RX_SIZE 1024
class WsClientBridge : public Client {
  WebSocketsClient& _ws;
  uint8_t  _buf[WSBRIDGE_RX_SIZE];
  uint32_t _head = 0, _tail = 0;
  bool     _connected = false;
  uint32_t _avail() const { return _head - _tail; }
public:
  explicit WsClientBridge(WebSocketsClient& ws) : _ws(ws) {}
  void push(const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n && _avail() < WSBRIDGE_RX_SIZE - 1; i++)
      _buf[(_head + i) % WSBRIDGE_RX_SIZE] = d[i];
    _head += n;
  }
  void setConnected(bool c) { _connected = c; if (!c) { _head = _tail = 0; } }
  int connect(IPAddress, uint16_t)   override { return (int)_connected; }
  int connect(const char*, uint16_t) override { return (int)_connected; }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* buf, size_t sz) override { _ws.sendBIN(buf, sz); return sz; }
  int available() override { if (!_avail()) _ws.loop(); return (int)_avail(); }
  int read() override {
    unsigned long t = millis();
    while (!_avail()) { _ws.loop(); if (millis() - t > 3000) return -1; }
    return _buf[_tail++ % WSBRIDGE_RX_SIZE];
  }
  int read(uint8_t* buf, size_t sz) override {
    int n = 0; while ((size_t)n < sz) { int b = read(); if (b < 0) break; buf[n++] = (uint8_t)b; } return n;
  }
  int peek() override { if (!_avail()) _ws.loop(); return _avail() ? _buf[_tail % WSBRIDGE_RX_SIZE] : -1; }
  void flush()  override {}
  void stop()   override { _ws.disconnect(); setConnected(false); }
  uint8_t connected() override { return _connected; }
  operator bool()     override { return _connected; }
};

#include <LittleFS.h>
#include <ArduinoJson.h>
#include "SD_MMC.h"
#include "FS.h"
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// ================= NETWORK =================
const char* ssid     = "staffshrdc";
const char* password = "w0rkCo0p2017@$hrDc";
bool eth_connected   = false;

// ================= DIGITAL IO =================
const int inputPins[8] = {4, 5, 6, 7, 8, 9, 10, 11};
uint8_t   outputState   = 0;

// ================= CONFIG STRUCTS =================
struct PinConfig { char topic[50]; };
PinConfig inputConfig[8];
PinConfig outputConfig[8];
char deviceName[30] = "esp32";

// MQTT connection config
struct MqttConfig {
  char mode[8];   // "local" or "cloud"
  char proto[8];  // local: "tcp"|"ssl"   cloud: "ws"|"wss"
  char host[100];
  int  port;
  char path[40];
} mqttCfg;

// ================= RS485 CONFIG =================
struct RS485Config {
  int  baud     = 9600;
  char parity[8]= "None";
  int  stopBits = 1;
  int  dataBits = 8;
  char addr[8]  = "1";
} rs485Config;

// ─── RS485 Hardware ─────────────────────────────────────────────────────────
#define RS485_RX  18
#define RS485_TX  17
#define RS485_DE  21   // DE / RE combined pin (HIGH = transmit, LOW = receive)

HardwareSerial RS485Serial(1);
ModbusMaster   modbusNode;

// ─── RS485 Register Table ────────────────────────────────────────────────────
#define MAX_RS485_REGS 16

struct RS485Register {
  char     name[32];
  uint8_t  fc;        // 1=coil, 2=discrete, 3=holding, 4=input
  uint16_t addr;
  uint8_t  dt;        // 0=uint16, 1=int16, 2=float32(2 regs), 3=scaled
  float    scale;     // used when dt==3
  bool     wr;        // writable flag
  bool     en;        // enabled (poll actively)
  // runtime state (not persisted)
  float    lastVal;
  bool     lastOk;
};
RS485Register  rs485Regs[MAX_RS485_REGS];
int            rs485RegCount    = 0;
bool           rs485Initialized = false;

static SemaphoreHandle_t rs485Mutex    = NULL;
static TaskHandle_t      rs485TaskHandle = NULL;

// ================= USER ACCOUNTS =================
struct UserAccount { char username[30]; char password[30]; };
UserAccount users[10];
int userCount = 0;

// ================= MACHINE STATE =================
#define MACHINE_STATE_TOPIC "esp32/machine/state"
uint8_t machineStateIdx = 1;
const char* MACHINE_LABELS[] = {"UNKNOWN", "RUNNING", "STOPPED"};

// ================= METRICS =================
unsigned long triggerCount[8]   = {0};
unsigned long totalOnTimeMs[8]  = {0};
unsigned long lastOnStartMs[8]  = {0};
bool          diCurrentlyOn[8]  = {false};
unsigned long cycleStartMs      = 0;
unsigned long lastCycleTimeMs   = 0;
bool          cycleActive       = false;
unsigned long rejectCount       = 0;
unsigned long runtimeMs         = 0;
unsigned long downtimeMs        = 0;
unsigned long lastMetricTickMs  = 0;
unsigned long lastMetricsSaveMs = 0;
float customEffPct = -1.0f;

// ================= DI DEBOUNCE =================
#define DEBOUNCE_MS 10
int           lastRawState[8];
unsigned long debounceTimer[8];

// ================= TIMING GUARDS =================
unsigned long lastDiScanMs    = 0;
unsigned long lastMetricAccMs = 0;

// ================= WEB SERVER + MQTT TRANSPORT =================
WebServer        server(80);
WiFiClient       tcpClient;
WiFiClientSecure mqttTlsClient;
WebSocketsClient wsClient;
WsClientBridge   wsBridge(wsClient);
PubSubClient     mqtt(tcpClient);
unsigned long    lastMqttRetry   = 0;
const unsigned long mqttRetryInterval = 5000;

// ================= SESSION =================
char activeToken[33] = "";

// ================================================================
//  TELEGRAM BOT
// ================================================================
struct TelegramConfig {
  char botToken[128];
  char chatId[24];
  bool enabled;
  int  rejectThreshold;
} tgConfig;

WiFiClientSecure     tgSecureClient;
UniversalTelegramBot tgBot("", tgSecureClient);
bool                 tgInitialized = false;

static SemaphoreHandle_t ioMutex    = NULL;
static QueueHandle_t     tgOutQueue = NULL;
static TaskHandle_t      tgTaskHandle = NULL;
volatile bool tgResetMetricsFlag = false;
const unsigned long TG_POLL_MS = 2500;

// ── Load / Save ──────────────────────────────────────────────────
void loadTelegramConfig() {
  memset(&tgConfig, 0, sizeof(tgConfig));
  tgConfig.rejectThreshold = 10;
  tgConfig.enabled         = false;
  if (!LittleFS.exists(F("/telegram.json"))) return;
  File f = LittleFS.open(F("/telegram.json"), "r");
  if (!f) return;
  StaticJsonDocument<384> doc;
  if (!deserializeJson(doc, f)) {
    strlcpy(tgConfig.botToken, doc["botToken"] | "", sizeof(tgConfig.botToken));
    strlcpy(tgConfig.chatId,   doc["chatId"]   | "", sizeof(tgConfig.chatId));
    tgConfig.enabled         = doc["enabled"]         | false;
    tgConfig.rejectThreshold = doc["rejectThreshold"] | 10;
  }
  f.close();
}

void saveTelegramConfig() {
  StaticJsonDocument<384> doc;
  doc["botToken"]        = tgConfig.botToken;
  doc["chatId"]          = tgConfig.chatId;
  doc["enabled"]         = tgConfig.enabled;
  doc["rejectThreshold"] = tgConfig.rejectThreshold;
  File f = LittleFS.open(F("/telegram.json"), "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

// ── Persist & restore metrics ────────────────────────────────────
void saveMetrics() {
  StaticJsonDocument<576> doc;
  doc["rejects"]      = rejectCount;
  doc["runtime"]      = runtimeMs;
  doc["downtime"]     = downtimeMs;
  doc["machineState"] = machineStateIdx;
  doc["customEff"]    = customEffPct;
  JsonArray tc = doc.createNestedArray("tc");
  JsonArray ot = doc.createNestedArray("ot");
  for (int i = 0; i < 8; i++) { tc.add(triggerCount[i]); ot.add(totalOnTimeMs[i]); }
  File f = LittleFS.open(F("/metrics.json"), "w");
  if (f) { serializeJson(doc, f); f.close(); }
}

void loadMetrics() {
  if (!LittleFS.exists(F("/metrics.json"))) return;
  File f = LittleFS.open(F("/metrics.json"), "r");
  if (!f) return;
  StaticJsonDocument<576> doc;
  if (!deserializeJson(doc, f)) {
    rejectCount     = doc["rejects"]      | (unsigned long)0;
    runtimeMs       = doc["runtime"]      | (unsigned long)0;
    downtimeMs      = doc["downtime"]     | (unsigned long)0;
    machineStateIdx = doc["machineState"] | (uint8_t)1;
    customEffPct    = doc["customEff"]    | -1.0f;
    for (int i = 0; i < 8; i++) {
      triggerCount[i]  = doc["tc"][i] | (unsigned long)0;
      totalOnTimeMs[i] = doc["ot"][i] | (unsigned long)0;
    }
  }
  f.close();
}

// ── Telegram bot init ─────────────────────────────────────────────
void initTelegramBot() {
  tgInitialized = false;
  if (strlen(tgConfig.botToken) < 10) return;
  tgSecureClient.setInsecure();
  tgSecureClient.setTimeout(8);
  tgBot = UniversalTelegramBot(tgConfig.botToken, tgSecureClient);
  tgInitialized = true;
}

// ── Thread-safe outgoing send ─────────────────────────────────────
void tgSend(const String& msg) {
  if (!tgInitialized || !tgConfig.enabled) return;
  if (strlen(tgConfig.chatId) == 0 || tgOutQueue == NULL) return;
  char buf[256] = {0};
  msg.substring(0, 254).toCharArray(buf, sizeof(buf));
  xQueueSend(tgOutQueue, buf, pdMS_TO_TICKS(0));
}

static String fmtMsBot(unsigned long ms) {
  unsigned long s = ms / 1000, m = s / 60, h = m / 60;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m % 60, s % 60);
  return String(buf);
}

String buildStatusMsg() {
  char buf[512]; int p = 0;
  String ip = eth_connected ? ETH.localIP().toString() : WiFi.localIP().toString();
  p += snprintf(buf+p, sizeof(buf)-p,
    "\xF0\x9F\x93\x8A *Status*\nMachine: *%s*\nIP: `%s`\n\n*Inputs:*\n",
    MACHINE_LABELS[machineStateIdx], ip.c_str());
  for (int i = 0; i < 8; i++)
    p += snprintf(buf+p, sizeof(buf)-p, "DI%d: %s\n", i+1, diCurrentlyOn[i] ? "\xF0\x9F\x9F\xA2 ON" : "\u26AB OFF");
  p += snprintf(buf+p, sizeof(buf)-p, "\n*Outputs:*\n");
  for (int i = 0; i < 8; i++)
    p += snprintf(buf+p, sizeof(buf)-p, "DO%d: %s\n", i+1, ((outputState>>i)&1) ? "\xF0\x9F\x9F\xA2 ON" : "\u26AB OFF");
  return String(buf);
}

String buildMetricsMsg() {
  unsigned long now = millis(), rt = runtimeMs, dt = downtimeMs;
  if (machineStateIdx == 1) rt += now - lastMetricTickMs;
  else if (machineStateIdx == 2) dt += now - lastMetricTickMs;
  float eff = (rt + dt > 0) ? (float)rt / (float)(rt + dt) * 100.0f : 0;
  char buf[256];
  snprintf(buf, sizeof(buf),
    "\xF0\x9F\x93\x88 *Metrics*\nRuntime: `%s`\nDowntime: `%s`\nCycle: `%s`\nRejects: `%lu`\nEfficiency: `%.1f%%`",
    fmtMsBot(rt).c_str(), fmtMsBot(dt).c_str(),
    lastCycleTimeMs > 0 ? (String(lastCycleTimeMs/1000.0, 2)+" s").c_str() : "--",
    rejectCount, eff);
  return String(buf);
}

String buildHealthMsg() {
  unsigned long upS = millis()/1000, upM = upS/60, upH = upM/60, upD = upH/24;
  char up[32], buf[256];
  if (upD > 0) snprintf(up, sizeof(up), "%lud %02lu:%02lu:%02lu", upD, upH%24, upM%60, upS%60);
  else         snprintf(up, sizeof(up), "%02lu:%02lu:%02lu", upH, upM%60, upS%60);
  snprintf(buf, sizeof(buf),
    "\xE2\x9D\xA4 *Health*\nTemp: `%.1f\xC2\xB0""C`\nHeap: `%lu KB`\nNet: `%s`\nCPU: `%u MHz`\nUp: `%s`",
    temperatureRead(), (unsigned long)(ESP.getFreeHeap()/1024),
    eth_connected ? "Ethernet" : (String(WiFi.RSSI())+" dBm").c_str(),
    getCpuFrequencyMhz(), up);
  return String(buf);
}

void handleTelegramMessage(const telegramMessage& m) {
  if (strlen(tgConfig.chatId) > 0 && m.chat_id != String(tgConfig.chatId)) {
    tgBot.sendMessage(m.chat_id, "\xE2\x9B\x94 Unauthorized.", "");
    return;
  }
  String text = m.text; text.trim();
  if (text.startsWith("/help")) {
    tgBot.sendMessage(m.chat_id,
      "\xF0\x9F\xA4\x96 *ESP32 Bot Commands*\n\n"
      "/status — DI/DO states & machine state\n"
      "/metrics — Runtime, downtime, rejects\n"
      "/health — Temp, heap, WiFi, uptime\n"
      "/do on N — Turn output N ON (1-8)\n"
      "/do off N — Turn output N OFF (1-8)\n"
      "/resetmetrics — Reset all counters\n"
      "/help — Show this menu", "Markdown");
  }
  else if (text == "/status")  { tgBot.sendMessage(m.chat_id, buildStatusMsg(),  "Markdown"); }
  else if (text == "/metrics") { tgBot.sendMessage(m.chat_id, buildMetricsMsg(), "Markdown"); }
  else if (text == "/health")  { tgBot.sendMessage(m.chat_id, buildHealthMsg(),  "Markdown"); }
  else if (text.startsWith("/do ")) {
    String sub = text.substring(4); sub.trim();
    bool turnOn = false; int ch = -1;
    if (sub.startsWith("on "))       { turnOn = true;  ch = sub.substring(3).toInt() - 1; }
    else if (sub.startsWith("off ")) { turnOn = false; ch = sub.substring(4).toInt() - 1; }
    if (ch >= 0 && ch <= 7) {
      setOutput(ch, turnOn);
      tgBot.sendMessage(m.chat_id,
        "\xE2\x9C\x85 DO" + String(ch+1) + " turned " + (turnOn ? "ON" : "OFF"), "");
    } else {
      tgBot.sendMessage(m.chat_id, "\xE2\x9A\xA0 Usage: /do on N  or  /do off N  (N = 1\xe2\x80\x938)", "");
    }
  }
  else if (text == "/resetmetrics") {
    tgResetMetricsFlag = true;
    tgBot.sendMessage(m.chat_id, "\xE2\x99\xBB Reset requested.", "");
  }
  else {
    tgBot.sendMessage(m.chat_id, "\xe2\x9d\x93 Unknown command. Send /help for the list.", "");
  }
}

void telegramTask(void* pvParameters) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(TG_POLL_MS));
    if (!tgInitialized || !tgConfig.enabled) continue;
    if (WiFi.status() != WL_CONNECTED && !eth_connected) continue;
    char outMsg[256];
    if (xQueueReceive(tgOutQueue, outMsg, 0) == pdTRUE) {
      tgBot.sendMessage(tgConfig.chatId, outMsg, "Markdown");
      continue;
    }
    int n = tgBot.getUpdates(tgBot.last_message_received + 1);
    for (int i = 0; i < n; i++) handleTelegramMessage(tgBot.messages[i]);
  }
}

// ================= I2C OUTPUT EXPANDER =================
#define EXIO_ADDR 0x20
void writeOutputs() {
  Wire.beginTransmission(EXIO_ADDR);
  Wire.write(outputState);
  Wire.endTransmission();
}

void setOutput(int ch, bool state) {
  if (ioMutex == NULL) return;
  xSemaphoreTake(ioMutex, portMAX_DELAY);
  bool cur = (outputState >> ch) & 1;
  if (cur != state) {
    if (state) outputState |=  (1 << ch);
    else       outputState &= ~(1 << ch);
    writeOutputs();
    if (strlen(outputConfig[ch].topic) > 0 && mqtt.connected())
      mqtt.publish(outputConfig[ch].topic, state ? "1" : "0");
  }
  xSemaphoreGive(ioMutex);
}

// ================= MACHINE STATE =================
void setMachineState(uint8_t idx) {
  if (machineStateIdx == idx) return;
  machineStateIdx = idx;
  if (idx == 1)      tgSend(F("\xF0\x9F\x9F\xA2 Machine: *RUNNING*"));
  else if (idx == 2) tgSend(F("\xF0\x9F\x94\xB4 Machine: *STOPPED*"));
}

void autoUpdateMachineState(int ch, int state) {
  if (ch == 0 && state == 0) { setMachineState(machineStateIdx == 1 ? 2 : 1); return; }
  if (ch == 1 && state == 1) { setMachineState(1); return; }
  if (ch == 2 && state == 1) { setMachineState(2); return; }
}

// ================= SD CARD =================
#define SD_CLK   48
#define SD_CMD   47
#define SD_D0    45
#define NET_SCS  16

bool          sdMounted            = false;
int           sdLogIntervalSec     = 60;
bool          sdLogEnabled         = true;
unsigned long lastSdLogMs          = 0;
char          currentLogFile[40]   = "";
unsigned long logRotateIntervalSec = 3600;
unsigned long logFileStartMs       = 0;

#define SD_LOG_TOPIC  "esp32/sd/log"
#define SD_INFO_TOPIC "esp32/sd/info"
#define SD_CMD_TOPIC  "esp32/sd/cmd"

void getLogFilename(char* buf, size_t len) {
  struct tm t;
  if (getLocalTime(&t, 200))
    snprintf(buf, len, "/log_%04d%02d%02d_%02d%02d.csv",
             t.tm_year+1900, t.tm_mon+1, t.tm_mday, t.tm_hour, t.tm_min);
  else
    snprintf(buf, len, "/log_boot_%luh.csv", millis()/3600000UL);
}

void ensureLogHeader() {
  if (currentLogFile[0] == '\0' || SD_MMC.exists(currentLogFile)) return;
  File f = SD_MMC.open(currentLogFile, FILE_WRITE);
  if (f) {
    f.println(F("millis,state,runtime_ms,downtime_ms,cycle_ms,rejects,eff_pct,"
                "di1,di2,di3,di4,di5,di6,di7,di8"));
    f.close();
  }
}

void initLogFile() {
  getLogFilename(currentLogFile, sizeof(currentLogFile));
  logFileStartMs = millis();
  ensureLogHeader();
}

void publishSDInfo() {
  if (!mqtt.connected()) return;
  StaticJsonDocument<128> doc;
  doc["mounted"]    = sdMounted;
  doc["logFile"]    = currentLogFile;
  if (sdMounted)
    doc["freeMB"] = (uint32_t)((SD_MMC.cardSize() - SD_MMC.usedBytes()) / (1024 * 1024));
  String out; serializeJson(doc, out);
  mqtt.publish(SD_INFO_TOPIC, out.c_str());
}

void sdLogRow() {
  if (!sdMounted || currentLogFile[0] == '\0') return;
  ensureLogHeader();
  File f = SD_MMC.open(currentLogFile, FILE_APPEND);
  if (!f) return;
  unsigned long now = millis();
  unsigned long rt  = runtimeMs, dt = downtimeMs;
  if (machineStateIdx == 1) rt += now - lastMetricTickMs;
  else if (machineStateIdx == 2) dt += now - lastMetricTickMs;
  float eff = (customEffPct >= 0.0f) ? customEffPct
            : ((rt + dt > 0) ? ((float)rt / (float)(rt + dt)) * 100.0f : 0.0f);
  char row[300]; int p = 0;
  p += snprintf(row+p, sizeof(row)-p, "%lu,%s,%lu,%lu,%lu,%lu,%.1f",
    now, MACHINE_LABELS[machineStateIdx], rt, dt, lastCycleTimeMs, rejectCount, eff);
  for (int i = 0; i < 8; i++)
    p += snprintf(row+p, sizeof(row)-p, ",%lu", triggerCount[i]);
  f.println(row);
  f.close();
  publishSDInfo();
}

// ================= CONFIG LOAD / SAVE =================
void loadConfig() {
  // defaults
  strlcpy(mqttCfg.mode,  "cloud", sizeof(mqttCfg.mode));
  strlcpy(mqttCfg.proto, "wss",   sizeof(mqttCfg.proto));
  strlcpy(mqttCfg.host,
    "1b88e7df-d8af-4d46-ac2b-43963d26bdf2-00-if445lcva84n.pike.replit.dev",
    sizeof(mqttCfg.host));
  mqttCfg.port = 443;
  strlcpy(mqttCfg.path, "/mqtt", sizeof(mqttCfg.path));

  if (!LittleFS.exists(F("/config.json"))) return;
  File f = LittleFS.open(F("/config.json"), "r");
  if (!f) return;
  StaticJsonDocument<2048> doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();

  strlcpy(deviceName, doc["deviceName"] | "esp32", sizeof(deviceName));

  if (doc.containsKey("mqttMode")) {
    strlcpy(mqttCfg.mode,  doc["mqttMode"]  | "cloud", sizeof(mqttCfg.mode));
    strlcpy(mqttCfg.proto, doc["mqttProto"] | "wss",   sizeof(mqttCfg.proto));
    strlcpy(mqttCfg.host,  doc["mqttHost"]  | "",      sizeof(mqttCfg.host));
    mqttCfg.port = doc["mqttPort"] | 443;
    strlcpy(mqttCfg.path,  doc["mqttPath"]  | "/mqtt", sizeof(mqttCfg.path));
  } else if (doc.containsKey("mqttBroker")) {
    strlcpy(mqttCfg.host, doc["mqttBroker"] | "", sizeof(mqttCfg.host));
  }

  for (int i = 0; i < 8; i++) {
    strlcpy(inputConfig[i].topic,  doc["inputs"][i]  | "", 50);
    strlcpy(outputConfig[i].topic, doc["outputs"][i] | "", 50);
  }
  rs485Config.baud     = doc["rs485"]["baud"]     | 9600;
  rs485Config.stopBits = doc["rs485"]["stopBits"] | 1;
  rs485Config.dataBits = doc["rs485"]["dataBits"] | 8;
  strlcpy(rs485Config.parity, doc["rs485"]["parity"] | "None", sizeof(rs485Config.parity));
  strlcpy(rs485Config.addr,   doc["rs485"]["addr"]   | "1",    sizeof(rs485Config.addr));
  sdLogIntervalSec     = doc["sdLogInterval"]      | 60;
  sdLogEnabled         = doc["sdLogEnabled"]        | true;
  logRotateIntervalSec = doc["sdLogRotateInterval"] | 3600UL;
}

void saveConfig() {
  StaticJsonDocument<2048> doc;
  doc["deviceName"] = deviceName;
  doc["mqttMode"]   = mqttCfg.mode;
  doc["mqttProto"]  = mqttCfg.proto;
  doc["mqttHost"]   = mqttCfg.host;
  doc["mqttPort"]   = mqttCfg.port;
  doc["mqttPath"]   = mqttCfg.path;
  for (int i = 0; i < 8; i++) {
    doc["inputs"][i]  = inputConfig[i].topic;
    doc["outputs"][i] = outputConfig[i].topic;
  }
  JsonObject rs = doc.createNestedObject("rs485");
  rs["baud"]     = rs485Config.baud;
  rs["parity"]   = rs485Config.parity;
  rs["stopBits"] = rs485Config.stopBits;
  rs["dataBits"] = rs485Config.dataBits;
  rs["addr"]     = rs485Config.addr;
  doc["sdLogInterval"]       = sdLogIntervalSec;
  doc["sdLogEnabled"]        = sdLogEnabled;
  doc["sdLogRotateInterval"] = logRotateIntervalSec;
  File f = LittleFS.open(F("/config.json"), "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

// ================= RS485 REGISTER PERSIST =================
void loadRS485Regs() {
  rs485RegCount = 0;
  if (!LittleFS.exists(F("/rs485_regs.json"))) return;
  File f = LittleFS.open(F("/rs485_regs.json"), "r");
  if (!f) return;
  StaticJsonDocument<4096> doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  for (JsonObject obj : doc.as<JsonArray>()) {
    if (rs485RegCount >= MAX_RS485_REGS) break;
    RS485Register& r = rs485Regs[rs485RegCount];
    strlcpy(r.name, obj["name"] | "Reg", sizeof(r.name));
    r.fc    = obj["fc"]    | 3;
    r.addr  = obj["addr"]  | 0;
    r.dt    = obj["dt"]    | 0;
    r.scale = obj["scale"] | 1.0f;
    r.wr    = obj["wr"]    | false;
    r.en    = obj["en"]    | true;
    r.lastOk  = false;
    r.lastVal = 0.0f;
    rs485RegCount++;
  }
}

void saveRS485Regs() {
  StaticJsonDocument<4096> doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < rs485RegCount; i++) {
    RS485Register& r = rs485Regs[i];
    JsonObject obj = arr.createNestedObject();
    obj["name"]  = r.name;
    obj["fc"]    = r.fc;
    obj["addr"]  = r.addr;
    obj["dt"]    = r.dt;
    obj["scale"] = r.scale;
    obj["wr"]    = r.wr;
    obj["en"]    = r.en;
  }
  File f = LittleFS.open(F("/rs485_regs.json"), "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
}

// ================= RS485 INIT =================
void rs485PreTx()  { digitalWrite(RS485_DE, HIGH); }
void rs485PostTx() { digitalWrite(RS485_DE, LOW);  }

// Build the correct SERIAL_xYz config constant from rs485Config fields
uint32_t buildSerialConfig() {
  int  db = rs485Config.dataBits;
  int  sb = rs485Config.stopBits;
  char p  = rs485Config.parity[0]; // 'N', 'E', 'O'
  if (db == 8) {
    if (p == 'N') return sb == 2 ? SERIAL_8N2 : SERIAL_8N1;
    if (p == 'E') return sb == 2 ? SERIAL_8E2 : SERIAL_8E1;
    if (p == 'O') return sb == 2 ? SERIAL_8O2 : SERIAL_8O1;
  }
  if (db == 7) {
    if (p == 'N') return SERIAL_7N1;
    if (p == 'E') return SERIAL_7E1;
    if (p == 'O') return SERIAL_7O1;
  }
  return SERIAL_8N1; // safe default
}

void initRS485() {
  RS485Serial.end();
  RS485Serial.begin(rs485Config.baud, buildSerialConfig(), RS485_RX, RS485_TX);
  int slaveAddr = String(rs485Config.addr).toInt();
  if (slaveAddr < 1 || slaveAddr > 247) slaveAddr = 1;
  modbusNode.begin((uint8_t)slaveAddr, RS485Serial);
  modbusNode.preTransmission(rs485PreTx);
  modbusNode.postTransmission(rs485PostTx);
  rs485Initialized = true;
  Serial.printf("RS485 init: %d baud, %s, slave %d\n",
    rs485Config.baud, rs485Config.parity, slaveAddr);
}

// ================= RS485 POLL (called from rs485Task) =================
// Polls a single register. Caller holds rs485Mutex.
void pollRS485Reg(int idx) {
  if (idx < 0 || idx >= rs485RegCount) return;
  RS485Register& r = rs485Regs[idx];
  if (!r.en) return;

  uint8_t result = ModbusMaster::ku8MBInvalidCRC; // safe sentinel
  switch (r.fc) {
    case 1: result = modbusNode.readCoils(r.addr, 1);             break;
    case 2: result = modbusNode.readDiscreteInputs(r.addr, 1);    break;
    case 3: result = modbusNode.readHoldingRegisters(r.addr, r.dt == 2 ? 2 : 1); break;
    case 4: result = modbusNode.readInputRegisters(r.addr, r.dt == 2 ? 2 : 1);   break;
    default: break;
  }

  if (result == ModbusMaster::ku8MBSuccess) {
    r.lastOk = true;
    if (r.fc == 1 || r.fc == 2) {
      r.lastVal = (float)(modbusNode.getResponseBuffer(0) & 0x01);
    } else {
      switch (r.dt) {
        case 0: r.lastVal = (float)(uint16_t)modbusNode.getResponseBuffer(0); break;
        case 1: r.lastVal = (float)(int16_t)modbusNode.getResponseBuffer(0);  break;
        case 2: {
          uint16_t hi = modbusNode.getResponseBuffer(0);
          uint16_t lo = modbusNode.getResponseBuffer(1);
          uint32_t raw = ((uint32_t)hi << 16) | lo;
          memcpy(&r.lastVal, &raw, sizeof(float));
          break;
        }
        case 3: r.lastVal = (float)(uint16_t)modbusNode.getResponseBuffer(0) * r.scale; break;
        default: r.lastVal = (float)modbusNode.getResponseBuffer(0); break;
      }
    }
  } else {
    r.lastOk = false;
    Serial.printf("Modbus error reg[%d] fc=%d addr=%d: 0x%02X\n", idx, r.fc, r.addr, result);
  }
}

// ================= RS485 FREERTOS TASK (Core 0) =================
void rs485Task(void* pvParameters) {
  // Stagger startup — let network + MQTT settle first
  vTaskDelay(pdMS_TO_TICKS(3000));
  int pollIdx = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(500)); // 500 ms between per-register polls
    if (!rs485Initialized || rs485RegCount == 0) continue;

    // Advance to next enabled register (round-robin)
    int tries = 0;
    do {
      pollIdx = (pollIdx + 1) % rs485RegCount;
      tries++;
    } while (!rs485Regs[pollIdx].en && tries < rs485RegCount);

    if (!rs485Regs[pollIdx].en) continue; // all disabled

    if (xSemaphoreTake(rs485Mutex, pdMS_TO_TICKS(300)) == pdTRUE) {
      pollRS485Reg(pollIdx);
      xSemaphoreGive(rs485Mutex);
    }
  }
}

// ================= USER MANAGEMENT =================
void loadUsers() {
  userCount = 0;
  if (!LittleFS.exists(F("/users.json"))) return;
  File f = LittleFS.open(F("/users.json"), "r");
  if (!f) return;
  StaticJsonDocument<768> doc;
  if (deserializeJson(doc, f)) { f.close(); return; }
  f.close();
  for (JsonObject u : doc.as<JsonArray>()) {
    if (userCount >= 10) break;
    strlcpy(users[userCount].username, u["username"] | "", 30);
    strlcpy(users[userCount].password, u["password"] | "", 30);
    userCount++;
  }
}

bool checkCredentials(const String& u, const String& p) {
  for (int i = 0; i < userCount; i++)
    if (strcmp(users[i].username, u.c_str()) == 0 &&
        strcmp(users[i].password, p.c_str()) == 0) return true;
  return false;
}

// ================= AUTH =================
void generateToken(char* buf) {
  randomSeed(micros() ^ millis());
  for (int i = 0; i < 32; i++) { int r = random(16); buf[i] = r<10 ? '0'+r : 'a'+r-10; }
  buf[32] = '\0';
}

bool isAuthenticated() {
  if (activeToken[0] == '\0') return false;
  String c = server.header("Cookie");
  int idx = c.indexOf(F("session="));
  if (idx == -1) return false;
  String v = c.substring(idx + 8);
  int e = v.indexOf(';'); if (e != -1) v = v.substring(0, e);
  v.trim();
  return strcmp(activeToken, v.c_str()) == 0;
}

bool requireAuth() {
  if (!isAuthenticated()) { server.sendHeader(F("Location"), F("/login")); server.send(302); return false; }
  return true;
}

// ================= STATIC FILE HELPER =================
void serveStatic(const char* path, const char* mime, bool noCache = false) {
  File f = LittleFS.open(path, "r");
  if (!f) { server.send(404, F("text/plain"), F("Not Found")); return; }
  if (!noCache) server.sendHeader(F("Cache-Control"), F("public, max-age=86400"));
  server.streamFile(f, mime);
  f.close();
}

// ================= LOGIN / LOGOUT =================
void handleLogin() {
  if (server.method() == HTTP_GET) {
    serveStatic("/login.html", "text/html", true);
  } else {
    if (checkCredentials(server.arg("username"), server.arg("password"))) {
      generateToken(activeToken);
      server.sendHeader(F("Set-Cookie"), String(F("session=")) + activeToken + F("; Path=/"));
      server.sendHeader(F("Location"), F("/"));
      server.send(302);
    } else {
      server.sendHeader(F("Location"), F("/login?error=1"));
      server.send(302);
    }
  }
}

void handleLogout() {
  activeToken[0] = '\0';
  server.sendHeader(F("Set-Cookie"), F("session=; Path=/; Max-Age=0"));
  server.sendHeader(F("Location"), F("/login"));
  server.send(302);
}

// ================= MQTT TRANSPORT =================
void applyMqttTransport() {
  mqtt.disconnect();
  wsBridge.setConnected(false);
  wsClient.disconnect();

  if (strcmp(mqttCfg.mode, "cloud") == 0) {
    mqtt.setClient(wsBridge);
    if (strcmp(mqttCfg.proto, "wss") == 0)
      wsClient.beginSSL(mqttCfg.host, mqttCfg.port, mqttCfg.path, "", "mqtt");
    else
      wsClient.begin(mqttCfg.host, mqttCfg.port, mqttCfg.path, "mqtt");
    wsClient.onEvent(wsEventHandler);
    wsClient.setReconnectInterval(5000);
  } else {
    if (strcmp(mqttCfg.proto, "ssl") == 0) {
      mqttTlsClient.setInsecure();
      mqtt.setClient(mqttTlsClient);
    } else {
      mqtt.setClient(tcpClient);
    }
  }
  mqtt.setServer(mqttCfg.host, mqttCfg.port);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(15);
  lastMqttRetry = 0;
}

void wsEventHandler(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:    wsBridge.setConnected(true);  break;
    case WStype_DISCONNECTED: wsBridge.setConnected(false); break;
    case WStype_BIN:          wsBridge.push(payload, length); break;
    default: break;
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, MACHINE_STATE_TOPIC) == 0) {
    char msg[12] = {0};
    memcpy(msg, payload, min(length, (unsigned int)10));
    if      (strcmp(msg, "RUNNING") == 0) setMachineState(1);
    else if (strcmp(msg, "STOPPED") == 0) setMachineState(2);
    return;
  }
  if (strcmp(topic, SD_CMD_TOPIC) == 0) {
    StaticJsonDocument<128> cmd;
    if (!deserializeJson(cmd, payload, length)) {
      const char* c = cmd["cmd"] | "";
      if      (strcmp(c, "getInfo")  == 0) publishSDInfo();
      else if (strcmp(c, "snapshot") == 0) sdLogRow();
    }
    return;
  }
  char msg[4] = {0};
  memcpy(msg, payload, min(length, (unsigned int)3));
  for (int i = 0; i < 8; i++) {
    if (strlen(outputConfig[i].topic) > 0 &&
        strcmp(topic, outputConfig[i].topic) == 0) {
      setOutput(i, strcmp(msg, "1") == 0);
      break;
    }
  }
}

void reconnectMQTT() {
  if (strcmp(mqttCfg.mode, "cloud") == 0 && !wsBridge.connected()) return;
  if (mqtt.connect(deviceName)) {
    for (int i = 0; i < 8; i++)
      if (strlen(outputConfig[i].topic) > 0) mqtt.subscribe(outputConfig[i].topic);
    mqtt.subscribe(MACHINE_STATE_TOPIC);
    mqtt.subscribe(SD_CMD_TOPIC);
  }
}

// ================= METRICS HELPER =================
unsigned long getOnTime(int ch) {
  unsigned long t = totalOnTimeMs[ch];
  if (diCurrentlyOn[ch]) t += millis() - lastOnStartMs[ch];
  return t;
}

// ================= API HANDLERS =================
void handleToggle() {
  if (!requireAuth()) return;
  int ch = server.arg("ch").toInt();
  if (ch < 0 || ch > 7) { server.send(400, F("text/plain"), F("Invalid channel")); return; }
  bool ns = !((outputState >> ch) & 1);
  setOutput(ch, ns);
  server.send(200, F("text/plain"), ns ? "1" : "0");
}

void handleStatus() {
  if (!requireAuth()) return;
  char buf[256]; int pos = 0;
  pos += snprintf(buf+pos, sizeof(buf)-pos, "{\"inputs\":[");
  for (int i = 0; i < 8; i++) { buf[pos++] = diCurrentlyOn[i] ? '1':'0'; if (i<7) buf[pos++] = ','; }
  pos += snprintf(buf+pos, sizeof(buf)-pos, "],\"outputs\":[");
  for (int i = 0; i < 8; i++) { buf[pos++] = (outputState&(1<<i)) ? '1':'0'; if (i<7) buf[pos++] = ','; }
  String ip = eth_connected ? ETH.localIP().toString() : WiFi.localIP().toString();
  snprintf(buf+pos, sizeof(buf)-pos, "],\"ip\":\"%s\",\"network\":\"%s\",\"machineState\":\"%s\"}",
           ip.c_str(), eth_connected ? "Ethernet" : "WiFi", MACHINE_LABELS[machineStateIdx]);
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.send(200, F("application/json"), buf);
}

void handleGetMetrics() {
  if (!requireAuth()) return;
  unsigned long now = millis(), rt = runtimeMs, dt = downtimeMs;
  if (machineStateIdx == 1) rt += now - lastMetricTickMs;
  else if (machineStateIdx == 2) dt += now - lastMetricTickMs;
  StaticJsonDocument<640> doc;
  JsonArray trig = doc.createNestedArray("triggers");
  JsonArray ont  = doc.createNestedArray("onTimes");
  JsonArray dis  = doc.createNestedArray("diState");
  for (int i = 0; i < 8; i++) {
    trig.add(triggerCount[i]); ont.add(getOnTime(i)); dis.add(diCurrentlyOn[i] ? 1 : 0);
  }
  doc["cycleTime"] = lastCycleTimeMs;
  doc["rejects"]   = rejectCount;
  doc["runtime"]   = rt;
  doc["downtime"]  = dt;
  String out; serializeJson(doc, out);
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.send(200, F("application/json"), out);
}

void handleResetMetrics() {
  if (!requireAuth()) return;
  String target = server.arg("target");
  unsigned long now = millis();
  if (target == "all") {
    for (int i = 0; i < 8; i++) {
      triggerCount[i] = 0; totalOnTimeMs[i] = 0;
      if (diCurrentlyOn[i]) lastOnStartMs[i] = now;
    }
    rejectCount = 0; runtimeMs = 0; downtimeMs = 0;
    lastCycleTimeMs = 0; lastMetricTickMs = now;
  } else if (target.startsWith("di")) {
    int ch = target.substring(2).toInt();
    if (ch >= 0 && ch < 8) {
      triggerCount[ch] = 0; totalOnTimeMs[ch] = 0;
      if (diCurrentlyOn[ch]) lastOnStartMs[ch] = now;
    }
  } else if (target == "rejects")  { rejectCount = 0; }
  else if (target == "runtime")    { runtimeMs = 0; downtimeMs = 0; lastMetricTickMs = now; }
  else if (target == "cycle")      { lastCycleTimeMs = 0; }
  saveMetrics();
  server.send(200, F("text/plain"), F("OK"));
}

void handleGetConfig() {
  if (!requireAuth()) return;
  StaticJsonDocument<896> doc;
  doc["deviceName"]    = deviceName;
  doc["mqttMode"]      = mqttCfg.mode;
  doc["mqttProto"]     = mqttCfg.proto;
  doc["mqttHost"]      = mqttCfg.host;
  doc["mqttPort"]      = mqttCfg.port;
  doc["mqttPath"]      = mqttCfg.path;
  doc["mqttConnected"] = mqtt.connected();
  for (int i = 0; i < 8; i++) {
    doc["inputs"][i]  = inputConfig[i].topic;
    doc["outputs"][i] = outputConfig[i].topic;
  }
  String out; serializeJson(doc, out);
  server.send(200, F("application/json"), out);
}

void handleGetRS485() {
  if (!requireAuth()) return;
  StaticJsonDocument<160> doc;
  doc["baud"]     = rs485Config.baud;
  doc["parity"]   = rs485Config.parity;
  doc["stopBits"] = rs485Config.stopBits;
  doc["dataBits"] = rs485Config.dataBits;
  doc["addr"]     = rs485Config.addr;
  String out; serializeJson(doc, out);
  server.send(200, F("application/json"), out);
}

void handleGetHealth() {
  if (!requireAuth()) return;
  StaticJsonDocument<160> doc;
  doc["temp"]     = temperatureRead();
  doc["freeHeap"] = (uint32_t)ESP.getFreeHeap();
  doc["rssi"]     = eth_connected ? 0 : (int)WiFi.RSSI();
  doc["cpuFreq"]  = getCpuFrequencyMhz();
  doc["uptime"]   = millis();
  String out; serializeJson(doc, out);
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.send(200, F("application/json"), out);
}

void handleSave() {
  if (!requireAuth()) return;
  String mode  = server.arg("mqttMode");
  String proto = server.arg("mqttProto");
  String host  = server.arg("mqttHost");
  String port  = server.arg("mqttPort");
  String path  = server.arg("mqttPath");

  bool mqttChanged = false;
  if (mode.length() > 0) {
    mqttChanged = (strcmp(mode.c_str(),  mqttCfg.mode)  != 0 ||
                   strcmp(proto.c_str(), mqttCfg.proto) != 0 ||
                   (host.length() > 0 && strcmp(host.c_str(), mqttCfg.host) != 0) ||
                   (port.length() > 0 && port.toInt() != mqttCfg.port));
    strlcpy(mqttCfg.mode,  mode.c_str(),  sizeof(mqttCfg.mode));
    strlcpy(mqttCfg.proto, proto.c_str(), sizeof(mqttCfg.proto));
    if (host.length() > 0) strlcpy(mqttCfg.host, host.c_str(), sizeof(mqttCfg.host));
    if (port.length() > 0) mqttCfg.port = port.toInt();
    if (path.length() > 0) strlcpy(mqttCfg.path, path.c_str(), sizeof(mqttCfg.path));
    else if (strcmp(mqttCfg.mode, "local") == 0) mqttCfg.path[0] = '\0';
  }
  for (int i = 0; i < 8; i++) {
    strlcpy(inputConfig[i].topic,  server.arg("in"  + String(i)).c_str(), 50);
    strlcpy(outputConfig[i].topic, server.arg("out" + String(i)).c_str(), 50);
  }
  saveConfig();
  if (mqttChanged) applyMqttTransport();
  server.send(200, F("text/plain"), F("OK"));
}

// handleSaveRS: saves serial port settings AND re-initializes RS485
void handleSaveRS() {
  if (!requireAuth()) return;
  rs485Config.baud     = server.arg("baud").toInt();
  rs485Config.stopBits = server.arg("stopBits").toInt();
  rs485Config.dataBits = server.arg("dataBits").toInt();
  strlcpy(rs485Config.parity, server.arg("parity").c_str(), sizeof(rs485Config.parity));
  strlcpy(rs485Config.addr,   server.arg("addr").c_str(),   sizeof(rs485Config.addr));
  saveConfig();
  // Re-init RS485 with new port settings (needs mutex so task doesn't mid-poll)
  if (xSemaphoreTake(rs485Mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    initRS485();
    xSemaphoreGive(rs485Mutex);
  } else {
    initRS485(); // take anyway if mutex unavailable (startup path)
  }
  server.send(200, F("text/plain"), F("OK"));
}

// ── GET /getRS485Regs — returns register config + latest live values ──────────
void handleGetRS485Regs() {
  if (!requireAuth()) return;
  // Build JSON from register array (take mutex briefly for lastVal/lastOk)
  String json = "[";
  if (xSemaphoreTake(rs485Mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    for (int i = 0; i < rs485RegCount; i++) {
      RS485Register& r = rs485Regs[i];
      if (i > 0) json += ",";
      char tmp[256];
      // Represent val as null when not yet polled or error
      char valStr[32];
      if (r.lastOk) {
        // Use enough precision for floats
        snprintf(valStr, sizeof(valStr), "%.6g", r.lastVal);
      } else {
        strcpy(valStr, "null");
      }
      snprintf(tmp, sizeof(tmp),
        "{\"name\":\"%s\",\"fc\":%d,\"addr\":%d,\"dt\":%d,\"scale\":%.6g"
        ",\"wr\":%s,\"en\":%s,\"ok\":%s,\"val\":%s}",
        r.name, r.fc, r.addr, r.dt, r.scale,
        r.wr ? "true" : "false",
        r.en ? "true" : "false",
        r.lastOk ? "true" : "false",
        valStr);
      json += tmp;
    }
    xSemaphoreGive(rs485Mutex);
  }
  json += "]";
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.send(200, F("application/json"), json);
}

// ── POST /saveRS485Regs — receives JSON array of register configs ─────────────
void handleSaveRS485Regs() {
  if (!requireAuth()) return;
  String body = server.arg("plain");
  if (body.length() == 0) { server.send(400, F("text/plain"), F("Empty body")); return; }

  StaticJsonDocument<4096> doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) { server.send(400, F("text/plain"), F("JSON error")); return; }

  if (xSemaphoreTake(rs485Mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    rs485RegCount = 0;
    for (JsonObject obj : doc.as<JsonArray>()) {
      if (rs485RegCount >= MAX_RS485_REGS) break;
      RS485Register& r = rs485Regs[rs485RegCount];
      strlcpy(r.name, obj["name"] | "Reg", sizeof(r.name));
      r.fc    = obj["fc"]    | 3;
      r.addr  = obj["addr"]  | 0;
      r.dt    = obj["dt"]    | 0;
      r.scale = obj["scale"] | 1.0f;
      r.wr    = obj["wr"]    | false;
      r.en    = obj["en"]    | true;
      r.lastOk  = false;
      r.lastVal = 0.0f;
      rs485RegCount++;
    }
    xSemaphoreGive(rs485Mutex);
  }
  saveRS485Regs();
  server.send(200, F("text/plain"), F("OK"));
}

// ── POST /writeRS485Reg — write a value to a writable register ───────────────
void handleWriteRS485Reg() {
  if (!requireAuth()) return;
  if (!rs485Initialized) { server.send(503, F("text/plain"), F("RS485 not initialized")); return; }

  int   idx = server.arg("idx").toInt();
  float val = server.arg("val").toFloat();

  if (idx < 0 || idx >= rs485RegCount) {
    server.send(400, F("text/plain"), F("Invalid index")); return;
  }

  RS485Register& r = rs485Regs[idx];
  if (!r.wr) { server.send(400, F("text/plain"), F("Register is read-only")); return; }
  if (r.fc != 1 && r.fc != 3) {
    server.send(400, F("text/plain"), F("Write only supported for FC01/FC03")); return;
  }

  uint8_t result = ModbusMaster::ku8MBInvalidCRC;

  if (xSemaphoreTake(rs485Mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
    if (r.fc == 1) {
      result = modbusNode.writeSingleCoil(r.addr, (uint16_t)(val != 0.0f ? 0xFF00 : 0x0000));
    } else { // FC03 holding register
      switch (r.dt) {
        case 0: result = modbusNode.writeSingleRegister(r.addr, (uint16_t)val);        break;
        case 1: result = modbusNode.writeSingleRegister(r.addr, (uint16_t)(int16_t)val); break;
        case 3: result = modbusNode.writeSingleRegister(r.addr, (uint16_t)(val / (r.scale != 0 ? r.scale : 1.0f))); break;
        case 2: {
          // FLOAT32 = 2 registers
          float fv = val;
          uint32_t raw; memcpy(&raw, &fv, sizeof(float));
          modbusNode.setTransmitBuffer(0, (uint16_t)(raw >> 16));
          modbusNode.setTransmitBuffer(1, (uint16_t)(raw & 0xFFFF));
          result = modbusNode.writeMultipleRegisters(r.addr, 2);
          break;
        }
        default: result = modbusNode.writeSingleRegister(r.addr, (uint16_t)val); break;
      }
    }
    xSemaphoreGive(rs485Mutex);
  } else {
    server.send(503, F("text/plain"), F("RS485 busy")); return;
  }

  if (result == ModbusMaster::ku8MBSuccess) {
    server.send(200, F("text/plain"), F("OK"));
  } else {
    char msg[32];
    snprintf(msg, sizeof(msg), "Modbus error 0x%02X", result);
    server.send(500, F("text/plain"), msg);
  }
}

void handleSetCustomEff() {
  if (!requireAuth()) return;
  String v = server.arg("eff");
  customEffPct = v.length() > 0 ? v.toFloat() : -1.0f;
  server.send(200, F("text/plain"), F("OK"));
}

// ================= TELEGRAM API HANDLERS =================
void handleGetTelegramConfig() {
  if (!requireAuth()) return;
  StaticJsonDocument<256> doc;
  bool hasToken = strlen(tgConfig.botToken) > 8;
  doc["tokenMasked"]     = hasToken ? String(tgConfig.botToken).substring(0,8)+"••••••••" : "";
  doc["hasToken"]        = hasToken;
  doc["chatId"]          = tgConfig.chatId;
  doc["enabled"]         = tgConfig.enabled;
  doc["rejectThreshold"] = tgConfig.rejectThreshold;
  doc["botReady"]        = tgInitialized;
  String out; serializeJson(doc, out);
  server.send(200, F("application/json"), out);
}

void handleSaveTelegramConfig() {
  if (!requireAuth()) return;
  String token = server.arg("botToken"), chatId = server.arg("chatId");
  String en    = server.arg("enabled"),  thresh = server.arg("rejectThreshold");
  if (token.length() > 8 && !token.endsWith("••••••••"))
    strlcpy(tgConfig.botToken, token.c_str(), sizeof(tgConfig.botToken));
  if (chatId.length() > 0) strlcpy(tgConfig.chatId, chatId.c_str(), sizeof(tgConfig.chatId));
  tgConfig.enabled = (en == "1" || en == "true");
  if (thresh.length() > 0) tgConfig.rejectThreshold = constrain(thresh.toInt(), 0, 9999);
  saveTelegramConfig();
  initTelegramBot();
  server.send(200, F("text/plain"), F("OK"));
}

void handleTelegramTest() {
  if (!requireAuth()) return;
  if (!tgInitialized || !tgConfig.enabled || strlen(tgConfig.chatId) == 0) {
    server.send(400, F("text/plain"), F("Bot not configured or disabled")); return;
  }
  sdLogRow();
  saveConfig();
  saveTelegramConfig();
  saveMetrics();
  tgSend(F("\xF0\x9F\x9F\xA2 *Test OK!* All configs saved. Bot is live."));
  server.send(200, F("text/plain"), F("Queued - check Telegram in ~3 seconds"));
}

// ================= SD CARD HTTP HANDLERS =================
void handleSDInfo() {
  if (!requireAuth()) return;
  StaticJsonDocument<256> doc;
  doc["mounted"]              = sdMounted;
  doc["logEnabled"]           = sdLogEnabled;
  doc["logIntervalSec"]       = sdLogIntervalSec;
  doc["logRotateIntervalSec"] = logRotateIntervalSec;
  String clf = String(currentLogFile);
  if (clf.startsWith("/")) clf = clf.substring(1);
  doc["currentLogFile"] = clf;
  if (sdMounted) {
    uint8_t t = SD_MMC.cardType();
    doc["cardType"] = (t==CARD_MMC)?"MMC":(t==CARD_SD)?"SDSC":(t==CARD_SDHC)?"SDHC":"?";
    doc["totalMB"]  = (uint32_t)(SD_MMC.cardSize()/(1024*1024));
    doc["usedMB"]   = (uint32_t)(SD_MMC.usedBytes()/(1024*1024));
    doc["freeMB"]   = (uint32_t)((SD_MMC.cardSize()-SD_MMC.usedBytes())/(1024*1024));
  }
  String out; serializeJson(doc, out);
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.send(200, F("application/json"), out);
}

void handleSDFiles() {
  if (!requireAuth()) return;
  if (!sdMounted) { server.send(503, F("application/json"), F("{\"error\":\"SD not mounted\"}")); return; }
  String json = "[";
  File root = SD_MMC.open("/");
  if (root) {
    File entry = root.openNextFile(); bool first = true;
    while (entry) {
      if (!entry.isDirectory()) {
        if (!first) json += ',';
        String nm = String(entry.name());
        if (nm.startsWith("/")) nm = nm.substring(1);
        char ts[20] = "--";
        time_t lw = entry.getLastWrite();
        if (lw > 1577836800L) { struct tm* t = localtime(&lw); strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", t); }
        json += "{\"name\":\""+nm+"\",\"size\":"+String(entry.size())+",\"lastWrite\":\""+String(ts)+"\"}";
        first = false;
      }
      entry = root.openNextFile();
    }
    root.close();
  }
  json += ']';
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.send(200, F("application/json"), json);
}

void handleSDRead() {
  if (!requireAuth()) return;
  if (!sdMounted) { server.send(503, F("text/plain"), F("SD not mounted")); return; }
  String fname = "/" + server.arg("file");
  File f = SD_MMC.open(fname.c_str(), FILE_READ);
  if (!f) { server.send(404, F("text/plain"), F("Not found")); return; }
  size_t toRead = min((size_t)f.size(), (size_t)16384);
  String content = ""; content.reserve(toRead+1);
  for (size_t i = 0; i < toRead; i++) content += (char)f.read();
  f.close();
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.send(200, F("text/plain"), content);
}

void handleSDDownload() {
  if (!requireAuth()) return;
  if (!sdMounted) { server.send(503, F("text/plain"), F("SD not mounted")); return; }
  String fname = "/" + server.arg("file");
  File f = SD_MMC.open(fname.c_str(), FILE_READ);
  if (!f) { server.send(404, F("text/plain"), F("Not found")); return; }
  server.sendHeader(F("Content-Disposition"), "attachment; filename=\"" + server.arg("file") + "\"");
  server.sendHeader(F("Cache-Control"), F("no-cache"));
  server.streamFile(f, F("text/csv"));
  f.close();
}

void handleSDDelete() {
  if (!requireAuth()) return;
  if (!sdMounted) { server.send(503, F("text/plain"), F("SD not mounted")); return; }
  String fname = "/" + server.arg("file");
  bool ok = SD_MMC.remove(fname.c_str());
  server.send(ok ? 200 : 500, F("text/plain"), ok ? F("Deleted") : F("Delete failed"));
}

void handleSDClearAllLogs() {
  if (!requireAuth()) return;
  if (!sdMounted) { server.send(503, F("text/plain"), F("SD not mounted")); return; }
  int deleted = 0;
  File root = SD_MMC.open("/");
  if (root) {
    File entry = root.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        String nm = "/" + String(entry.name());
        if (strcmp(nm.c_str(), currentLogFile) != 0) {
          entry.close();
          SD_MMC.remove(nm.c_str());
          deleted++;
          entry = root.openNextFile();
          continue;
        }
      }
      entry = root.openNextFile();
    }
    root.close();
  }
  char msg[40];
  snprintf(msg, sizeof(msg), "Deleted %d file(s)", deleted);
  server.send(200, F("text/plain"), msg);
}

void handleSDSnapshot() {
  if (!requireAuth()) return;
  sdLogRow();
  server.send(200, F("text/plain"), F("Snapshot written"));
}

void handleSDLogConfig() {
  if (!requireAuth()) return;
  if (server.method() == HTTP_POST) {
    String iv = server.arg("interval"), en = server.arg("enabled"), ri = server.arg("rotateInterval");
    if (iv.length() > 0) { int v = iv.toInt(); if (v >= 5 && v <= 3600) sdLogIntervalSec = v; }
    if (en.length() > 0) sdLogEnabled = (en == "1" || en == "true");
    if (ri.length() > 0) { long v = ri.toInt(); if (v >= 60 && v <= 86400) logRotateIntervalSec = (unsigned long)v; }
    saveConfig();
    server.send(200, F("text/plain"), F("OK"));
  } else {
    StaticJsonDocument<96> doc;
    doc["interval"] = sdLogIntervalSec; doc["enabled"] = sdLogEnabled; doc["rotateInterval"] = logRotateIntervalSec;
    String out; serializeJson(doc, out);
    server.send(200, F("application/json"), out);
  }
}

void handleSDClearLog() {
  if (!requireAuth()) return;
  if (!sdMounted) { server.send(503, F("text/plain"), F("SD not mounted")); return; }
  if (currentLogFile[0] != '\0') SD_MMC.remove(currentLogFile);
  initLogFile();
  server.send(200, F("text/plain"), F("Log cleared"));
}

// ================= NETWORK EVENT =================
void WiFiEvent(WiFiEvent_t event) {
  if (event == ARDUINO_EVENT_ETH_GOT_IP) {
    eth_connected = true;
    Serial.print(F("ETH IP: ")); Serial.println(ETH.localIP());
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  Serial.println(F("Booting ESP32..."));

  for (int i = 0; i < 8; i++) {
    pinMode(inputPins[i], INPUT_PULLUP);
    lastRawState[i]  = HIGH;
    debounceTimer[i] = 0;
  }

  Wire.begin(42, 41);
  writeOutputs();

  // RS485 DE/RE pin — low = receive mode at startup
  pinMode(RS485_DE, OUTPUT);
  digitalWrite(RS485_DE, LOW);

  if (!LittleFS.begin(true)) Serial.println(F("LittleFS mount failed!"));
  else                       Serial.println(F("LittleFS OK"));

  loadConfig();
  loadUsers();
  loadTelegramConfig();
  loadMetrics();
  loadRS485Regs();

  // ── SD Card ──
  pinMode(NET_SCS, OUTPUT);
  digitalWrite(NET_SCS, HIGH);
  if (!SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0)) {
    Serial.println(F("SD pin map failed"));
  } else if (!SD_MMC.begin("/sdcard", true, false, 10000)) {
    Serial.println(F("SD mount failed"));
  } else {
    sdMounted = true;
    Serial.println(F("SD mounted OK"));
  }

  // ── Network ──
  WiFi.onEvent(WiFiEvent);
  ETH.begin();
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  unsigned long cs = millis();
  while (!eth_connected && WiFi.status() != WL_CONNECTED) {
    if (millis() - cs > 15000) break;
    Serial.print(F(".")); delay(200);
  }
  Serial.println();
  if (eth_connected) {
    Serial.print(F("Network: Ethernet, IP: ")); Serial.println(ETH.localIP());
  } else if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Network: WiFi, IP: ")); Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("No network"));
  }

  lastMetricTickMs  = millis();
  lastMetricsSaveMs = millis();

  // ── NTP (Malaysia UTC+8) ──
  configTime(8*3600, 0, "pool.ntp.org", "time.cloudflare.com");
  struct tm ntpCheck;
  for (int i = 0; i < 5; i++) { if (getLocalTime(&ntpCheck, 1000)) break; }

  if (sdMounted) initLogFile();

  // ── RS485 init ──
  initRS485();

  // ── MQTT transport ──
  applyMqttTransport();

  // ── FreeRTOS ──
  ioMutex    = xSemaphoreCreateMutex();
  rs485Mutex = xSemaphoreCreateMutex();
  tgOutQueue = xQueueCreate(6, 256);
  initTelegramBot();
  xTaskCreatePinnedToCore(telegramTask, "TGTask",    12288, NULL, 1, &tgTaskHandle,   0);
  xTaskCreatePinnedToCore(rs485Task,    "RS485Task",  4096, NULL, 1, &rs485TaskHandle, 0);

  // ── HTTP routes ──
  const char* headerKeys[] = {"Cookie"};
  server.collectHeaders(headerKeys, 1);

  server.on("/login",  handleLogin);
  server.on("/logout", handleLogout);
  server.on("/style.css",      HTTP_GET, []() { serveStatic("/style.css",      "text/css"); });
  server.on("/shrdc_logo.png", HTTP_GET, []() { serveStatic("/shrdc_logo.png", "image/png"); });
  server.on("/msf_logo.png",   HTTP_GET, []() { serveStatic("/msf_logo.png",   "image/png"); });

  server.on("/",           HTTP_GET, []() { if (!requireAuth()) return; serveStatic("/index.html",      "text/html", true); });
  server.on("/monitoring", HTTP_GET, []() { if (!requireAuth()) return; serveStatic("/monitoring.html", "text/html", true); });
  server.on("/mqtt",       HTTP_GET, []() { if (!requireAuth()) return; serveStatic("/mqtt.html",       "text/html", true); });
  server.on("/rs485",      HTTP_GET, []() { if (!requireAuth()) return; serveStatic("/rs485.html",      "text/html", true); });
  server.on("/sdcard",     HTTP_GET, []() { if (!requireAuth()) return; serveStatic("/sdcard.html",     "text/html", true); });
  server.on("/telegram",   HTTP_GET, []() { if (!requireAuth()) return; serveStatic("/telegram.html",   "text/html", true); });
  server.on("/variables.json", HTTP_GET, []() { if (!requireAuth()) return; serveStatic("/variables.json", "application/json"); });

  server.on("/toggle",       handleToggle);
  server.on("/status",       handleStatus);
  server.on("/getConfig",    handleGetConfig);
  server.on("/getRS485",     handleGetRS485);
  server.on("/getMetrics",   handleGetMetrics);
  server.on("/getHealth",    handleGetHealth);
  server.on("/save",         HTTP_POST, handleSave);
  server.on("/saveRS",       HTTP_POST, handleSaveRS);
  server.on("/resetMetrics", HTTP_POST, handleResetMetrics);

  // ── RS485 Modbus register routes (new) ──
  server.on("/getRS485Regs",  HTTP_GET,  handleGetRS485Regs);
  server.on("/saveRS485Regs", HTTP_POST, handleSaveRS485Regs);
  server.on("/writeRS485Reg", HTTP_POST, handleWriteRS485Reg);

  server.on("/sdInfo",         HTTP_GET,  handleSDInfo);
  server.on("/sdFiles",        HTTP_GET,  handleSDFiles);
  server.on("/sdRead",         HTTP_GET,  handleSDRead);
  server.on("/sdDownload",     HTTP_GET,  handleSDDownload);
  server.on("/sdDelete",       HTTP_POST, handleSDDelete);
  server.on("/sdSnapshot",     HTTP_POST, handleSDSnapshot);
  server.on("/sdLogConfig",    handleSDLogConfig);
  server.on("/sdClearLog",     HTTP_POST, handleSDClearLog);
  server.on("/sdClearAllLogs", HTTP_POST, handleSDClearAllLogs);

  server.on("/setCustomEff",       HTTP_POST, handleSetCustomEff);
  server.on("/getTelegramConfig",  HTTP_GET,  handleGetTelegramConfig);
  server.on("/saveTelegramConfig", HTTP_POST, handleSaveTelegramConfig);
  server.on("/telegramTest",       HTTP_POST, handleTelegramTest);

  server.begin();
  Serial.println(F("HTTP server started"));

  String ip = eth_connected ? ETH.localIP().toString() : WiFi.localIP().toString();
  tgSend("\xF0\x9F\x9A\x80 *ESP32 Online*\nIP: `" + ip + "`");
}

// ================= LOOP =================
void loop() {
  server.handleClient();

  if (strcmp(mqttCfg.mode, "cloud") == 0) wsClient.loop();
  mqtt.loop();

  if (!mqtt.connected()) {
    unsigned long now = millis();
    if (now - lastMqttRetry > mqttRetryInterval) { lastMqttRetry = now; reconnectMQTT(); }
  }

  unsigned long now = millis();

  // Cross-task reset flag from Telegram /resetmetrics
  if (tgResetMetricsFlag) {
    tgResetMetricsFlag = false;
    for (int i = 0; i < 8; i++) {
      triggerCount[i] = 0; totalOnTimeMs[i] = 0;
      if (diCurrentlyOn[i]) lastOnStartMs[i] = now;
    }
    rejectCount = 0; runtimeMs = 0; downtimeMs = 0;
    lastCycleTimeMs = 0; lastMetricTickMs = now;
    saveMetrics();
  }

  // Runtime / Downtime accumulation
  if (now - lastMetricAccMs >= 100) {
    unsigned long elapsed = now - lastMetricTickMs;
    if      (machineStateIdx == 1) runtimeMs  += elapsed;
    else if (machineStateIdx == 2) downtimeMs += elapsed;
    lastMetricTickMs = now;
    lastMetricAccMs  = now;
  }

  // Periodic metrics save (every 5 minutes)
  if (now - lastMetricsSaveMs >= 300000UL) {
    lastMetricsSaveMs = now;
    saveMetrics();
  }

  // SD card log rotation
  if (sdMounted && currentLogFile[0] != '\0') {
    if (now - logFileStartMs >= logRotateIntervalSec * 1000UL) initLogFile();
  }

  // SD card periodic logging
  if (sdMounted && sdLogEnabled) {
    if (now - lastSdLogMs >= (unsigned long)sdLogIntervalSec * 1000UL) {
      lastSdLogMs = now; sdLogRow();
    }
  }

  // Digital Input scanning every 5 ms
  if (now - lastDiScanMs < 5) return;
  lastDiScanMs = now;

  for (int i = 0; i < 8; i++) {
    int raw = (i == 0) ? !digitalRead(inputPins[i]) : digitalRead(inputPins[i]);
    if (raw != lastRawState[i]) { lastRawState[i] = raw; debounceTimer[i] = now; }
    if ((now - debounceTimer[i]) >= DEBOUNCE_MS) {
      int logical = (raw == LOW) ? 1 : 0;
      if (logical != (int)diCurrentlyOn[i]) {
        diCurrentlyOn[i] = (logical == 1);
        if (logical == 1) {
          triggerCount[i]++;
          lastOnStartMs[i] = now;
          if (i == 3) { cycleStartMs = now; cycleActive = true; }
          if (i == 5 && cycleActive) { lastCycleTimeMs = now - cycleStartMs; cycleActive = false; }
          if (i == 6) {
            rejectCount++;
            if (tgConfig.rejectThreshold > 0 &&
                (rejectCount % (unsigned long)tgConfig.rejectThreshold == 0))
              tgSend("\xE2\x9A\xA0\xEF\xB8\x8F *Reject Alert!* Total: *" + String(rejectCount) + "*");
          }
        } else {
          totalOnTimeMs[i] += now - lastOnStartMs[i];
        }
        autoUpdateMachineState(i, logical);
        if (strlen(inputConfig[i].topic) > 0 && mqtt.connected())
          mqtt.publish(inputConfig[i].topic, logical ? "1" : "0");
      }
    }
  }
}
