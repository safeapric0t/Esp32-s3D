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
extern volatile uint32_t g_printer_ok_counter;
extern volatile uint32_t g_printer_error_counter;
extern volatile bool g_sd_uploading;
extern String g_last_printer_line;
extern volatile uint32_t g_printer_line_counter;

AsyncWebServer g_server(80);
AsyncWebSocket g_ws("/ws");

extern bool send_gcode(const String& cmd);

// Upload state is shared between the multipart body callback and the final
// request callback.  The old route sent an empty 200 response immediately,
// which made the browser parse an empty body as JSON and report upload failed.
static File g_upload_file;
static String g_upload_filename;
static String g_upload_error;
static bool g_upload_success = false;
static bool g_upload_on_printer_sd = false;
static String g_upload_warning;

static bool is_gcode_filename(const String& filename) {
    String lower = filename;
    lower.toLowerCase();
    return lower.endsWith(".gcode") || lower.endsWith(".g") || lower.endsWith(".gco");
}

static String sanitize_gcode_filename(const String& original) {
    int slash = original.lastIndexOf('/');
    int backslash = original.lastIndexOf('\\');
    int separator = slash > backslash ? slash : backslash;
    String name = separator >= 0 ? original.substring(separator + 1) : original;
    name.trim();

    String safe;
    for (size_t i = 0; i < name.length(); i++) {
        char c = name[i];
        if (isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-') safe += c;
        else if (c == ' ') safe += '_';
    }
    if (safe.length() == 0) safe = "upload.gcode";

    // ESP32 SPIFFS has a short maximum path length.  Keep the extension and
    // truncate only the base name so long browser filenames remain valid
    // G-code files instead of failing fopen().
    const size_t max_filename_length = 24;
    if (safe.length() > max_filename_length) {
        int dot = safe.lastIndexOf('.');
        String extension = dot > 0 ? safe.substring(dot) : ".gcode";
        size_t base_limit = max_filename_length > extension.length()
            ? max_filename_length - extension.length() : 1;
        String base = dot > 0 ? safe.substring(0, dot) : safe;
        if (base.length() > base_limit) base = base.substring(0, base_limit);
        safe = base + extension;
    }
    return safe;
}

static String gcode_storage_path(const String& filename) {
    return "/gcode_" + filename;
}

static String gcode_display_name(const String& path) {
    String name = path;
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    if (name.startsWith("gcode_")) name = name.substring(6);
    return name;
}

static bool printer_sd_contains(const String& filename) {
    for (size_t i = 0; i < printer_status.sd_files.size(); i++) {
        if (printer_status.sd_files[i].equalsIgnoreCase(filename)) return true;
    }
    return false;
}

static String make_printer_sd_filename(const String& source) {
    int dot = source.lastIndexOf('.');
    String base = dot > 0 ? source.substring(0, dot) : source;
    base.toUpperCase();

    String clean;
    for (size_t i = 0; i < base.length(); i++) {
        char c = base[i];
        if (isalnum((unsigned char)c)) clean += c;
    }
    if (clean.length() == 0) clean = "UPLOAD";
    if (clean.length() > 8) clean = clean.substring(0, 8);

    String candidate = clean + ".GCO";
    if (!printer_sd_contains(candidate)) return candidate;

    for (uint8_t suffix = 1; suffix < 100; suffix++) {
        String numbered = clean;
        if (numbered.length() > 6) numbered = numbered.substring(0, 6);
        if (suffix < 10) numbered += "0";
        numbered += String(suffix);
        numbered += ".GCO";
        if (!printer_sd_contains(numbered)) return numbered;
    }
    return "UPLOAD99.GCO";
}

static bool wait_for_printer_ack(uint32_t ok_before, uint32_t error_before, uint32_t timeout_ms) {
    uint32_t started = millis();
    while (millis() - started < timeout_ms) {
        if (g_printer_error_counter != error_before) return false;
        if (g_printer_ok_counter != ok_before) return true;
        delay(10);
    }
    return false;
}

static bool wait_for_printer_text(uint32_t line_before, const char* first, const char* second, const char* third, uint32_t timeout_ms) {
    uint32_t started = millis();
    while (millis() - started < timeout_ms) {
        if (g_printer_line_counter != line_before) {
            String line = g_last_printer_line;
            if ((first && line.indexOf(first) >= 0) ||
                (second && line.indexOf(second) >= 0) ||
                (third && line.indexOf(third) >= 0)) {
                return true;
            }
            line_before = g_printer_line_counter;
        }
        delay(10);
    }
    return false;
}

static void refresh_printer_sd_list() {
    if (!g_cdc_serial || !g_cdc_serial->connected()) return;
    g_cdc_serial->println("M21");
    delay(150);
    g_cdc_serial->println("M20");
    delay(900);
}

static bool copy_spiffs_file_to_printer_sd(const String& source_name, const String& printer_name) {
    if (!g_usb_connected || !g_cdc_serial || !g_cdc_serial->connected() || !printer_status.sd_inserted) {
        return false;
    }

    File source = SPIFFS.open(gcode_storage_path(source_name), "r");
    if (!source) return false;

    g_sd_uploading = true;
    uint32_t line_before = g_printer_line_counter;
    g_cdc_serial->println("M28 " + printer_name);
    // Never send file bytes merely because an unrelated M105/M114 returned
    // "ok". Marlin must explicitly confirm that M28 opened the file.
    if (!wait_for_printer_text(line_before, "Writing to file", "File opened", "SD write", 2500)) {
        source.close();
        g_sd_uploading = false;
        return false;
    }

    uint8_t buffer[256];
    bool write_ok = true;
    while (source.available()) {
        size_t read_count = source.read(buffer, sizeof(buffer));
        if (read_count == 0) {
            write_ok = false;
            break;
        }
        size_t sent = g_cdc_serial->write(buffer, read_count);
        if (sent != read_count) {
            write_ok = false;
            break;
        }
        delay(8);
    }
    source.close();
    if (!write_ok) {
        g_cdc_serial->println("M29");
        g_sd_uploading = false;
        return false;
    }

    delay(250);
    line_before = g_printer_line_counter;
    g_cdc_serial->println("M29");
    bool closed_ok = wait_for_printer_text(line_before, "Done saving file", "File saved", "ok", 4000);
    if (!closed_ok) {
        g_sd_uploading = false;
        return false;
    }

    refresh_printer_sd_list();
    g_sd_uploading = false;
    return true;
}

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

    JsonObject chamber = data.createNestedObject("chamber");
    chamber["current"] = printer_status.chamber.current;
    chamber["target"] = printer_status.chamber.target;
    chamber["power"] = printer_status.chamber.power;
    
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
    
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fullName = file.name();
            String name = gcode_display_name(fullName);
            if (fullName.indexOf("gcode_") >= 0 && is_gcode_filename(name)) {
                JsonObject obj = arr.createNestedObject();
                obj["name"] = name;
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

    JsonObject chamber = data.createNestedObject("chamber");
    chamber["current"] = printer_status.chamber.current;
    chamber["target"] = printer_status.chamber.target;
    chamber["power"] = printer_status.chamber.power;
    
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
    
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fullName = file.name();
            String name = gcode_display_name(fullName);
            if (fullName.indexOf("gcode_") >= 0 && is_gcode_filename(name)) {
                JsonObject obj = arr.createNestedObject();
                obj["name"] = name;
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

void handle_download(AsyncWebServerRequest* request) {
    if (!request->hasParam("name")) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"No file parameter\"}");
        return;
    }
    String name = sanitize_gcode_filename(request->getParam("name")->value());
    String path = gcode_storage_path(name);
    if (!SPIFFS.exists(path)) {
        request->send(404, "application/json", "{\"success\":false,\"error\":\"File not found\"}");
        return;
    }
    request->send(SPIFFS, path, "application/octet-stream", true);
}

void handle_upload(AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
    if (index == 0) {
        g_upload_success = false;
        g_upload_error = "";
        g_upload_filename = "";
        g_upload_on_printer_sd = false;
        g_upload_warning = "";
        if (!is_gcode_filename(filename)) {
            g_upload_error = "Only .gcode, .g or .gco files are allowed";
            return;
        }

        g_upload_filename = sanitize_gcode_filename(filename);
        g_upload_file = SPIFFS.open(gcode_storage_path(g_upload_filename), "w");
        if (!g_upload_file) {
            Serial0.printf("GCODE upload open failed: %s total=%u used=%u free=%u\n",
                           gcode_storage_path(g_upload_filename).c_str(),
                           SPIFFS.totalBytes(), SPIFFS.usedBytes(),
                           SPIFFS.totalBytes() - SPIFFS.usedBytes());
            g_upload_error = "Failed to create file on ESP32";
            return;
        }
    }

    if (len > 0 && g_upload_file && g_upload_error.length() == 0) {
        size_t written = g_upload_file.write(data, len);
        if (written != len) g_upload_error = "Failed while writing file to ESP32";
    }

    if (final) {
        if (g_upload_file) {
            g_upload_file.close();
            g_upload_file = File();
        }
        g_upload_success = g_upload_error.length() == 0;
    }
}

void handle_delete(AsyncWebServerRequest* request) {
    if (!request->hasParam("name")) { request->send(400); return; }
    String name = request->getParam("name")->value();
    bool on_printer_sd = request->hasParam("printer_sd") && request->getParam("printer_sd")->value() == "true";
    
    String path = gcode_storage_path(name);
    bool removed_any = false;
    if (SPIFFS.exists(path)) {
        removed_any = SPIFFS.remove(path) || removed_any;
    }
    if (on_printer_sd) {
        if (g_cdc_serial && g_cdc_serial->connected()) {
            g_cdc_serial->println("M30 " + name);
            delay(300);
            refresh_printer_sd_list();
            removed_any = true;
        }
    }
    if (!removed_any) {
        request->send(404, "application/json", "{\"success\":false,\"error\":\"File not found\"}");
        return;
    }
    broadcast_file_list();
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_copy_to_printer(AsyncWebServerRequest* request) {
    if (!request->hasParam("name")) { request->send(400); return; }
    String name = request->getParam("name")->value();
    
    String path = gcode_storage_path(name);
    File f = SPIFFS.open(path, "r");
    if (!f) { request->send(404); return; }
    
    String printer_name = make_printer_sd_filename(name);
    f.close();
    if (!copy_spiffs_file_to_printer_sd(name, printer_name)) {
        request->send(409, "application/json", "{\"success\":false,\"error\":\"Could not copy file to printer SD card\"}");
        return;
    }
    request->send(200, "application/json", "{\"success\":true,\"filename\":\"" + printer_name + "\",\"on_printer_sd\":true}");
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
    
    if (mode != "stream" && mode != "sd") {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid print mode\"}");
        return;
    }
    if (!g_usb_connected || !g_cdc_serial || !g_cdc_serial->connected()) {
        request->send(503, "application/json", "{\"success\":false,\"error\":\"Printer USB is not connected\"}");
        return;
    }
    if (mode == "sd" && !printer_status.sd_inserted) {
        request->send(409, "application/json", "{\"success\":false,\"error\":\"Printer SD card is not detected\"}");
        return;
    }
    if (mode == "stream" && !SPIFFS.exists(gcode_storage_path(file))) {
        request->send(404, "application/json", "{\"success\":false,\"error\":\"G-code file is not stored on ESP32\"}");
        return;
    }

    extern bool start_stream_print(const String&);
    extern bool start_sd_print(const String&);
    bool started = mode == "stream" ? start_stream_print(file) : start_sd_print(file);
    if (!started) {
        String error = mode == "stream"
            ? "G-code file is not available on ESP32 or a print is already running"
            : "Printer rejected the SD print command";
        request->send(409, "application/json", "{\"success\":false,\"error\":\"" + error + "\"}");
        return;
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
    if (!request->hasParam("command")) { request->send(400); return; }
    String cmd = request->getParam("command")->value();
    if (!send_gcode(cmd)) {
        request->send(503, "application/json", "{\"success\":false,\"error\":\"Printer USB is not connected\"}");
        return;
    }
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
    bool relative = request->hasParam("relative") && request->getParam("relative")->value() == "true";
    if (relative && !send_gcode("G91")) {
        request->send(503, "application/json", "{\"success\":false,\"error\":\"Printer USB is not connected\"}");
        return;
    }
    if (!send_gcode(cmd)) {
        request->send(503, "application/json", "{\"success\":false,\"error\":\"Printer USB is not connected\"}");
        return;
    }
    if (relative) send_gcode("G90");
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_temp(AsyncWebServerRequest* request) {
    bool sent = false;
    if (request->hasParam("hotend")) sent = send_gcode("M104 S" + request->getParam("hotend")->value()) || sent;
    if (request->hasParam("bed")) sent = send_gcode("M140 S" + request->getParam("bed")->value()) || sent;
    if (!sent) {
        request->send(503, "application/json", "{\"success\":false,\"error\":\"Printer USB is not connected\"}");
        return;
    }
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_fan(AsyncWebServerRequest* request) {
    int fan = request->hasParam("fan_id") ? request->getParam("fan_id")->value().toInt() : 0;
    int speed = request->hasParam("speed") ? request->getParam("speed")->value().toInt() : 0;
    if (!g_cdc_serial || !g_cdc_serial->connected()) {
        request->send(503, "application/json", "{\"success\":false,\"error\":\"Printer USB is not connected\"}");
        return;
    }
    g_cdc_serial->println("M106 P" + String(fan) + " S" + String(speed * 255 / 100));
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
    if (!send_gcode(cmd)) {
        request->send(503, "application/json", "{\"success\":false,\"error\":\"Printer USB is not connected\"}");
        return;
    }
    request->send(200, "application/json", "{\"success\":true}");
}

void handle_printer_sd_files(AsyncWebServerRequest* request) {
    if (!g_cdc_serial || !g_cdc_serial->connected()) {
        request->send(503, "application/json", "{\"error\":\"Printer not connected\"}");
        return;
    }
    // M20 is asynchronous over USB.  Wait for the Marlin response before
    // returning; otherwise the first page load races the parser and receives
    // an empty cached list even though the SD card is present.
    g_cdc_serial->println("M21");
    delay(150);
    g_cdc_serial->println("M20");
    delay(1000);
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
    // The UI is updated together with firmware/SPIFFS.  Prevent the browser
    // from keeping an older app.js after a refresh.
    DefaultHeaders::Instance().addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    
    g_ws.onEvent(on_ws_event);
    g_server.addHandler(&g_ws);
    
    // CORS
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    
    // API Routes
    g_server.on("/api/status", HTTP_GET, handle_status);
    // Register the more specific download route before /api/files.  The
    // AsyncWebServer matcher otherwise lets the shorter /api/files route
    // answer the download request with the JSON file list.
    g_server.on("/api/files/download", HTTP_GET, handle_download);
    g_server.on("/api/files", HTTP_GET, handle_files);
    g_server.on("/api/files/upload", HTTP_POST, [](AsyncWebServerRequest* request) {
        if (g_upload_success) {
            // Uploads are staged on SPIFFS by the multipart callback.  Once
            // the complete file is closed, move it to the printer SD card so
            // Marlin executes it locally instead of relying on live USB
            // streaming from the ESP32.
            if (g_usb_connected && g_cdc_serial && g_cdc_serial->connected() && printer_status.sd_inserted) {
                String printer_name = make_printer_sd_filename(g_upload_filename);
                if (copy_spiffs_file_to_printer_sd(g_upload_filename, printer_name)) {
                    SPIFFS.remove(gcode_storage_path(g_upload_filename));
                    g_upload_filename = printer_name;
                    g_upload_on_printer_sd = true;
                } else {
                    g_upload_warning = "File stored on ESP32, but SD transfer failed";
                }
            } else {
                g_upload_warning = "File stored on ESP32; printer SD card is not available";
            }

            broadcast_file_list();
            String message = g_upload_on_printer_sd ? "Stored on printer SD card" : "Stored on ESP32";
            String json = "{\"success\":true,\"filename\":\"" + g_upload_filename + "\",\"message\":\"" + message + "\",\"on_printer_sd\":" + String(g_upload_on_printer_sd ? "true" : "false");
            if (g_upload_warning.length()) json += ",\"warning\":\"" + g_upload_warning + "\"";
            json += "}";
            request->send(200, "application/json", json);
        } else {
            String error = g_upload_error.length() ? g_upload_error : "Upload did not complete";
            request->send(400, "application/json", "{\"success\":false,\"error\":\"" + error + "\"}");
        }
    }, handle_upload);
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
    
    g_server.on("/api/debug/request", HTTP_POST, handle_debug_request);
    
    // Debug: List SPIFFS files
    g_server.on("/api/debug/spiffs", HTTP_GET, [](AsyncWebServerRequest* request) {
        String json = "[";
        File root = SPIFFS.open("/webui");
        if (root) {
            File file = root.openNextFile();
            bool first = true;
            while (file) {
                if (!first) json += ",";
                json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
                first = false;
                file = root.openNextFile();
            }
        }
        json += "]";
        request->send(200, "application/json", json);
    });
    
    // Debug: Test page with inline JS to verify browser JS execution
    g_server.on("/test", HTTP_GET, [](AsyncWebServerRequest* request) {
        String html = "<!DOCTYPE html><html><head><title>Test</title></head><body>"
            "<h1>ESP32-S3 Test Page</h1>"
            "<div id='status'>Loading...</div>"
            "<script>"
            "document.getElementById('status').innerText = 'JS Working! Time: ' + new Date().toISOString();"
            "console.log('Inline JS executed');"
            "fetch('/api/status').then(r=>r.json()).then(d=>{"
            "  document.getElementById('status').innerText += ' | API: ' + d.data.state;"
            "}).catch(e=>{"
            "  document.getElementById('status').innerText += ' | API Error: ' + e;"
            "});"
            "</script>"
            "</body></html>";
        request->send(200, "text/html", html);
    });
    
    // Serve gzipped static files with proper Content-Encoding
    g_server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest* request){
        AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/webui/app.js.gz", "application/javascript");
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });
    g_server.on("/styles.css", HTTP_GET, [](AsyncWebServerRequest* request){
        AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/webui/styles.css.gz", "text/css");
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });
    g_server.on("/vendor.js", HTTP_GET, [](AsyncWebServerRequest* request){
        AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/webui/vendor.js.gz", "application/javascript");
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });
    g_server.on("/charts.js", HTTP_GET, [](AsyncWebServerRequest* request){
        AsyncWebServerResponse* response = request->beginResponse(SPIFFS, "/webui/charts.js.gz", "application/javascript");
        response->addHeader("Content-Encoding", "gzip");
        request->send(response);
    });
    
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
