# ESP32-C3 WiFi Setup

ESP32-C3 phat WiFi rieng, ban ket noi vao de cau hinh mang WiFi nha qua trinh duyet.

## Cach su dung

1. Nap firmware len ESP32-C3
2. Tren dien thoai/laptop, ket noi WiFi **`ESP32-C3-Setup`** (mat khau: `12345678`)
3. Trinh duyet tu mo trang (hoac vao `http://192.168.4.1`) — trang mo ngay
4. Danh sach WiFi duoc **quet nen** tu dong; bam chon mang hoac go tay SSID + mat khau
5. Bam **Luu va ket noi** (co nut **Quet lai** neu can)
5. ESP32 luu cau hinh va tu ket noi vao WiFi nha

## Nap firmware

```bash
pio run -t upload --upload-port /dev/ttyACM0
```

## Xem log Serial

```bash
pio device monitor --port /dev/ttyACM0
```

Baud rate: **115200**

## Cau hinh (tuy chon)

Sua `include/wifi_config.h`:

- `AP_SSID` — ten WiFi ma ESP32 phat ra
- `AP_PASSWORD` — mat khau AP (de trong neu muon WiFi mo)
- `CONFIG_PORTAL_TIMEOUT_SEC` — thoi gian cho cau hinh

## Reset cau hinh WiFi

Xoa flash hoac them nut reset trong code. Cach nhanh: xoa NVS bang esptool:

```bash
esptool.py --chip esp32c3 --port /dev/ttyACM0 erase_flash
```

Sau do nap lai firmware.

## Luu y

- Chi ho tro WiFi **2.4 GHz**
- Neu upload loi, giu nut **BOOT** khi bat dau upload
# test-wifi-esp32
