#include <Arduino.h>
#include <WiFi.h>
#include <EspUsbHost.h>
#include "web_server.h"
#include "response_parser.h"
#include "command_scheduler.h"
#include "gcode_streamer.h"
#include "printer_manager.h"

// ============================================================================
// CONFIGURATION
// ============================================================================
const char* WIFI_SSID = "TURKNET_B907B";
const char* WIFI_PASSWORD = "TYkyRDkN";
const char* AP_SSID = "Ender3-Controller";
const char* AP_PASSWORD = "ender3controller";
const bool AP_MODE = false;

const char* API_KEY = "ender3s1secure2024";

// ============================================================================
// USB
// ============================================================================
EspUsbHost* g_usb_host = nullptr;
EspUsbHostCdcSerial* g_cdc_serial = nullptr;
bool g_usb_connected = false;
String g_usb_rx_buffer = "";
uint32_t g_last_rx_time = 0;

// Legacy streaming (for backward compat)
File g_print_file;
bool g_streaming = false;
bool g_paused = false;
String g_current_file = "";
uint32_t g_file_size = 0;
uint32_t g_file_pos = 0;
uint32_t g_lines_sent = 0;
uint32_t g_last_status_req = 0;

PrinterStatus printer_status;

// ============================================================================
// USB CDC CALLBACKS
// ============================================================================
void on_usb_data(const EspUsbHostSerialData& data) {
  Serial0.printf("USB Data: addr=%d, port=%d, len=%d\n", data.address, data.port, data.length);
  
  String line = String((char*)data.data, data.length);
  g_usb_rx_buffer += line;
  
  int nl_pos;
  while ((nl_pos = g_usb_rx_buffer.indexOf('\n')) >= 0) {
    String line = g_usb_rx_buffer.substring(0, nl_pos);
    g_usb_rx_buffer = g_usb_rx_buffer.substring(nl_pos + 1);
    line.trim();
    
    if (line.length() == 0) continue;
    
    Serial0.printf("USB RX: %s\n", line.c_str());
    
    // Use the new ResponseParser
    g_response_parser.parse_line(line);
    
    g_last_rx_time = millis();
  }
}

void on_usb_connected(const EspUsbHostDeviceInfo& info) {
  Serial0.printf("USB device CONNECTED: VID=%04X PID=%04X, Class=%d, Speed=%d\n", 
                info.vid, info.pid, info.deviceClass, info.speed);
  Serial0.printf("  Manufacturer: %s\n", info.manufacturer);
  Serial0.printf("  Product: %s\n", info.product);
  Serial0.printf("  Serial: %s\n", info.serial);
  g_usb_connected = true;
  printer_status.usb_connected = true;
  g_printer_manager.set_usb_status(true);
  broadcast_status(nullptr);
  
  delay(1000);
  if (g_cdc_serial && g_cdc_serial->connected()) {
    g_cdc_serial->println("M105");
    g_cdc_serial->println("M114");
  }
}

void on_usb_disconnected(const EspUsbHostDeviceInfo& info) {
  Serial0.println("USB device DISCONNECTED");
  g_usb_connected = false;
  printer_status.usb_connected = false;
  g_printer_manager.set_usb_status(false);
  broadcast_status(nullptr);
  
  if (g_streaming) {
    g_streaming = false;
    g_paused = false;
    if (g_print_file) g_print_file.close();
  }
  
  g_gcode_streamer.cancel();
}

// ============================================================================
// PRINTING LOGIC
// ============================================================================
bool send_gcode(const String& cmd) {
  if (!g_usb_connected || !g_cdc_serial || !g_cdc_serial->connected()) return false;
  
  g_cdc_serial->println(cmd);
  return true;
}

void start_stream_print(const String& filename) {
  if (g_streaming) return;
  
  String path = "/gcode/" + filename;
  g_print_file = SPIFFS.open(path, "r");
  if (!g_print_file) {
    Serial0.printf("Failed to open: %s\n", path.c_str());
    return;
  }
  
  g_file_size = g_print_file.size();
  g_file_pos = 0;
  g_lines_sent = 0;
  
  printer_status.progress.filename = filename;
  printer_status.progress.file_size = g_file_size;
  printer_status.progress.file_pos = 0;
  printer_status.progress.progress = 0;
  printer_status.state = PRINTER_STATE_PRINTING;
  
  g_streaming = true;
  g_paused = false;
  
  StreamConfig config;
  config.max_in_flight = 3;
  config.ack_timeout_ms = 5000;
  config.max_retries = 3;
  
  g_gcode_streamer.begin(g_print_file, config);
  g_gcode_streamer.set_callbacks(
    [](const StreamStats& stats) {
      uint32_t total = stats.total_lines > 0 ? stats.total_lines : 1;
      g_printer_manager.set_progress(stats.sent_lines * 100 / total, "Printing...");
    },
    [](bool success, const String& message) {
      if (success) {
        g_printer_manager.print_complete(true, message);
      } else {
        g_printer_manager.print_complete(false, message);
      }
    }
  );
  
  g_streaming = true;
  g_paused = false;
  
  broadcast_status(nullptr);
  broadcast_progress();
}

void stop_stream_print() {
  g_streaming = false;
  g_paused = false;
  if (g_print_file) g_print_file.close();
  
  g_gcode_streamer.cancel();
  
  printer_status.state = PRINTER_STATE_IDLE;
  broadcast_status(nullptr);
}

void handle_streaming() {
  if (!g_streaming || !g_print_file || g_paused) return;
  
  g_gcode_streamer.handle_streaming();
}

void start_sd_print(const String& filename) {
  send_gcode("M23 " + filename);
  send_gcode("M24");
  printer_status.state = PRINTER_STATE_PRINTING;
  printer_status.progress.filename = filename;
  printer_status.progress.progress = 0;
  printer_status.progress.file_pos = 0;
  printer_status.progress.file_size = 0;
  printer_status.sd_printing = true;
  broadcast_status(nullptr);
  broadcast_progress();
}

void stop_sd_print() {
  printer_status.sd_printing = false;
  printer_status.state = PRINTER_STATE_IDLE;
  printer_status.progress.progress = 0;
  printer_status.progress.file_pos = 0;
  broadcast_status(nullptr);
  broadcast_progress();
}

void handle_sd_print_progress() {
  if (!printer_status.sd_printing || !g_usb_connected || !g_cdc_serial || !g_cdc_serial->connected()) return;
  
  g_cdc_serial->println("M27");
}

void pause_print() {
  if (g_streaming) g_paused = true;
  g_gcode_streamer.pause();
  send_gcode("M0");
}

void resume_print() {
  if (g_streaming) g_paused = false;
  g_gcode_streamer.resume();
  send_gcode("M24");
}

void cancel_print() {
  send_gcode("M112");
  delay(500);
  send_gcode("M999");
  stop_stream_print();
  
  g_gcode_streamer.cancel();
}

// ============================================================================
// SETUP & LOOP
// ============================================================================
void setup() {
  Serial0.begin(115200);
  delay(1000);
  Serial0.println("\n=== Ender 3 S1 ESP32-S3 Controller ===");
  
  if (!SPIFFS.begin(true)) {
    Serial0.println("SPIFFS mount failed!");
  } else {
    Serial0.println("SPIFFS mounted");
    SPIFFS.mkdir("/gcode");
  }
  
  g_usb_host = new EspUsbHost();
  
  g_usb_host->onDeviceConnected([](const EspUsbHostDeviceInfo& info) {
    on_usb_connected(info);
  });
  g_usb_host->onDeviceDisconnected([](const EspUsbHostDeviceInfo& info) {
    on_usb_disconnected(info);
  });
  g_usb_host->onSerialData(on_usb_data);
  
  EspUsbHostConfig config;
  config.taskStackSize = 8192;
  config.taskPriority = 5;
  config.taskCore = tskNO_AFFINITY;
  config.port = ESP_USB_HOST_PORT_DEFAULT;
  
  Serial0.println("Starting USB Host on OTG port...");
  if (!g_usb_host->begin(config)) {
    Serial0.printf("USB Host init failed! Error: %s\n", g_usb_host->lastErrorName());
  } else {
    Serial0.println("USB Host initialized successfully on OTG port");
    Serial0.println("Connect printer to ESP32-S3 OTG USB-C port (not native USB)");
  }
  
  g_cdc_serial = new EspUsbHostCdcSerial(*g_usb_host);
  if (!g_cdc_serial->begin(115200)) {
    Serial0.println("CDC Serial init failed!");
  } else {
    Serial0.println("CDC Serial initialized");
  }
  
  if (AP_MODE) {
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial0.printf("AP Mode: %s\n", AP_SSID);
    Serial0.printf("IP: %s\n", WiFi.softAPIP().toString().c_str());
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial0.printf("Connecting to %s...", WIFI_SSID);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial0.print(".");
    }
    Serial0.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
  }
  
  web_server_init();
  
  // Setup printer manager callbacks
  g_printer_manager.set_callbacks(
    [](const String& state) {
      Serial0.printf("Printer state: %s\n", state.c_str());
    },
    [](uint32_t progress, const String& message) {
      Serial0.printf("Progress: %u%% - %s\n", progress, message.c_str());
    },
    [](bool success, const String& message) {
      if (success) {
        Serial0.printf("Print completed: %s\n", message.c_str());
      } else {
        Serial0.printf("Print failed: %s\n", message.c_str());
      }
    }
  );
  
  // Setup response parser callback
  g_response_parser.set_callback([](const String& type, const String& data) {
    if (type == "ok") {
      g_gcode_streamer.handle_ack("ok");
      g_command_scheduler.mark_ack_received(0, "ok"); // simplified
    } else if (type == "error") {
      g_gcode_streamer.handle_ack("Error");
      g_command_scheduler.mark_error(0, data);
    } else if (type == "temperature") {
      int comma = data.indexOf(',');
      if (comma > 0) {
        float cur = data.substring(0, comma).toFloat();
        float tgt = data.substring(comma + 1).toFloat();
        printer_status.hotend.current = cur;
        printer_status.hotend.target = tgt;
      }
    } else if (type == "bed_temperature") {
      int comma = data.indexOf(',');
      if (comma > 0) {
        float cur = data.substring(0, comma).toFloat();
        float tgt = data.substring(comma + 1).toFloat();
        printer_status.bed.current = cur;
        printer_status.bed.target = tgt;
      }
    } else if (type == "bed_power") {
      printer_status.bed.power = data.toInt();
    } else if (type == "position") {
      int comma1 = data.indexOf(',');
      int comma2 = data.indexOf(',', comma1 + 1);
      int comma3 = data.indexOf(',', comma2 + 1);
      if (comma1 > 0 && comma2 > 0 && comma3 > 0) {
        float x = data.substring(0, comma1).toFloat();
        float y = data.substring(comma1 + 1, comma2).toFloat();
        float z = data.substring(comma2 + 1, comma3).toFloat();
        float e = data.substring(comma3 + 1).toFloat();
        printer_status.position.x = x;
        printer_status.position.y = y;
        printer_status.position.z = z;
        printer_status.position.e = e;
      }
    } else if (type == "sd_progress") {
      int comma = data.indexOf(',');
      if (comma > 0) {
        uint32_t cur = data.substring(0, comma).toInt();
        uint32_t total = data.substring(comma + 1).toInt();
        printer_status.progress.file_pos = cur;
        printer_status.progress.file_size = total;
        printer_status.progress.progress = (total > 0) ? (cur * 100.0f / total) : 0;
      }
    } else if (type == "file_list_begin") {
      printer_status.sd_files.clear();
    } else if (type == "file_list_end") {
      broadcast_file_list();
    } else if (type == "file_list_entry") {
      printer_status.sd_files.push_back(data);
    } else if (type == "error") {
      Serial0.printf("Printer error: %s\n", data.c_str());
    }
  });
  
  // Initialize printer manager
  g_printer_manager.begin();
  
  printer_status.state = PRINTER_STATE_DISCONNECTED;
  printer_status.firmware_version = "";
  printer_status.printer_model = "Ender 3 S1";
}

void loop() {
  // Handle USB Host tasks
  // The EspUsbHost library handles USB events in its own task
  
  // Handle streaming
  handle_streaming();
  handle_sd_print_progress();
  
  // Handle G-code streaming
  g_gcode_streamer.handle_streaming();
  
  // Process command scheduler
  PrinterCommand cmd;
  if (g_command_scheduler.get_next_ready_command(cmd)) {
    send_gcode(cmd.command);
  }
  
  // Handle printer manager
  g_printer_manager.handle_sd_print_progress();
  
  // Periodic status requests
  static uint32_t last_status = 0;
  if (g_usb_connected && g_cdc_serial && g_cdc_serial->connected() && millis() - last_status > 2000) {
    if (g_cdc_serial && g_cdc_serial->connected()) {
      g_cdc_serial->println("M105");
    }
    static int pos_ctr = 0;
    if (++pos_ctr >= 5) {
      if (g_cdc_serial && g_cdc_serial->connected()) {
        g_cdc_serial->println("M114");
      }
      pos_ctr = 0;
    }
    last_status = millis();
  }
  
  static uint32_t heap_ctr = 0;
  if (++heap_ctr >= 30000) {
    Serial0.printf("Free heap: %u\n", ESP.getFreeHeap());
    heap_ctr = 0;
  }
  
  delay(1);
}