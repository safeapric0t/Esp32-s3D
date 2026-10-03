#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Camera interface for future expansion
typedef struct {
    // Initialize camera
    esp_err_t (*init)(void);
    
    // Deinitialize camera
    void (*deinit)(void);
    
    // Capture single frame (JPEG)
    esp_err_t (*capture)(uint8_t** buffer, size_t* size);
    
    // Start MJPEG stream
    esp_err_t (*start_stream)(void);
    
    // Stop MJPEG stream
    void (*stop_stream)(void);
    
    // Get stream frame
    esp_err_t (*get_frame)(uint8_t** buffer, size_t* size);
    
    // Camera ready?
    bool (*is_ready)(void);
    
    // Camera name
    const char* name;
} camera_interface_t;

// Dummy camera implementation (no camera connected)
extern const camera_interface_t camera_dummy;

// Get current camera interface
const camera_interface_t* camera_get_interface(void);

// Set camera interface (for future hardware)
void camera_set_interface(const camera_interface_t* iface);

#ifdef __cplusplus
}
#endif