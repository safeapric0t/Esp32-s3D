#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// USB CDC callbacks
typedef void (*cdc_data_cb_t)(const uint8_t* data, size_t len, void* user_ctx);
typedef void (*cdc_event_cb_t)(bool connected, void* user_ctx);

typedef struct {
    cdc_data_cb_t on_data;
    cdc_event_cb_t on_event;
    void* user_ctx;
} usb_cdc_callbacks_t;

// Initialize USB Host CDC
esp_err_t usb_cdc_init(const usb_cdc_callbacks_t* callbacks);

// Deinitialize USB Host CDC
void usb_cdc_deinit(void);

// Send data to printer (non-blocking, returns bytes queued)
size_t usb_cdc_write(const uint8_t* data, size_t len);

// Send string to printer with newline
size_t usb_cdc_write_str(const char* str);

// Send G-code command and wait for OK/ERROR response (blocking with timeout)
esp_err_t usb_cdc_send_command(const char* command, char* response, size_t response_size, uint32_t timeout_ms);

// Check if printer is connected
bool usb_cdc_is_connected(void);

// Get connection stats
void usb_cdc_get_stats(uint32_t* tx_bytes, uint32_t* rx_bytes, uint32_t* errors);

// Flush TX buffer
void usb_cdc_flush(void);

#ifdef __cplusplus
}
#endif