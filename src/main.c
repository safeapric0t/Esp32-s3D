#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include "types.h"
#include "usb_host_cdc.h"
#include "printer_protocol.h"
#include "web_server.h"
#include "storage.h"
#include "camera_if.h"
#include "json_utils.h"

static const char* TAG = "MAIN";

// System configuration
static system_config_t s_config = {
    .wifi_ssid = "TURKNET_B907B",
    .wifi_password = "TYkyRDkN",
    .wifi_ap_ssid = "Ender3-Controller",
    .wifi_ap_password = "ender3controller",
    .ap_mode = false,  // STA mode - connect to your WiFi
    .auth = {
        .enabled = true,
        .key = "ender3s1secure2024"  // Change this in production!
    },
    .printer_name = "Ender 3 S1",
    .baud_rate = 115200,
    .auto_connect = true,
    .status_interval_ms = 2000
};

// Event group for WiFi
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_apstaconnected_t*)event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d",
                 MAC2STR(event->mac), event->aid);
    }
}

// USB CDC callbacks
static void on_usb_data(const uint8_t* data, size_t len, void* ctx) {
    // Handled by printer_protocol
}

static void on_usb_event(bool connected, void* ctx) {
    ESP_LOGI(TAG, "USB CDC %s", connected ? "connected" : "disconnected");
    
    if (xSemaphoreTake(s_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_status.usb_connected = connected;
        if (!connected) {
            s_status.state = PRINTER_STATE_DISCONNECTED;
        }
        xSemaphoreGive(s_status_mutex);
    }
    
    if (connected && s_config.auto_connect) {
        // Request initial status
        vTaskDelay(pdMS_TO_TICKS(1000));
        printer_request_status();
        printer_request_position();
    }
}

// Printer protocol callbacks
static void on_status_update(const printer_status_t* status, void* ctx) {
    web_server_broadcast_status(status);
}

static void on_progress_update(const print_progress_t* progress, void* ctx) {
    web_server_broadcast_progress(progress);
}

static void on_line_received(const char* line, void* ctx) {
    web_server_broadcast_log("printer", line);
}

static void on_state_change(printer_state_t new_state, void* ctx) {
    const char* state_str[] = {"IDLE", "PRINTING", "PAUSED", "ERROR", "DISCONNECTED", "BOOTING", "HEATING"};
    ESP_LOGI(TAG, "Printer state: %s", state_str[new_state]);
    
    DynamicJsonDocument doc(256);
    doc["type"] = "state_change";
    doc["data"]["state"] = new_state;
    doc["data"]["state_str"] = state_str[new_state];
    
    String msg;
    serializeJson(doc, msg);
    // Broadcast via web_server websocket
    extern AsyncWebSocket* s_ws;
    extern uint32_t s_ws_client_count;
    if (s_ws && s_ws_client_count > 0) {
        s_ws->textAll(msg);
    }
}

// Web server callbacks
static void on_print_start(const char* filename, bool stream_mode, void* ctx) {
    ESP_LOGI(TAG, "Print start requested: %s (stream=%d)", filename, stream_mode);
    
    char full_path[256];
    snprintf(full_path, sizeof(full_path), "/littlefs/gcode/%s", filename);
    
    if (stream_mode) {
        printer_start_print_stream(full_path);
    } else {
        printer_start_print_sd(filename);
    }
}

static void on_print_pause(void* ctx) {
    printer_pause_print();
}

static void on_print_resume(void* ctx) {
    printer_resume_print();
}

static void on_print_cancel(void* ctx) {
    printer_cancel_print();
}

static void on_gcode_command(const char* gcode, void* ctx) {
    printer_send_gcode(gcode, NULL, 0);
}

static void on_file_upload(const char* filename, const uint8_t* data, size_t len, void* ctx) {
    ESP_LOGI(TAG, "File uploaded: %s", filename);
    web_server_broadcast_file_list(NULL, 0);  // Trigger refresh
}

static void on_file_delete(const char* filename, bool on_printer_sd, void* ctx) {
    storage_delete_file(filename, on_printer_sd);
    web_server_broadcast_file_list(NULL, 0);
}

static void on_file_copy_to_printer(const char* filename, void* ctx) {
    storage_copy_to_printer_sd(filename);
}

static void on_move_axes(float x, float y, float z, float e, float feedrate, void* ctx) {
    printer_move_axes(x, y, z, e, feedrate);
}

static void on_set_temp(float hotend, float bed, void* ctx) {
    if (hotend >= 0) printer_set_hotend_temp(hotend);
    if (bed >= 0) printer_set_bed_temp(bed);
}

static void on_set_fan(uint8_t fan_id, uint8_t speed, void* ctx) {
    printer_set_fan_speed(fan_id, speed);
}

static void on_home_axes(bool x, bool y, bool z, void* ctx) {
    printer_home_axes(x, y, z);
}

// Initialize NVS
static esp_err_t init_nvs(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

// Initialize WiFi
static esp_err_t init_wifi(void) {
    s_wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    if (s_config.ap_mode) {
        esp_netif_create_default_wifi_ap();
    } else {
        esp_netif_create_default_wifi_sta();
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    
    return ESP_OK;
}

// Main task for periodic operations
static void main_task(void* arg) {
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t interval = pdMS_TO_TICKS(1000);  // 1 second
    
    while (1) {
        // Periodic status request to printer
        if (usb_cdc_is_connected()) {
            printer_request_status();
            
            // Request position less frequently
            static int pos_counter = 0;
            if (++pos_counter >= 5) {
                printer_request_position();
                pos_counter = 0;
            }
        }
        
        // Print free heap every 30 seconds
        static int heap_counter = 0;
        if (++heap_counter >= 30) {
            ESP_LOGI(TAG, "Free heap: %u bytes", esp_get_free_heap_size());
            heap_counter = 0;
        }
        
        vTaskDelayUntil(&last_wake, interval);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "=== Ender 3 S1 ESP32-S3 Controller Starting ===");
    ESP_LOGI(TAG, "Firmware: %s", "1.0.0");
    
    // Initialize NVS
    ESP_ERROR_CHECK(init_nvs());
    
    // Initialize WiFi
    ESP_ERROR_CHECK(init_wifi());
    
    // Initialize storage (LittleFS)
    ESP_ERROR_CHECK(storage_init());
    
    // Initialize camera (dummy for now)
    const camera_interface_t* cam = camera_get_interface();
    if (cam->init) cam->init();
    
    // Initialize USB Host CDC
    usb_cdc_callbacks_t usb_callbacks = {
        .on_data = on_usb_data,
        .on_event = on_usb_event,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(usb_cdc_init(&usb_callbacks));
    
    // Initialize printer protocol
    printer_callbacks_t printer_callbacks = {
        .on_status_update = on_status_update,
        .on_progress_update = on_progress_update,
        .on_line_received = on_line_received,
        .on_state_change = on_state_change,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(printer_protocol_init(&printer_callbacks));
    
    // Initialize web server
    web_server_callbacks_t ws_callbacks = {
        .on_print_start = on_print_start,
        .on_print_pause = on_print_pause,
        .on_print_resume = on_print_resume,
        .on_print_cancel = on_print_cancel,
        .on_gcode_command = on_gcode_command,
        .on_file_upload = on_file_upload,
        .on_file_delete = on_file_delete,
        .on_file_copy_to_printer = on_file_copy_to_printer,
        .on_move_axes = on_move_axes,
        .on_set_temp = on_set_temp,
        .on_set_fan = on_set_fan,
        .on_home_axes = on_home_axes,
        .user_ctx = NULL
    };
    ESP_ERROR_CHECK(web_server_init(&ws_callbacks, &s_config));
    
    // Start web server
    ESP_ERROR_CHECK(web_server_start());
    
    // Create main periodic task
    xTaskCreatePinnedToCore(main_task, "main_task", 4096, NULL, 5, NULL, 1);
    
    ESP_LOGI(TAG, "=== System Ready ===");
    ESP_LOGI(TAG, "Web UI: http://%s/", s_config.ap_mode ? "192.168.4.1" : "<STA_IP>");
    ESP_LOGI(TAG, "API Key: %s", s_config.auth.key);
    
    // Main loop - just keep alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}