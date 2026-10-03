#include "storage.h"
#include "printer_protocol.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_spiffs.h"
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char* TAG = "STORAGE";

#define LITTLEFS_BASE_PATH "/littlefs"
#define LITTLEFS_PARTITION "storage"

static bool s_initialized = false;

esp_err_t storage_init(void) {
    ESP_LOGI(TAG, "Initializing LittleFS on partition '%s'...", LITTLEFS_PARTITION);
    
    esp_vfs_spiffs_conf_t conf = {
        .base_path = LITTLEFS_BASE_PATH,
        .partition_label = LITTLEFS_PARTITION,
        .max_files = 100,
        .format_if_mount_failed = true
    };
    
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(err));
        return err;
    }
    
    // Check partition info
    size_t total = 0, used = 0;
    err = esp_spiffs_info(LITTLEFS_PARTITION, &total, &used);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS info: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "LittleFS: %u KB total, %u KB used", total / 1024, used / 1024);
    }
    
    // Create gcode directory if not exists
    struct stat st = {0};
    if (stat(LITTLEFS_BASE_PATH "/gcode", &st) == -1) {
        mkdir(LITTLEFS_BASE_PATH "/gcode", 0755);
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "LittleFS initialized at %s", LITTLEFS_BASE_PATH);
    return ESP_OK;
}

void storage_deinit(void) {
    if (s_initialized) {
        esp_vfs_spiffs_unregister(LITTLEFS_PARTITION);
        s_initialized = false;
        ESP_LOGI(TAG, "LittleFS unmounted");
    }
}

esp_err_t storage_list_files(file_info_t** files, uint8_t* count) {
    if (!files || !count) return ESP_ERR_INVALID_ARG;
    
    *files = NULL;
    *count = 0;
    
    // List LittleFS files
    DIR* dir = opendir(LITTLEFS_BASE_PATH "/gcode");
    if (!dir) {
        ESP_LOGW(TAG, "Failed to open gcode directory");
        return ESP_OK;  // Not an error, just empty
    }
    
    struct dirent* entry;
    uint8_t capacity = 50;
    file_info_t* list = (file_info_t*)malloc(capacity * sizeof(file_info_t));
    if (!list) {
        closedir(dir);
        return ESP_ERR_NO_MEM;
    }
    
    uint8_t idx = 0;
    while ((entry = readdir(dir)) != NULL && idx < capacity) {
        // Skip directories and hidden files
        if (entry->d_type == DT_DIR || entry->d_name[0] == '.') continue;
        
        // Only .gcode files
        const char* ext = strrchr(entry->d_name, '.');
        if (!ext || strcasecmp(ext, ".gcode") != 0) continue;
        
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/gcode/%s", LITTLEFS_BASE_PATH, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == 0) {
            strncpy(list[idx].name, entry->d_name, sizeof(list[idx].name) - 1);
            list[idx].size = st.st_size;
            list[idx].date = st.st_mtime;
            list[idx].is_dir = false;
            list[idx].on_printer_sd = false;
            idx++;
        }
    }
    closedir(dir);
    
    // TODO: Also list printer SD card files via M20 command
    // This would require printer_protocol integration
    
    *files = list;
    *count = idx;
    
    return ESP_OK;
}

esp_err_t storage_delete_file(const char* filename, bool on_printer_sd) {
    if (!filename) return ESP_ERR_INVALID_ARG;
    
    if (on_printer_sd) {
        // Delete from printer SD using M30 command
        char cmd[160];
        snprintf(cmd, sizeof(cmd), "M30 %s", filename);
        return printer_send_gcode(cmd, NULL, 0);
    } else {
        // Delete from LittleFS
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/gcode/%s", LITTLEFS_BASE_PATH, filename);
        
        if (remove(full_path) == 0) {
            ESP_LOGI(TAG, "Deleted file: %s", filename);
            return ESP_OK;
        } else {
            ESP_LOGE(TAG, "Failed to delete file: %s", filename);
            return ESP_ERR_NOT_FOUND;
        }
    }
}

int32_t storage_get_file_size(const char* filename) {
    if (!filename) return -1;
    
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/gcode/%s", LITTLEFS_BASE_PATH, filename);
    
    struct stat st;
    if (stat(full_path, &st) == 0) {
        return st.st_size;
    }
    return -1;
}

esp_err_t storage_read_file(const char* filename, uint8_t** buffer, size_t* size) {
    if (!filename || !buffer || !size) return ESP_ERR_INVALID_ARG;
    
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/gcode/%s", LITTLEFS_BASE_PATH, filename);
    
    FILE* f = fopen(full_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for reading: %s", filename);
        return ESP_ERR_NOT_FOUND;
    }
    
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fclose(f);
        return ESP_ERR_INVALID_SIZE;
    }
    
    *buffer = (uint8_t*)malloc(file_size);
    if (!*buffer) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    size_t read = fread(*buffer, 1, file_size, f);
    fclose(f);
    
    if (read != (size_t)file_size) {
        free(*buffer);
        *buffer = NULL;
        return ESP_ERR_INVALID_SIZE;
    }
    
    *size = read;
    return ESP_OK;
}

esp_err_t storage_write_file(const char* filename, const uint8_t* buffer, size_t size) {
    if (!filename || !buffer || size == 0) return ESP_ERR_INVALID_ARG;
    
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/gcode/%s", LITTLEFS_BASE_PATH, filename);
    
    FILE* f = fopen(full_path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filename);
        return ESP_ERR_NOT_FOUND;
    }
    
    size_t written = fwrite(buffer, 1, size, f);
    fclose(f);
    
    if (written != size) {
        remove(full_path);
        return ESP_ERR_INVALID_SIZE;
    }
    
    ESP_LOGI(TAG, "Wrote file: %s (%u bytes)", filename, size);
    return ESP_OK;
}

esp_err_t storage_copy_to_printer_sd(const char* filename) {
    if (!filename) return ESP_ERR_INVALID_ARG;
    
    ESP_LOGI(TAG, "Copying %s to printer SD...", filename);
    
    // Open file from LittleFS
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "%s/gcode/%s", LITTLEFS_BASE_PATH, filename);
    
    FILE* f = fopen(full_path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open source file: %s", filename);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Get file size
    fseek(f, 0, SEEK_END);
    uint32_t file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    // Send M28 to start writing to printer SD
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "M28 %s", filename);
    esp_err_t err = printer_send_gcode(cmd, NULL, 0);
    if (err != ESP_OK) {
        fclose(f);
        return err;
    }
    
    // Stream file content in chunks
    uint8_t buffer[512];
    size_t bytes_read;
    uint32_t total_sent = 0;
    
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        // Send raw data to printer (no OK wait for each chunk)
        // Note: This is a simplified version. Real implementation needs
        // to handle the USB CDC write properly for binary data.
        size_t written = usb_cdc_write(buffer, bytes_read);
        if (written != bytes_read) {
            ESP_LOGE(TAG, "Failed to write chunk to printer");
            break;
        }
        total_sent += written;
        
        // Small delay to prevent buffer overflow
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    fclose(f);
    
    // Send M29 to finish writing
    err = printer_send_gcode("M29", NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to finalize SD write");
        return err;
    }
    
    ESP_LOGI(TAG, "Copied %u bytes to printer SD", total_sent);
    return ESP_OK;
}

esp_err_t storage_get_info(uint32_t* total_bytes, uint32_t* used_bytes) {
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    
    size_t total = 0, used = 0;
    esp_err_t err = esp_spiffs_info(LITTLEFS_PARTITION, &total, &used);
    
    if (total_bytes) *total_bytes = total;
    if (used_bytes) *used_bytes = used;
    
    return err;
}

esp_err_t storage_format(void) {
    ESP_LOGW(TAG, "Formatting LittleFS partition...");
    storage_deinit();
    
    esp_err_t err = esp_spiffs_format(LITTLEFS_PARTITION);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to format: %s", esp_err_to_name(err));
        return err;
    }
    
    return storage_init();
}