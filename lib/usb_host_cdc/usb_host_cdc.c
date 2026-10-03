#include "usb_host_cdc.h"
#include "ring_buffer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "driver/usb_host.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

static const char* TAG = "USB_CDC";

// CDC ACM device handle
static cdc_acm_dev_hdl_t s_cdc_dev = NULL;
static TaskHandle_t s_rx_task = NULL;
static TaskHandle_t s_event_task = NULL;
static SemaphoreHandle_t s_tx_mutex = NULL;
static SemaphoreHandle_t s_rx_mutex = NULL;
static QueueHandle_t s_response_queue = NULL;

static ring_buffer_t* s_rx_buffer = NULL;
static ring_buffer_t* s_tx_buffer = NULL;

static usb_cdc_callbacks_t s_callbacks = {0};
static bool s_connected = false;
static uint32_t s_tx_bytes = 0;
static uint32_t s_rx_bytes = 0;
static uint32_t s_errors = 0;

// Response waiting
typedef struct {
    char* buffer;
    size_t size;
    SemaphoreHandle_t done;
    bool received;
} response_wait_t;

static response_wait_t* s_waiting_response = NULL;

#define RX_BUFFER_SIZE 8192
#define TX_BUFFER_SIZE 4096
#define RESPONSE_TIMEOUT_MS 5000
#define CDC_TASK_STACK 4096
#define CDC_TASK_PRIORITY 10

// Forward declarations
static void rx_task(void* arg);
static void event_task(void* arg);
static void handle_cdc_data(const uint8_t* data, size_t len);
static bool check_for_ok_response(const char* line);

esp_err_t usb_cdc_init(const usb_cdc_callbacks_t* callbacks) {
    ESP_LOGI(TAG, "Initializing USB Host CDC...");
    
    if (callbacks) {
        s_callbacks = *callbacks;
    }
    
    // Create buffers
    s_rx_buffer = ring_buffer_create(RX_BUFFER_SIZE);
    s_tx_buffer = ring_buffer_create(TX_BUFFER_SIZE);
    if (!s_rx_buffer || !s_tx_buffer) {
        ESP_LOGE(TAG, "Failed to create ring buffers");
        return ESP_ERR_NO_MEM;
    }
    
    // Create mutexes and queue
    s_tx_mutex = xSemaphoreCreateMutex();
    s_rx_mutex = xSemaphoreCreateMutex();
    s_response_queue = xQueueCreate(10, sizeof(char*));
    
    if (!s_tx_mutex || !s_rx_mutex || !s_response_queue) {
        ESP_LOGE(TAG, "Failed to create synchronization primitives");
        return ESP_ERR_NO_MEM;
    }
    
    // Install USB Host driver
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB host: %s", esp_err_to_name(err));
        return err;
    }
    
    // CDC ACM host configuration
    cdc_acm_host_config_t cdc_config = {
        .vid = 0,  // Auto-detect
        .pid = 0,  // Auto-detect
        .interface_num = 0,
        .data_in_ep = 0,
        .data_out_ep = 0,
        .notify_ep = 0,
        .callback = NULL,
        .user_arg = NULL,
    };
    
    // Wait for device connection
    ESP_LOGI(TAG, "Waiting for CDC device...");
    uint32_t start = xTaskGetTickCount();
    while (xTaskGetTickCount() - start < pdMS_TO_TICKS(30000)) {
        err = cdc_acm_host_open(&cdc_config, &s_cdc_dev);
        if (err == ESP_OK) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No CDC device found, will retry in background");
        // Don't return error, let event task handle hot-plug
    } else {
        s_connected = true;
        if (s_callbacks.on_event) {
            s_callbacks.on_event(true, s_callbacks.user_ctx);
        }
        ESP_LOGI(TAG, "CDC device connected");
    }
    
    // Create RX task
    xTaskCreatePinnedToCore(rx_task, "usb_cdc_rx", CDC_TASK_STACK, NULL, CDC_TASK_PRIORITY, &s_rx_task, 0);
    
    // Create event handling task
    xTaskCreatePinnedToCore(event_task, "usb_cdc_evt", CDC_TASK_STACK, NULL, CDC_TASK_PRIORITY - 1, &s_event_task, 0);
    
    ESP_LOGI(TAG, "USB Host CDC initialized");
    return ESP_OK;
}

void usb_cdc_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing USB Host CDC...");
    
    if (s_rx_task) {
        vTaskDelete(s_rx_task);
        s_rx_task = NULL;
    }
    if (s_event_task) {
        vTaskDelete(s_event_task);
        s_event_task = NULL;
    }
    
    if (s_cdc_dev) {
        cdc_acm_host_close(s_cdc_dev);
        s_cdc_dev = NULL;
    }
    
    usb_host_uninstall();
    
    if (s_rx_buffer) {
        ring_buffer_destroy(s_rx_buffer);
        s_rx_buffer = NULL;
    }
    if (s_tx_buffer) {
        ring_buffer_destroy(s_tx_buffer);
        s_tx_buffer = NULL;
    }
    
    if (s_tx_mutex) vSemaphoreDelete(s_tx_mutex);
    if (s_rx_mutex) vSemaphoreDelete(s_rx_mutex);
    if (s_response_queue) vQueueDelete(s_response_queue);
    
    s_connected = false;
    ESP_LOGI(TAG, "USB Host CDC deinitialized");
}

static void rx_task(void* arg) {
    uint8_t buffer[512];
    
    while (1) {
        if (!s_cdc_dev || !s_connected) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        size_t received = 0;
        esp_err_t err = cdc_acm_host_data_read(s_cdc_dev, buffer, sizeof(buffer), &received, pdMS_TO_TICKS(100));
        
        if (err == ESP_OK && received > 0) {
            s_rx_bytes += received;
            
            if (xSemaphoreTake(s_rx_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                ring_buffer_push(s_rx_buffer, buffer, received);
                xSemaphoreGive(s_rx_mutex);
            }
            
            handle_cdc_data(buffer, received);
        } else if (err == ESP_ERR_TIMEOUT) {
            // No data, continue
        } else if (err == ESP_ERR_NOT_FOUND || err == ESP_ERR_INVALID_STATE) {
            // Device disconnected
            ESP_LOGW(TAG, "CDC device disconnected");
            s_connected = false;
            if (s_callbacks.on_event) {
                s_callbacks.on_event(false, s_callbacks.user_ctx);
            }
        } else {
            s_errors++;
        }
    }
}

static void event_task(void* arg) {
    while (1) {
        // Handle USB host events
        uint32_t event_flags;
        usb_host_lib_handle_events(pdMS_TO_TICKS(100), &event_flags);
        
        // Check for device connection/disconnection
        if (!s_connected) {
            cdc_acm_host_config_t cdc_config = {0};
            esp_err_t err = cdc_acm_host_open(&cdc_config, &s_cdc_dev);
            if (err == ESP_OK) {
                s_connected = true;
                if (s_callbacks.on_event) {
                    s_callbacks.on_event(true, s_callbacks.user_ctx);
                }
                ESP_LOGI(TAG, "CDC device connected (hot-plug)");
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

static void handle_cdc_data(const uint8_t* data, size_t len) {
    // Process complete lines from ring buffer
    if (xSemaphoreTake(s_rx_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        char line[256];
        while (ring_buffer_find_line(s_rx_buffer, line, sizeof(line))) {
            ESP_LOGD(TAG, "RX: %s", line);
            
            // Check if this is a response we're waiting for
            if (s_waiting_response && check_for_ok_response(line)) {
                if (s_waiting_response->buffer && s_waiting_response->size > 0) {
                    strncpy(s_waiting_response->buffer, line, s_waiting_response->size - 1);
                    s_waiting_response->buffer[s_waiting_response->size - 1] = '\0';
                }
                s_waiting_response->received = true;
                xSemaphoreGive(s_waiting_response->done);
                s_waiting_response = NULL;
            }
            
            // Callback for general data
            if (s_callbacks.on_data) {
                s_callbacks.on_data((const uint8_t*)line, strlen(line), s_callbacks.user_ctx);
            }
        }
        xSemaphoreGive(s_rx_mutex);
    }
}

static bool check_for_ok_response(const char* line) {
    if (!line) return false;
    // Marlin responses: "ok", "ok T:...", "error", "Error:", "!!"
    return (strncmp(line, "ok", 2) == 0) || 
           (strncmp(line, "error", 5) == 0) ||
           (strncmp(line, "Error", 5) == 0) ||
           (strncmp(line, "!!", 2) == 0);
}

size_t usb_cdc_write(const uint8_t* data, size_t len) {
    if (!s_cdc_dev || !s_connected || !data || len == 0) return 0;
    
    if (xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return 0;
    }
    
    size_t written = 0;
    esp_err_t err = cdc_acm_host_data_write(s_cdc_dev, data, len, &written, pdMS_TO_TICKS(2000));
    
    xSemaphoreGive(s_tx_mutex);
    
    if (err == ESP_OK) {
        s_tx_bytes += written;
        return written;
    }
    
    s_errors++;
    return 0;
}

size_t usb_cdc_write_str(const char* str) {
    if (!str) return 0;
    return usb_cdc_write((const uint8_t*)str, strlen(str));
}

esp_err_t usb_cdc_send_command(const char* command, char* response, size_t response_size, uint32_t timeout_ms) {
    if (!s_connected || !command) return ESP_ERR_INVALID_STATE;
    
    // Prepare response wait structure
    response_wait_t wait = {
        .buffer = response,
        .size = response_size,
        .done = xSemaphoreCreateBinary(),
        .received = false
    };
    
    if (!wait.done) return ESP_ERR_NO_MEM;
    
    s_waiting_response = &wait;
    
    // Send command with newline
    char cmd_with_nl[260];
    snprintf(cmd_with_nl, sizeof(cmd_with_nl), "%s\n", command);
    usb_cdc_write_str(cmd_with_nl);
    
    // Wait for response
    esp_err_t result = ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(wait.done, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        result = wait.received ? ESP_OK : ESP_FAIL;
    }
    
    vSemaphoreDelete(wait.done);
    s_waiting_response = NULL;
    
    return result;
}

bool usb_cdc_is_connected(void) {
    return s_connected;
}

void usb_cdc_get_stats(uint32_t* tx_bytes, uint32_t* rx_bytes, uint32_t* errors) {
    if (tx_bytes) *tx_bytes = s_tx_bytes;
    if (rx_bytes) *rx_bytes = s_rx_bytes;
    if (errors) *errors = s_errors;
}

void usb_cdc_flush(void) {
    if (s_cdc_dev && s_connected) {
        cdc_acm_host_flush(s_cdc_dev);
    }
}