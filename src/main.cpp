/**
 * ESP32-WROOM-32 coil driver: PCA9685 + BTS7960 + MLX90393 + DualSense + web UI.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config.h"
#include "www_page.h"
#include "cli.h"
#include "coil_output.h"
#include "cmd_queue.h"
#include "field_engine.h"
#include "i2c_bus.h"
#include "mlx_app.h"
#include "pad.h"
#include "pca9685.h"
#include "serial_io.h"

WebServer server(80);
static SemaphoreHandle_t g_hw_mu;
static TaskHandle_t g_sample_task;
static TaskHandle_t g_flush_task;
static hw_timer_t *g_timer;

extern "C" void serial_write_bytes(const char *s)
{
  if (s) Serial.print(s);
}

extern "C" int serial_read_byte(void)
{
  if (!Serial.available()) return -1;
  return Serial.read();
}

static void lock_hw(void)
{
  if (g_hw_mu) xSemaphoreTake(g_hw_mu, portMAX_DELAY);
}

static void unlock_hw(void)
{
  if (g_hw_mu) xSemaphoreGive(g_hw_mu);
}

static void run_cli(const char *line, char *reply, size_t cap)
{
  lock_hw();
  CLI_Exec(line, reply, cap);
  unlock_hw();
}

static bool extract_json_line(const char *raw, String &out)
{
  if (!raw) return false;
  const char *p = strchr(raw, '{');
  if (!p) return false;
  const char *end = strrchr(p, '}');
  if (!end || end <= p) return false;
  out = String(p).substring(0, (int)(end - p + 1));
  return out.indexOf("\"coils\"") >= 0;
}

static void handleRoot()
{
  server.send_P(200, "text/html", WWW_PAGE);
}

static String json_escape(const String &s)
{
  String o;
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') {
      o += '\\';
      o += c;
    } else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else o += c;
  }
  return o;
}

static bool refreshStatusLine(String &line)
{
  char raw[4096];
  run_cli("json", raw, sizeof(raw));
  return extract_json_line(raw, line);
}

static void handleStatus()
{
  String status;
  bool ok = refreshStatusLine(status);
  String out = String("{\"ok\":") + (ok ? "true" : "false") +
               ",\"linked\":" + (ok ? "true" : "false");
  if (ok) {
    out += ",\"status\":";
    out += status;
  }
  out += "}";
  server.send(200, "application/json", out);
}

static String parse_cmd_json(const String &body)
{
  int k = body.indexOf("\"cmd\"");
  if (k < 0) return "";
  int colon = body.indexOf(':', k);
  if (colon < 0) return "";
  int q1 = body.indexOf('"', colon + 1);
  if (q1 < 0) return "";
  int q2 = q1 + 1;
  while (q2 < (int)body.length()) {
    if (body[q2] == '"' && body[q2 - 1] != '\\') break;
    q2++;
  }
  if (q2 >= (int)body.length()) return "";
  return body.substring(q1 + 1, q2);
}

static void handleCmd()
{
  if (!server.hasArg("plain") && server.method() != HTTP_POST) {
    server.send(405, "text/plain", "POST JSON {\"cmd\":\"...\"}");
    return;
  }

  String cmd = parse_cmd_json(server.arg("plain"));
  cmd.trim();
  if (cmd.length() == 0 || cmd.length() > 127 ||
      cmd.indexOf('\r') >= 0 || cmd.indexOf('\n') >= 0) {
    server.send(400, "text/plain", "cmd must be one line, 1..127 chars");
    return;
  }

  char raw[4096];
  run_cli(cmd.c_str(), raw, sizeof(raw));

  String preview;
  for (unsigned i = 0; raw[i] && preview.length() < 160; i++) {
    char c = raw[i];
    if (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c < 127)) preview += c;
    else preview += '.';
  }

  String status;
  bool linked = refreshStatusLine(status);
  String out = String("{\"ok\":true,\"cmd\":\"") + json_escape(cmd) +
               "\",\"raw\":\"" + json_escape(preview) + "\",\"linked\":" +
               (linked ? "true" : "false");
  if (linked) {
    out += ",\"status\":";
    out += status;
  }
  out += "}";
  server.send(200, "application/json", out);
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

static void IRAM_ATTR onSineTimer()
{
  BaseType_t woken = pdFALSE;
  if (g_sample_task) {
    vTaskNotifyGiveFromISR(g_sample_task, &woken);
    if (woken) portYIELD_FROM_ISR();
  }
}

static void sampleTask(void *arg)
{
  (void)arg;
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    Field_SampleTick();
  }
}

static void flushTask(void *arg)
{
  uint32_t last_ramp = millis();
  uint32_t last_tele = millis();
  (void)arg;
  for (;;) {
    lock_hw();
    PCA9685_Flush();
    {
      uint32_t now = millis();
      if ((now - last_ramp) >= (1000U / CURRENT_LOOP_HZ)) {
        last_ramp = now;
        Rotate_RampUpdate();
      }
      if (g_mlx_en && g_mlx_n_present) {
        Mlx_PollAll();
      }
      if (g_teleplot_en && (now - last_tele) >= (1000U / TELEPLOT_HZ)) {
        last_tele = now;
        unlock_hw();
        Teleplot_Emit();
        lock_hw();
      }
      char cmd[CLI_LINE_MAX];
      while (CmdQ_Dequeue(cmd, sizeof(cmd))) {
        CLI_Exec(cmd, NULL, 0);
      }
    }
    unlock_hw();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("\nESP32-WROOM-32 coil driver");

  g_hw_mu = xSemaphoreCreateMutex();
  CmdQ_Init();
  Coil_GpioInit();
  i2c_bus_init();
  Field_EngineInit();
  PCA9685_Init();
  Coil_AllOff();
  mlx_try_init();

  xTaskCreatePinnedToCore(sampleTask, "sine", 4096, NULL, 20, &g_sample_task, 1);
  xTaskCreatePinnedToCore(flushTask, "flush", 8192, NULL, 10, &g_flush_task, 1);

  g_timer = timerBegin(0, 80, true);
  timerAttachInterrupt(g_timer, &onSineTimer, true);
  timerAlarmWrite(g_timer, 1000, true);
  timerAlarmEnable(g_timer);

  startWifi();
  Pad_Begin();

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/cmd", HTTP_POST, handleCmd);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server on :80");

  if (g_pca_ok) {
    Serial.printf("PCA9685 ok, MLX %u/4. Type 'help'.\r\n> ",
                  (unsigned)g_mlx_n_present);
  } else {
    Serial.print("PCA9685 MISSING on I2C0 0x40 — PWM dead. Type 'help'.\r\n> ");
  }
}

void loop()
{
  server.handleClient();
  Pad_Poll();
  lock_hw();
  CLI_PollSerial();
  unlock_hw();
  delay(2);
}
