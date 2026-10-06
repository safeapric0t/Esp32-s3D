#pragma once

#include <Arduino.h>
#include <functional>

class ResponseParser {
public:
    using ResponseCallback = std::function<void(const String& type, const String& data)>;

    ResponseParser() {}

    void set_callback(std::function<void(const String&, const String&)> callback) {
        callback_ = callback;
    }

    void parse_line(const String& line) {
        String line_trimmed = line;
        line_trimmed.trim();
        
        if (line_trimmed.length() == 0) return;

        // OK response
        if (line_trimmed.startsWith("ok") || line_trimmed.startsWith("OK")) {
            if (callback_) callback_("ok", "");
            return;
        }

        // Error responses
        if (line_trimmed.startsWith("Error:") || line_trimmed.startsWith("error:") || line_trimmed.startsWith("!!")) {
            String error = line_trimmed;
            if (line_trimmed.startsWith("Error:")) error = line_trimmed.substring(6);
            else if (line_trimmed.startsWith("error:")) error = line_trimmed.substring(6);
            else if (line_trimmed.startsWith("!!")) error = line_trimmed.substring(2);
            error.trim();
            if (callback_) callback_("error", error);
            return;
        }

        // Temperature responses
        if (line.startsWith("T:") || line.startsWith("T0:") || line.startsWith("T1:")) {
            parse_temperature(line);
            return;
        }

        if (line.startsWith("B:") || line.startsWith("B0:")) {
            parse_bed_temperature(line);
            return;
        }

        // Position response
        if (line.startsWith("X:") && line.indexOf("Y:") > 0 && line.indexOf("Z:") > 0) {
            parse_position(line);
            return;
        }

        // SD printing progress
        if (line.indexOf("SD printing byte") >= 0) {
            parse_sd_progress(line);
            return;
        }

        // M20 file list
        if (line.startsWith("Begin file list") || line.startsWith("Begin file list:")) {
            if (callback_) callback_("file_list_begin", "");
            return;
        }
        if (line.startsWith("End file list") || line.startsWith("End file list:")) {
            if (callback_) callback_("file_list_end", "");
            return;
        }

        // M20 file list entries
        if (line.length() > 0 && 
            !line.startsWith("ok") && 
            !line.startsWith("Error") && 
            !line.startsWith("Error:") && 
            !line.startsWith("T:") && 
            !line.startsWith("T0:") && 
            !line.startsWith("@:") && 
            !line.startsWith("B:") && 
            !line.startsWith("B@:") && 
            !line.startsWith("X:") && 
            !line.startsWith("SD printing") &&
            !line.startsWith("Begin file list") && 
            !line.startsWith("End file list") &&
            !line.startsWith("echo:") &&
            !line.startsWith("Not ") &&  // "Not an SD card" etc
            !line.startsWith("Card ")) {  // "Card inserted" etc
        
            // Potential file entry from M20 - skip echo lines and status messages
            String cleanLine = line;
            cleanLine.trim();
            
            // Check if it looks like a filename (has extension or is a simple name)
            bool isFile = false;
            if (cleanLine.endsWith(".gcode") || cleanLine.endsWith(".g") || cleanLine.endsWith(".gco") ||
                cleanLine.endsWith(".GCODE") || cleanLine.endsWith(".G") || cleanLine.endsWith(".GCO") ||
                cleanLine.endsWith(".gco") || cleanLine.endsWith(".GCO")) {
                isFile = true;
            }
            
            // Also accept lines that look like "filename.gcode 12345" (name + size)
            if (!isFile) {
                int spaceIdx = cleanLine.indexOf(' ');
                if (spaceIdx > 0) {
                    String namePart = cleanLine.substring(0, spaceIdx);
                    if (namePart.endsWith(".gcode") || namePart.endsWith(".g") || namePart.endsWith(".gco") ||
                        namePart.endsWith(".GCODE") || namePart.endsWith(".G") || namePart.endsWith(".GCO")) {
                        cleanLine = namePart;
                        isFile = true;
                    }
                }
                
                if (!isFile && cleanLine.length() > 3) {
                    bool validChars = true;
                    for (size_t i = 0; i < cleanLine.length(); i++) {
                        char c = cleanLine[i];
                        if (!isalnum(c) && c != '.' && c != '_' && c != '-' && c != '~') {
                            validChars = false;
                            break;
                        }
                    }
                    if (validChars && cleanLine.indexOf('.') > 0) {
                        isFile = true;
                    }
                }
                
                if (isFile && cleanLine.length() > 0) {
                    String filename = cleanLine;
                    filename.trim();
                    if (callback_) callback_("file_list_entry", filename);
                }
            }
        }
        
        // Power reports
        if (line.startsWith("@:")) {
            if (callback_) callback_("power", line.substring(2));
            return;
        }

        if (line.startsWith("B@:")) {
            if (callback_) callback_("bed_power", line.substring(3));
            return;
        }
    }

private:
    std::function<void(const String&, const String&)> callback_;

    void parse_temperature(const String& line) {
        int t_idx = line.indexOf("T:");
        if (t_idx < 0) t_idx = line.indexOf("T0:");
        if (t_idx >= 0) {
            float cur = 0, tgt = 0;
            sscanf(line.substring(t_idx + 2).c_str(), "%f/%f", &cur, &tgt);
            if (callback_) callback_("temperature", String(cur) + "," + String(tgt));
        }
    }

    void parse_bed_temperature(const String& line) {
        int b_idx = line.indexOf("B:");
        if (b_idx >= 0) {
            float cur = 0, tgt = 0;
            sscanf(line.substring(b_idx + 2).c_str(), "%f/%f", &cur, &tgt);
            if (callback_) callback_("bed_temperature", String(cur) + "," + String(tgt));
        }
    }

    void parse_position(const String& line) {
        float x=0, y=0, z=0, e=0;
        sscanf(line.c_str(), "X:%f Y:%f Z:%f E:%f", &x, &y, &z, &e);
        if (callback_) callback_("position", String(x) + "," + String(y) + "," + String(z) + "," + String(e));
    }

    void parse_sd_progress(const String& line) {
        uint32_t cur = 0, total = 0;
        sscanf(line.c_str(), "SD printing byte %u/%u", &cur, &total);
        if (callback_) callback_("sd_progress", String(cur) + "," + String(total));
    }
};

extern ResponseParser g_response_parser;