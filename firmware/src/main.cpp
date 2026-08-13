/*
 * cc-reminder - ESP8266 (Wemos D1 Mini) + WS2812
 * Ban ROAMING: nho 3 mang WiFi, tu chon mang manh nhat, va tra loi
 * UDP discovery de host tim duoc IP o bat ky mang nao.
 *
 * Cam o nha hay o cong ty deu chay, khong phai cau hinh lai.
 *
 * DAU NOI
 *   strip VCC -> 3V3
 *   strip DIN -> D9 / RX (GPIO3 - bat buoc, DMA chi chay chan nay)
 *   strip GND -> G
 *
 * API
 *   GET  /                    trang cau hinh
 *   GET  /state?s=IDLE|WORKING|INTERACT
 *   GET  /status              ten trang thai, dang text
 *   GET  /api/status          JSON
 *   GET  /api/config          JSON cau hinh (khong co mat khau)
 *   POST /api/config          luu cau hinh
 *   GET  /api/scan            quet WiFi
 *   POST /api/reboot | /api/reset
 *
 * UDP DISCOVERY
 *   Host broadcast "CCR?" toi port 45678 -> thiet bi tra ve JSON co IP.
 *   Nho vay host khong phu thuoc mDNS (mDNS hay chet tren WSL).
 *
 * Sua HTML o firmware/web/page.html roi chay tools/embed_page.py.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <NeoPixelBus.h>

/* ===================== hang so ===================== */
#define MAX_PIXELS    24
#define NET_SLOTS     3
#define CFG_MAGIC     0x43524D35UL     // doi so = xoa cau hinh cu
#define FX_COUNT      6                // so hieu ung (gom ca "tat")
#define PREVIEW_MS    15000UL          // xem thu hieu ung bao lau
#define AP_SSID       "cc-reminder-setup"
#define EEPROM_SIZE   512
#define DISCO_PORT    45678
#define DISCO_PROBE   "CCR?"
#define RECONNECT_MS  20000UL          // mat wifi bao lau thi quet lai

const uint8_t ST_IDLE     = 0;
const uint8_t ST_WORKING   = 1;
const uint8_t ST_INTERACT  = 2;

/* ===================== cau hinh ===================== */
struct Net {
  char ssid[33];
  char pass[65];
};

struct Config {
  uint32_t magic;
  Net      net[NET_SLOTS];
  char     host[24];
  uint8_t  pixels;
  uint8_t  bright;
  uint8_t  order;              // 0 = GRB (mac dinh), 1 = RGB
  uint8_t  col[3][3];
  uint8_t  pulse[3];
  uint16_t period[3];

  /* Ambient: IDLE lau qua thi chuyen sang hieu ung trang tri */
  uint16_t idleAfter;          // giay; 0 = tat han
  uint8_t  effect;             // 0 tat, 1 nen, 2 cau vong, 3 nhip tho,
                               // 4 lap lanh, 5 cuc quang
  uint8_t  fxSpeed;            // 1..10, 5 = binh thuong
  uint8_t  fxBright;           // do sang rieng cho ambient (thuong thap hon)
  uint8_t  fxCol[3];           // mau dung cho hieu ung nhip tho
};

Config cfg;

/* Chan loi im lang: them field vao Config ma quen tang EEPROM_SIZE thi
   EEPROM.put se ghi tran. Bat loi ngay luc bien dich. */
static_assert(sizeof(Config) <= EEPROM_SIZE, "Config lon hon EEPROM_SIZE");

void setDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = CFG_MAGIC;
  strncpy(cfg.host, "cc-reminder", sizeof(cfg.host) - 1);
  cfg.pixels = 1;
  cfg.bright = 60;
  cfg.order  = 0;

  cfg.col[0][0] = 0;   cfg.col[0][1] = 255; cfg.col[0][2] = 0;   // IDLE
  cfg.pulse[0]  = 0;   cfg.period[0] = 2000;
  cfg.col[1][0] = 255; cfg.col[1][1] = 90;  cfg.col[1][2] = 0;   // WORKING
  cfg.pulse[1]  = 1;   cfg.period[1] = 2400;
  cfg.col[2][0] = 255; cfg.col[2][1] = 0;   cfg.col[2][2] = 0;   // INTERACT
  cfg.pulse[2]  = 1;   cfg.period[2] = 900;

  cfg.idleAfter = 300;         // 5 phut
  cfg.effect    = 1;           // nen
  cfg.fxSpeed   = 5;
  cfg.fxBright  = 28;          // toi hon han trang thai - de tren ban ban dem
  cfg.fxCol[0] = 255; cfg.fxCol[1] = 120; cfg.fxCol[2] = 30;
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cfg);
  if (cfg.magic != CFG_MAGIC) {
    Serial.println("eeprom trong -> dung mac dinh");
    setDefaults();
    return;
  }
  if (cfg.pixels < 1 || cfg.pixels > MAX_PIXELS) cfg.pixels = 1;
  if (cfg.bright < 1) cfg.bright = 1;
  if (cfg.order > 1)  cfg.order = 0;
  for (uint8_t i = 0; i < 3; i++) {
    if (cfg.period[i] < 200)   cfg.period[i] = 900;
    if (cfg.period[i] > 10000) cfg.period[i] = 10000;
    if (cfg.pulse[i] > 1)      cfg.pulse[i] = 0;
  }
  for (uint8_t i = 0; i < NET_SLOTS; i++) {
    cfg.net[i].ssid[sizeof(cfg.net[i].ssid) - 1] = '\0';
    cfg.net[i].pass[sizeof(cfg.net[i].pass) - 1] = '\0';
  }
  cfg.host[sizeof(cfg.host) - 1] = '\0';
  if (cfg.host[0] == '\0') strncpy(cfg.host, "cc-reminder", sizeof(cfg.host) - 1);
  if (cfg.effect >= FX_COUNT) cfg.effect = 0;
  if (cfg.fxSpeed < 1 || cfg.fxSpeed > 10) cfg.fxSpeed = 5;
  if (cfg.fxBright < 1) cfg.fxBright = 1;
  if (cfg.idleAfter > 86400) cfg.idleAfter = 300;
}

void saveConfig() {
  cfg.magic = CFG_MAGIC;
  EEPROM.put(0, cfg);
  EEPROM.commit();
  Serial.println("eeprom da luu");
}

uint8_t netCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < NET_SLOTS; i++) if (cfg.net[i].ssid[0]) n++;
  return n;
}

/* ===================== LED ===================== */
NeoPixelBus<NeoGrbFeature, NeoEsp8266Dma800KbpsMethod> strip(MAX_PIXELS);

uint8_t state = ST_IDLE;
unsigned long stateAt   = 0;      // luc trang thai doi lan cuoi
unsigned long previewTil = 0;     // dang xem thu hieu ung den khi nao
uint8_t previewFx = 0;
bool    fxActive  = false;        // dang chay ambient?

void setPx(uint16_t i, uint8_t r, uint8_t g, uint8_t b) {
  // order == 1 (strip RGB): dao r/g vi feature dang la GRB
  strip.SetPixelColor(i, (cfg.order == 1) ? RgbColor(g, r, b) : RgbColor(r, g, b));
}

void blankRest() {
  for (uint16_t i = cfg.pixels; i < MAX_PIXELS; i++) setPx(i, 0, 0, 0);
}

/* Lam min chuyen mau cho duong "mot mau dong nhat".
   Moi frame tien 25% ve dich -> hang so thoi gian ~60ms o 50Hz.
   Du de xoa cam giac giat khi doi trang thai, khong du de lam cham nhip dap. */
float curR = 0, curG = 0, curB = 0;

void fillPx(uint8_t r, uint8_t g, uint8_t b, bool smooth) {
  if (smooth) {
    curR += (r - curR) * 0.25f;
    curG += (g - curG) * 0.25f;
    curB += (b - curB) * 0.25f;
  } else {
    curR = r; curG = g; curB = b;
  }
  uint8_t rr = (uint8_t)(curR + 0.5f);
  uint8_t gg = (uint8_t)(curG + 0.5f);
  uint8_t bb = (uint8_t)(curB + 0.5f);
  for (uint16_t i = 0; i < cfg.pixels; i++) setPx(i, rr, gg, bb);
  blankRest();
  strip.Show();
}

void writeAll(uint8_t r, uint8_t g, uint8_t b) { fillPx(r, g, b, false); }

float pulseK(uint16_t periodMs, float floorK) {
  if (periodMs < 1) periodMs = 1;
  float deg = (float)(millis() % periodMs) / (float)periodMs * 360.0f;
  float s = 0.5f - 0.5f * cos(deg * PI / 180.0f);
  return floorK + (1.0f - floorK) * s;
}

/* ---------------- hieu ung ambient ----------------
   Tat ca deu la hieu ung THEO THOI GIAN, khong phai theo khong gian, vi
   thiet bi mac dinh chi co 1 LED. Cac tham so i (chi so pixel) chi tao do
   lech pha, nen dat 8-16 LED thi tu nhien thanh hieu ung chay doc.        */

const char* fxName(uint8_t f) {
  switch (f) {
    case 1: return "candle";
    case 2: return "rainbow";
    case 3: return "breathe";
    case 4: return "twinkle";
    case 5: return "aurora";
    default: return "off";
  }
}

void renderEffect(uint8_t fx) {
  float sp = (float)cfg.fxSpeed / 5.0f;             // 5 = binh thuong
  float t  = (float)millis() / 1000.0f * sp;
  float bm = (float)cfg.fxBright / 255.0f;

  // nen: random walk cong 2 sine lech tan -> chay tu nhien, khong theo chu ky
  static float flick = 0.75f;
  if (fx == 1) {
    flick += ((float)random(-45, 46)) / 1000.0f;
    if (flick < 0.35f) flick = 0.35f;
    if (flick > 1.0f)  flick = 1.0f;
  }

  for (uint16_t i = 0; i < cfg.pixels; i++) {
    float ph = (float)i * 0.55f;                   // do lech pha giua cac pixel
    float h = 0, sat = 1.0f, v = 0;

    switch (fx) {
      case 1: {                                    // nen
        float w = 0.62f + 0.22f * sin(t * 6.1f + ph)
                        + 0.16f * sin(t * 13.7f + ph);
        v = bm * flick * (0.55f + 0.45f * w);
        h = 0.055f + 0.02f * w;                    // cam am, hoi dao quanh
        sat = 0.92f;
        break;
      }
      case 2: {                                    // cau vong troi
        h = t * 0.035f + (float)i / (float)cfg.pixels * 0.6f;
        h = h - floor(h);
        v = bm;
        break;
      }
      case 3: {                                    // nhip tho theo mau fxCol
        float k = 0.12f + 0.88f * (0.5f - 0.5f * cos(t * 0.9f + ph));
        uint8_t r = (uint8_t)(cfg.fxCol[0] * bm * k);
        uint8_t g = (uint8_t)(cfg.fxCol[1] * bm * k);
        uint8_t b = (uint8_t)(cfg.fxCol[2] * bm * k);
        setPx(i, r, g, b);
        continue;
      }
      case 4: {                                    // lap lanh
        float a  = sin(t * 1.7f  + ph * 3.1f);
        float b2 = sin(t * 2.63f + ph * 1.7f);
        float k = a * b2;
        k = (k > 0) ? k * k * k : 0;                // dinh nhon, phan lon la toi
        v = bm * (0.06f + 0.94f * k);
        h = 0.13f; sat = 0.25f;                     // trang am
        break;
      }
      case 5: {                                    // cuc quang
        h = 0.44f + 0.24f * sin(t * 0.31f + ph);
        v = bm * (0.35f + 0.65f * (0.5f + 0.5f * sin(t * 0.47f + ph * 1.3f)));
        sat = 0.85f;
        break;
      }
      default:
        setPx(i, 0, 0, 0);
        continue;
    }

    if (v < 0) v = 0;
    if (v > 1) v = 1;
    RgbColor c = RgbColor(HsbColor(h, sat, v));
    setPx(i, c.R, c.G, c.B);
  }
  blankRest();
  strip.Show();
  curR = curG = curB = 0;      // reset smoothing de khong keo mau cu vao
}

void renderLed() {
  static unsigned long last = 0;
  if (millis() - last < 20) return;                // ~50Hz
  last = millis();

  // xem thu tu trang cau hinh
  if (previewTil && millis() < previewTil) {
    fxActive = true;
    renderEffect(previewFx);
    return;
  }
  if (previewTil && millis() >= previewTil) {
    previewTil = 0;
    stateAt = millis();                            // dem lai tu dau
  }

  // IDLE lau qua -> ambient
  bool wantFx = cfg.effect != 0 && cfg.idleAfter > 0 && state == ST_IDLE &&
                (millis() - stateAt) > (unsigned long)cfg.idleAfter * 1000UL;
  fxActive = wantFx;
  if (wantFx) { renderEffect(cfg.effect); return; }

  uint8_t s = state;
  float k = 1.0f;
  if (cfg.pulse[s]) k = pulseK(cfg.period[s], (s == ST_INTERACT) ? 0.15f : 0.40f);

  float bk = ((float)cfg.bright / 255.0f) * k;
  fillPx((uint8_t)(cfg.col[s][0] * bk),
         (uint8_t)(cfg.col[s][1] * bk),
         (uint8_t)(cfg.col[s][2] * bk), true);
}

/* ===================== web / udp ===================== */
ESP8266WebServer server(80);
DNSServer  dns;
WiFiUDP    disco;
bool apMode = false;
int  activeSlot = -1;

extern const char PAGE_HTML[] PROGMEM;

const char* stateName(uint8_t s) {
  if (s == ST_WORKING)  return "WORKING";
  if (s == ST_INTERACT) return "INTERACT";
  return "IDLE";
}

String jstr(const String& in) {
  String o = in;
  o.replace("\\", "\\\\");
  o.replace("\"", "\\\"");
  return o;
}

String hex3(const uint8_t* c) {
  char b[10];
  snprintf(b, sizeof(b), "#%02x%02x%02x", c[0], c[1], c[2]);
  return String(b);
}

bool parseHex3(const String& s, uint8_t* out) {
  if (s.length() < 7 || s.charAt(0) != '#') return false;
  for (uint8_t i = 0; i < 3; i++) {
    out[i] = (uint8_t)strtol(s.substring(1 + i * 2, 3 + i * 2).c_str(), NULL, 16);
  }
  return true;
}

/* ---- UDP discovery ---- */
void discoBegin() {
  disco.begin(DISCO_PORT);
  Serial.printf("udp discovery: port %d\n", DISCO_PORT);
}

void discoLoop() {
  int sz = disco.parsePacket();
  if (sz <= 0) return;

  char buf[32];
  int n = disco.read(buf, sizeof(buf) - 1);
  if (n < 0) n = 0;
  buf[n] = '\0';

  if (strncmp(buf, DISCO_PROBE, strlen(DISCO_PROBE)) != 0) return;

  String o = "{\"cc-reminder\":1,\"host\":\"";
  o += jstr(String(cfg.host));
  o += "\",\"ip\":\"";   o += WiFi.localIP().toString();
  o += "\",\"port\":80,\"state\":\""; o += stateName(state);
  o += "\"}";

  disco.beginPacket(disco.remoteIP(), disco.remotePort());
  disco.write((const uint8_t*)o.c_str(), o.length());
  disco.endPacket();
}

/* ---- handlers ---- */
void handleRoot() {
  server.send_P(200, "text/html", PAGE_HTML);
}

void handleApiStatus() {
  String o = "{";
  o += "\"state\":\"";  o += stateName(state); o += "\",";
  o += "\"ap\":";       o += (apMode ? "true" : "false"); o += ",";
  o += "\"slot\":";     o += activeSlot;       o += ",";
  o += "\"ssid\":\"";   o += jstr(WiFi.SSID()); o += "\",";
  o += "\"ip\":\"";     o += (apMode ? WiFi.softAPIP().toString()
                                     : WiFi.localIP().toString()); o += "\",";
  o += "\"host\":\"";   o += jstr(String(cfg.host)); o += "\",";
  o += "\"rssi\":";     o += WiFi.RSSI();      o += ",";
  o += "\"uptime\":";   o += (millis() / 1000); o += ",";
  o += "\"heap\":";     o += ESP.getFreeHeap();     o += ",";
  o += "\"fx\":";       o += (fxActive ? "true" : "false"); o += ",";
  o += "\"fxname\":\""; o += fxName(previewTil ? previewFx : cfg.effect);
  o += "\",";
  o += "\"idlefor\":";  o += ((millis() - stateAt) / 1000);
  o += "}";
  server.send(200, "application/json", o);
}

void handleApiConfigGet() {
  String o = "{\"nets\":[";
  for (uint8_t i = 0; i < NET_SLOTS; i++) {
    if (i) o += ",";
    o += "{\"ssid\":\""; o += jstr(String(cfg.net[i].ssid));
    o += "\",\"saved\":"; o += (cfg.net[i].pass[0] ? "true" : "false");
    o += "}";
  }
  o += "],";
  o += "\"host\":\"";  o += jstr(String(cfg.host)); o += "\",";
  o += "\"pixels\":";  o += cfg.pixels; o += ",";
  o += "\"bright\":";  o += cfg.bright; o += ",";
  o += "\"order\":";   o += cfg.order;  o += ",";
  o += "\"col\":[";
  for (uint8_t i = 0; i < 3; i++) { o += "\""; o += hex3(cfg.col[i]); o += "\"";
                                    if (i < 2) o += ","; }
  o += "],\"pulse\":[";
  for (uint8_t i = 0; i < 3; i++) { o += cfg.pulse[i];  if (i < 2) o += ","; }
  o += "],\"period\":[";
  for (uint8_t i = 0; i < 3; i++) { o += cfg.period[i]; if (i < 2) o += ","; }
  o += "],";
  o += "\"idle\":";  o += cfg.idleAfter; o += ",";
  o += "\"fx\":";    o += cfg.effect;    o += ",";
  o += "\"fxs\":";   o += cfg.fxSpeed;   o += ",";
  o += "\"fxb\":";   o += cfg.fxBright;  o += ",";
  o += "\"fxc\":\""; o += hex3(cfg.fxCol); o += "\"";
  o += "}";
  server.send(200, "application/json", o);
}

void handleApiConfigPost() {
  bool needReboot = false;
  char key[8];

  for (uint8_t i = 0; i < NET_SLOTS; i++) {
    snprintf(key, sizeof(key), "ssid%u", i);
    if (server.hasArg(key)) {
      String v = server.arg(key);
      if (v != String(cfg.net[i].ssid)) {
        strncpy(cfg.net[i].ssid, v.c_str(), sizeof(cfg.net[i].ssid) - 1);
        cfg.net[i].ssid[sizeof(cfg.net[i].ssid) - 1] = '\0';
        if (v.length() == 0) cfg.net[i].pass[0] = '\0';   // xoa slot
        needReboot = true;
      }
    }
    // o trong = giu mat khau cu
    snprintf(key, sizeof(key), "pass%u", i);
    if (server.hasArg(key) && server.arg(key).length() > 0) {
      strncpy(cfg.net[i].pass, server.arg(key).c_str(), sizeof(cfg.net[i].pass) - 1);
      cfg.net[i].pass[sizeof(cfg.net[i].pass) - 1] = '\0';
      needReboot = true;
    }
  }

  if (server.hasArg("host")) {
    String v = server.arg("host");
    if (v.length() > 0 && v != String(cfg.host)) {
      strncpy(cfg.host, v.c_str(), sizeof(cfg.host) - 1);
      cfg.host[sizeof(cfg.host) - 1] = '\0';
      needReboot = true;
    }
  }
  if (server.hasArg("pixels")) {
    uint8_t v = (uint8_t)constrain(server.arg("pixels").toInt(), 1, MAX_PIXELS);
    if (v != cfg.pixels) { cfg.pixels = v; needReboot = true; }
  }
  if (server.hasArg("bright"))
    cfg.bright = (uint8_t)constrain(server.arg("bright").toInt(), 1, 255);
  if (server.hasArg("order"))
    cfg.order = (server.arg("order").toInt() == 1) ? 1 : 0;

  for (uint8_t i = 0; i < 3; i++) {
    snprintf(key, sizeof(key), "c%u", i);
    if (server.hasArg(key)) {
      uint8_t rgb[3];
      if (parseHex3(server.arg(key), rgb)) {
        cfg.col[i][0] = rgb[0]; cfg.col[i][1] = rgb[1]; cfg.col[i][2] = rgb[2];
      }
    }
    snprintf(key, sizeof(key), "p%u", i);
    if (server.hasArg(key)) cfg.pulse[i] = (server.arg(key).toInt() == 1) ? 1 : 0;
    snprintf(key, sizeof(key), "t%u", i);
    if (server.hasArg(key))
      cfg.period[i] = (uint16_t)constrain(server.arg(key).toInt(), 200, 10000);
  }

  if (server.hasArg("idle"))
    cfg.idleAfter = (uint16_t)constrain(server.arg("idle").toInt(), 0, 86400);
  if (server.hasArg("fx"))
    cfg.effect = (uint8_t)constrain(server.arg("fx").toInt(), 0, FX_COUNT - 1);
  if (server.hasArg("fxs"))
    cfg.fxSpeed = (uint8_t)constrain(server.arg("fxs").toInt(), 1, 10);
  if (server.hasArg("fxb"))
    cfg.fxBright = (uint8_t)constrain(server.arg("fxb").toInt(), 1, 255);
  if (server.hasArg("fxc")) {
    uint8_t rgb[3];
    if (parseHex3(server.arg("fxc"), rgb)) {
      cfg.fxCol[0] = rgb[0]; cfg.fxCol[1] = rgb[1]; cfg.fxCol[2] = rgb[2];
    }
  }

  saveConfig();
  String o = "{\"ok\":true,\"reboot\":";
  o += (needReboot ? "true" : "false");
  o += "}";
  server.send(200, "application/json", o);
}

/* Xem thu hieu ung ngay tren den that, tu tat sau PREVIEW_MS.
   Khong luu vao EEPROM - chi de thu. */
void handleApiPreview() {
  int f = server.arg("fx").toInt();
  previewFx  = (uint8_t)constrain(f, 0, FX_COUNT - 1);
  previewTil = millis() + PREVIEW_MS;
  String o = "{\"ok\":true,\"fx\":\"";
  o += fxName(previewFx);
  o += "\",\"seconds\":";
  o += (PREVIEW_MS / 1000);
  o += "}";
  server.send(200, "application/json", o);
}

void handleApiScan() {
  int n = WiFi.scanNetworks();
  String o = "[";
  for (int i = 0; i < n && i < 20; i++) {
    if (i) o += ",";
    o += "{\"ssid\":\""; o += jstr(WiFi.SSID(i));
    o += "\",\"rssi\":";  o += WiFi.RSSI(i);
    o += "}";
  }
  o += "]";
  server.send(200, "application/json", o);
}

void handleSetState() {
  String s = server.arg("s");
  s.toUpperCase();
  if      (s == "IDLE")     state = ST_IDLE;
  else if (s == "WORKING")  state = ST_WORKING;
  else if (s == "INTERACT") state = ST_INTERACT;
  else { server.send(400, "text/plain", "BAD_STATE\n"); return; }
  stateAt    = millis();          // dem lai dong ho idle
  previewTil = 0;                 // huy xem thu neu dang chay
  server.send(200, "text/plain", String("OK ") + stateName(state) + "\n");
}

void handleStatusText() {
  server.send(200, "text/plain", String(stateName(state)) + "\n");
}

void handleReboot() {
  server.send(200, "application/json", "{\"ok\":true}");
  delay(300);
  ESP.restart();
}

void handleFactoryReset() {
  setDefaults();
  saveConfig();
  server.send(200, "application/json", "{\"ok\":true}");
  delay(300);
  ESP.restart();
}

void handleNotFound() {
  if (apMode) {
    server.sendHeader("Location",
                      String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "NOT_FOUND\n");
}

void setupRoutes() {
  server.on("/",           HTTP_GET,  handleRoot);
  server.on("/api/status", HTTP_GET,  handleApiStatus);
  server.on("/api/config", HTTP_GET,  handleApiConfigGet);
  server.on("/api/config", HTTP_POST, handleApiConfigPost);
  server.on("/api/scan",   HTTP_GET,  handleApiScan);
  server.on("/api/preview",HTTP_POST, handleApiPreview);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/reset",  HTTP_POST, handleFactoryReset);
  server.on("/state",      HTTP_GET,  handleSetState);
  server.on("/status",     HTTP_GET,  handleStatusText);
  server.onNotFound(handleNotFound);
}

/* ===================== WiFi roaming ===================== */
void startAP() {
  apMode = true;
  activeSlot = -1;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("AP: %s -> http://%s/\n",
                AP_SSID, WiFi.softAPIP().toString().c_str());
}

// Thu 1 slot. Tra ve true neu noi duoc.
bool tryConnect(uint8_t slot, uint16_t timeoutMs) {
  if (!cfg.net[slot].ssid[0]) return false;
  Serial.printf("thu slot %u \"%s\"", slot, cfg.net[slot].ssid);

  WiFi.begin(cfg.net[slot].ssid, cfg.net[slot].pass);
  unsigned long t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf(" OK %s\n", WiFi.localIP().toString().c_str());
      activeSlot = slot;
      return true;
    }
    // nhay xanh duong trong luc cho
    writeAll(0, 0, ((millis() / 250) % 2) ? 30 : 0);
    delay(60);
  }
  Serial.println(" that bai");
  WiFi.disconnect();
  return false;
}

/* Quet xem mang nao dang co mat, uu tien mang manh nhat.
   Nho vay mang qua cong ty khong phai cau hinh lai.          */
bool connectBest() {
  apMode = false;
  WiFi.mode(WIFI_STA);
  WiFi.hostname(cfg.host);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  if (netCount() == 0) return false;

  int8_t order[NET_SLOTS];
  int32_t rssi[NET_SLOTS];
  uint8_t cnt = 0;

  int found = WiFi.scanNetworks();
  for (uint8_t s = 0; s < NET_SLOTS; s++) {
    if (!cfg.net[s].ssid[0]) continue;
    int32_t best = -32768;
    for (int i = 0; i < found; i++) {
      if (WiFi.SSID(i) == cfg.net[s].ssid && WiFi.RSSI(i) > best) best = WiFi.RSSI(i);
    }
    if (best > -32768) { order[cnt] = s; rssi[cnt] = best; cnt++; }
  }

  // sap xep giam dan theo RSSI (n <= 3, insertion sort la du)
  for (uint8_t i = 1; i < cnt; i++) {
    int8_t  ks = order[i];
    int32_t kr = rssi[i];
    int8_t  j  = i - 1;
    while (j >= 0 && rssi[j] < kr) {
      order[j + 1] = order[j]; rssi[j + 1] = rssi[j]; j--;
    }
    order[j + 1] = ks; rssi[j + 1] = kr;
  }

  for (uint8_t i = 0; i < cnt; i++) {
    Serial.printf("thay \"%s\" (%d dBm)\n", cfg.net[order[i]].ssid, (int)rssi[i]);
    if (tryConnect(order[i], 12000)) break;
  }

  // khong thay mang nao da luu -> thu het cac slot (SSID an)
  if (WiFi.status() != WL_CONNECTED) {
    for (uint8_t s = 0; s < NET_SLOTS; s++) {
      bool tried = false;
      for (uint8_t i = 0; i < cnt; i++) if (order[i] == (int8_t)s) tried = true;
      if (!tried && tryConnect(s, 8000)) break;
    }
  }

  if (WiFi.status() != WL_CONNECTED) return false;

  if (MDNS.begin(cfg.host)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mdns: http://%s.local/\n", cfg.host);
  }
  discoBegin();
  return true;
}

/* ===================== setup / loop ===================== */
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== cc-reminder (roaming) ===");

  loadConfig();
  strip.Begin();
  strip.Show();
  writeAll(0, 0, 30);

  if (!connectBest()) startAP();

  setupRoutes();
  server.begin();
  state   = ST_IDLE;
  stateAt = millis();
  Serial.println("san sang");
}

void loop() {
  server.handleClient();

  if (apMode) {
    dns.processNextRequest();
    static unsigned long t = 0;
    if (millis() - t > 20) {
      t = millis();
      writeAll(0, 0, (uint8_t)(60 * pulseK(1600, 0.10f)));
    }
    return;
  }

  MDNS.update();
  discoLoop();
  renderLed();

  /* Mat WiFi qua lau -> quet lai tu dau. Day la co che cho phep
     rut o nha cam o cong ty ma khong phai lam gi.               */
  static unsigned long lostAt = 0;
  if (WiFi.status() != WL_CONNECTED) {
    if (lostAt == 0) {
      lostAt = millis();
      Serial.println("mat wifi");
    } else if (millis() - lostAt > RECONNECT_MS) {
      Serial.println("quet lai cac mang da luu");
      lostAt = 0;
      if (!connectBest()) startAP();
    }
  } else {
    lostAt = 0;
  }
}

/* ===================== trang web =====================
   Phan duoi day do tools/embed_page.py sinh ra tu firmware/web/page.html.
   SUA HTML O FILE DO roi chay: python3 tools/embed_page.py
   Dung sua truc tiep trong day - se bi ghi de.                          */
/* ---8<--- PAGE BEGIN --- */
const char PAGE_HTML[] PROGMEM = R"CCR(
<!doctype html>
<html lang="vi"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>cc-reminder</title>
<style>
:root{--bg:#15171c;--card:#1e2128;--line:#2c313b;--tx:#e6e8ec;--dim:#8b93a1;--ac:#4a9eff}
*{box-sizing:border-box}
body{margin:0;padding:16px;background:var(--bg);color:var(--tx);
font:15px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
max-width:520px;margin:0 auto}
h1{font-size:19px;margin:8px 0 4px}
h1 span{color:var(--dim);font-weight:400;font-size:14px}
h2{font-size:14px;text-transform:uppercase;letter-spacing:.06em;color:var(--dim);
margin:0 0 12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;
padding:14px;margin:12px 0}
.row{display:flex;align-items:center;gap:10px;margin:10px 0}
.row label{flex:1;min-width:0}
input,select{background:#12141a;color:var(--tx);border:1px solid var(--line);
border-radius:7px;padding:8px 10px;font-size:15px;font-family:inherit}
input[type=text],input[type=password]{width:100%}
input[type=number]{width:78px}
input[type=color]{width:46px;height:34px;padding:2px;cursor:pointer}
input[type=range]{flex:2;accent-color:var(--ac)}
input[type=checkbox]{width:18px;height:18px;accent-color:var(--ac)}
button{background:#2a2f39;color:var(--tx);border:1px solid var(--line);
border-radius:7px;padding:9px 14px;font-size:14px;font-family:inherit;
cursor:pointer}
button:active{transform:translateY(1px)}
button.pri{background:var(--ac);border-color:var(--ac);color:#08111d;font-weight:600}
button.dgr{color:#ff8080}
.btns{display:flex;gap:8px;flex-wrap:wrap}
.st{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;font-size:14px}
.st b{color:var(--dim);font-weight:400}
.mono{font-variant-numeric:tabular-nums}
.dot{width:10px;height:10px;border-radius:50%;display:inline-block;
margin-right:6px;vertical-align:middle}
.hint{color:var(--dim);font-size:13px;margin-top:8px}
#msg{position:fixed;left:0;right:0;bottom:0;padding:12px;text-align:center;
background:var(--ac);color:#08111d;font-weight:600;transform:translateY(100%);
transition:transform .2s}
#msg.on{transform:none}
</style></head><body>
<h1>cc-reminder <span id="hn"></span></h1>
<div class="card">
<h2>Trạng thái</h2>
<div class="st">
<b>Đèn</b><span id="s-state">—</span>
<b>WiFi</b><span id="s-wifi">—</span>
<b>IP</b><span id="s-ip" class="mono">—</span>
<b>Tín hiệu</b><span id="s-rssi" class="mono">—</span>
<b>Uptime</b><span id="s-up" class="mono">—</span>
<b>Ambient</b><span id="s-fx">—</span>
</div>
</div>
<div class="card">
<h2>Thử đèn</h2>
<div class="btns">
<button onclick="setState('IDLE')">IDLE</button>
<button onclick="setState('WORKING')">WORKING</button>
<button onclick="setState('INTERACT')">INTERACT</button>
</div>
<div class="hint">Bấm để xem màu ngay trên đèn thật.</div>
</div>
<div class="card">
<h2>LED</h2>
<div class="row">
<label for="bright">Độ sáng <span id="bv" class="mono"></span></label>
<input type="range" id="bright" min="1" max="255">
</div>
<div class="row">
<label for="pixels">Số LED</label>
<input type="number" id="pixels" min="1" max="24">
</div>
<div class="row">
<label for="order">Thứ tự màu</label>
<select id="order">
<option value="0">GRB — mặc định</option>
<option value="1">RGB — nếu đỏ ra xanh lá</option>
</select>
</div>
<div id="states"></div>
<div class="hint">Bỏ tick “nhịp” để đèn sáng đứng. Chu kỳ tính bằng ms.</div>
</div>
<div class="card">
<h2>Ambient</h2>
<div class="row">
<label for="idle">Chạy hiệu ứng sau khi IDLE (giây, 0 = tắt)</label>
<input type="number" id="idle" min="0" max="86400" step="30">
</div>
<div class="row">
<label for="fx">Hiệu ứng</label>
<select id="fx">
<option value="0">Tắt</option>
<option value="1">Nến cháy</option>
<option value="2">Cầu vồng trôi</option>
<option value="3">Nhịp thở</option>
<option value="4">Lấp lánh</option>
<option value="5">Cực quang</option>
</select>
</div>
<div class="row">
<label for="fxb">Độ sáng <span id="fxbv" class="mono"></span></label>
<input type="range" id="fxb" min="1" max="255">
</div>
<div class="row">
<label for="fxs">Tốc độ <span id="fxsv" class="mono"></span></label>
<input type="range" id="fxs" min="1" max="10">
</div>
<div class="row">
<label for="fxc">Màu (dùng cho Nhịp thở)</label>
<input type="color" id="fxc">
</div>
<div class="btns"><button onclick="preview()">Xem thử 15 giây</button></div>
<div class="hint">Chỉ có 1 LED nên đây đều là hiệu ứng theo thời gian. Cắt
thêm LED thì Cầu vồng, Lấp lánh và Cực quang tự thành hiệu ứng chạy dọc.
Độ sáng để thấp hơn hẳn trạng thái vì ambient chạy lúc bạn không nhìn.</div>
</div>
<div class="card">
<h2>WiFi</h2>
<div class="row">
<label>Nhớ tối đa 3 mạng — nhà, công ty, hotspot</label>
<button onclick="scan()" id="btn-scan">Quét</button>
</div>
<datalist id="nets"></datalist>
<div id="wifis"></div>
<div class="row"><label for="host">Hostname</label></div>
<div class="row"><input type="text" id="host" autocomplete="off"></div>
<div class="hint">Bật nguồn ở đâu, thiết bị tự quét và chọn mạng có tín hiệu
mạnh nhất trong danh sách. Xoá trống ô SSID để bỏ mạng đó.</div>
</div>
<div class="card">
<div class="btns">
<button class="pri" onclick="save(0)">Lưu</button>
<button onclick="save(1)">Lưu &amp; khởi động lại</button>
<button class="dgr" onclick="factoryReset()">Xoá cấu hình</button>
</div>
</div>
<div id="msg"></div>
<script>
var NAMES = ['IDLE', 'WORKING', 'INTERACT'];
function el(id) { return document.getElementById(id); }
function toast(t) {
var m = el('msg');
m.textContent = t;
m.classList.add('on');
setTimeout(function () { m.classList.remove('on'); }, 2200);
}
function buildWifis() {
var h = '';
for (var i = 0; i < 3; i++) {
h += '<div class="row"><label for="ssid' + i + '" style="flex:0 0 58px">#' +
(i + 1) + '</label>' +
'<input type="text" id="ssid' + i + '" list="nets" autocomplete="off" ' +
'placeholder="SSID"></div>' +
'<div class="row"><label style="flex:0 0 58px"></label>' +
'<input type="password" id="pass' + i + '" ' +
'placeholder="mật khẩu"></div>';
}
el('wifis').innerHTML = h;
}
function buildStates() {
var h = '';
for (var i = 0; i < 3; i++) {
h += '<div class="row">' +
'<label for="c' + i + '">' + NAMES[i] + '</label>' +
'<input type="color" id="c' + i + '">' +
'<label for="p' + i + '" style="flex:0 0 auto">nhịp</label>' +
'<input type="checkbox" id="p' + i + '">' +
'<input type="number" id="t' + i + '" min="200" max="10000" step="100">' +
'</div>';
}
el('states').innerHTML = h;
}
function loadConfig() {
fetch('/api/config').then(function (r) { return r.json(); }).then(function (c) {
for (var k = 0; k < 3; k++) {
el('ssid' + k).value = c.nets[k].ssid;
el('pass' + k).placeholder = c.nets[k].saved ? 'đã lưu — để trống là giữ nguyên'
: 'mật khẩu';
}
el('host').value   = c.host;
el('pixels').value = c.pixels;
el('bright').value = c.bright;
el('bv').textContent = c.bright;
el('order').value  = c.order;
for (var i = 0; i < 3; i++) {
el('c' + i).value   = c.col[i];
el('p' + i).checked = c.pulse[i] === 1;
el('t' + i).value   = c.period[i];
}
el('idle').value = c.idle;
el('fx').value   = c.fx;
el('fxb').value  = c.fxb;
el('fxs').value  = c.fxs;
el('fxc').value  = c.fxc;
el('fxbv').textContent = c.fxb;
el('fxsv').textContent = c.fxs;
}).catch(function () { toast('Không đọc được cấu hình'); });
}
function loadStatus() {
fetch('/api/status').then(function (r) { return r.json(); }).then(function (s) {
var col = { IDLE: '#22c55e', WORKING: '#f59e0b', INTERACT: '#ef4444' };
el('s-state').innerHTML =
'<span class="dot" style="background:' + (col[s.state] || '#888') + '"></span>' + s.state;
el('s-wifi').textContent = s.ap ? 'Chế độ AP (chưa cấu hình)'
: ((s.ssid || '—') + (s.slot >= 0 ? '  (#' + (s.slot + 1) + ')' : ''));
el('s-ip').textContent   = s.ip;
el('s-fx').textContent = s.fx ? ('đang chạy — ' + s.fxname)
: (s.fxname === 'off' ? 'tắt' : 'chờ ' + s.idlefor + 's');
el('s-rssi').textContent = s.ap ? '—' : (s.rssi + ' dBm');
var u = s.uptime, d = Math.floor(u / 86400), h = Math.floor(u % 86400 / 3600),
m = Math.floor(u % 3600 / 60);
el('s-up').textContent = (d ? d + 'd ' : '') + h + 'h ' + m + 'm';
el('hn').textContent = s.host ? s.host + '.local' : '';
}).catch(function () {});
}
function setState(s) {
fetch('/state?s=' + s).then(function () { loadStatus(); });
}
function scan() {
var b = el('btn-scan');
b.textContent = 'Đang quét…';
b.disabled = true;
fetch('/api/scan').then(function (r) { return r.json(); }).then(function (list) {
list.sort(function (a, b2) { return b2.rssi - a.rssi; });
var h = '';
for (var i = 0; i < list.length; i++) {
h += '<option value="' + list[i].ssid.replace(/"/g, '&quot;') + '">' +
list[i].rssi + ' dBm</option>';
}
el('nets').innerHTML = h;
toast('Thấy ' + list.length + ' mạng — bấm vào ô SSID để chọn');
}).catch(function () {
toast('Quét thất bại');
}).then(function () {
b.textContent = 'Quét';
b.disabled = false;
});
}
function save(thenReboot) {
var d = new URLSearchParams();
for (var k = 0; k < 3; k++) {
d.append('ssid' + k, el('ssid' + k).value);
d.append('pass' + k, el('pass' + k).value);
}
d.append('host',   el('host').value);
d.append('idle',   el('idle').value);
d.append('fx',     el('fx').value);
d.append('fxs',    el('fxs').value);
d.append('fxb',    el('fxb').value);
d.append('fxc',    el('fxc').value);
d.append('pixels', el('pixels').value);
d.append('bright', el('bright').value);
d.append('order',  el('order').value);
for (var i = 0; i < 3; i++) {
d.append('c' + i, el('c' + i).value);
d.append('p' + i, el('p' + i).checked ? '1' : '0');
d.append('t' + i, el('t' + i).value);
}
fetch('/api/config', {
method: 'POST',
headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
body: d.toString()
}).then(function (r) { return r.json(); }).then(function (res) {
for (var k = 0; k < 3; k++) el('pass' + k).value = '';
if (thenReboot || res.reboot) {
toast('Đã lưu — đang khởi động lại…');
fetch('/api/reboot', { method: 'POST' });
} else {
toast('Đã lưu, áp dụng ngay');
}
}).catch(function () { toast('Lưu thất bại'); });
}
function preview() {
fetch('/api/preview?fx=' + el('fx').value, { method: 'POST' })
.then(function (r) { return r.json(); })
.then(function (res) { toast('Xem thử ' + res.fx + ' — ' + res.seconds + 's'); })
.catch(function () { toast('Không xem thử được'); });
}
function factoryReset() {
if (!confirm('Xoá toàn bộ cấu hình, kể cả WiFi. Thiết bị sẽ quay về chế độ AP. Tiếp tục?')) return;
fetch('/api/reset', { method: 'POST' }).then(function () {
toast('Đã xoá — đang khởi động lại…');
});
}
buildWifis();
buildStates();
loadConfig();
loadStatus();
el('bright').addEventListener('input', function () { el('bv').textContent = this.value; });
el('fxb').addEventListener('input', function () { el('fxbv').textContent = this.value; });
el('fxs').addEventListener('input', function () { el('fxsv').textContent = this.value; });
setInterval(loadStatus, 3000);
</script>
</body></html>
)CCR";
/* ---8<--- PAGE END --- */
