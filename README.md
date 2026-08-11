# cc-reminder-esp8266

Đèn báo trạng thái Claude Code, chạy trên Wemos D1 Mini (ESP8266) + WS2812.
Kèm vỏ in 3D tham số hoá.

Port từ [Curlyfuu/cc_reminder](https://github.com/Curlyfuu/cc_reminder) — bản
gốc dùng ESP32-C3 + BLE. ESP8266 không có Bluetooth nên bản này chuyển sang
WiFi/HTTP, và hoá ra lại nhanh hơn: mỗi hook mất ~50ms thay vì 1–3 giây scan BLE.
Đổi lại, đèn cắm nguồn USB ở đâu cũng được, không cần ở gần máy tính.

![Vỏ lắp hoàn chỉnh](docs/images/case-assembled.png)

| Màu | Trạng thái | Khi nào |
|---|---|---|
| Xanh lá, sáng đứng | `IDLE` | Claude Code rảnh |
| Vàng cam, đập chậm | `WORKING` | đang chạy |
| Đỏ, đập nhanh | `INTERACT` | đang xin xác nhận quyền |

## Phần cứng

| Món | Ghi chú |
|---|---|
| Wemos D1 Mini (ESP8266) | |
| 1 LED WS2812B | cắt ra từ dây strip |
| 3 sợi dây 26–28 AWG | dây nhiều sợi mảnh, đừng dùng dây đơn cứng |

### Đấu nối

| Strip | D1 Mini |
|---|---|
| VCC (5V) | **3V3** |
| DIN | **D9 / RX (GPIO3)** |
| GND | G |

Hai điểm dễ sai:

**GPIO3 là bắt buộc.** Thư viện `NeoPixelBus` chỉ chạy được DMA trên chân này.
Nếu dùng bit-bang (`Adafruit_NeoPixel`) thì WiFi interrupt sẽ cắt ngang làm sai
timing → LED nhảy màu ngẫu nhiên. Đây là lỗi kinh điển của combo
ESP8266 + WS2812 + WiFi.

**Cấp nguồn từ 3V3, không phải 5V.** Ngưỡng HIGH của WS2812 là 0.7 × Vdd. Ở
3.3V thì ngưỡng chỉ ~2.3V, khớp thoải mái với mức GPIO. Nếu cấp 5V thì ngưỡng
lên 3.5V, cao hơn mức GPIO xuất ra — chạy được nhưng không ổn định. Với 1 LED
thì 3V3 dư sức.

Nếu dùng nhiều LED và buộc phải cấp 5V: mắc 1 diode 1N4148 nối tiếp trên dây
nguồn, sụt 0.6V xuống ~4.4V để hạ ngưỡng về ~3.1V.

## Cài đặt

### 1. Test LED trước

```bash
cd test
pio run -t upload && pio device monitor
```

Phải thấy đỏ → xanh lá → xanh dương → trắng. Nếu đỏ và xanh lá đổi chỗ nhau,
sửa `NeoGrbFeature` thành `NeoRgbFeature`.

### 2. Nạp firmware

```bash
cd firmware
cp src/config.h.example src/config.h
$EDITOR src/config.h          # dien WiFi SSID / password
pio run -t upload
```

`config.h` nằm trong `.gitignore` nên mật khẩu WiFi không bị commit lên.

Kiểm tra: `curl http://cc-reminder.local/`

### 3. Cấu hình hook Claude Code

Chép nội dung `host/settings.example.json` vào `~/.claude/settings.json`, sửa
đường dẫn cho đúng.

Nếu mDNS `.local` không resolve được (thường gặp trên WSL), đặt IP tĩnh:

```bash
export CC_REMINDER_HOST=192.168.1.50
```

Script luôn trả về exit code 0 kể cả khi không kết nối được, nên đèn offline sẽ
không làm Claude Code bị fail.

## Vỏ in 3D

![Các part](docs/images/case-parts.png)

STL nằm trong `hardware/stl/`, đã lật sẵn đúng hướng in.

| File | Màu | |
|---|---|---|
| `base.stl` | **đen** | đế. Màu đen quan trọng — chặn sáng loang ra đáy |
| `diffuser.stl` | **trắng** trong/sữa | nắp tán sáng, vành răng lược |
| `led_bar.stl` | đen | thanh đỡ vắt ngang, giữ LED ở giữa |
| `led_clip.stl` | đen | thanh gài, trượt ngang để ép LED |
| `fit_test.stl` | bất kỳ | miếng thử khớp, xem bên dưới |

Tổng thể 46.6 × 46.6 × 27mm, lòng trong 40 × 40mm.

### In `fit_test.stl` trước

Miếng thử 15×34mm, in ~5 phút, gồm đúng phần thành USB + hai đầu hốc board.
Đặt board vào, cắm dây USB, rồi đối chiếu:

| Kết quả | Sửa biến | Cách sửa |
|---|---|---|
| Board chặt quá / không vào | `pcb_fit` | tăng 0.1 mỗi lần |
| Board lỏng, lắc được | `pcb_fit` | giảm về 0.15, hoặc `crush_h` lên 0.5 |
| Lỗ USB lệch cao/thấp | `usb_off_z` | + lên, − xuống |
| Lỗ USB lệch ngang | `usb_off_y` | + là sang +Y |
| Phích không tới cổng | `usb_cb_d` | tăng lên 2.8 |
| Nắp chặt quá / còn lỏng | `lip_nub` | giảm 0.15 / tăng 0.35 |

Chỉ khi miếng thử khớp mới in `base.stl`. Board mỗi lô lệch nhau 0.2–0.3mm nên
bước này gần như luôn cần.

### Thông số in

| | base / led_bar / led_clip | diffuser |
|---|---|---|
| Layer | 0.2mm | 0.2mm |
| Wall loops | 4 | **2** (giữ đúng 0.8mm cho răng lược) |
| Infill | 25% | **0%** |
| Top/bottom | 4 | **5** |
| Support | không | không |

`diffuser` in **úp ngược** — mặt tán sáng áp xuống bàn nên phẳng mịn nhất.
STL đã lật sẵn.

### Sửa thiết kế

Mở `hardware/cc_reminder_case.scad`, các biến đều ở đầu file.

```bash
openscad -D 'part="base"' -o base.stl cc_reminder_case.scad
```

`part` nhận: `base`, `diffuser`, `led_bar`, `led_clip`, `fit_test`, `all`.

### Thứ tự lắp

1. Luồn 3 dây LED qua khe trên `led_bar`
2. Đặt strip vào hốc giữa 2 ray
3. **Trượt** `led_clip` vào từ đầu hở, đẩy tới khi chạm chân chặn
4. Đặt D1 Mini vào đế — gờ bù sai số sẽ hơi cấn, đó là bình thường
5. Nối dây theo bảng ở trên
6. Trượt `led_bar` xuống 2 rãnh ở thành ±Y
7. Ấn nắp vào — 8 nub trên gờ cắm sẽ giữ khít

## API

| Endpoint | |
|---|---|
| `GET /` | trang trạng thái dạng text (state, IP, RSSI, uptime) |
| `GET /state?s=IDLE\|WORKING\|INTERACT` | đổi trạng thái |
| `GET /status` | tên trạng thái hiện tại |

## Ghi chú kỹ thuật

- **Serial:** DMA chiếm GPIO3 nên Serial chỉ gửi ra được, không nhận vào.
  Serial Monitor vẫn xem log bình thường. Cần RX thì đổi sang
  `NeoEsp8266Uart1800KbpsMethod` (dùng GPIO2).
- **Giới hạn dòng GPIO:** ESP8266 là 12mA/chân, ESP32 là 20mA. Đây là lý do
  bản gốc ESP32 có thể cắm LED rời không cần điện trở mà vẫn chạy, còn ESP8266
  thì không nên. WS2812 tránh được vấn đề này vì có IC hằng dòng tích hợp.
- **Chân cần tránh trên D1 Mini:** D3 (GPIO0) và D8 (GPIO15) quyết định boot
  mode; D0 (GPIO16) không có PWM; D4 (GPIO2) đã có LED onboard và là TX1 lúc boot.

## License

MIT
