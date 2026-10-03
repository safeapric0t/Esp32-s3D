#pragma once

#include <ArduinoJson.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Convert printer_status_t to JSON string
char* printer_status_to_json(const printer_status_t* status);

// Convert file_info_t array to JSON string
char* file_list_to_json(const file_info_t* files, uint8_t count, bool include_printer_sd);

// Convert print_progress_t to JSON string
char* print_progress_to_json(const print_progress_t* progress);

// Parse JSON command from WebSocket/HTTP
bool parse_gcode_command(const char* json, char* command, size_t max_len);

// Create standard API response
char* create_api_response(bool success, const char* message, const char* data);

// Free JSON string allocated by above functions
void free_json_string(char* str);

#ifdef __cplusplus
}
#endif