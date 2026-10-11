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

        // Marlin commonly prefixes reports with "ok ": parse payloads before
        // handling the acknowledgement so data is not discarded.
        if (line.indexOf("T:") >= 0 || line.indexOf("T0:") >= 0 || line.indexOf("T1:") >= 0) {
            parse_temperature(line);
        }

        if (line.indexOf("B:") >= 0 || line.indexOf("B0:") >= 0) {
            parse_bed_temperature(line);
        }

        // Position response
        if (line.indexOf("X:") >= 0 && line.indexOf("Y:") >= 0 && line.indexOf("Z:") >= 0) {
            parse_position(line);
        }

        // SD printing progress
        if (line.indexOf("SD printing byte") >= 0) {
            parse_sd_progress(line);
            if (callback_) callback_("sd_inserted", "1");
        }

        if (line.indexOf("SD card ok") >= 0 || line.indexOf("SD card inserted") >= 0) {
            if (callback_) callback_("sd_inserted", "1");
        } else if (line.indexOf("No SD card") >= 0 || line.indexOf("SD init fail") >= 0 || line.indexOf("SD card error") >= 0) {
            if (callback_) callback_("sd_inserted", "0");
        }

        // M20 file list
        if (line.startsWith("Begin file list") || line.startsWith("Begin file list:")) {
            file_list_active_ = true;
            if (callback_) callback_("file_list_begin", "");
        }
        if (line.startsWith("End file list") || line.startsWith("End file list:")) {
            if (callback_) callback_("file_list_end", "");
            file_list_active_ = false;
        }

        // M20 file list entries
        if (file_list_active_ && line.length() > 0 && 
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
            
            // Marlin normally returns "NAME.GCO 12345". Some USB/SD
            // combinations concatenate the size or the next entry, so use the
            // first complete G-code extension as the end of the filename.
            // Using the last extension produced values such as
            // "GLOCK4~1.GCO.GCO" and merged two entries into one name.
            String upper = cleanLine;
            upper.toUpperCase();
            int extension_end = -1;
            const char* extensions[] = { ".GCODE", ".GCO", ".G" };
            const int extension_lengths[] = { 6, 4, 2 };
            for (int i = 0; i < 3; i++) {
                int pos = upper.indexOf(extensions[i]);
                while (pos >= 0) {
                    int end = pos + extension_lengths[i];
                    // A short .G suffix is only valid at a token boundary;
                    // otherwise it would match the .G prefix of .GCO.
                    bool boundary = end >= (int)upper.length() ||
                                    upper[end] == ' ' || upper[end] == '\t' ||
                                    upper[end] == '\r' || upper[end] == '\n' ||
                                    upper[end] == '.';
                    if (boundary && (extension_end < 0 || pos < extension_end)) {
                        extension_end = end;
                    }
                    if (extension_end >= 0) break;
                    pos = upper.indexOf(extensions[i], pos + 1);
                }
            }

            bool isFile = extension_end > 1;
            if (isFile) cleanLine = cleanLine.substring(0, extension_end);
            // If a size and another filename were glued to this line, do not
            // expose the ambiguous combined token in the UI.
            if (cleanLine.indexOf(' ') >= 0 || cleanLine.indexOf('\t') >= 0) isFile = false;

            if (isFile && cleanLine.length() > 0) {
                String filename = cleanLine;
                filename.trim();
                if (callback_) callback_("file_list_entry", filename);
            }
        }

        // OK response (after extracting any payload on the same line).
        if (line_trimmed.startsWith("ok") || line_trimmed.startsWith("OK")) {
            if (callback_) callback_("ok", "");
            return;
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
    bool file_list_active_ = false;

    void parse_temperature(const String& line) {
        int value_offset = 2;
        int t_idx = line.indexOf("T:");
        if (t_idx < 0) {
            t_idx = line.indexOf("T0:");
            value_offset = 3;
        }
        if (t_idx < 0) {
            t_idx = line.indexOf("T1:");
            value_offset = 3;
        }
        if (t_idx >= 0) {
            float cur = 0, tgt = 0;
            sscanf(line.substring(t_idx + value_offset).c_str(), "%f/%f", &cur, &tgt);
            if (callback_) callback_("temperature", String(cur) + "," + String(tgt));
        }
    }

    void parse_bed_temperature(const String& line) {
        int value_offset = 2;
        int b_idx = line.indexOf("B:");
        if (b_idx < 0) {
            b_idx = line.indexOf("B0:");
            value_offset = 3;
        }
        if (b_idx >= 0) {
            float cur = 0, tgt = 0;
            sscanf(line.substring(b_idx + value_offset).c_str(), "%f/%f", &cur, &tgt);
            if (callback_) callback_("bed_temperature", String(cur) + "," + String(tgt));
        }
    }

    void parse_position(const String& line) {
        float x=0, y=0, z=0, e=0;
        int x_idx = line.indexOf("X:");
        sscanf(line.substring(x_idx).c_str(), "X:%f Y:%f Z:%f E:%f", &x, &y, &z, &e);
        if (callback_) callback_("position", String(x) + "," + String(y) + "," + String(z) + "," + String(e));
    }

    void parse_sd_progress(const String& line) {
        uint32_t cur = 0, total = 0;
        sscanf(line.c_str(), "SD printing byte %u/%u", &cur, &total);
        if (callback_) callback_("sd_progress", String(cur) + "," + String(total));
    }
};

extern ResponseParser g_response_parser;
