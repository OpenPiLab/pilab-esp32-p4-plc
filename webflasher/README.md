# PiLab PLC ESP32-P4 Web Flasher

This is a GitHub Pages-ready single-page flasher for PiLab PLC firmware.

It uses ESP Web Tools, which depends on the browser Web Serial API. Use Chrome, Edge, or Brave on desktop.

## Current ESP-IDF flash layout

The attached `idf.py flash` output shows this exact write-flash command:

```bash
esptool --chip esp32p4 -p COM3 -b 460800 --before=default-reset --after=hard-reset write-flash \
  --flash-mode dio --flash-freq 40m --flash-size 16MB \
  0x2000 bootloader/bootloader.bin \
  0x8000 partition_table/partition-table.bin \
  0x29000 ota_data_initial.bin \
  0x40000 P4_PLC_Base.bin
```

So the default `manifest.json` uses the same offsets in decimal:

| File | Offset hex | Offset decimal |
|---|---:|---:|
| `firmware/bootloader.bin` | `0x2000` | `8192` |
| `firmware/partition-table.bin` | `0x8000` | `32768` |
| `firmware/ota_data_initial.bin` | `0x29000` | `167936` |
| `firmware/P4_PLC_Base.bin` | `0x40000` | `262144` |

## Files to copy after an ESP-IDF build

Create/copy these files into the `firmware/` folder:

```text
firmware/
  bootloader.bin
  partition-table.bin
  ota_data_initial.bin
  P4_PLC_Base.bin
```

From your ESP-IDF build folder, that usually means:

```powershell
copy build\bootloader\bootloader.bin docs\flasher\firmware\bootloader.bin
copy build\partition_table\partition-table.bin docs\flasher\firmware\partition-table.bin
copy build\ota_data_initial.bin docs\flasher\firmware\ota_data_initial.bin
copy build\P4_PLC_Base.bin docs\flasher\firmware\P4_PLC_Base.bin
```

Adjust the destination path if your flasher folder is somewhere else.

## Local test

Web Serial requires a secure context. `localhost` is allowed:

```bash
cd pilab_web_flasher_update
python -m http.server 8000
```

Then open:

```text
http://localhost:8000
```

GitHub Pages also works because it serves over HTTPS.

## Optional merged binary approach

The default project uses a multi-part manifest because it maps directly to the ESP-IDF flash output. If you prefer one file, generate a merged image and switch `index.html` to use `manifest-merged.json`.

With esptool v5, use `merge-bin`:

```bash
python -m esptool --chip esp32p4 merge-bin \
  -o firmware/pilab-plc-merged.bin \
  --flash-mode dio \
  --flash-freq 40m \
  --flash-size 16MB \
  0x2000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x29000 build/ota_data_initial.bin \
  0x40000 build/P4_PLC_Base.bin
```

Espressif documents `merge-bin` as the command for combining several binaries into a single flash image.

## Publishing on GitHub Pages

One simple option is to put this folder under your repository as:

```text
docs/flasher/
```

Then the page will be available at something like:

```text
https://openpilab.github.io/pilab-esp32-p4-plc/flasher/
```

## Notes

- The board flash size shown by the ESP-IDF flash command is `16MB`.
- Flash mode is `dio`.
- Flash frequency is `40m`.
- If ESP Web Tools reports that ESP32-P4 is unsupported in your browser/package version, the manifest may be correct but the web flashing library may need a newer release or a fallback esptool-js page.
