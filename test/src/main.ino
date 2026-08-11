/*
 * Test WS2812 - 1 LED - Wemos D1 Mini
 *
 * ĐẤU NỐI:
 *   strip VCC (5V) -> D1 Mini 3V3      <- dùng 3V3, không phải 5V
 *   strip DIN      -> D1 Mini D9 / RX  (GPIO3, bắt buộc cho DMA)
 *   strip GND      -> D1 Mini G
 *
 * Hàn vào đầu có mũi tên CHỈ ĐI RA XA. Cắm ngược thì LED không sáng,
 * không hư gì - nhưng đảo VCC/GND sẽ cháy IC.
 *
 * PIXEL_COUNT và BRIGHT được set trong platformio.ini (build_flags).
 *
 * LƯU Ý VỀ SERIAL: DMA chiếm GPIO3 (chân RX), nên Serial chỉ GỬI ra được,
 * không nhận vào. Serial Monitor vẫn xem log bình thường.
 * Nếu cần RX, đổi sang NeoEsp8266Uart1800KbpsMethod (dùng GPIO2 / D4).
 */

#include <Arduino.h>
#include <NeoPixelBus.h>

#ifndef PIXEL_COUNT
#define PIXEL_COUNT 1
#endif
#ifndef BRIGHT
#define BRIGHT 60
#endif

// Nếu gọi đỏ mà ra xanh lá -> đổi NeoGrbFeature thành NeoRgbFeature
NeoPixelBus<NeoGrbFeature, NeoEsp8266Dma800KbpsMethod> strip(PIXEL_COUNT);

void fill(uint8_t r, uint8_t g, uint8_t b) {
  for (uint16_t i = 0; i < PIXEL_COUNT; i++) {
    strip.SetPixelColor(i, RgbColor(r, g, b));
  }
  strip.Show();
}

void hold(const char* label, uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
  Serial.printf("  %s\n", label);
  fill(r, g, b);
  delay(ms);
}

// Nhịp đập cho trạng thái INTERACT - chu kỳ 2 giây
void breathe(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    float phase = (millis() - t0) / 1000.0f * PI;
    float k = 0.15f + 0.85f * (0.5f - 0.5f * cosf(phase));
    fill((uint8_t)(r * k), (uint8_t)(g * k), (uint8_t)(b * k));
    delay(16);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.printf("=== WS2812 test | %d pixel | brightness %d ===\n",
                PIXEL_COUNT, BRIGHT);

  strip.Begin();
  strip.Show();          // tắt hết trước khi bắt đầu
  delay(500);
}

void loop() {
  // GIAI ĐOẠN 1 - kiểm tra thứ tự màu.
  // Phải thấy đúng thứ tự: đỏ, xanh lá, xanh dương.
  Serial.println("[1] Kiem tra thu tu mau");
  hold("do",          BRIGHT, 0, 0, 1200);
  hold("xanh la",     0, BRIGHT, 0, 1200);
  hold("xanh duong",  0, 0, BRIGHT, 1200);
  hold("trang",       BRIGHT, BRIGHT, BRIGHT, 1200);
  fill(0, 0, 0);
  delay(800);

  // GIAI ĐOẠN 2 - xem trước 3 trạng thái của cc-reminder.
  Serial.println("[2] Xem truoc trang thai cc-reminder");
  hold("IDLE (xanh la)",      0, BRIGHT, 0, 2000);
  hold("WORKING (vang cam)",  BRIGHT, (uint8_t)(BRIGHT * 0.35f), 0, 2000);

  Serial.println("  INTERACT (do, dap nhip)");
  breathe(BRIGHT, 0, 0, 4000);

  fill(0, 0, 0);
  Serial.println("--- xong, lap lai sau 2s ---\n");
  delay(2000);
}
