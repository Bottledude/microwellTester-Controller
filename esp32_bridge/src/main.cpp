/**
 * ESP32-S3 N16R8 WiFi bridge for the H743 coil controller.
 *
 * SoftAP (default CoilControl / coilcontrol) serves an interactive webpage.
 * Commands are forwarded over UART to STM32 USART3; Serial Monitor on
 * STM32 USART1 continues to work in parallel.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

#include "config.h"
#include "www_page.h"

WebServer server(80);
HardwareSerial StmSerial(1);

static String g_last_raw;
static String g_last_json_line;
static SemaphoreHandle_t g_uart_mu;

// #region agent log
/* In-memory NDJSON-ish debug ring; exposed via /api/status and /api/debug for host ingest */
static const int DBG_RING = 24;
static String g_dbg[DBG_RING];
static int g_dbg_w = 0;
static int g_dbg_n = 0;

static void dbgLog(const char *hypothesisId, const char *location, const char *message, const String &dataJson)
{
  char ts[16];
  snprintf(ts, sizeof(ts), "%lu", (unsigned long)millis());
  String line = "{";
  line += "\"sessionId\":\"07440e\",";
  line += "\"hypothesisId\":\"";
  line += hypothesisId;
  line += "\",\"location\":\"";
  line += location;
  line += "\",\"message\":\"";
  line += message;
  line += "\",\"timestamp\":";
  line += ts;
  line += ",\"data\":";
  line += dataJson.length() ? dataJson : "{}";
  line += "}";
  g_dbg[g_dbg_w] = line;
  g_dbg_w = (g_dbg_w + 1) % DBG_RING;
  if (g_dbg_n < DBG_RING) g_dbg_n++;
  Serial.print("DBG|");
  Serial.println(line);
}

static void dbgAttachTo(JsonDocument &resp)
{
  JsonArray arr = resp["_dbg"].as<JsonArray>();
  int start = (g_dbg_n < DBG_RING) ? 0 : g_dbg_w;
  for (int i = 0; i < g_dbg_n; i++) {
    arr.add(g_dbg[(start + i) % DBG_RING]);
  }
}
// #endregion

static void drainStmInput(uint32_t ms)
{
  uint32_t start = millis();
  while (millis() - start < ms) {
    while (StmSerial.available()) {
      (void)StmSerial.read();
    }
    delay(1);
  }
}

/** Fraction of printable ASCII (incl. CR/LF/TAB) in buffer, 0..100. */
static int asciiScore(const String &s)
{
  if (s.length() == 0) return 0;
  int ok = 0;
  for (unsigned i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c < 127)) ok++;
  }
  return (ok * 100) / (int)s.length();
}

/** Listen without transmitting — floating RX + WiFi often yields binary noise. */
static int uartNoiseProbe(uint32_t ms)
{
  drainStmInput(2);
  String acc;
  uint32_t start = millis();
  while (millis() - start < ms) {
    while (StmSerial.available()) {
      acc += (char)StmSerial.read();
      if (acc.length() > 256) break;
    }
    delay(1);
  }
  // #region agent log
  {
    String d = "{";
    d += "\"noiseLen\":" + String(acc.length()) + ",";
    d += "\"asciiPct\":" + String(asciiScore(acc));
    d += "}";
    dbgLog("F", "main.cpp:uartNoiseProbe", "rx_idle_sample", d);
  }
  // #endregion
  return (int)acc.length();
}

/** Send a CLI line to the STM32 and capture text until idle or timeout. */
static String stm32Transaction(const String &cmd, uint32_t timeout_ms = STM32_CMD_TIMEOUT_MS)
{
  if (g_uart_mu) xSemaphoreTake(g_uart_mu, portMAX_DELAY);

  /* Drain any late bytes from a previous truncated JSON poll */
  drainStmInput(30);
  StmSerial.print(cmd);
  if (!cmd.endsWith("\n")) StmSerial.print("\r\n");
  StmSerial.flush();

  const bool wantJson = (cmd.indexOf("json") >= 0);
  const uint32_t idle_ms = wantJson ? 120U : 80U;

  String acc;
  uint32_t start = millis();
  uint32_t last_rx = start;
  while (millis() - start < timeout_ms) {
    while (StmSerial.available()) {
      char c = (char)StmSerial.read();
      acc += c;
      last_rx = millis();
      if (acc.length() > 4096) acc.remove(0, acc.length() - 3072);
    }
    if (acc.length() > 0 && (millis() - last_rx) > idle_ms) {
      /* For json, require closing brace before accepting idle end */
      if (wantJson && acc.indexOf('}') < 0) {
        /* keep waiting */
      } else {
        break;
      }
    }
    /* Abort early if buffer is clearly line noise, not UART text */
    if (!wantJson && acc.length() >= 24 && asciiScore(acc) < 40) break;
    delay(1);
  }

  uint32_t elapsed = millis() - start;
  if (g_uart_mu) xSemaphoreGive(g_uart_mu);

  int score = asciiScore(acc);
  g_last_raw = acc;
  bool foundJson = false;
  /* Prefer a line that looks like JSON status. */
  int from = 0;
  while (from >= 0 && from < (int)acc.length()) {
    int nl = acc.indexOf('\n', from);
    String line = (nl < 0) ? acc.substring(from) : acc.substring(from, nl);
    line.trim();
    if (line.startsWith("{") && line.indexOf("\"coils\"") >= 0 && line.endsWith("}")) {
      g_last_json_line = line;
      foundJson = true;
      break;
    }
    /* Also accept single-line JSON without requiring trailing newline trim quirks */
    if (line.startsWith("{") && line.indexOf("\"coils\"") >= 0) {
      int end = line.lastIndexOf('}');
      if (end > 0) {
        g_last_json_line = line.substring(0, end + 1);
        foundJson = true;
        break;
      }
    }
    if (nl < 0) break;
    from = nl + 1;
  }

  // #region agent log
  {
    String d = "{";
    d += "\"cmd\":\"" + cmd + "\",";
    d += "\"rxLen\":" + String(acc.length()) + ",";
    d += "\"elapsedMs\":" + String(elapsed) + ",";
    d += "\"asciiPct\":" + String(score) + ",";
    d += "\"foundJson\":" + String(foundJson ? "true" : "false") + ",";
    d += "\"idleMs\":" + String(idle_ms) + ",";
    d += "\"rawHead\":\"";
    for (unsigned i = 0; i < acc.length() && i < 80; i++) {
      char c = acc[i];
      if (c == '"' || c == '\\') d += '\\';
      if (c >= 32 && c < 127) d += c;
      else d += '.';
    }
    d += "\"}";
    const char *hid = (acc.length() == 0) ? "B" : (score < 70 ? "F" : (foundJson ? "Cok" : "C"));
    dbgLog(hid, "main.cpp:stm32Transaction", "uart_txn", d);
  }
  // #endregion

  return acc;
}

static bool parseStatusJson(const String &line, JsonDocument &doc)
{
  if (line.length() < 8) return false;
  DeserializationError err = deserializeJson(doc, line);
  if (err) return false;
  if (!doc["coils"].is<JsonArray>()) return false;
  /* Accept ok as int 1 or bool true */
  return doc["ok"].as<int>() == 1 || doc["ok"].as<bool>();
}

/** Cheap link check — does not depend on JSON/ADC. */
static bool pingLink()
{
  String raw = stm32Transaction("ping", 400);
  bool ok = (asciiScore(raw) >= 70) && (raw.indexOf("PONG") >= 0);
  // #region agent log
  dbgLog(ok ? "Hok" : "H", "main.cpp:pingLink", "ping",
         String("{\"ok\":") + (ok ? "true" : "false") +
             ",\"asciiPct\":" + String(asciiScore(raw)) +
             ",\"rxLen\":" + String(raw.length()) + "}");
  // #endregion
  return ok;
}

static bool refreshStatus(JsonDocument &doc)
{
  // #region agent log
  uint32_t t0 = millis();
  // #endregion
  g_last_json_line = ""; /* do not reuse a stale line */
  stm32Transaction("json");
  if (asciiScore(g_last_raw) < 70) {
    // #region agent log
    dbgLog("F", "main.cpp:refreshStatus", "status_reject_noise",
           String("{\"elapsedMs\":") + String(millis() - t0) +
               ",\"asciiPct\":" + String(asciiScore(g_last_raw)) + "}");
    // #endregion
    return false;
  }
  if (parseStatusJson(g_last_json_line, doc)) {
    // #region agent log
    dbgLog("D", "main.cpp:refreshStatus", "status_ok",
           String("{\"elapsedMs\":") + String(millis() - t0) + ",\"retry\":false}");
    // #endregion
    return true;
  }
  /* Retry once in case of framing noise */
  g_last_json_line = "";
  stm32Transaction("json");
  bool ok = (asciiScore(g_last_raw) >= 70) && parseStatusJson(g_last_json_line, doc);
  // #region agent log
  dbgLog(ok ? "D" : "C", "main.cpp:refreshStatus", ok ? "status_ok_retry" : "status_fail",
         String("{\"elapsedMs\":") + String(millis() - t0) + ",\"retry\":true,\"rawLen\":" +
             String(g_last_raw.length()) + ",\"asciiPct\":" + String(asciiScore(g_last_raw)) +
             ",\"jsonLen\":" + String(g_last_json_line.length()) + "}");
  // #endregion
  return ok;
}

static void sendJson(JsonDocument &doc, int code = 200)
{
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void handleRoot()
{
  // #region agent log
  dbgLog("E", "main.cpp:handleRoot", "serve_index", "{\"ok\":true}");
  // #endregion
  server.send_P(200, "text/html", WWW_PAGE);
}

static void handleDebug()
{
  JsonDocument resp;
  resp["ok"] = true;
  dbgAttachTo(resp);
  sendJson(resp);
}

static void handleStatus()
{
  JsonDocument status;
  JsonDocument resp;

  /* Link = ping heartbeat (stable). Coil fields = optional json snapshot. */
  bool linked = pingLink();
  bool haveCoils = false;
  if (linked) {
    haveCoils = refreshStatus(status);
  }
  resp["ok"] = linked;
  resp["linked"] = linked;
  if (haveCoils) resp["status"] = status;
  else if (g_last_raw.length()) resp["raw"] = g_last_raw;

  // #region agent log
  dbgAttachTo(resp);
  dbgLog(linked ? "Hok" : "H", "main.cpp:handleStatus", "http_status",
         String("{\"linked\":") + (linked ? "true" : "false") +
             ",\"haveCoils\":" + (haveCoils ? "true" : "false") +
             ",\"rawLen\":" + String(g_last_raw.length()) + "}");
  // #endregion
  sendJson(resp, 200);
}

static void handleCmd()
{
  // #region agent log
  {
    String d = "{";
    d += "\"method\":" + String((int)server.method()) + ",";
    d += "\"hasPlain\":" + String(server.hasArg("plain") ? "true" : "false") + ",";
    d += "\"plainLen\":" + String(server.hasArg("plain") ? server.arg("plain").length() : 0);
    d += "}";
    dbgLog("A", "main.cpp:handleCmd:entry", "cmd_request", d);
  }
  // #endregion

  if (!server.hasArg("plain") && server.method() != HTTP_POST) {
    // #region agent log
    dbgLog("A", "main.cpp:handleCmd", "reject_405", "{}");
    // #endregion
    server.send(405, "text/plain", "POST JSON {\"cmd\":\"...\"}");
    return;
  }

  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err || !body["cmd"].is<const char *>()) {
    // #region agent log
    {
      String d = "{";
      d += "\"parseErr\":\"";
      d += err.c_str();
      d += "\",\"hasCmd\":";
      d += body["cmd"].isNull() ? "false" : "true";
      d += ",\"isConstChar\":";
      d += body["cmd"].is<const char *>() ? "true" : "false";
      d += "}";
      dbgLog("A", "main.cpp:handleCmd", "reject_400", d);
    }
    // #endregion
    server.send(400, "text/plain", "Need JSON {\"cmd\":\"drive M1 128 +\"}");
    return;
  }

  String cmd = body["cmd"].as<String>();
  cmd.trim();
  String raw = stm32Transaction(cmd);
  int score = asciiScore(raw);

  JsonDocument resp;
  resp["ok"] = (score >= 70);
  resp["cmd"] = cmd;
  resp["asciiPct"] = score;
  /* Only expose printable preview to the UI (avoid binary spam). */
  {
    String preview;
    for (unsigned i = 0; i < raw.length() && preview.length() < 160; i++) {
      char c = raw[i];
      if (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c < 127)) preview += c;
      else preview += '.';
    }
    resp["raw"] = preview;
  }

  /* After a control command, pull fresh JSON status for the UI. */
  JsonDocument status;
  if (score >= 70 && refreshStatus(status)) {
    resp["status"] = status;
  }
  // #region agent log
  dbgAttachTo(resp);
  // #endregion
  sendJson(resp, 200);
}

static void handleNotFound()
{
  server.send(404, "text/plain", "Not found");
}

static void startWifi()
{
  WiFi.mode(WIFI_AP_STA);
  bool ap = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS, WIFI_AP_CHANNEL);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("SoftAP %s  %s  ip=%s\n",
                ap ? "OK" : "FAIL", WIFI_AP_SSID, ip.toString().c_str());
  // #region agent log
  {
    String d = "{";
    d += "\"apOk\":";
    d += ap ? "true" : "false";
    d += ",\"ip\":\"";
    d += ip.toString();
    d += "\",\"ssid\":\"";
    d += WIFI_AP_SSID;
    d += "\"}";
    dbgLog("E", "main.cpp:startWifi", "softap", d);
  }
  // #endregion

  if (WIFI_STA_SSID[0] != '\0') {
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
    Serial.printf("STA joining %s …\n", WIFI_STA_SSID);
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 12000) {
      delay(250);
      Serial.print('.');
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("STA IP %s\n", WiFi.localIP().toString().c_str());
    } else {
      Serial.println("STA not connected (SoftAP still active)");
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("\nESP32-S3 coil WiFi bridge");

  g_uart_mu = xSemaphoreCreateMutex();

  /* UART1 on GPIO17/18 (DevKitC U1TXD/U1RXD). RX pull-up fights floating-line WiFi noise. */
  pinMode(PIN_STM32_RX, INPUT_PULLUP);
  StmSerial.setRxBufferSize(1024);
  StmSerial.setTimeout(20);
  StmSerial.begin(STM32_UART_BAUD, SERIAL_8N1, PIN_STM32_RX, PIN_STM32_TX);
  delay(50);
  drainStmInput(20);
  // #region agent log
  {
    String d = "{";
    d += "\"baud\":" + String(STM32_UART_BAUD) + ",";
    d += "\"rxPin\":" + String(PIN_STM32_RX) + ",";
    d += "\"txPin\":" + String(PIN_STM32_TX);
    d += "}";
    dbgLog("B", "main.cpp:setup", "uart_begin", d);
  }
  uartNoiseProbe(80);
  // #endregion

  startWifi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/cmd", HTTP_POST, handleCmd);
  server.on("/api/debug", HTTP_GET, handleDebug);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server on :80");

  /* Nudge the STM32 bridge port */
  stm32Transaction("json");
  // #region agent log
  dbgLog("B", "main.cpp:setup", "boot_json_probe",
         String("{\"rawLen\":") + String(g_last_raw.length()) +
             ",\"jsonLen\":" + String(g_last_json_line.length()) +
             ",\"asciiPct\":" + String(asciiScore(g_last_raw)) + "}");
  // #endregion
}

void loop()
{
  server.handleClient();
  delay(2);
}
