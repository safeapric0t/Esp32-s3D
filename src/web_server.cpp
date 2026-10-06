#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <vector>
#include "EspUsbHost.h"
#include "web_server.h"

extern EspUsbHost* g_usb_host;
extern EspUsbHostCdcSerial* g_cdc_serial;
extern bool g_usb_connected;
extern bool g_streaming;
extern bool g_paused;
extern File g_print_file;
extern String g_usb_rx_buffer;
extern uint32_t g_file_size;
extern uint32_t g_file_pos;
extern uint32_t g_lines_sent;
extern String g_current_file;

AsyncWebServer g_server(80);
AsyncWebSocket g_ws("/ws");

extern bool send_gcode(const String& cmd);

void broadcast_status(AsyncWebSocketClient* client) {
    DynamicJsonDocument doc(4096);
    doc["type"] = "status";
    JsonObject data = doc.createNestedObject("data");
    
    data["state"] = printer_status.state;
    data["firmware_version"] = printer_status.firmware_version;
    data["printer_model"] = printer_status.printer_model;
    data["usb_connected"] = printer_status.usb_connected;
    data["sd_inserted"] = printer_status.sd_inserted;
    data["ws_clients"] = printer_status.ws_clients;
    
    JsonObject hotend = data.createNestedObject("hotend");
    hotend["current"] = printer_status.hotend.current;
    hotend["target"] = printer_status.hotend.target;
    hotend["power"] = printer_status.hotend.power;
    
    JsonObject bed = data.createNestedObject("bed");
    bed["current"] = printer_status.bed.current;
    bed["target"] = printer_status.bed.target;
    bed["power"] = printer_status.bed.power;
    
    JsonObject pos = data.createNestedObject("position");
    pos["x"] = printer_status.position.x;
    pos["y"] = printer_status.position.y;
    pos["z"] = printer_status.position.z;
    pos["e"] = printer_status.position.e;
    
    JsonObject prog = data.createNestedObject("progress");
    prog["filename"] = printer_status.progress.filename;
    prog["file_size"] = printer_status.progress.file_size;
    prog["file_pos"] = printer_status.progress.file_pos;
    prog["progress"] = printer_status.progress.progress;
    prog["layer_current"] = printer_status.progress.layer_current;
    prog["layer_total"] = printer_status.progress.layer_total;
    prog["current_z"] = printer_status.progress.current_z;
    prog["time_elapsed"] = printer_status.progress.time_elapsed;
    prog["time_remaining"] = printer_status.progress.time_remaining;
    
    JsonArray fans = data.createNestedArray("fans");
    for (size_t i = 0; i < printer_status.fans.size(); i++) {
        JsonObject fan = fans.createNestedObject();
        fan["id"] = printer_status.fans[i].id;
        fan["speed"] = printer_status.fans[i].speed;
    }
    
    String msg;
    serializeJson(doc, msg);
    if (client) {
        client->text(msg);
    } else {
        g_ws.textAll(msg);
    }
}

void broadcast_progress() {
    DynamicJsonDocument doc(1024);
    doc["type"] = "progress";
    JsonObject data = doc.createNestedObject("data");
    data["filename"] = printer_status.progress.filename;
    data["file_size"] = printer_status.progress.file_size;
    data["file_pos"] = printer_status.progress.file_pos;
    data["progress"] = printer_status.progress.progress;
    data["layer_current"] = printer_status.progress.layer_current;
    data["layer_total"] = printer_status.progress.layer_total;
    data["current_z"] = printer_status.progress.current_z;
    data["time_elapsed"] = printer_status.progress.time_elapsed;
    data["time_remaining"] = printer_status.progress.time_remaining;
    
    String msg;
    serializeJson(doc, msg);
    g_ws.textAll(msg);
}

void broadcast_log(const char* level, const char* message) {
    DynamicJsonDocument doc(512);
    doc["type"] = "log";
    JsonObject data = doc.createNestedObject("data");
    data["level"] = level;
    data["message"] = message;
    
    String msg;
    serializeJson(doc, msg);
    g_ws.textAll(msg);
}

void broadcast_state_change(int state, const char* state_str) {
    DynamicJsonDocument doc(256);
    doc["type"] = "state_change";
    JsonObject data = doc.createNestedObject("data");
    data["state"] = state;
    data["state_str"] = state_str;
    
    String msg;
    serializeJson(doc, msg);
    g_ws.textAll(msg);
}

void broadcast_file_list() {
    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.createNestedArray("data");
    
    File root = SPIFFS.open("/gcode");
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String name = file.name();
            if (name.endsWith(".gcode") || name.endsWith(".g") || name.endsWith(".gco")) {
                JsonObject obj = arr.createNestedObject();
                obj["name"] = name.substring(name.lastIndexOf('/') + 1);
                obj["size"] = file.size();
                obj["date"] = file.getLastWrite();
                obj["is_dir"] = false;
                obj["on_printer_sd"] = false;
            }
        }
        file = root.openNextFile();
    }
    
    for (size_t i = 0; i < printer_status.sd_files.size(); i++) {
        JsonObject obj = arr.createNestedObject();
        obj["name"] = printer_status.sd_files[i];
        obj["size"] = 0;
        obj["date"] = 0;
        obj["is_dir"] = false;
        obj["on_printer_sd"] = true;
    }
    
    String json;
    serializeJson(arr, json);
    
    DynamicJsonDocument wrap(8192);
    wrap["type"] = "file_list";
    wrap["data"] = json;
    
    String msg;
    serializeJson(wrap, msg);
    g_ws.textAll(msg);
}

void on_ws_event(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    if (type == WS_EVT_CONNECT) {
        printer_status.ws_clients++;
        Serial0.printf("WS client #%u connected\n", client->id());
        broadcast_status(client);
    } else if (type == WS_EVT_DISCONNECT) {
        printer_status.ws_clients--;
        Serial0.printf("WS client #%u disconnected\n", client->id());
    } else if (type == WS_EVT_DATA && len > 0) {
        String msg = String((char*)data).substring(0, len);
        DynamicJsonDocument doc(512);
        DeserializationError err = deserializeJson(doc, msg);
        if (!err) {
            const char* type = doc["type"];
            if (type) {
                if (strcmp(type, "gcode") == 0) {
                    const char* cmd = doc["command"];
                    if (cmd && g_cdc_serial && g_cdc_serial->connected()) {
                        g_cdc_serial->println(cmd);
                    }
                } else if (strcmp(type, "move") == 0) {
                    float x = doc["x"] | NAN;
                    float y = doc["y"] | NAN;
                    float z = doc["z"] | NAN;
                    float e = doc["e"] | NAN;
                    float f = doc["feedrate"] | 3000;
                    String cmd = "G1";
                    if (!isnan(x)) cmd += " X" + String(x, 2);
                    if (!isnan(y)) cmd += " Y" + String(y, 2);
                    if (!isnan(z)) cmd += " Z" + String(z, 2);
                    if (!isnan(e)) cmd += " E" + String(e, 2);
                    cmd += " F" + String((int)f);
                    send_gcode(cmd);
                } else if (strcmp(type, "temp") == 0) {
                    float hotend = doc["hotend"] | -1;
                    float bed = doc["bed"] | -1;
                    if (hotend >= 0) send_gcode("M104 S" + String(hotend));
                    if (bed >= 0) send_gcode("M140 S" + String(bed));
                } else if (strcmp(type, "fan") == 0) {
                    int fan = doc["fan_id"] | 0;
                    int speed = doc["speed"] | 0;
                    if (g_cdc_serial && g_cdc_serial->connected()) {
                        g_cdc_serial->println("M106 P" + String(fan) + " S" + String(speed * 255 / 100));
                    }
                } else if (strcmp(type, "home") == 0) {
                    bool x = doc["x"] | false;
                    bool y = doc["y"] | false;
                    bool z = doc["z"] | false;
                    String cmd = "G28";
                    if (x) cmd += " X";
                    if (y) cmd += " Y";
                    if (z) cmd += " Z";
                    send_gcode(cmd);
}
            }
        }
    }
    }

void handle_status(AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(4096);
    doc["type"] = "status";
    JsonObject data = doc.createNestedObject("data");
    
    data["state"] = printer_status.state;
    data["firmware_version"] = printer_status.firmware_version;
    data["printer_model"] = printer_status.printer_model;
    data["usb_connected"] = printer_status.usb_connected;
    data["sd_inserted"] = printer_status.sd_inserted;
    data["ws_clients"] = printer_status.ws_clients;
    
    JsonObject hotend = data.createNestedObject("hotend");
    hotend["current"] = printer_status.hotend.current;
    hotend["target"] = printer_status.hotend.target;
    hotend["power"] = printer_status.hotend.power;
    
    JsonObject bed = data.createNestedObject("bed");
    bed["current"] = printer_status.bed.current;
    bed["target"] = printer_status.bed.target;
    bed["power"] = printer_status.bed.power;
    
    JsonObject pos = data.createNestedObject("position");
    pos["x"] = printer_status.position.x;
    pos["y"] = printer_status.position.y;
    pos["z"] = printer_status.position.z;
    pos["e"] = printer_status.position.e;
    
    JsonObject prog = data.createNestedObject("progress");
    prog["filename"] = printer_status.progress.filename;
    prog["file_size"] = printer_status.progress.file_size;
    prog["file_pos"] = printer_status.progress.file_pos;
    prog["progress"] = printer_status.progress.progress;
    prog["layer_current"] = printer_status.progress.layer_current;
    prog["layer_total"] = printer_status.progress.layer_total;
    prog["current_z"] = printer_status.progress.current_z;
    prog["time_elapsed"] = printer_status.progress.time_elapsed;
    prog["time_remaining"] = printer_status.progress.time_remaining;
    
    JsonArray fans = data.createNestedArray("fans");
    for (size_t i = 0; i < printer_status.fans.size(); i++) {
        JsonObject fan = fans.createNestedObject();
        fan["id"] = printer_status.fans[i].id;
        fan["speed"] = printer_status.fans[i].speed;
    }
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void handle_files(AsyncWebServerRequest* request) {
    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();
    
    File root = SPIFFS.open("/gcode");
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String name = file.name();
            if (name.endsWith(".gcode") || name.endsWith(".g") || name.endsWith(".gco")) {
                JsonObject obj = arr.createNestedObject();
                obj["name"] = name.substring(name.lastIndexOf('/') + 1);
                obj["size"] = file.size();
                obj["date"] = file.getLastWrite();
                obj["is_dir"] = false;
                obj["on_printer_sd"] = false;
            }
        }
        file = root.openNextFile();
    }
    
    for (size_t i = 0; i < printer_status.sd_files.size(); i++) {
        JsonObject obj = arr.createNestedObject();
        obj["name"] = printer_status.sd_files[i];
        obj["size"] = 0;
        obj["date"] = 0;
        obj["is_dir"] = false;
        obj["on_printer_sd"] = true;
    }
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
}

void handle_upload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
    static bool to_printer_sd = false;
    static File upload_file;
    static String safeFilename;
    static String spiffsPath;
    
    if (index == 0) {
        if (!filename.endsWith(".gcode") && !filename.endsWith(".g") && !filename.endsWith(".gco")) {
            request->send(400, "application/json", "{\"error\":\"Only .gcode files allowed\"}");
            return;
        }
        
        safeFilename = filename;
        int dotIdx = safeFilename.lastIndexOf('.');
        String base = (dotIdx > 0) ? safeFilename.substring(0, dotIdx) : safeFilename;
        String ext = (dotIdx > 0) ? safeFilename.substring(dotIdx) : "";
        if (base.length() > 8) base = base.substring(0, 8);
        if (ext.length() > 4) ext = ext.substring(0, 4);
        safeFilename = base + ext;
        
        spiffsPath = "/gcode/" + safeFilename;
        
        upload_file = SPIFFS.open(spiffsPath, "w");
        if (!upload_file) {
            request->send(500, "application/json", "{\"error\":\"Failed to create file on ESP32\"}");
            return;
        }
    }
    
    if (len > 0) {
        if (upload_file) {
            upload_file.write(data, len);
        }
    }
    
    if (final) {
        if (upload_file) {
            upload_file.close();
            upload_file = File();
        }
        
        if (g_cdc_serial && g_cdc_serial->connected()) {
            Serial0.printf("Copy to printer SD: M28 %s\n", safeFilename.c_str());
            g_cdc_serial->println("M28 " + safeFilename);
            delay(1000);
            
            File spiffsFile = SPIFFS.open(spiffsPath, "r");
            if (spiffsFile) {
                uint8_t buf[512];
                while (spiffsFile.available()) {
                    size_t len = spiffsFile.read(buf, sizeof(buf));
                    if (len > 0) {
                        g_cdc_serial->write(buf, len);
                        delay(10);
                    }
                }
                spiffsFile.close();
            }
            
            delay(500);
            Serial0.println("Upload: M29");
            g_cdc_serial->println("M29");
            delay(1000);
            
            g_cdc_serial->println("M20");
            
            request->send(200, "application/json", "{\"success\":true,\"filename\":\"" + safeFilename + "\",\"message\":\"Uploaded to ESP32, copying to printer SD\"}");
        } else {
            request->send(200, "application/json", "{\"success\":true,\"filename\":\"" + safeFilename + "\",\"message\":\"Stored on ESP32 (printer not connected)\"}");
        }
    }
}

void handle_delete(AsyncWebServerRequest* request) {
    if (!request->hasParam("name")) { request->send(400); return; }
    String name = request->getParam("name")->value();
    bool on_printer_sd = request->hasParam("printer_sd") && request->getParam("printer_sd")->value() == "true";
    
    String path = "/gcode/" + name;
    if (SPIFFS.exists(path)) {
        SPIFFS.remove(path);
    }
    if (on_printer_sd) {
        if (g_cdc_serial && g_cdc_serial->connected()) {
            g_cdc_serial->println("M30 " + name);
        }
    }
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_copy_to_printer(AsyncWebServerRequest* request) {
    if (!request->hasParam("name")) { request->send(400); return; }
    String name = request->getParam("name")->value();
    
    String path = "/gcode/" + name;
    File f = SPIFFS.open(path, "r");
    if (!f) { request->send(404); return; }
    
    if (g_cdc_serial && g_cdc_serial->connected()) {
        g_cdc_serial->println("M28 " + name);
        delay(100);
        
        uint8_t buf[512];
        while (f.available()) {
            size_t len = f.read(buf, sizeof(buf));
            if (len > 0) g_cdc_serial->write(buf, len);
            delay(5);
        }
        f.close();
        
        delay(100);
        send_gcode("M29");
    }
    f.close();
    
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_print_start(AsyncWebServerRequest* request) {
    int params = request->params();
    String file = "";
    String mode = "stream";
    
    for (int i = 0; i < params; i++) {
        const AsyncWebParameter* p = request->getParam(i);
        if (p->name() == "file") {
            file = p->value();
        } else if (p->name() == "mode") {
            mode = p->value();
        }
    }
    
    if (file.length() == 0) { 
        request->send(400, "application/json", "{\"error\":\"No file parameter\"}");
        return; 
    }
    
    extern void start_stream_print(const String&);
    extern void start_sd_print(const String&);
    
    if (mode == "stream") {
        start_stream_print(file);
    } else {
        start_sd_print(file);
    }
    request->send(200, "application/json", "{\"success\":true,\"file\":\"" + file + "\",\"mode\":\"" + mode + "\"}");
}

void handle_pause(AsyncWebServerRequest* request) {
    extern void pause_print();
    pause_print();
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_resume(AsyncWebServerRequest* request) {
    extern void resume_print();
    resume_print();
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_cancel(AsyncWebServerRequest* request) {
    extern void cancel_print();
    cancel_print();
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_gcode(AsyncWebServerRequest* request) {
    if (!request->hasParam("command", true)) { request->send(400); return; }
    String cmd = request->getParam("command", true)->value();
    send_gcode(cmd);
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_move(AsyncWebServerRequest* request) {
    float x = NAN, y = NAN, z = NAN, e = NAN, f = 3000;
    if (request->hasParam("x")) x = request->getParam("x")->value().toFloat();
    if (request->hasParam("y")) y = request->getParam("y")->value().toFloat();
    if (request->hasParam("z")) z = request->getParam("z")->value().toFloat();
    if (request->hasParam("e")) e = request->getParam("e")->value().toFloat();
    if (request->hasParam("feedrate")) f = request->getParam("feedrate")->value().toFloat();
    
    String cmd = "G1";
    if (!isnan(x)) cmd += " X" + String(x, 2);
    if (!isnan(y)) cmd += " Y" + String(y, 2);
    if (!isnan(z)) cmd += " Z" + String(z, 2);
    if (!isnan(e)) cmd += " E" + String(e, 2);
    cmd += " F" + String((int)f);
    send_gcode(cmd);
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_temp(AsyncWebServerRequest* request) {
    if (request->hasParam("hotend")) send_gcode("M104 S" + request->getParam("hotend")->value());
    if (request->hasParam("bed")) send_gcode("M140 S" + request->getParam("bed")->value());
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_fan(AsyncWebServerRequest* request) {
    int fan = request->hasParam("fan_id") ? request->getParam("fan_id")->value().toInt() : 0;
    int speed = request->hasParam("speed") ? request->getParam("speed")->value().toInt() : 0;
    if (g_cdc_serial && g_cdc_serial->connected()) {
        g_cdc_serial->println("M106 P" + String(fan) + " S" + String(speed * 255 / 100));
    }
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_home(AsyncWebServerRequest* request) {
    bool x = !request->hasParam("x") || request->getParam("x")->value() == "true";
    bool y = !request->hasParam("y") || request->getParam("y")->value() == "true";
    bool z = !request->hasParam("z") || request->getParam("z")->value() == "true";
    String cmd = "G28";
    if (x) cmd += " X";
    if (y) cmd += " Y";
    if (z) cmd += " Z";
    send_gcode(cmd);
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_printer_sd_files(AsyncWebServerRequest* request) {
    if (!g_cdc_serial || !g_cdc_serial->connected()) {
        request->send(503, "application/json", "{\"error\":\"Printer not connected\"}");
        return;
    }
    // Request file list from printer
    g_cdc_serial->println("M20");
    // Build JSON response from cached printer_status.sd_files
    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();
    for (size_t i = 0; i < printer_status.sd_files.size(); i++) {
        JsonObject obj = arr.createNestedObject();
        obj["name"] = printer_status.sd_files[i];
        obj["size"] = 0;
        obj["date"] = 0;
        obj["is_dir"] = false;
        obj["on_printer_sd"] = true;
    }
    String json;
    serializeJson(arr, json);
    request->send(200, "application/json", json);
}

void handle_debug_request(AsyncWebServerRequest* request) {
    int params = request->params();
    String json = "{\"params\":" + String(request->params()) + ",\"names\":[";
    for (int i = 0; i < params; i++) {
        const AsyncWebParameter* p = request->getParam(i);
        if (i > 0) json += ",";
        json += "{\"name\":\"" + p->name() + "\",\"value\":\"" + p->value() + "\"}";
    }
    json += "]}";
    request->send(200, "application/json", json);
}

void web_server_init() {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    
    g_ws.onEvent(on_ws_event);
    g_server.addHandler(&g_ws);
    
    // CORS
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    
    // API Routes
    g_server.on("/api/status", HTTP_GET, handle_status);
    g_server.on("/api/files", HTTP_GET, handle_files);
    g_server.on("/api/files/upload", HTTP_POST, [](AsyncWebServerRequest* r){r->send(200);}, handle_upload);
    g_server.on("/api/files/delete", HTTP_DELETE, handle_delete);
    g_server.on("/api/files/copy-to-printer", HTTP_POST, handle_copy_to_printer);
    g_server.on("/api/print/start", HTTP_POST, handle_print_start);
    g_server.on("/api/print/pause", HTTP_POST, handle_pause);
    g_server.on("/api/print/resume", HTTP_POST, handle_resume);
    g_server.on("/api/print/cancel", HTTP_POST, handle_cancel);
    g_server.on("/api/gcode", HTTP_POST, handle_gcode);
    g_server.on("/api/move", HTTP_POST, handle_move);
    g_server.on("/api/temperature", HTTP_POST, handle_temp);
    g_server.on("/api/fan", HTTP_POST, handle_fan);
    g_server.on("/api/home", HTTP_POST, handle_home);
    g_server.on("/api/files/printer-sd", HTTP_GET, handle_printer_sd_files);
    g_server.on("/api/debug/request", HTTP_POST, handle_debug_request);
    
    // Serve Web UI from SPIFFS
    g_server.serveStatic("/", SPIFFS, "/webui/").setDefaultFile("index.html");
    g_server.onNotFound([](AsyncWebServerRequest* request){
        if (!request->url().startsWith("/api/") && !request->url().startsWith("/ws")) {
            request->send(SPIFFS, "/webui/index.html", "text/html");
        } else {
            request->send(404);
        }
    });
    
    g_server.begin();
}