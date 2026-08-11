/*
 * cc-reminder - ESP8266 (Wemos D1 Mini) + WS2812
 * Ban co TRANG WEB CAU HINH: WiFi + LED, luu vao EEPROM.
 *
 * Lan dau bat nguon (hoac khi khong noi duoc WiFi cu):
 *   -> tu tao AP ten "cc-reminder-setup" (mo, khong mat khau)
 *   -> ket noi vao roi mo http://192.168.4.1
 *      (dien thoai thuong tu bung trang len nho captive portal)
 *   -> dien WiFi, Luu & khoi dong lai
 *
 * Sau khi vao duoc WiFi nha: http://cc-reminder.local  (hoac IP)
 *
 * DAU NOI
 *   strip VCC -> 3V3
 *   strip DIN -> D9 / RX (GPIO3 - bat buoc, DMA chi chay chan nay)
 *   strip GND -> G
 *
 * API (giu nguyen de host/cc_reminder_http.py khong phai sua)
 *   GET /state?s=IDLE|WORKING|INTERACT
 *   GET /status
 *
 * File nay la .cpp cho PlatformIO. Dung Arduino IDE thi doi ten
 * thanh main.ino va bo dong #include <Arduino.h>.
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <NeoPixelBus.h>

/* ===================== hang so ===================== */
#define MAX_PIXELS   24
#define CFG_MAGIC    0x43524D33UL      // doi so nay = xoa cau hinh cu
#define AP_SSID      "cc-reminder-setup"
#define EEPROM_SIZE  512

const uint8_t ST_IDLE     = 0;
const uint8_t ST_WORKING   = 1;
const uint8_t ST_INTERACT  = 2;

/* ===================== cau hinh ===================== */
struct Config {
  uint32_t magic;
  char     ssid[33];
  char     pass[65];
  char     host[24];
  uint8_t  pixels;
  uint8_t  bright;
  uint8_t  order;          // 0 = GRB (mac dinh), 1 = RGB
  uint8_t  col[3][3];      // [state][r,g,b]
  uint8_t  pulse[3];       // 0 = sang dung, 1 = dap nhip
  uint16_t period[3];      // chu ky nhip dap, ms
};

Config cfg;

void setDefaults() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic  = CFG_MAGIC;
  cfg.ssid[0] = '\0';
  cfg.pass[0] = '\0';
  strncpy(cfg.host, "cc-reminder", sizeof(cfg.host) - 1);
  cfg.pixels = 1;
  cfg.bright = 60;
  cfg.order  = 0;

  // IDLE - xanh la, sang dung
  cfg.col[0][0] = 0;   cfg.col[0][1] = 255; cfg.col[0][2] = 0;
  cfg.pulse[0]  = 0;   cfg.period[0] = 2000;
  // WORKING - vang cam, dap cham
  cfg.col[1][0] = 255; cfg.col[1][1] = 90;  cfg.col[1][2] = 0;
  cfg.pulse[1]  = 1;   cfg.period[1] = 2400;
  // INTERACT - do, dap nhanh
  cfg.col[2][0] = 255; cfg.col[2][1] = 0;   cfg.col[2][2] = 0;
  cfg.pulse[2]  = 1;   cfg.period[2] = 900;
}

void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, cfg);
  if (cfg.magic != CFG_MAGIC) {
    Serial.println("eeprom trong -> dung mac dinh");
    setDefaults();
    return;
  }
  // chan gia tri sai lam treo thiet bi
  if (cfg.pixels < 1 || cfg.pixels > MAX_PIXELS) cfg.pixels = 1;
  if (cfg.bright < 1) cfg.bright = 1;
  if (cfg.order > 1) cfg.order = 0;
  for (uint8_t i = 0; i < 3; i++) {
    if (cfg.period[i] < 200)   cfg.period[i] = 900;
    if (cfg.period[i] > 10000) cfg.period[i] = 10000;
    if (cfg.pulse[i] > 1)      cfg.pulse[i] = 0;
  }
  cfg.ssid[sizeof(cfg.ssid) - 1] = '\0';
  cfg.pass[sizeof(cfg.pass) - 1] = '\0';
  cfg.host[sizeof(cfg.host) - 1] = '\0';
  if (cfg.host[0] == '\0') strncpy(cfg.host, "cc-reminder", sizeof(cfg.host) - 1);
}

void saveConfig() {
  cfg.magic = CFG_MAGIC;
  EEPROM.put(0, cfg);
  EEPROM.commit();
  Serial.println("eeprom da luu");
}

/* ===================== LED ===================== */
// Cap phat co dinh MAX_PIXELS, chi dieu khien cfg.pixels con dau.
NeoPixelBus<NeoGrbFeature, NeoEsp8266Dma800KbpsMethod> strip(MAX_PIXELS);

uint8_t state = ST_IDLE;

void writeAll(uint8_t r, uint8_t g, uint8_t b) {
  // cfg.order == 1 (strip RGB): dao r/g vi feature dang la GRB
  RgbColor c = (cfg.order == 1) ? RgbColor(g, r, b) : RgbColor(r, g, b);
  for (uint16_t i = 0; i < MAX_PIXELS; i++) {
    strip.SetPixelColor(i, (i < cfg.pixels) ? c : RgbColor(0, 0, 0));
  }
  strip.Show();
}

// he so 0..1 theo nhip dap
float pulseK(uint16_t periodMs, float floorK) {
  if (periodMs < 1) periodMs = 1;
  float deg = (float)(millis() % periodMs) / (float)periodMs * 360.0f;
  float s = 0.5f - 0.5f * cos(deg * PI / 180.0f);
  return floorK + (1.0f - floorK) * s;
}

void renderLed() {
  static unsigned long last = 0;
  if (millis() - last < 20) return;        // ~50Hz
  last = millis();

  uint8_t s = state;
  float k = 1.0f;
  if (cfg.pulse[s]) k = pulseK(cfg.period[s], (s == ST_INTERACT) ? 0.15f : 0.40f);

  float bk = ((float)cfg.bright / 255.0f) * k;
  writeAll((uint8_t)(cfg.col[s][0] * bk),
           (uint8_t)(cfg.col[s][1] * bk),
           (uint8_t)(cfg.col[s][2] * bk));
}

/* ===================== web ===================== */
ESP8266WebServer server(80);
DNSServer dns;
bool apMode = false;

extern const char PAGE_HTML[] PROGMEM;

const char* stateName(uint8_t s) {
  if (s == ST_WORKING)  return "WORKING";
  if (s == ST_INTERACT) return "INTERACT";
  return "IDLE";
}

String hex3(const uint8_t* c) {
  char b[10];
  snprintf(b, sizeof(b), "#%02x%02x%02x", c[0], c[1], c[2]);
  return String(b);
}

// "#rrggbb" -> 3 byte. Tra ve false neu chuoi khong dung dang.
bool parseHex3(const String& s, uint8_t* out) {
  if (s.length() < 7 || s.charAt(0) != '#') return false;
  for (uint8_t i = 0; i < 3; i++) {
    out[i] = (uint8_t)strtol(s.substring(1 + i * 2, 3 + i * 2).c_str(), NULL, 16);
  }
  return true;
}

void handleRoot() {
  server.send_P(200, "text/html", PAGE_HTML);
}

void handleApiStatus() {
  String o = "{";
  o += "\"state\":\"";  o += stateName(state);      o += "\",";
  o += "\"ap\":";       o += (apMode ? "true" : "false"); o += ",";
  o += "\"ssid\":\"";   o += WiFi.SSID();           o += "\",";
  o += "\"ip\":\"";     o += (apMode ? WiFi.softAPIP().toString()
                                    : WiFi.localIP().toString()); o += "\",";
  o += "\"host\":\"";   o += cfg.host;              o += "\",";
  o += "\"rssi\":";     o += WiFi.RSSI();           o += ",";
  o += "\"uptime\":";   o += (millis() / 1000);     o += ",";
  o += "\"heap\":";     o += ESP.getFreeHeap();
  o += "}";
  server.send(200, "application/json", o);
}

void handleApiConfigGet() {
  String o = "{";
  o += "\"ssid\":\"";  o += cfg.ssid;   o += "\",";
  o += "\"host\":\"";  o += cfg.host;   o += "\",";
  o += "\"pixels\":";  o += cfg.pixels; o += ",";
  o += "\"bright\":";  o += cfg.bright; o += ",";
  o += "\"order\":";   o += cfg.order;  o += ",";
  o += "\"col\":[";
  for (uint8_t i = 0; i < 3; i++) {
    o += "\""; o += hex3(cfg.col[i]); o += "\"";
    if (i < 2) o += ",";
  }
  o += "],\"pulse\":[";
  for (uint8_t i = 0; i < 3; i++) { o += cfg.pulse[i];  if (i < 2) o += ","; }
  o += "],\"period\":[";
  for (uint8_t i = 0; i < 3; i++) { o += cfg.period[i]; if (i < 2) o += ","; }
  o += "]}";
  // mat khau WiFi khong bao gio gui ve trinh duyet
  server.send(200, "application/json", o);
}

void handleApiConfigPost() {
  bool needReboot = false;

  if (server.hasArg("ssid")) {
    String v = server.arg("ssid");
    if (v != String(cfg.ssid)) {
      strncpy(cfg.ssid, v.c_str(), sizeof(cfg.ssid) - 1);
      cfg.ssid[sizeof(cfg.ssid) - 1] = '\0';
      needReboot = true;
    }
  }
  // chi ghi mat khau khi nguoi dung go moi (o trong = giu nguyen)
  if (server.hasArg("pass") && server.arg("pass").length() > 0) {
    strncpy(cfg.pass, server.arg("pass").c_str(), sizeof(cfg.pass) - 1);
    cfg.pass[sizeof(cfg.pass) - 1] = '\0';
    needReboot = true;
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

  const char* ck[3] = { "c0", "c1", "c2" };
  const char* pk[3] = { "p0", "p1", "p2" };
  const char* tk[3] = { "t0", "t1", "t2" };
  for (uint8_t i = 0; i < 3; i++) {
    if (server.hasArg(ck[i])) {
      uint8_t rgb[3];
      if (parseHex3(server.arg(ck[i]), rgb)) {
        cfg.col[i][0] = rgb[0]; cfg.col[i][1] = rgb[1]; cfg.col[i][2] = rgb[2];
      }
    }
    if (server.hasArg(pk[i])) cfg.pulse[i] = (server.arg(pk[i]).toInt() == 1) ? 1 : 0;
    if (server.hasArg(tk[i]))
      cfg.period[i] = (uint16_t)constrain(server.arg(tk[i]).toInt(), 200, 10000);
  }

  saveConfig();
  String o = "{\"ok\":true,\"reboot\":";
  o += (needReboot ? "true" : "false");
  o += "}";
  server.send(200, "application/json", o);
}

void handleApiScan() {
  int n = WiFi.scanNetworks();
  String o = "[";
  for (int i = 0; i < n && i < 20; i++) {
    if (i) o += ",";
    String ss = WiFi.SSID(i);
    ss.replace("\\", "\\\\");
    ss.replace("\"", "\\\"");
    o += "{\"ssid\":\""; o += ss;
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
  Serial.printf("-> %s\n", stateName(state));
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
  // O che do AP: dieu huong moi thu ve trang cau hinh (captive portal)
  if (apMode) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "NOT_FOUND\n");
}

void setupRoutes() {
  server.on("/",              HTTP_GET,  handleRoot);
  server.on("/api/status",    HTTP_GET,  handleApiStatus);
  server.on("/api/config",    HTTP_GET,  handleApiConfigGet);
  server.on("/api/config",    HTTP_POST, handleApiConfigPost);
  server.on("/api/scan",      HTTP_GET,  handleApiScan);
  server.on("/api/reboot",    HTTP_POST, handleReboot);
  server.on("/api/reset",     HTTP_POST, handleFactoryReset);
  // giu API cu cho host script
  server.on("/state",         HTTP_GET,  handleSetState);
  server.on("/status",        HTTP_GET,  handleStatusText);
  server.onNotFound(handleNotFound);
}

/* ===================== WiFi ===================== */
void startAP() {
  apMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  Serial.printf("AP: %s -> http://%s/\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

bool connectSTA() {
  apMode = false;
  WiFi.mode(WIFI_STA);
  WiFi.hostname(cfg.host);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(cfg.ssid, cfg.pass);

  Serial.printf("noi wifi \"%s\"", cfg.ssid);
  for (uint8_t i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) {
    // nhay xanh duong trong luc cho
    writeAll(0, 0, (i % 2) ? 30 : 0);
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("that bai");
    return false;
  }
  Serial.print("ip: ");
  Serial.println(WiFi.localIP());
  if (MDNS.begin(cfg.host)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mdns: http://%s.local/\n", cfg.host);
  }
  return true;
}

/* ===================== setup / loop ===================== */
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== cc-reminder (web config) ===");

  loadConfig();

  strip.Begin();
  strip.Show();
  writeAll(0, 0, 30);          // xanh duong mo = dang khoi dong

  bool ok = false;
  if (strlen(cfg.ssid) > 0) ok = connectSTA();
  if (!ok) startAP();

  setupRoutes();
  server.begin();
  state = ST_IDLE;
  Serial.println("web server san sang");
}

void loop() {
  if (apMode) {
    dns.processNextRequest();
    // o che do AP: dap xanh duong de biet dang cho cau hinh
    static unsigned long t = 0;
    if (millis() - t > 20) {
      t = millis();
      float k = pulseK(1600, 0.10f);
      writeAll(0, 0, (uint8_t)(60 * k));
    }
  } else {
    MDNS.update();
    renderLed();
  }
  server.handleClient();
}

/* ===================== trang web =====================
   Phan duoi day do tools/embed_page.py sinh ra tu firmware/web/page.html.
   SUA HTML O FILE DO, roi chay: python3 tools/embed_page.py
   Dung sua truc tiep trong day.                                         */
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
<h2>WiFi</h2>
<div class="row">
<label for="ssid">SSID</label>
<button onclick="scan()" id="btn-scan">Quét</button>
</div>
<div class="row"><input type="text" id="ssid" list="nets" autocomplete="off">
<datalist id="nets"></datalist></div>
<div class="row"><label for="pass">Mật khẩu</label></div>
<div class="row"><input type="password" id="pass" placeholder="để trống = giữ nguyên"></div>
<div class="row"><label for="host">Hostname</label></div>
<div class="row"><input type="text" id="host" autocomplete="off"></div>
<div class="hint">Đổi WiFi, hostname hoặc số LED thì thiết bị cần khởi động lại.</div>
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
el('ssid').value   = c.ssid;
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
}).catch(function () { toast('Không đọc được cấu hình'); });
}
function loadStatus() {
fetch('/api/status').then(function (r) { return r.json(); }).then(function (s) {
var col = { IDLE: '#22c55e', WORKING: '#f59e0b', INTERACT: '#ef4444' };
el('s-state').innerHTML =
'<span class="dot" style="background:' + (col[s.state] || '#888') + '"></span>' + s.state;
el('s-wifi').textContent = s.ap ? 'Chế độ AP (chưa cấu hình)' : (s.ssid || '—');
el('s-ip').textContent   = s.ip;
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
d.append('ssid',   el('ssid').value);
d.append('pass',   el('pass').value);
d.append('host',   el('host').value);
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
el('pass').value = '';
if (thenReboot || res.reboot) {
toast('Đã lưu — đang khởi động lại…');
fetch('/api/reboot', { method: 'POST' });
} else {
toast('Đã lưu, áp dụng ngay');
}
}).catch(function () { toast('Lưu thất bại'); });
}
function factoryReset() {
if (!confirm('Xoá toàn bộ cấu hình, kể cả WiFi. Thiết bị sẽ quay về chế độ AP. Tiếp tục?')) return;
fetch('/api/reset', { method: 'POST' }).then(function () {
toast('Đã xoá — đang khởi động lại…');
});
}
buildStates();
loadConfig();
loadStatus();
el('bright').addEventListener('input', function () { el('bv').textContent = this.value; });
setInterval(loadStatus, 3000);
</script>
</body></html>
)CCR";
/* ---8<--- PAGE END --- */
