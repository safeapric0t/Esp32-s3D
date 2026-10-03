#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Printer protocol callbacks
typedef struct {
    void (*on_status_update)(const printer_status_t* status, void* ctx);
    void (*on_progress_update)(const print_progress_t* progress, void* ctx);
    void (*on_line_received)(const char* line, void* ctx);
    void (*on_state_change)(printer_state_t new_state, void* ctx);
    void* user_ctx;
} printer_callbacks_t;

// Initialize printer protocol
esp_err_t printer_protocol_init(const printer_callbacks_t* callbacks);

// Deinitialize
void printer_protocol_deinit(void);

// Start printing from file (streaming mode)
esp_err_t printer_start_print_stream(const char* filename);

// Start printing from printer SD card
esp_err_t printer_start_print_sd(const char* filename);

// Pause print
esp_err_t printer_pause_print(void);

// Resume print
esp_err_t printer_resume_print(void);

// Cancel print
esp_err_t printer_cancel_print(void);

// Send single G-code command
esp_err_t printer_send_gcode(const char* gcode, char* response, size_t resp_size);

// Request status update (M105, M114, etc.)
esp_err_t printer_request_status(void);

// Request position (M114)
esp_err_t printer_request_position(void);

// Request SD status (M27)
esp_err_t printer_request_sd_status(void);

// Set target temperatures
esp_err_t printer_set_hotend_temp(float temp);
esp_err_t printer_set_bed_temp(float temp);

// Set fan speed
esp_err_t printer_set_fan_speed(uint8_t fan_id, uint8_t speed);

// Move axes (G0/G1)
esp_err_t printer_move_axes(float x, float y, float z, float e, float feedrate);

// Home axes (G28)
esp_err_t printer_home_axes(bool x, bool y, bool z);

// Get current status (thread-safe copy)
void printer_get_status(printer_status_t* status);

// Check if currently printing
bool printer_is_printing(void);

// Get print progress
void printer_get_progress(print_progress_t* progress);

// Emergency stop (M112)
esp_err_t printer_emergency_stop(void);

#ifdef __cplusplus
}
#endif