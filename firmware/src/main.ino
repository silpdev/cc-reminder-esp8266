/*
 * cc-reminder - ESP8266 (Wemos D1 Mini) + WS2812
 *
 * Den bao trang thai Claude Code. Port tu Curlyfuu/cc_reminder
 * (ban goc dung ESP32-C3 + BLE). ESP8266 khong co Bluetooth nen ban nay
 * dung WiFi/HTTP - nhanh hon: ~50ms moi hook thay vi 1-3s scan BLE.
 *
 * DAU NOI
 *   strip VCC -> 3V3     (1 LED thi 3V3 khop muc logic, khong can diode)
 *   strip DIN -> D9 / RX (GPIO3 - bat buoc, DMA chi chay chan nay)
 *   strip GND -> G
 *
 * API
 *   GET /                       -> trang trang thai dang text
 *   GET /state?s=IDLE|WORKING|INTERACT
 *   GET /status                 -> ten trang thai hien tai
 *
 * SERIAL: DMA chiem GPIO3 nen Serial chi GUI ra duoc, khong nhan vao.
 * Serial Monitor van xem log binh thuong.
 *
 * Copy src/config.h.example -> src/config.h roi dien WiFi truoc khi build.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <NeoPixelBus.h>

#include "config.h"

/* ---------------- trang thai ---------------- */
const uint8_t ST_IDLE     = 0;
const uint8_t ST_WORKING  = 1;
const uint8_t ST_INTERACT = 2;

uint8_t state = ST_IDLE;

const char* stateName(uint8_t s) {
  if (s == ST_WORKING)  return "WORKING";
  if (s == ST_INTERACT) return "INTERACT";
  return "IDLE";
}

/* ---------------- LED ---------------- */
// Neu goi do ma ra xanh la -> doi NeoGrbFeature thanh NeoRgbFeature
NeoPixelBus<NeoGrbFeature, NeoEsp8266Dma800KbpsMethod> strip(PIXEL_COUNT);

ESP8266WebServer server(80);

void fill(uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < PIXEL_COUNT; i++) {
    strip.SetPixelColor(i, RgbColor(r, g, b));
  }
  strip.Show();
}

// Nhip dap: tra ve he so 0..1 theo chu ky
float breathe(uint16_t periodMs, float floorK) {
  float ph = (millis() % periodMs) / (float)periodMs * 360.0f;
  float s  = 0.5f - 0.5f * cos(ph * PI / 180.0f);
  return floorK + (1.0f - floorK) * s;
}

void renderLed() {
  static unsigned long last = 0;
  if (millis() - last < 20) return;   // ~50Hz, du muot va nhe cho CPU
  last = millis();

  float k = 1.0f;
  uint8_t r = 0, g = 0, b = 0;

  if (state == ST_INTERACT) {
    // do, dap nhanh - de nhan ra bang khoe mat
    k = breathe(900, 0.15f);
    r = COL_INTERACT_R; g = COL_INTERACT_G; b = COL_INTERACT_B;
  } else if (state == ST_WORKING) {
    // vang cam, dap cham
    k = breathe(2400, 0.40f);
    r = COL_WORKING_R; g = COL_WORKING_G; b = COL_WORKING_B;
  } else {
    // xanh la, sang dung
    r = COL_IDLE_R; g = COL_IDLE_G; b = COL_IDLE_B;
  }

  float bk = (BRIGHT / 255.0f) * k;
  fill((uint8_t)(r * bk), (uint8_t)(g * bk), (uint8_t)(b * bk));
}

/* ---------------- HTTP ---------------- */
void handleState() {
  String s = server.arg("s");
  s.toUpperCase();

  if      (s == "IDLE")     state = ST_IDLE;
  else if (s == "WORKING")  state = ST_WORKING;
  else if (s == "INTERACT") state = ST_INTERACT;
  else { server.send(400, "text/plain", "BAD_STATE\n"); return; }

  Serial.printf("-> %s\n", stateName(state));
  server.send(200, "text/plain", String("OK ") + stateName(state) + "\n");
}

void handleStatus() {
  server.send(200, "text/plain", String(stateName(state)) + "\n");
}

void handleRoot() {
  String o = "cc-reminder\n";
  o += "state    : "; o += stateName(state); o += "\n";
  o += "ip       : "; o += WiFi.localIP().toString(); o += "\n";
  o += "rssi     : "; o += WiFi.RSSI(); o += " dBm\n";
  o += "uptime   : "; o += (millis() / 1000); o += " s\n";
  o += "pixels   : "; o += PIXEL_COUNT; o += "\n";
  o += "\nGET /state?s=IDLE|WORKING|INTERACT\n";
  server.send(200, "text/plain", o);
}

/* ---------------- setup / loop ---------------- */
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== cc-reminder (ESP8266 + WS2812) ===");

  strip.Begin();
  // xanh duong mo trong luc cho WiFi
  fill(0, 0, 40);

  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("wifi");
  uint16_t tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 120) {
    delay(250);
    Serial.print(".");
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("ip: ");
    Serial.println(WiFi.localIP());
    if (MDNS.begin(HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      Serial.printf("mdns: http://%s.local/\n", HOSTNAME);
    }
  } else {
    // khong noi duoc thi nhay do de biet, van chay tiep de tu reconnect
    Serial.println("wifi FAIL - se tu thu lai");
    for (uint8_t i = 0; i < 6; i++) {
      fill(60, 0, 0); delay(150);
      fill(0, 0, 0);  delay(150);
    }
  }

  server.on("/",       handleRoot);
  server.on("/state",  handleState);
  server.on("/status", handleStatus);
  server.onNotFound([]() { server.send(404, "text/plain", "NOT_FOUND\n"); });
  server.begin();

  state = ST_IDLE;
}

void loop() {
  server.handleClient();
  MDNS.update();
  renderLed();
}
