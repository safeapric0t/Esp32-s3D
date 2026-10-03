#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Web server callbacks
typedef struct {
    void (*on_print_start)(const char* filename, bool stream_mode, void* ctx);
    void (*on_print_pause)(void* ctx);
    void (*on_print_resume)(void* ctx);
    void (*on_print_cancel)(void* ctx);
    void (*on_gcode_command)(const char* gcode, void* ctx);
    void (*on_file_upload)(const char* filename, const uint8_t* data, size_t len, void* ctx);
    void (*on_file_delete)(const char* filename, bool on_printer_sd, void* ctx);
    void (*on_file_copy_to_printer)(const char* filename, void* ctx);
    void (*on_move_axes)(float x, float y, float z, float e, float feedrate, void* ctx);
    void (*on_set_temp)(float hotend, float bed, void* ctx);
    void (*on_set_fan)(uint8_t fan_id, uint8_t speed, void* ctx);
    void (*on_home_axes)(bool x, bool y, bool z, void* ctx);
    void* user_ctx;
} web_server_callbacks_t;

// Initialize web server
esp_err_t web_server_init(const web_server_callbacks_t* callbacks, const system_config_t* config);

// Start web server
esp_err_t web_server_start(void);

// Stop web server
void web_server_stop(void);

// Broadcast status to all WebSocket clients
void web_server_broadcast_status(const printer_status_t* status);

// Broadcast progress to all WebSocket clients
void web_server_broadcast_progress(const print_progress_t* progress);

// Broadcast log message
void web_server_broadcast_log(const char* level, const char* message);

// Broadcast file list update
void web_server_broadcast_file_list(const file_info_t* files, uint8_t count);

#ifdef __cplusplus
}
#endif