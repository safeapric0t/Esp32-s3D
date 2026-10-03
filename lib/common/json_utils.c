#include "json_utils.h"
#include <stdlib.h>
#include <string.h>

char* printer_status_to_json(const printer_status_t* status) {
    if (!status) return NULL;
    
    DynamicJsonDocument doc(4096);
    
    doc["state"] = status->state;
    doc["firmware_version"] = status->firmware_version;
    doc["printer_model"] = status->printer_model;
    doc["usb_connected"] = status->usb_connected;
    doc["sd_inserted"] = status->sd_inserted;
    doc["ws_clients"] = status->ws_clients;
    
    JsonObject hotend = doc.createNestedObject("hotend");
    hotend["current"] = status->hotend.current;
    hotend["target"] = status->hotend.target;
    hotend["power"] = status->hotend.power;
    
    JsonObject bed = doc.createNestedObject("bed");
    bed["current"] = status->bed.current;
    bed["target"] = status->bed.target;
    bed["power"] = status->bed.power;
    
    JsonObject pos = doc.createNestedObject("position");
    pos["x"] = status->position.x;
    pos["y"] = status->position.y;
    pos["z"] = status->position.z;
    pos["e"] = status->position.e;
    
    JsonArray fans = doc.createNestedArray("fans");
    for (int i = 0; i < status->fan_count; i++) {
        JsonObject fan = fans.createNestedObject();
        fan["id"] = status->fans[i].id;
        fan["speed"] = status->fans[i].speed;
        fan["enabled"] = status->fans[i].enabled;
    }
    
    JsonObject progress = doc.createNestedObject("progress");
    progress["filename"] = status->progress.filename;
    progress["file_size"] = status->progress.file_size;
    progress["file_pos"] = status->progress.file_pos;
    progress["percent"] = status->progress.progress;
    progress["layer_current"] = status->progress.layer_current;
    progress["layer_total"] = status->progress.layer_total;
    progress["current_z"] = status->progress.current_z;
    progress["time_elapsed"] = status->progress.time_elapsed;
    progress["time_remaining"] = status->progress.time_remaining;
    
    char* output = (char*)malloc(4096);
    if (output) {
        serializeJson(doc, output, 4096);
    }
    return output;
}

char* file_list_to_json(const file_info_t* files, uint8_t count, bool include_printer_sd) {
    DynamicJsonDocument doc(8192);
    JsonArray arr = doc.to<JsonArray>();
    
    for (int i = 0; i < count; i++) {
        if (!include_printer_sd && files[i].on_printer_sd) continue;
        
        JsonObject obj = arr.createNestedObject();
        obj["name"] = files[i].name;
        obj["size"] = files[i].size;
        obj["date"] = files[i].date;
        obj["is_dir"] = files[i].is_dir;
        obj["on_printer_sd"] = files[i].on_printer_sd;
    }
    
    char* output = (char*)malloc(8192);
    if (output) {
        serializeJson(doc, output, 8192);
    }
    return output;
}

char* print_progress_to_json(const print_progress_t* progress) {
    if (!progress) return NULL;
    
    DynamicJsonDocument doc(1024);
    doc["filename"] = progress->filename;
    doc["file_size"] = progress->file_size;
    doc["file_pos"] = progress->file_pos;
    doc["percent"] = progress->progress;
    doc["layer_current"] = progress->layer_current;
    doc["layer_total"] = progress->layer_total;
    doc["current_z"] = progress->current_z;
    doc["time_elapsed"] = progress->time_elapsed;
    doc["time_remaining"] = progress->time_remaining;
    doc["bytes_printed"] = progress->bytes_printed;
    
    char* output = (char*)malloc(1024);
    if (output) {
        serializeJson(doc, output, 1024);
    }
    return output;
}

bool parse_gcode_command(const char* json, char* command, size_t max_len) {
    if (!json || !command || max_len == 0) return false;
    
    DynamicJsonDocument doc(512);
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;
    
    const char* cmd = doc["command"] | doc["gcode"];
    if (!cmd) return false;
    
    strncpy(command, cmd, max_len - 1);
    command[max_len - 1] = '\0';
    return true;
}

char* create_api_response(bool success, const char* message, const char* data) {
    DynamicJsonDocument doc(1024);
    doc["success"] = success;
    if (message) doc["message"] = message;
    if (data) {
        // Try to parse data as JSON and embed it
        DynamicJsonDocument dataDoc(512);
        DeserializationError err = deserializeJson(dataDoc, data);
        if (!err) {
            doc["data"] = dataDoc;
        } else {
            doc["data"] = data;
        }
    }
    
    char* output = (char*)malloc(1024);
    if (output) {
        serializeJson(doc, output, 1024);
    }
    return output;
}

void free_json_string(char* str) {
    if (str) free(str);
}