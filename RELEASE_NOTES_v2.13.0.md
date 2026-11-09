## Glove-M v2.13.0

Firmware cho thiết bị `Glove-M` (ESP32-C3, 4 MB flash).

### Thay đổi so với zclaw upstream

- **ESP-NOW 2 chiều** — transport `espnow.c` + tool `espnow_send`, nhận được message từ peer.
- **Peripheral drivers** — nút bấm, rotary encoder, cảm biến analog, OLED (SSD1306), LCD (ST7789), microphone, speaker. Tool `peripheral_write` / `peripheral_read`.
- **Request builder không dùng cJSON** — `main/json_builder.c`. Lý do: build request cũ cần ~52 KB heap cùng lúc (cây cJSON + printbuffer + bản copy), vượt quá heap thực tế của ESP32-C3 và gây lỗi `Failed to build request JSON`.
- **Model rotation** — model chính bị drop nếu lỗi 2 lần liên tiếp hoặc chậm > 18 s, sau đó chuyển sang model free dự phòng.
- **Partition OTA mở rộng** — mỗi slot 1.875 MB.

### Cấu hình mặc định

| Mục | Giá trị |
|---|---|
| Backend | `openrouter` |
| Model chính | `deepseek/deepseek-v4.1-flash-1017` |
| Fallback | các model `:free` trên OpenRouter |

### Tải firmware

| File | Offset | Dung lượng |
|---|---|---|
| `glove-m-v2.13.0-esp32c3.bin` | `0x20000` | 998 KiB |
| `bootloader.bin` | `0x0` | 21 KiB |
| `partition-table.bin` | `0x8000` | 3 KiB |

### Nạp firmware

```sh
esptool.py --chip esp32c3 --port COM12 write_flash \
    0x0     bootloader.bin \
    0x8000  partition-table.bin \
    0x20000 glove-m-v2.13.0-esp32c3.bin
```

### Provision credentials

Credentials (WiFi, API key, Telegram token) nằm trong NVS, **không** có trong image:

```sh
bash scripts/provision.sh --port COM12 \
    --ssid "<wifi-ssid>" --pass "<wifi-pass>" \
    --backend openrouter --model "deepseek/deepseek-v4.1-flash-1017" \
    --api-key "<openrouter-key>" \
    --tg-token "<bot-token>" --tg-chat-ids "<chat-id>" \
    --yes
```

### Build từ source

```sh
idf.py -B build-esp32c3 build
```

Yêu cầu ESP-IDF v5.5.x (giữ cho board legacy) hoặc v6.0.2.
