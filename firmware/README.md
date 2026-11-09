# Firmware builds

Pre-built firmware images for the `Glove-M` device.

## ESP32-C3 (`esp32c3/`)

| File | Offset | Size | Purpose |
|---|---|---|---|
| `glove-m-v2.13.0-esp32c3.bin` | `0x20000` | 998 KiB | Application, flashed to the `ota_0` slot |
| `bootloader.bin` | `0x0` | 21 KiB | Second-stage bootloader |
| `partition-table.bin` | `0x8000` | 3 KiB | 4 MB layout with two 1.875 MB OTA slots |

The application image is built for the `ota_0` slot. To flash it to the
other slot instead, flash it to `0x290000` and let the bootloader pick the
active one from `otadata`.

### Flash with `esptool`

```sh
esptool.py --chip esp32c3 --port COM12 write_flash \
    0x0    bootloader.bin \
    0x8000 partition-table.bin \
    0x20000 glove-m-v2.13.0-esp32c3.bin
```

### Flash with `idf.py`

```sh
idf.py -B build-esp32c3 -p COM12 flash
```

## Rebuilding

The images above come from the `build-esp32c3` directory. To refresh them:

```sh
idf.py -B build-esp32c3 build
cp build-esp32c3/zclaw.bin              firmware/esp32c3/glove-m-v2.13.0-esp32c3.bin
cp build-esp32c3/bootloader/bootloader.bin firmware/esp32c3/bootloader.bin
cp build-esp32c3/partition_table/partition-table.bin firmware/esp32c3/partition-table.bin
```

Bump the file name whenever the project version in `main/config.h` changes.

## Notes

- The application partition holds 1.875 MB per OTA slot, so the image fits
  with roughly 48% of the slot still free.
- Runtime credentials (WiFi, API key, Telegram token) live in NVS and are
  **not** part of these images. Provision them with
  `scripts/provision.sh` after flashing.
