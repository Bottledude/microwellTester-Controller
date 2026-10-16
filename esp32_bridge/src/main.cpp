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

/** Send a CLI line to the STM32 and capture text until idle or timeout. */
static String stm32TransactionOnce(const String &cmd, uint32_t timeout_ms)
{
  drainStmInput(15);
  StmSerial.print(cmd);
  if (!cmd.endsWith("\n")) StmSerial.print("\r\n");
  StmSerial.flush();

  const bool wantJson = (cmd.indexOf("json") >= 0);
  const bool mlxCal = (cmd.indexOf("mlx offset") >= 0);
  const uint32_t idle_ms = wantJson ? 250U : (mlxCal ? 500U : 120U);

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
      if (wantJson && acc.indexOf('}') < 0) {
        /* keep waiting for closing brace */
      } else {
        break;
      }
    }
    if (!wantJson && acc.length() >= 24 && asciiScore(acc) < 40) {
      break;
    }
    delay(1);
  }
  return acc;
}

static String stm32Transaction(const String &cmd, uint32_t timeout_ms = STM32_CMD_TIMEOUT_MS)
{
  if (g_uart_mu) xSemaphoreTake(g_uart_mu, portMAX_DELAY);

  const bool wantJson = (cmd.indexOf("json") >= 0);
  String acc = stm32TransactionOnce(cmd, timeout_ms);
  int score = asciiScore(acc);

  /* Short CLI replies sometimes get lost under SoftAP load — retry once. */
  if (!wantJson && (acc.length() == 0 || score < 70)) {
    delay(5);
    acc = stm32TransactionOnce(cmd, timeout_ms);
    score = asciiScore(acc);
  }

  if (g_uart_mu) xSemaphoreGive(g_uart_mu);

  g_last_raw = acc;
  int from = 0;
  while (from >= 0 && from < (int)acc.length()) {
    int nl = acc.indexOf('\n', from);
    String line = (nl < 0) ? acc.substring(from) : acc.substring(from, nl);
    line.trim();
    if (line.startsWith("{") && line.indexOf("\"coils\"") >= 0 && line.endsWith("}")) {
      g_last_json_line = line;
      break;
    }
    if (line.startsWith("{") && line.indexOf("\"coils\"") >= 0) {
      int end = line.lastIndexOf('}');
      if (end > 0) {
        g_last_json_line = line.substring(0, end + 1);
        break;
      }
    }
    if (nl < 0) break;
    from = nl + 1;
  }

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

/** Cheap UART check — does not depend on JSON/ADC. */
static bool pingLink()
{
  String raw = stm32Transaction("ping", 400);
  return (asciiScore(raw) >= 70) && (raw.indexOf("PONG") >= 0);
}

static bool refreshStatus(JsonDocument &doc)
{
  g_last_json_line = ""; /* do not reuse a stale line */
  stm32Transaction("json");
  if (asciiScore(g_last_raw) < 70) return false;
  if (parseStatusJson(g_last_json_line, doc)) return true;
  /* Retry once in case of framing noise */
  g_last_json_line = "";
  stm32Transaction("json");
  return (asciiScore(g_last_raw) >= 70) && parseStatusJson(g_last_json_line, doc);
}

static void sendJson(JsonDocument &doc, int code = 200)
{
  String out;
  serializeJson(doc, out);
  server.send(code, "application/json", out);
}

static void handleRoot()
{
  server.send_P(200, "text/html", WWW_PAGE);
}

static void handleStatus()
{
  JsonDocument status;
  JsonDocument resp;

  /*
   * linked = have a parsed coil snapshot from json.
   * uart_ok = ping succeeded when json failed (UART up, status stale).
   */
  bool haveCoils = refreshStatus(status);
  bool uartOk = haveCoils;
  if (!uartOk) {
    uartOk = pingLink();
  }
  resp["ok"] = haveCoils;
  resp["linked"] = haveCoils;
  resp["uart_ok"] = uartOk;
  if (haveCoils) resp["status"] = status;
  else if (g_last_raw.length()) resp["raw"] = g_last_raw;

  sendJson(resp, 200);
}

static void handleCmd()
{
  if (!server.hasArg("plain") && server.method() != HTTP_POST) {
    server.send(405, "text/plain", "POST JSON {\"cmd\":\"...\"}");
    return;
  }

  JsonDocument body;
  DeserializationError err = deserializeJson(body, server.arg("plain"));
  if (err || !body["cmd"].is<const char *>()) {
    server.send(400, "text/plain", "Need JSON {\"cmd\":\"drive M1 128 +\"}");
    return;
  }

  String cmd = body["cmd"].as<String>();
  cmd.trim();
  /* One CLI line per request: an embedded CR/LF would run extra commands,
   * and the STM32 drops chars past CLI_LINE_MAX-1 (127), running a cut line. */
  if (cmd.length() == 0 || cmd.length() > 127 ||
      cmd.indexOf('\r') >= 0 || cmd.indexOf('\n') >= 0) {
    server.send(400, "text/plain", "cmd must be one line, 1..127 chars");
    return;
  }
  uint32_t to = STM32_CMD_TIMEOUT_MS;
  String raw = stm32Transaction(cmd, to);
  int score = asciiScore(raw);

  JsonDocument resp;
  resp["ok"] = (score >= 70);
  resp["cmd"] = cmd;
  resp["asciiPct"] = score;
  {
    String preview;
    for (unsigned i = 0; i < raw.length() && preview.length() < 160; i++) {
      char c = raw[i];
      if (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c < 127)) preview += c;
      else preview += '.';
    }
    resp["raw"] = preview;
  }

  /* Only refresh status after a clean reply — avoids stacking another long UART txn on failure. */
  JsonDocument status;
  if (score >= 70 && refreshStatus(status)) {
    resp["status"] = status;
    resp["linked"] = true;
  } else {
    resp["linked"] = false;
  }
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

  startWifi();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/cmd", HTTP_POST, handleCmd);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server on :80");

  /* Nudge the STM32 bridge port */
  stm32Transaction("json");
}

void loop()
{
  server.handleClient();
  delay(2);
}
