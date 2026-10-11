# Ender 3 S1 ESP32-S3 Controller

An open-source remote controller for Marlin-based Ender 3 S1 printers. The ESP32-S3 connects to the printer through USB Host and exposes a local web dashboard for monitoring, printer control, file management, and remote G-code printing.

## Features

- Live hotend, bed, position, and print-progress monitoring
- Web-based jog movement and auto-home controls
- Manual G-code console
- Printer SD-card file listing and SD printing
- G-code upload to ESP32 storage
- SD-independent printing by streaming a file from ESP32 SPIFFS over USB
- Optional copy of ESP32 files to the printer SD card
- WebSocket updates for live status and progress
- AP mode for a standalone network or Station mode for a home Wi-Fi network

> This project is designed for local-network use. Do not expose the device directly to the internet.

## Hardware

- Creality Ender 3 S1 or a compatible Marlin printer
- ESP32-S3 N16R8 board
  - 16 MB Flash
  - 8 MB PSRAM
- Data-capable USB-C cable
- USB connection for flashing the ESP32-S3

Connect the printer to the ESP32-S3 USB Host/OTG USB-C port. Charge-only USB cables will not work.

## How it works

```text
Web browser
    │ HTTP REST + WebSocket
    ▼
ESP32-S3 controller
    │ USB Host CDC/ACM
    ▼
Ender 3 S1 Marlin firmware
```

There are two independent file sources:

| File source | Print method | Printer SD card required? |
|---|---|---:|
| Printer SD card | Marlin `M23` + `M24` | Yes |
| ESP32 SPIFFS | USB G-code streaming | No |

An uploaded ESP32 file stays in ESP32 storage and can be printed without a printer SD card.

## Requirements

- VS Code with the PlatformIO extension, or PlatformIO CLI
- Node.js and npm
- A suitable ESP32-S3 USB driver on the host computer

## Clone the repository

```bash
git clone https://github.com/safeapric0t/Esp32-s3D.git
cd Esp32-s3D
```

You can also use **Code → Download ZIP** on GitHub.

## Wi-Fi configuration

### Access Point mode (default)

The default firmware creates its own Wi-Fi network. No home Wi-Fi credentials are required:

```text
SSID:       Ender3-Controller
Password:   ender3controller
Web address: http://192.168.4.1
```

Connect your phone or computer to `Ender3-Controller`, then open `http://192.168.4.1`.

### Station mode (connect to your router)

Create this local-only file before compiling:

```text
include/local_config.h
```

Put your network credentials in it:

```cpp
#define LOCAL_WIFI_SSID "YOUR_WIFI_NAME"
#define LOCAL_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define LOCAL_AP_MODE false
```

The file is ignored by Git and must not be committed. The ESP32 prints its assigned address to the serial monitor:

```text
Connected! IP: 192.168.1.xxx
```

Open that address in a browser. The computer and ESP32 must be on the same local network.

## Build the web interface

The React/Vite source is in `web_ui_src/`:

```bash
cd web_ui_src
npm ci
npm run build
cd ..
```

The compiled interface is written to `data/webui/`.

## Build and flash the firmware

Find the ESP32-S3 serial port first. On Windows it may be something like `COM13`.

Flash firmware only:

```bash
pio run -e esp32s3 -t upload --upload-port COM13
```

Replace `COM13` with your actual port.

If you rebuilt the web interface, upload the filesystem as well:

```bash
pio run -e esp32s3 -t uploadfs --upload-port COM13
```

> `uploadfs` rewrites the ESP32 filesystem and can remove G-code files stored on the ESP32. Back up those files first. A normal firmware upload does not erase them.

Open the serial monitor with:

```bash
pio device monitor -p COM13 -b 115200
```

Look for a USB connection message and the Wi-Fi IP address.

## Recommended startup order

1. Flash the firmware.
2. Connect the ESP32-S3 Host/OTG port to the printer.
3. Turn on the printer.
4. Wait for USB enumeration to complete.
5. Open the AP address or the Station-mode IP in a browser.

If the printer is power-cycled while the ESP32 is running, wait a few seconds for USB and SD information to be refreshed.

## Printing G-code

### Print from ESP32 storage

1. Open **G-code Files** or the dashboard upload card.
2. Select a `.gcode`, `.gco`, or `.g` file.
3. Wait for the upload to finish.
4. Confirm that the source is shown as **ESP32**.
5. Select **Print** or **Start print**.

The file is streamed from ESP32 storage and does not require a printer SD card.

Long filenames may be shortened because of the ESP32 filesystem path limit. For example, `frc-bumper-keychain_PLA_21m49s.gcode` may appear as `frc-bumper-keychai.gcode`; it is still the same uploaded file.

### Print from the printer SD card

1. Insert the SD card into the printer.
2. Wait until the printer reports that the card is ready.
3. Open **G-code Files** and confirm the SD source tag.
4. Select the SD file and press **Print**.

SD printing uses Marlin’s `M20`, `M23`, and `M24` commands.

### Copy an ESP32 file to printer SD

The interface can try to copy an ESP32 file to the printer SD card using Marlin `M28` / `M29`. This depends on the Marlin build and the card being writable. If the printer returns `open failed`, the file remains safe in ESP32 storage and can still be printed with the ESP32 streaming method.

## Interface sections

- **Dashboard:** active job, progress, temperatures, connection, and quick actions
- **G-code Files:** ESP32 and printer SD files, upload, delete, copy, and print
- **Printer Control:** temperature targets, fan control, jog movement, and auto home
- **Console:** live serial messages and manual G-code commands
- **Camera:** reserved for a future camera integration
- **Settings:** local interface settings and connection information

The current firmware does not include a camera connection.

## REST API

| Endpoint | Method | Description |
|---|---|---|
| `/api/status` | GET | Temperatures, position, connection, and print state |
| `/api/files` | GET | ESP32 and printer SD files |
| `/api/files/printer-sd` | GET | Printer SD files only |
| `/api/files/upload` | POST | Upload G-code to ESP32 and try SD copy |
| `/api/files/download?name=...` | GET | Download an ESP32 G-code file |
| `/api/files/delete?name=...` | DELETE | Delete an ESP32 or SD file |
| `/api/files/copy-to-printer?name=...` | POST | Try to copy an ESP32 file to printer SD |
| `/api/print/start?file=...&mode=stream` | POST | Start an ESP32 stream print |
| `/api/print/start?file=...&mode=sd` | POST | Start an SD print |
| `/api/print/pause` | POST | Pause a print |
| `/api/print/resume` | POST | Resume a print |
| `/api/print/cancel` | POST | Normal print cancellation |
| `/api/gcode?command=...` | POST | Send raw G-code |
| `/api/move?...` | POST | Move an axis |
| `/api/temperature?...` | POST | Set hotend and bed targets |
| `/api/home?...` | POST | Home selected axes |
| `/ws` | WebSocket | Live status, progress, and events |

Use the normal **Stop/Cancel** action for a regular cancellation. `M112` is Marlin’s emergency shutdown command and may require a printer power cycle afterward.

## Troubleshooting

### The web page does not open

- In AP mode, connect to `Ender3-Controller`.
- In Station mode, make sure the computer and ESP32 are on the same network.
- Use the IP printed by the serial monitor.
- Refresh with Ctrl+F5 after uploading a new web filesystem.

### The printer is not detected

- Use the ESP32-S3 Host/OTG port.
- Check that the USB cable carries data.
- Power-cycle the printer and wait for enumeration.
- Look for `USB device CONNECTED` in the serial monitor.

### SD files are missing

- Confirm that the printer detects the card.
- Wait a few seconds after inserting or changing the card.
- Restart the printer and ESP32 if necessary.
- Refresh the file library.

### An ESP32 file starts but the printer does not react

- Confirm the file source is **ESP32**, not **SD**.
- ESP32 files use `stream`; printer SD files use `sd`.
- Test the USB path with a small safe file containing `G28` or `M117 TEST`.
- Check for `GcodeStreamer: Started streaming` and Marlin `ok` responses in the serial monitor.

### Upload succeeds but SD copy fails

The printer may be able to read the card but not create files with `M28`. Check free space, FAT32 formatting, card health, and Marlin SD-write support. The ESP32 copy remains usable even when SD copy fails.

## Project structure

```text
Esp32-s3D/
├── data/webui/             # Compiled web interface
├── include/                # Headers and local Wi-Fi configuration
├── lib/                    # USB Host and helper libraries
├── src/
│   ├── main.cpp            # Wi-Fi, USB Host, and main loop
│   ├── web_server.cpp      # REST API, WebSocket, and file operations
│   ├── response_parser.h   # Marlin response and telemetry parsing
│   ├── gcode_streamer.*    # ESP32-to-printer G-code streaming
│   ├── command_scheduler.* # Command and ACK scheduling
│   └── printer_manager.*   # Printer state management
├── web_ui_src/             # React + TypeScript source
├── partitions.csv          # Flash partition layout
├── platformio.ini          # PlatformIO configuration
└── README.md
```

## Security and contributions

- Never commit real Wi-Fi credentials.
- Do not commit personal G-code files.
- The web server has no user account system or TLS; keep it on a trusted local network.
- Before opening a pull request, run `pio run -e esp32s3` and `npm run build`.

## License

This project is released under the MIT License.
