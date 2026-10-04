# Esp32-s3D - Ender 3 S1 ESP32-S3 Controller

BambuLab-style web interface for Ender 3 S1 using ESP32-S3 N16R8 via USB OTG.

## Features

- 🌐 **Web UI** - React-based BambuLab-style dashboard
- 📡 **USB OTG Communication** - Direct USB CDC/ACM to printer MCU
- 📁 **File Management** - Upload G-code to ESP32 LittleFS, copy to printer SD
- 📊 **Real-time Monitoring** - Temperatures, position, progress via WebSocket
- 🎮 **Full Control** - Movement, temperature, fans, custom G-code
- 📷 **Camera Ready** - Abstraction layer for future ESP32-CAM/USB UVC
- 🔐 **API Key Auth** - Simple token-based authentication
- 📱 **Responsive** - Works on desktop and mobile

## Hardware Requirements

- Ender 3 S1 (STM32F401 MCU with USB CDC)
- ESP32-S3 N16R8 (16MB Flash, 8MB PSRAM)
- USB-C to USB-C cable (data + power)

## Software Stack

- **Firmware**: ESP-IDF v5.2+ via PlatformIO
- **Web UI**: React 18 + TypeScript + Vite
- **Communication**: USB Host CDC/ACM → Marlin G-code protocol
- **Storage**: LittleFS (ESP32 internal flash) + Printer SD card

## Quick Start

### 1. Clone and Setup

```bash
git clone https://github.com/safeapric0t/Esp32-s3D.git
cd Esp32-s3D
```

### 2. Configure WiFi & API Key

Edit `src/main.c` and update:
```c
static system_config_t s_config = {
    .wifi_ssid = "YOUR_WIFI_SSID",
    .wifi_password = "YOUR_WIFI_PASSWORD",
    .wifi_ap_ssid = "Ender3-Controller",
    .wifi_ap_password = "ender3controller",
    .ap_mode = true,  // Start in AP mode for easy setup
    .auth = {
        .enabled = true,
        .key = "changeme123"  // CHANGE THIS!
    },
    // ...
};
```

### 3. Build Web UI (Optional - prebuilt included)

```bash
cd web_ui_src
npm install
npm run build
cd ..
./scripts/build_web_ui.sh
```

### 4. Build & Flash Firmware

```bash
# Using PlatformIO
pio run -t upload

# Or with ESP-IDF directly
idf.py build flash monitor
```

### 5. Connect Hardware

1. Power off Ender 3 S1
2. Connect USB-C cable: ESP32-S3 USB-C → Ender 3 S1 USB-C (Type-C port)
3. Power on both devices
4. ESP32-S3 will create WiFi AP: `Ender3-Controller` / `ender3controller`

### 6. Access Web UI

- **AP Mode**: http://192.168.4.1
- **STA Mode**: http://<assigned-ip>
- **API Key**: `changeme123` (change in settings!)

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        ESP32-S3 N16R8                           │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             │
│  │  USB OTG    │  │    WiFi     │  │   SPI/ SD   │             │
│  │   HOST      │  │  AP/STA     │  │   (LittleFS)│             │
│  │  (CDC/ACM)  │  │  + WebSocket│  │  G-code dep.│             │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘             │
│         │                │                │                     │
│         ▼                ▼                ▼                     │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │              FIRMWARE (ESP-IDF)                          │   │
│  │  USB Host CDC → Printer Protocol → Web Server → Web UI  │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
         │
         │ USB-C Cable (OTG Host → Device)
         ▼
┌─────────────────────────────────────────────────────────────────┐
│                   ENDER 3 S1 (STM32F401)                        │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             │
│  │  USB CDC    │  │   Marlin    │  │   SD Card   │             │
│  │  (Device)   │  │  Firmware   │  │  (Printer)  │             │
│  └─────────────┘  └─────────────┘  └─────────────┘             │
└─────────────────────────────────────────────────────────────────┘
```

## API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/status` | GET | Printer status (temps, position, progress) |
| `/api/printer` | GET | Printer info |
| `/api/files` | GET | List files (ESP32 + Printer SD) |
| `/api/files/upload` | POST | Upload G-code (multipart) |
| `/api/files/delete` | DELETE | Delete file |
| `/api/files/copy-to-printer` | POST | Copy to printer SD (M28/M29) |
| `/api/print/start` | POST | Start print |
| `/api/print/pause` | POST | Pause print |
| `/api/print/resume` | POST | Resume print |
| `/api/print/cancel` | POST | Cancel print |
| `/api/gcode` | POST | Send raw G-code |
| `/api/move` | POST | Jog axes |
| `/api/temperature` | POST | Set temperatures |
| `/api/fan` | POST | Set fan speed |
| `/api/home` | POST | Home axes |
| `/ws` | WS | Real-time updates |

## WebSocket Messages

```json
// Server → Client
{"type": "status", "data": {...}}
{"type": "progress", "data": {...}}
{"type": "log", "data": {"level": "info", "message": "ok"}}
{"type": "state_change", "data": {"state": "printing", "state_str": "Printing"}}
{"type": "file_list", "data": [...]}
```

## Development

### Project Structure

```
Esp32-s3D/
├── platformio.ini          # PlatformIO config
├── partitions.csv          # Partition table (8MB LittleFS)
├── sdkconfig.defaults      # ESP32-S3 config
├── scripts/
│   └── build_web_ui.sh     # Build & embed web UI
├── src/
│   └── main.c              # Application entry
├── lib/
│   ├── common/             # Shared types, ring buffer, JSON
│   ├── usb_host_cdc/       # USB Host CDC/ACM driver
│   ├── printer_protocol/   # G-code streaming & parsing
│   ├── web_server/         # HTTP + WebSocket + REST API
│   ├── storage/            # LittleFS + Printer SD bridge
│   └── camera_abstraction/ # Future camera support
└── web_ui_src/             # React frontend source
    ├── src/
    │   ├── components/     # UI components
    │   ├── hooks/          # React hooks
    │   ├── services/       # API client
    │   ├── types/          # TypeScript types
    │   └── styles/         # CSS (BambuLab dark theme)
    └── package.json
```

### Adding Camera Support

1. Implement `camera_interface_t` in `lib/camera_abstraction/`
2. Call `camera_set_interface(&your_camera_impl)` in `app_main()`
3. Camera UI already has placeholder in Monitor tab

## Configuration

Key settings in `src/main.c`:

```c
static system_config_t s_config = {
    .wifi_ssid = "YOUR_SSID",           // STA mode WiFi
    .wifi_password = "YOUR_PASSWORD",
    .wifi_ap_ssid = "Ender3-Controller", // AP mode
    .wifi_ap_password = "ender3controller",
    .ap_mode = true,                     // true = AP, false = STA
    .auth = { .enabled = true, .key = "your-api-key" },
    .printer_name = "Ender 3 S1",
    .auto_connect = true,
    .status_interval_ms = 2000,
};
```

## Troubleshooting

### USB Not Connecting
- Check USB-C cable supports data (not charge-only)
- Verify Ender 3 S1 USB port works (try with PC)
- Check `dmesg` / serial monitor for USB enumeration

### Web UI Not Loading
- Ensure web UI was built and embedded (`scripts/build_web_ui.sh`)
- Check LittleFS partition mounted (see serial monitor)
- Try hard refresh (Ctrl+Shift+R)

### Print Stops Unexpectedly
- Check USB cable quality (use short, high-quality cable)
- Monitor `consecutive_errors` in status
- Ensure printer firmware has USB CDC enabled

## License

MIT License - See LICENSE file

## Credits

- ESP-IDF by Espressif
- PlatformIO
- React, Vite, Recharts
- BambuLab for UI inspiration
