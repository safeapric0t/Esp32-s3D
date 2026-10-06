#include "web_server.h"
#include "printer_protocol.h"
#include "storage.h"
#include "json_utils.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_server.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <dirent.h>

static const char* TAG = "WEB_SERVER";

// AsyncWebServer (from ESPAsyncWebServer library)
extern "C" {
#include "esp_async_webserver.h"
}

// Global instances
static AsyncWebServer* s_server = NULL;
static AsyncWebSocket* s_ws = NULL;
static web_server_callbacks_t s_callbacks = {0};
static system_config_t s_config = {0};
static SemaphoreHandle_t s_clients_mutex = NULL;
static uint32_t s_ws_client_count = 0;

// Authentication
static bool check_auth(AsyncWebServerRequest* request) {
    if (!s_config.auth.enabled) return true;
    
    AsyncWebHeader* auth_header = request->getHeader("Authorization");
    if (!auth_header) return false;
    
    const char* auth_value = auth_header->value().c_str();
    if (strncmp(auth_value, "Bearer ", 7) != 0) return false;
    
    return strcmp(auth_value + 7, s_config.auth.key) == 0;
}

// WebSocket event handler
static void on_ws_event(AsyncWebSocket* server, AsyncWebSocketClient* client, 
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {
        case WS_EVT_CONNECT:
            ESP_LOGI(TAG, "WS client #%u connected from %s", client->id(), client->remoteIP().toString().c_str());
            if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                s_ws_client_count++;
                xSemaphoreGive(s_clients_mutex);
            }
            break;
            
        case WS_EVT_DISCONNECT:
            ESP_LOGI(TAG, "WS client #%u disconnected", client->id());
            if (xSemaphoreTake(s_clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (s_ws_client_count > 0) s_ws_client_count--;
                xSemaphoreGive(s_clients_mutex);
            }
            break;
            
        case WS_EVT_DATA:
            {
                AwsFrameInfo* info = (AwsFrameInfo*)arg;
                if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
                    data[len] = '\0';
                    ESP_LOGD(TAG, "WS message: %s", (char*)data);
                    
                    // Parse command
                    DynamicJsonDocument doc(512);
                    DeserializationError err = deserializeJson(doc, (char*)data);
                    if (!err) {
                        const char* type = doc["type"];
                        if (type) {
                            if (strcmp(type, "gcode") == 0) {
                                const char* cmd = doc["command"];
                                if (cmd && s_callbacks.on_gcode_command) {
                                    s_callbacks.on_gcode_command(cmd, s_callbacks.user_ctx);
                                }
                            } else if (strcmp(type, "move") == 0) {
                                float x = doc["x"] | NAN;
                                float y = doc["y"] | NAN;
                                float z = doc["z"] | NAN;
                                float e = doc["e"] | NAN;
                                float f = doc["feedrate"] | 3000;
                                if (s_callbacks.on_move_axes) {
                                    s_callbacks.on_move_axes(x, y, z, e, f, s_callbacks.user_ctx);
                                }
                            } else if (strcmp(type, "temp") == 0) {
                                float hotend = doc["hotend"] | -1;
                                float bed = doc["bed"] | -1;
                                if (s_callbacks.on_set_temp) {
                                    s_callbacks.on_set_temp(hotend, bed, s_callbacks.user_ctx);
                                }
                            } else if (strcmp(type, "fan") == 0) {
                                uint8_t fan_id = doc["fan_id"] | 0;
                                uint8_t speed = doc["speed"] | 0;
                                if (s_callbacks.on_set_fan) {
                                    s_callbacks.on_set_fan(fan_id, speed, s_callbacks.user_ctx);
                                }
                            } else if (strcmp(type, "home") == 0) {
                                bool x = doc["x"] | true;
                                bool y = doc["y"] | true;
                                bool z = doc["z"] | true;
                                if (s_callbacks.on_home_axes) {
                                    s_callbacks.on_home_axes(x, y, z, s_callbacks.user_ctx);
                                }
                            }
                        }
                    }
                }
            }
            break;
            
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

// HTTP request handlers
static void handle_api_status(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    printer_status_t status;
    printer_get_status(&status);
    
    char* json = printer_status_to_json(&status);
    if (json) {
        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
        free_json_string(json);
    } else {
        request->send(500, "application/json", "{\"error\":\"Failed to generate status\"}");
    }
}

static void handle_api_printer(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    printer_status_t status;
    printer_get_status(&status);
    
    DynamicJsonDocument doc(2048);
    doc["firmware_version"] = status.firmware_version;
    doc["printer_model"] = status.printer_model;
    doc["state"] = status.state;
    doc["usb_connected"] = status.usb_connected;
    doc["sd_inserted"] = status.sd_inserted;
    
    char* json = (char*)malloc(2048);
    serializeJson(doc, json, 2048);
    
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    free(json);
}

static void handle_api_files(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }

    // 1. Request SD files from printer first
    printer_request_sd_files();

    file_info_t* files = NULL;
    uint8_t count = 0;

    // 2. Get files from LittleFS
    storage_list_files(&files, &count);

    char* json = file_list_to_json(files, count, true);

    if (files) free(files);

    if (json) {
        AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
        response->addHeader("Access-Control-Allow-Origin", "*");
        request->send(response);
        free_json_string(json);
    } else {
        request->send(200, "application/json", "[]");
    }
}

static void handle_api_file_upload(AsyncWebServerRequest* request, 
                                    const String& filename, size_t index, uint8_t* data, 
                                    size_t len, bool final) {
    if (!check_auth(request)) {
        request->send(401);
        return;
    }
    
    static FILE* upload_file = NULL;
    static char upload_path[256];
    
    if (index == 0) {
        // Start upload
        snprintf(upload_path, sizeof(upload_path), "/littlefs/%s", filename.c_str());
        upload_file = fopen(upload_path, "wb");
        if (!upload_file) {
            ESP_LOGE(TAG, "Failed to create upload file: %s", upload_path);
            return;
        }
        ESP_LOGI(TAG, "Upload started: %s", filename.c_str());
    }
    
    if (upload_file && len > 0) {
        fwrite(data, 1, len, upload_file);
    }
    
    if (final) {
        if (upload_file) {
            fclose(upload_file);
            upload_file = NULL;
        }
        ESP_LOGI(TAG, "Upload finished: %s (%d bytes)", filename.c_str(), index + len);

        if (s_callbacks.on_file_upload) {
            // Corrected: We don't pass data pointer because the file is already on disk in LittleFS
            // But we pass the length so the callback knows the size
            s_callbacks.on_file_upload(filename.c_str(), NULL, index + len, s_callbacks.user_ctx);
        }

        request->send(200, "application/json", "{\"success\":true,\"message\":\"File uploaded\"}");
    }
}

static void handle_api_file_delete(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    if (!request->hasParam("name")) {
        request->send(400, "application/json", "{\"error\":\"Missing filename\"}");
        return;
    }
    
    String filename = request->getParam("name")->value();
    bool on_printer_sd = request->hasParam("printer_sd") && 
                         request->getParam("printer_sd")->value() == "true";
    
    esp_err_t err = storage_delete_file(filename.c_str(), on_printer_sd);
    
    if (err == ESP_OK && s_callbacks.on_file_delete) {
        s_callbacks.on_file_delete(filename.c_str(), on_printer_sd, s_callbacks.user_ctx);
    }
    
    char* json = create_api_response(err == ESP_OK, 
                                     err == ESP_OK ? "File deleted" : "Delete failed", NULL);
    AsyncWebServerResponse* response = request->beginResponse(
        err == ESP_OK ? 200 : 500, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    free_json_string(json);
}

static void handle_api_file_copy(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    if (!request->hasParam("name")) {
        request->send(400, "application/json", "{\"error\":\"Missing filename\"}");
        return;
    }
    
    String filename = request->getParam("name")->value();
    
    if (s_callbacks.on_file_copy_to_printer) {
        s_callbacks.on_file_copy_to_printer(filename.c_str(), s_callbacks.user_ctx);
    }
    
    char* json = create_api_response(true, "Copy to printer SD started", NULL);
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    free_json_string(json);
}

static void handle_api_print_start(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    if (!request->hasParam("file")) {
        request->send(400, "application/json", "{\"error\":\"Missing filename\"}");
        return;
    }
    
    String filename = request->getParam("file")->value();
    bool stream_mode = true;
    if (request->hasParam("mode")) {
        stream_mode = (request->getParam("mode")->value() == "stream");
    }
    
    if (s_callbacks.on_print_start) {
        s_callbacks.on_print_start(filename.c_str(), stream_mode, s_callbacks.user_ctx);
    }
    
    char* json = create_api_response(true, "Print started", NULL);
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    free_json_string(json);
}

static void handle_api_print_pause(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    if (s_callbacks.on_print_pause) {
        s_callbacks.on_print_pause(s_callbacks.user_ctx);
    }
    
    char* json = create_api_response(true, "Print paused", NULL);
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    free_json_string(json);
}

static void handle_api_print_resume(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    if (s_callbacks.on_print_resume) {
        s_callbacks.on_print_resume(s_callbacks.user_ctx);
    }
    
    char* json = create_api_response(true, "Print resumed", NULL);
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    free_json_string(json);
}

static void handle_api_print_cancel(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    if (s_callbacks.on_print_cancel) {
        s_callbacks.on_print_cancel(s_callbacks.user_ctx);
    }
    
    char* json = create_api_response(true, "Print cancelled", NULL);
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    free_json_string(json);
}

static void handle_api_gcode(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    if (!request->hasParam("command", true)) {
        request->send(400, "application/json", "{\"error\":\"Missing command\"}");
        return;
    }
    
    String command = request->getParam("command", true)->value();
    
    if (s_callbacks.on_gcode_command) {
        s_callbacks.on_gcode_command(command.c_str(), s_callbacks.user_ctx);
    }
    
    char* json = create_api_response(true, "Command sent", NULL);
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    free_json_string(json);
}

static void handle_api_move(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    float x = NAN, y = NAN, z = NAN, e = NAN, f = 3000;
    
    if (request->hasParam("x")) x = request->getParam("x")->value().toFloat();
    if (request->hasParam("y")) y = request->getParam("y")->value().toFloat();
    if (request->hasParam("z")) z = request->getParam("z")->value().toFloat();
    if (request->hasParam("e")) e = request->getParam("e")->value().toFloat();
    if (request->hasParam("feedrate")) f = request->getParam("feedrate")->value().toFloat();
    
    if (s_callbacks.on_move_axes) {
        s_callbacks.on_move_axes(x, y, z, e, f, s_callbacks.user_ctx);
    }
    
    char* json = create_api_response(true, "Move command sent", NULL);
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    free_json_string(json);
}

static void handle_api_temp(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    float hotend = -1, bed = -1;
    
    if (request->hasParam("hotend")) hotend = request->getParam("hotend")->value().toFloat();
    if (request->hasParam("bed")) bed = request->getParam("bed")->value().toFloat();
    
    if (s_callbacks.on_set_temp) {
        s_callbacks.on_set_temp(hotend, bed, s_callbacks.user_ctx);
    }
    
    char* json = create_api_response(true, "Temperature set", NULL);
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    free_json_string(json);
}

static void handle_api_fan(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    uint8_t fan_id = 0, speed = 0;
    
    if (request->hasParam("fan_id")) fan_id = request->getParam("fan_id")->value().toInt();
    if (request->hasParam("speed")) speed = request->getParam("speed")->value().toInt();
    
    if (s_callbacks.on_set_fan) {
        s_callbacks.on_set_fan(fan_id, speed, s_callbacks.user_ctx);
    }
    
    char* json = create_api_response(true, "Fan speed set", NULL);
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    free_json_string(json);
}

static void handle_api_home(AsyncWebServerRequest* request) {
    if (!check_auth(request)) {
        request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");
        return;
    }
    
    bool x = true, y = true, z = true;
    
    if (request->hasParam("x")) x = request->getParam("x")->value() == "true";
    if (request->hasParam("y")) y = request->getParam("y")->value() == "true";
    if (request->hasParam("z")) z = request->getParam("z")->value() == "true";
    
    if (s_callbacks.on_home_axes) {
        s_callbacks.on_home_axes(x, y, z, s_callbacks.user_ctx);
    }
    
    char* json = create_api_response(true, "Homing started", NULL);
    AsyncWebServerResponse* response = request->beginResponse(200, "application/json", json);
    response->addHeader("Access-Control-Allow-Origin", "*");
    request->send(response);
    free_json_string(json);
}

// Serve embedded web UI
static void handle_root(AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", index_html_gz, index_html_gz_len, "gzip");
}

static void handle_static_file(AsyncWebServerRequest* request) {
    String path = request->url();
    if (path.endsWith(".js")) {
        request->send_P(200, "application/javascript", app_js_gz, app_js_gz_len, "gzip");
    } else if (path.endsWith(".css")) {
        request->send_P(200, "text/css", styles_css_gz, styles_css_gz_len, "gzip");
    } else if (path.endsWith(".ico")) {
        request->send_P(200, "image/x-icon", favicon_ico_gz, favicon_ico_gz_len, "gzip");
    } else {
        request->send(404);
    }
}

// External references for embedded web UI (generated at build time)
extern const uint8_t index_html_gz[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");
extern const uint32_t index_html_gz_len;

extern const uint8_t app_js_gz[] asm("_binary_app_js_gz_start");
extern const uint8_t app_js_gz_end[] asm("_binary_app_js_gz_end");
extern const uint32_t app_js_gz_len;

extern const uint8_t styles_css_gz[] asm("_binary_styles_css_gz_start");
extern const uint8_t styles_css_gz_end[] asm("_binary_styles_css_gz_end");
extern const uint32_t styles_css_gz_len;

extern const uint8_t favicon_ico_gz[] asm("_binary_favicon_ico_gz_start");
extern const uint8_t favicon_ico_gz_end[] asm("_binary_favicon_ico_gz_end");
extern const uint32_t favicon_ico_gz_len;

esp_err_t web_server_init(const web_server_callbacks_t* callbacks, const system_config_t* config) {
    ESP_LOGI(TAG, "Initializing web server...");
    
    if (callbacks) s_callbacks = *callbacks;
    if (config) s_config = *config;
    
    s_clients_mutex = xSemaphoreCreateMutex();
    if (!s_clients_mutex) return ESP_ERR_NO_MEM;
    
    // Create server on port 80
    s_server = new AsyncWebServer(80);
    s_ws = new AsyncWebSocket("/ws");
    
    s_ws->onEvent(on_ws_event);
    s_server->addHandler(s_ws);
    
    // CORS headers
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    
    // API routes
    s_server->on("/api/status", HTTP_GET, handle_api_status);
    s_server->on("/api/printer", HTTP_GET, handle_api_printer);
    s_server->on("/api/files", HTTP_GET, handle_api_files);
    s_server->on("/api/files/upload", HTTP_POST, 
                 [](AsyncWebServerRequest* request){ request->send(200); },
                 handle_api_file_upload);
    s_server->on("/api/files/delete", HTTP_DELETE, handle_api_file_delete);
    s_server->on("/api/files/copy-to-printer", HTTP_POST, handle_api_file_copy);
    s_server->on("/api/print/start", HTTP_POST, handle_api_print_start);
    s_server->on("/api/print/pause", HTTP_POST, handle_api_print_pause);
    s_server->on("/api/print/resume", HTTP_POST, handle_api_print_resume);
    s_server->on("/api/print/cancel", HTTP_POST, handle_api_print_cancel);
    s_server->on("/api/gcode", HTTP_POST, handle_api_gcode);
    s_server->on("/api/move", HTTP_POST, handle_api_move);
    s_server->on("/api/temperature", HTTP_POST, handle_api_temp);
    s_server->on("/api/fan", HTTP_POST, handle_api_fan);
    s_server->on("/api/home", HTTP_POST, handle_api_home);
    
    // Serve web UI
    s_server->on("/", HTTP_GET, handle_root);
    s_server->on("/index.html", HTTP_GET, handle_root);
    s_server->on("/app.js", HTTP_GET, handle_static_file);
    s_server->on("/styles.css", HTTP_GET, handle_static_file);
    s_server->on("/favicon.ico", HTTP_GET, handle_static_file);
    s_server->onNotFound([](AsyncWebServerRequest* request) {
        // SPA fallback - serve index.html for all non-API routes
        if (!request->url().startsWith("/api/") && !request->url().startsWith("/ws")) {
            request->send_P(200, "text/html", index_html_gz, index_html_gz_len, "gzip");
        } else {
            request->send(404);
        }
    });
    
    ESP_LOGI(TAG, "Web server initialized");
    return ESP_OK;
}

esp_err_t web_server_start(void) {
    if (!s_server) return ESP_ERR_INVALID_STATE;
    
    // Start WiFi
    if (s_config.ap_mode) {
        // AP mode
        wifi_config_t ap_config = {0};
        strncpy((char*)ap_config.ap.ssid, s_config.wifi_ap_ssid, sizeof(ap_config.ap.ssid));
        strncpy((char*)ap_config.ap.password, s_config.wifi_ap_password, sizeof(ap_config.ap.password));
        ap_config.ap.ssid_len = strlen(s_config.wifi_ap_ssid);
        ap_config.ap.channel = 1;
        ap_config.ap.max_connection = 4;
        ap_config.ap.authmode = strlen(s_config.wifi_ap_password) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        
        ESP_LOGI(TAG, "AP mode started: %s", s_config.wifi_ap_ssid);
    } else {
        // Station mode
        wifi_config_t sta_config = {0};
        strncpy((char*)sta_config.sta.ssid, s_config.wifi_ssid, sizeof(sta_config.sta.ssid));
        strncpy((char*)sta_config.sta.password, s_config.wifi_password, sizeof(sta_config.sta.password));
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
        
        ESP_LOGI(TAG, "STA mode started, connecting to: %s", s_config.wifi_ssid);
    }
    
    // Start server
    s_server->begin();
    
    ESP_LOGI(TAG, "Web server started on port 80");
    return ESP_OK;
}

void web_server_stop(void) {
    if (s_server) {
        s_server->end();
        delete s_server;
        s_server = NULL;
    }
    if (s_ws) {
        delete s_ws;
        s_ws = NULL;
    }
    if (s_clients_mutex) {
        vSemaphoreDelete(s_clients_mutex);
        s_clients_mutex = NULL;
    }
}

void web_server_broadcast_status(const printer_status_t* status) {
    if (!s_ws || s_ws_client_count == 0) return;
    
    char* json = printer_status_to_json(status);
    if (json) {
        DynamicJsonDocument doc(4096);
        doc["type"] = "status";
        doc["data"] = serialized(json);
        
        String msg;
        serializeJson(doc, msg);
        s_ws->textAll(msg);
        free_json_string(json);
    }
}

void web_server_broadcast_progress(const print_progress_t* progress) {
    if (!s_ws || s_ws_client_count == 0) return;
    
    char* json = print_progress_to_json(progress);
    if (json) {
        DynamicJsonDocument doc(1024);
        doc["type"] = "progress";
        doc["data"] = serialized(json);
        
        String msg;
        serializeJson(doc, msg);
        s_ws->textAll(msg);
        free_json_string(json);
    }
}

void web_server_broadcast_log(const char* level, const char* message) {
    if (!s_ws || s_ws_client_count == 0) return;
    
    DynamicJsonDocument doc(512);
    doc["type"] = "log";
    doc["data"]["level"] = level;
    doc["data"]["message"] = message;
    
    String msg;
    serializeJson(doc, msg);
    s_ws->textAll(msg);
}

void web_server_broadcast_file_list(const file_info_t* files, uint8_t count) {
    if (!s_ws || s_ws_client_count == 0) return;
    
    char* json = file_list_to_json(files, count, true);
    if (json) {
        DynamicJsonDocument doc(8192);
        doc["type"] = "file_list";
        doc["data"] = serialized(json);
        
        String msg;
        serializeJson(doc, msg);
        s_ws->textAll(msg);
        free_json_string(json);
    }
}