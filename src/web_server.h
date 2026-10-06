#pragma once

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <vector>

extern AsyncWebServer g_server;
extern AsyncWebSocket g_ws;

enum PrinterState {
  PRINTER_STATE_IDLE = 0,
  PRINTER_STATE_PRINTING,
  PRINTER_STATE_PAUSED,
  PRINTER_STATE_ERROR,
  PRINTER_STATE_DISCONNECTED,
  PRINTER_STATE_BOOTING,
  PRINTER_STATE_HEATING
};

struct Temperature {
  float current = 0;
  float target = 0;
  float power = 0;
};

struct Position {
  float x = 0, y = 0, z = 0, e = 0;
};

struct PrintProgress {
  String filename = "";
  uint32_t file_size = 0;
  uint32_t file_pos = 0;
  float progress = 0;
  uint32_t layer_current = 0;
  uint32_t layer_total = 0;
  float current_z = 0;
  uint32_t time_elapsed = 0;
  uint32_t time_remaining = 0;
  uint32_t bytes_printed = 0;
};

struct PrinterStatus {
  PrinterState state = PRINTER_STATE_DISCONNECTED;
  String firmware_version = "";
  String printer_model = "Ender 3 S1";
  Temperature hotend;
  Temperature bed;
  Temperature chamber;
  Position position;
  PrintProgress progress;
  bool sd_inserted = false;
  bool usb_connected = false;
  bool sd_printing = false;
  uint32_t last_ok_time = 0;
  uint32_t consecutive_errors = 0;
  uint8_t ws_clients = 0;
  std::vector<String> sd_files;
  
  struct Fan {
    uint8_t id = 0;
    uint8_t speed = 0;
  };
  std::vector<Fan> fans;
};

extern PrinterStatus printer_status;

extern AsyncWebServer g_server;
extern AsyncWebSocket g_ws;

void broadcast_status(AsyncWebSocketClient* client);
void broadcast_progress();
void broadcast_log(const char* level, const char* message);
void broadcast_state_change(int state, const char* state_str);
void broadcast_file_list();
void web_server_init();