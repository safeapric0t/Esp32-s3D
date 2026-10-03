#include "printer_protocol.h"
#include "usb_host_cdc.h"
#include "ring_buffer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

static const char* TAG = "PRINTER_PROTO";

// Internal state
static printer_callbacks_t s_callbacks = {0};
static TaskHandle_t s_stream_task = NULL;
static TaskHandle_t s_status_task = NULL;
static SemaphoreHandle_t s_status_mutex = NULL;
static SemaphoreHandle_t s_stream_mutex = NULL;

static printer_status_t s_status = {0};
static print_progress_t s_progress = {0};
static gcode_cmd_t* s_gcode_queue = NULL;
static size_t s_queue_head = 0;
static size_t s_queue_tail = 0;
static size_t s_queue_size = 0;
static const size_t MAX_QUEUE_SIZE = 256;

static bool s_streaming = false;
static bool s_paused = false;
static FILE* s_print_file = NULL;
static char s_current_file[128] = {0};
static uint32_t s_file_size = 0;
static uint32_t s_file_pos = 0;
static uint32_t s_lines_sent = 0;
static uint32_t s_last_status_request = 0;

// Temperature parsing
static void parse_temperature_response(const char* line) {
    // Format: "ok T:200/200 B:60/60 @:127" or "ok T0:200/200 B:60/60 @:127 B@:127"
    float hotend_cur = 0, hotend_tgt = 0, bed_cur = 0, bed_tgt = 0;
    int hotend_pwm = 0, bed_pwm = 0;
    
    // Parse T: or T0:
    const char* t_ptr = strstr(line, "T:");
    if (!t_ptr) t_ptr = strstr(line, "T0:");
    if (t_ptr) {
        sscanf(t_ptr + 2, "%f/%f", &hotend_cur, &hotend_tgt);
    }
    
    // Parse @: (hotend PWM)
    const char* at_ptr = strstr(line, "@:");
    if (at_ptr) {
        sscanf(at_ptr + 2, "%d", &hotend_pwm);
    }
    
    // Parse B:
    const char* b_ptr = strstr(line, "B:");
    if (b_ptr) {
        sscanf(b_ptr + 2, "%f/%f", &bed_cur, &bed_tgt);
    }
    
    // Parse B@: (bed PWM)
    const char* bat_ptr = strstr(line, "B@:");
    if (bat_ptr) {
        sscanf(bat_ptr + 3, "%d", &bed_pwm);
    }
    
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (hotend_tgt > 0 || hotend_cur > 0) {
            s_status.hotend.current = hotend_cur;
            s_status.hotend.target = hotend_tgt;
            s_status.hotend.power = (hotend_tgt > 0) ? (hotend_pwm * 100.0f / 255.0f) : 0;
        }
        if (bed_tgt > 0 || bed_cur > 0) {
            s_status.bed.current = bed_cur;
            s_status.bed.target = bed_tgt;
            s_status.bed.power = (bed_tgt > 0) ? (bed_pwm * 100.0f / 255.0f) : 0;
        }
        xSemaphoreGive(s_status_mutex);
    }
}

// Position parsing (M114 response)
static void parse_position_response(const char* line) {
    // Format: "ok X:10.0 Y:20.0 Z:5.0 E:0.0 Count: 1234"
    float x = 0, y = 0, z = 0, e = 0;
    
    const char* x_ptr = strstr(line, "X:");
    const char* y_ptr = strstr(line, "Y:");
    const char* z_ptr = strstr(line, "Z:");
    const char* e_ptr = strstr(line, "E:");
    
    if (x_ptr) sscanf(x_ptr + 2, "%f", &x);
    if (y_ptr) sscanf(y_ptr + 2, "%f", &y);
    if (z_ptr) sscanf(z_ptr + 2, "%f", &z);
    if (e_ptr) sscanf(e_ptr + 2, "%f", &e);
    
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        s_status.position.x = x;
        s_status.position.y = y;
        s_status.position.z = z;
        s_status.position.e = e;
        xSemaphoreGive(s_status_mutex);
    }
}

// SD print progress parsing (M27 response)
static void parse_sd_progress(const char* line) {
    // Format: "SD printing byte 12345/67890"
    uint32_t current = 0, total = 0;
    if (sscanf(line, "SD printing byte %u/%u", &current, &total) == 2) {
        if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_progress.file_pos = current;
            s_progress.file_size = total;
            s_progress.progress = (total > 0) ? (current * 100.0f / total) : 0;
            xSemaphoreGive(s_status_mutex);
        }
    }
}

// M1002 detailed status parsing
static void parse_m1002_response(const char* line) {
    // Format: "ok T:200/200 B:60/60 T0:200/200 @:127 B@:127"
    parse_temperature_response(line);
}

// Process a complete line from printer
static void process_printer_line(const char* line) {
    if (!line) return;
    
    ESP_LOGD(TAG, "Processing: %s", line);
    
    // Callback for raw line
    if (s_callbacks.on_line_received) {
        s_callbacks.on_line_received(line, s_callbacks.user_ctx);
    }
    
    // Check for state changes
    if (strstr(line, "echo:busy: paused for user") || strstr(line, "action:pause")) {
        if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (s_status.state != PRINTER_STATE_PAUSED) {
                s_status.state = PRINTER_STATE_PAUSED;
                if (s_callbacks.on_state_change) {
                    s_callbacks.on_state_change(PRINTER_STATE_PAUSED, s_callbacks.user_ctx);
                }
            }
            xSemaphoreGive(s_status_mutex);
        }
    } else if (strstr(line, "echo:busy: paused for user") == NULL && 
               s_status.state == PRINTER_STATE_PAUSED &&
               (strstr(line, "ok") || strstr(line, "Resuming"))) {
        if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_status.state = PRINTER_STATE_PRINTING;
            if (s_callbacks.on_state_change) {
                s_callbacks.on_state_change(PRINTER_STATE_PRINTING, s_callbacks.user_ctx);
            }
            xSemaphoreGive(s_status_mutex);
        }
    }
    
    // Parse temperature
    if (strstr(line, "T:") || strstr(line, "T0:")) {
        parse_temperature_response(line);
    }
    
    // Parse position
    if (strstr(line, "X:") && strstr(line, "Y:") && strstr(line, "Z:")) {
        parse_position_response(line);
    }
    
    // Parse SD progress
    if (strstr(line, "SD printing byte")) {
        parse_sd_progress(line);
    }
    
    // Parse M1002
    if (strstr(line, "M1002") || (strstr(line, "ok") && strstr(line, "@:") && strstr(line, "B@:"))) {
        parse_m1002_response(line);
    }
    
    // Check for errors
    if (strstr(line, "Error:") || strstr(line, "error:") || strstr(line, "!!")) {
        ESP_LOGW(TAG, "Printer error: %s", line);
        if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_status.state = PRINTER_STATE_ERROR;
            s_status.consecutive_errors++;
            if (s_callbacks.on_state_change) {
                s_callbacks.on_state_change(PRINTER_STATE_ERROR, s_callbacks.user_ctx);
            }
            xSemaphoreGive(s_status_mutex);
        }
    }
    
    // Update last OK time
    if (strncmp(line, "ok", 2) == 0) {
        if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            s_status.last_ok_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            s_status.consecutive_errors = 0;
            xSemaphoreGive(s_status_mutex);
        }
    }
}

// USB CDC data callback
static void on_usb_data(const uint8_t* data, size_t len, void* ctx) {
    // Process each line
    char line[256];
    const char* ptr = (const char*)data;
    const char* end = ptr + len;
    
    while (ptr < end) {
        const char* nl = memchr(ptr, '\n', end - ptr);
        if (!nl) break;
        
        size_t line_len = nl - ptr;
        if (line_len > 0 && ptr[line_len - 1] == '\r') line_len--;
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        
        memcpy(line, ptr, line_len);
        line[line_len] = '\0';
        
        process_printer_line(line);
        
        ptr = nl + 1;
    }
}

// Streaming task - sends G-code lines from file
static void stream_task(void* arg) {
    char line[256];
    char response[256];
    
    ESP_LOGI(TAG, "Stream task started for %s", s_current_file);
    
    while (s_streaming && s_print_file) {
        if (s_paused) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // Read next line
        if (fgets(line, sizeof(line), s_print_file) == NULL) {
            // End of file
            ESP_LOGI(TAG, "End of file reached");
            break;
        }
        
        s_file_pos = ftell(s_print_file);
        
        // Skip comments and empty lines
        char* trimmed = line;
        while (*trimmed && isspace(*trimmed)) trimmed++;
        if (*trimmed == ';' || *trimmed == '\0' || *trimmed == '\n') {
            continue;
        }
        
        // Remove newline
        char* nl = strchr(trimmed, '\n');
        if (nl) *nl = '\0';
        nl = strchr(trimmed, '\r');
        if (nl) *nl = '\0';
        
        // Send line and wait for OK
        esp_err_t err = usb_cdc_send_command(trimmed, response, sizeof(response), 30000);
        
        if (err == ESP_OK) {
            s_lines_sent++;
            
            // Update progress
            if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                s_progress.file_pos = s_file_pos;
                s_progress.progress = (s_file_size > 0) ? (s_file_pos * 100.0f / s_file_size) : 0;
                s_progress.bytes_printed = s_file_pos;
                xSemaphoreGive(s_status_mutex);
            }
            
            // Notify progress callback every 10 lines
            if (s_lines_sent % 10 == 0 && s_callbacks.on_progress_update) {
                s_callbacks.on_progress_update(&s_progress, s_callbacks.user_ctx);
            }
        } else {
            ESP_LOGE(TAG, "Failed to send line: %s (err: %s)", trimmed, esp_err_to_name(err));
            s_status.consecutive_errors++;
            
            if (s_status.consecutive_errors > 5) {
                ESP_LOGE(TAG, "Too many errors, stopping print");
                break;
            }
        }
        
        // Small delay to not flood
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    // Print finished
    if (s_print_file) {
        fclose(s_print_file);
        s_print_file = NULL;
    }
    
    s_streaming = false;
    
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (s_status.state == PRINTER_STATE_PRINTING) {
            s_status.state = PRINTER_STATE_IDLE;
            if (s_callbacks.on_state_change) {
                s_callbacks.on_state_change(PRINTER_STATE_IDLE, s_callbacks.user_ctx);
            }
        }
        xSemaphoreGive(s_status_mutex);
    }
    
    ESP_LOGI(TAG, "Stream task finished");
    s_stream_task = NULL;
    vTaskDelete(NULL);
}

// Status polling task
static void status_task(void* arg) {
    const TickType_t interval = pdMS_TO_TICKS(2000);  // 2 seconds
    
    while (1) {
        if (s_connected()) {
            printer_request_status();
            printer_request_position();
            
            if (s_status.state == PRINTER_STATE_PRINTING || s_status.state == PRINTER_STATE_PAUSED) {
                printer_request_sd_status();
            }
            
            // Notify status update
            if (s_callbacks.on_status_update) {
                printer_status_t status_copy;
                printer_get_status(&status_copy);
                s_callbacks.on_status_update(&status_copy, s_callbacks.user_ctx);
            }
        }
        
        vTaskDelay(interval);
    }
}

// Check USB connection (extern from usb_host_cdc)
extern bool usb_cdc_is_connected(void);
static inline bool s_connected(void) { return usb_cdc_is_connected(); }

esp_err_t printer_protocol_init(const printer_callbacks_t* callbacks) {
    ESP_LOGI(TAG, "Initializing printer protocol...");
    
    if (callbacks) {
        s_callbacks = *callbacks;
    }
    
    s_status_mutex = xSemaphoreCreateMutex();
    s_stream_mutex = xSemaphoreCreateMutex();
    
    if (!s_status_mutex || !s_stream_mutex) {
        ESP_LOGE(TAG, "Failed to create mutexes");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize status
    memset(&s_status, 0, sizeof(s_status));
    memset(&s_progress, 0, sizeof(s_progress));
    s_status.state = PRINTER_STATE_DISCONNECTED;
    strcpy(s_status.printer_model, "Ender 3 S1");
    
    // Allocate G-code queue
    s_gcode_queue = (gcode_cmd_t*)malloc(MAX_QUEUE_SIZE * sizeof(gcode_cmd_t));
    if (!s_gcode_queue) {
        return ESP_ERR_NO_MEM;
    }
    
    // Start status polling task
    xTaskCreatePinnedToCore(status_task, "printer_status", 4096, NULL, 8, &s_status_task, 1);
    
    ESP_LOGI(TAG, "Printer protocol initialized");
    return ESP_OK;
}

void printer_protocol_deinit(void) {
    if (s_streaming) {
        printer_cancel_print();
    }
    
    if (s_status_task) {
        vTaskDelete(s_status_task);
    }
    
    if (s_status_mutex) vSemaphoreDelete(s_status_mutex);
    if (s_stream_mutex) vSemaphoreDelete(s_stream_mutex);
    
    if (s_gcode_queue) {
        free(s_gcode_queue);
        s_gcode_queue = NULL;
    }
}

esp_err_t printer_start_print_stream(const char* filename) {
    if (!filename || s_streaming) return ESP_ERR_INVALID_STATE;
    
    // Open file from LittleFS
    s_print_file = fopen(filename, "r");
    if (!s_print_file) {
        ESP_LOGE(TAG, "Failed to open file: %s", filename);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Get file size
    fseek(s_print_file, 0, SEEK_END);
    s_file_size = ftell(s_print_file);
    fseek(s_print_file, 0, SEEK_SET);
    
    strncpy(s_current_file, filename, sizeof(s_current_file) - 1);
    s_file_pos = 0;
    s_lines_sent = 0;
    
    // Initialize progress
    memset(&s_progress, 0, sizeof(s_progress));
    strncpy(s_progress.filename, filename, sizeof(s_progress.filename) - 1);
    s_progress.file_size = s_file_size;
    
    // Update status
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_status.state = PRINTER_STATE_PRINTING;
        s_status.progress = s_progress;
        xSemaphoreGive(s_status_mutex);
    }
    
    s_streaming = true;
    s_paused = false;
    
    // Create stream task
    xTaskCreatePinnedToCore(stream_task, "gcode_stream", 8192, NULL, 10, &s_stream_task, 0);
    
    ESP_LOGI(TAG, "Started streaming print: %s (%u bytes)", filename, s_file_size);
    return ESP_OK;
}

esp_err_t printer_start_print_sd(const char* filename) {
    if (!filename) return ESP_ERR_INVALID_ARG;
    
    // Select file on SD
    char cmd[160];
    snprintf(cmd, sizeof(cmd), "M23 %s", filename);
    esp_err_t err = usb_cdc_send_command(cmd, NULL, 0, 5000);
    if (err != ESP_OK) return err;
    
    // Start print
    err = usb_cdc_send_command("M24", NULL, 0, 5000);
    if (err != ESP_OK) return err;
    
    // Update status
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_status.state = PRINTER_STATE_PRINTING;
        strncpy(s_status.progress.filename, filename, sizeof(s_status.progress.filename) - 1);
        xSemaphoreGive(s_status_mutex);
    }
    
    ESP_LOGI(TAG, "Started SD print: %s", filename);
    return ESP_OK;
}

esp_err_t printer_pause_print(void) {
    if (!s_streaming && s_status.state != PRINTER_STATE_PRINTING) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_streaming) {
        s_paused = true;
    }
    
    return usb_cdc_send_command("M0", NULL, 0, 5000);
}

esp_err_t printer_resume_print(void) {
    if (!s_streaming && s_status.state != PRINTER_STATE_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (s_streaming) {
        s_paused = false;
    }
    
    return usb_cdc_send_command("M24", NULL, 0, 5000);
}

esp_err_t printer_cancel_print(void) {
    // Emergency stop
    usb_cdc_send_command("M112", NULL, 0, 1000);
    
    // Wait a bit
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // Clean up streaming
    if (s_streaming) {
        s_streaming = false;
        s_paused = false;
        
        if (s_print_file) {
            fclose(s_print_file);
            s_print_file = NULL;
        }
        
        if (s_stream_task) {
            vTaskDelay(pdMS_TO_TICKS(100));  // Let task clean up
        }
    }
    
    // Reset printer
    usb_cdc_send_command("M999", NULL, 0, 2000);
    
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_status.state = PRINTER_STATE_IDLE;
        s_status.progress.file_pos = 0;
        s_status.progress.progress = 0;
        xSemaphoreGive(s_status_mutex);
    }
    
    ESP_LOGI(TAG, "Print cancelled");
    return ESP_OK;
}

esp_err_t printer_send_gcode(const char* gcode, char* response, size_t resp_size) {
    if (!gcode) return ESP_ERR_INVALID_ARG;
    return usb_cdc_send_command(gcode, response, resp_size, 5000);
}

esp_err_t printer_request_status(void) {
    char response[256];
    return usb_cdc_send_command("M105", response, sizeof(response), 2000);
}

esp_err_t printer_request_position(void) {
    char response[256];
    return usb_cdc_send_command("M114", response, sizeof(response), 2000);
}

esp_err_t printer_request_sd_status(void) {
    char response[256];
    return usb_cdc_send_command("M27", response, sizeof(response), 2000);
}

esp_err_t printer_set_hotend_temp(float temp) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "M104 S%.0f", temp);
    return usb_cdc_send_command(cmd, NULL, 0, 2000);
}

esp_err_t printer_set_bed_temp(float temp) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "M140 S%.0f", temp);
    return usb_cdc_send_command(cmd, NULL, 0, 2000);
}

esp_err_t printer_set_fan_speed(uint8_t fan_id, uint8_t speed) {
    char cmd[32];
    if (fan_id == 0) {
        snprintf(cmd, sizeof(cmd), "M106 S%d", speed * 255 / 100);
    } else {
        snprintf(cmd, sizeof(cmd), "M106 P%d S%d", fan_id, speed * 255 / 100);
    }
    return usb_cdc_send_command(cmd, NULL, 0, 2000);
}

esp_err_t printer_move_axes(float x, float y, float z, float e, float feedrate) {
    char cmd[128];
    int len = snprintf(cmd, sizeof(cmd), "G1");
    
    if (!isnan(x)) len += snprintf(cmd + len, sizeof(cmd) - len, " X%.2f", x);
    if (!isnan(y)) len += snprintf(cmd + len, sizeof(cmd) - len, " Y%.2f", y);
    if (!isnan(z)) len += snprintf(cmd + len, sizeof(cmd) - len, " Z%.2f", z);
    if (!isnan(e)) len += snprintf(cmd + len, sizeof(cmd) - len, " E%.2f", e);
    if (feedrate > 0) len += snprintf(cmd + len, sizeof(cmd) - len, " F%.0f", feedrate);
    
    return usb_cdc_send_command(cmd, NULL, 0, 10000);
}

esp_err_t printer_home_axes(bool x, bool y, bool z) {
    char cmd[32] = "G28";
    int len = 3;
    
    if (x) len += snprintf(cmd + len, sizeof(cmd) - len, " X");
    if (y) len += snprintf(cmd + len, sizeof(cmd) - len, " Y");
    if (z) len += snprintf(cmd + len, sizeof(cmd) - len, " Z");
    
    return usb_cdc_send_command(cmd, NULL, 0, 60000);  // Homing can take long
}

void printer_get_status(printer_status_t* status) {
    if (!status) return;
    
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *status = s_status;
        xSemaphoreGive(s_status_mutex);
    }
}

bool printer_is_printing(void) {
    bool printing = false;
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        printing = (s_status.state == PRINTER_STATE_PRINTING || s_status.state == PRINTER_STATE_PAUSED);
        xSemaphoreGive(s_status_mutex);
    }
    return printing;
}

void printer_get_progress(print_progress_t* progress) {
    if (!progress) return;
    
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *progress = s_progress;
        xSemaphoreGive(s_status_mutex);
    }
}

esp_err_t printer_emergency_stop(void) {
    return usb_cdc_send_command("M112", NULL, 0, 1000);
}