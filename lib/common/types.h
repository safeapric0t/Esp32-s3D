#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Printer states
typedef enum {
    PRINTER_STATE_IDLE = 0,
    PRINTER_STATE_PRINTING,
    PRINTER_STATE_PAUSED,
    PRINTER_STATE_ERROR,
    PRINTER_STATE_DISCONNECTED,
    PRINTER_STATE_BOOTING,
    PRINTER_STATE_HEATING
} printer_state_t;

// Temperature data
typedef struct {
    float current;
    float target;
    float power;  // PWM 0-100%
} temperature_t;

// Position data
typedef struct {
    float x, y, z, e;
    bool homed[4];
} position_t;

// Fan data
typedef struct {
    uint8_t id;
    float speed;  // 0-100%
    bool enabled;
} fan_t;

// Print progress
typedef struct {
    char filename[128];
    uint32_t file_size;
    uint32_t file_pos;
    float progress;       // 0-100%
    uint32_t layer_current;
    uint32_t layer_total;
    float current_z;
    uint32_t time_elapsed;    // seconds
    uint32_t time_remaining;  // seconds (estimated)
    uint32_t bytes_printed;
} print_progress_t;

// Complete printer status
typedef struct {
    printer_state_t state;
    char firmware_version[64];
    char printer_model[64];
    
    temperature_t hotend;
    temperature_t bed;
    temperature_t chamber;
    
    position_t position;
    
    fan_t fans[4];
    uint8_t fan_count;
    
    print_progress_t progress;
    
    bool sd_inserted;
    char sd_files[50][128];
    uint8_t sd_file_count;
    
    bool usb_connected;
    uint32_t last_ok_time;
    uint32_t consecutive_errors;
    
    // WebSocket clients count
    uint8_t ws_clients;
} printer_status_t;

// G-code command queue item
typedef struct {
    char command[256];
    bool needs_ack;
    uint32_t timestamp;
    uint8_t retries;
} gcode_cmd_t;

// File info
typedef struct {
    char name[128];
    uint32_t size;
    uint32_t date;
    bool is_dir;
    bool on_printer_sd;  // true = printer SD, false = ESP32 LittleFS
} file_info_t;

// API Key auth
typedef struct {
    char key[64];
    bool enabled;
} auth_config_t;

// System config
typedef struct {
    char wifi_ssid[64];
    char wifi_password[64];
    char wifi_ap_ssid[64];
    char wifi_ap_password[64];
    bool ap_mode;
    
    auth_config_t auth;
    
    char printer_name[64];
    uint32_t baud_rate;  // Not used for USB CDC but kept for compatibility
    
    bool auto_connect;
    uint32_t status_interval_ms;  // WebSocket broadcast interval
} system_config_t;

// WebSocket message types
typedef enum {
    WS_MSG_STATUS = 0,
    WS_MSG_PROGRESS,
    WS_MSG_TEMPERATURE,
    WS_MSG_POSITION,
    WS_MSG_LOG,
    WS_MSG_FILE_LIST,
    WS_MSG_STATE_CHANGE,
    WS_MSG_ERROR
} ws_msg_type_t;

#ifdef __cplusplus
}
#endif