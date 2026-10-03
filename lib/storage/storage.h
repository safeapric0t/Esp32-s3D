#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize storage (LittleFS)
esp_err_t storage_init(void);

// Deinitialize storage
void storage_deinit(void);

// List files (both LittleFS and printer SD)
esp_err_t storage_list_files(file_info_t** files, uint8_t* count);

// Delete file
esp_err_t storage_delete_file(const char* filename, bool on_printer_sd);

// Get file size
int32_t storage_get_file_size(const char* filename);

// Read file to buffer
esp_err_t storage_read_file(const char* filename, uint8_t** buffer, size_t* size);

// Write buffer to file
esp_err_t storage_write_file(const char* filename, const uint8_t* buffer, size_t size);

// Copy file from LittleFS to printer SD card (using M28/M29)
esp_err_t storage_copy_to_printer_sd(const char* filename);

// Get LittleFS info
esp_err_t storage_get_info(uint32_t* total_bytes, uint32_t* used_bytes);

// Format LittleFS
esp_err_t storage_format(void);

#ifdef __cplusplus
}
#endif