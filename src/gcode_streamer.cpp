#include "gcode_streamer.h"
#include <EspUsbHost.h>

extern EspUsbHostCdcSerial* g_cdc_serial;

GcodeStreamer g_gcode_streamer;

void GcodeStreamer::begin(File& file, const StreamConfig& config) {
    if (state != StreamState::IDLE) return;
    
    file_handle = file;
    config_ = config;
    state = StreamState::PREPARING;
    
    file_handle.seek(0);
    total_lines = 0;
    while (file_handle.available()) {
        String line = file_handle.readStringUntil('\n');
        if (line.length() > 0 && !line.startsWith(";")) {
            total_lines++;
        }
    }
    file_handle.seek(0);
    
    stats = StreamStats();
    stats.total_lines = count_total_lines();
    stats.start_time = millis();
    
    file_handle.seek(0);
    state = StreamState::STREAMING;
    
    Serial0.printf("GcodeStreamer: Started streaming, %u lines\n", stats.total_lines);
}

void GcodeStreamer::set_callbacks(std::function<void(const StreamStats&)> progress_cb,
                                  std::function<void(bool, const String&)> completion_cb) {
    progress_cb_ = progress_cb;
    completion_cb_ = completion_cb;
}

void GcodeStreamer::handle_streaming() {
    if (state != StreamState::STREAMING) return;
    
    check_timeouts();
    
    while (in_flight_count() < config_.max_in_flight && state == StreamState::STREAMING) {
        if (!send_next_line()) {
            if (in_flight_count() == 0) {
                state = StreamState::COMPLETED;
                if (completion_cb_) completion_cb_(true, "Print completed");
                if (progress_cb_) progress_cb_(stats);
            }
            break;
        }
    }
    
    if (stats.error_count > 10) {
        state = StreamState::ERROR;
        if (completion_cb_) completion_cb_(false, "Too many errors");
    }
}

void GcodeStreamer::handle_ack(const String& response) {
    if (in_flight.empty()) return;
    
    for (auto it = in_flight.begin(); it != in_flight.end(); ) {
        if (response.startsWith("ok") || response.startsWith("OK")) {
            stats.acked_lines++;
            stats.last_ack_time = millis();
            it = in_flight.erase(it);
            break;
        } else if (response.startsWith("Error:") || response.startsWith("error:") || 
                   response.startsWith("!!")) {
            stats.error_count++;
            it = in_flight.erase(it);
            break;
        } else {
            ++it;
        }
    }
    
    if (progress_cb_) progress_cb_(stats);
}

void GcodeStreamer::pause() {
    if (state == StreamState::STREAMING) {
        state = StreamState::PAUSED;
    }
}

void GcodeStreamer::resume() {
    if (state == StreamState::PAUSED) {
        state = StreamState::STREAMING;
    }
}

void GcodeStreamer::cancel() {
    state = StreamState::CANCELLED;
}

StreamState GcodeStreamer::get_state() const { return state; }
const StreamStats& GcodeStreamer::get_stats() const { return stats; }
bool GcodeStreamer::is_active() const { return state == StreamState::STREAMING || state == StreamState::PAUSED; }

uint32_t GcodeStreamer::count_total_lines() {
    uint32_t count = 0;
    file_handle.seek(0);
    while (file_handle.available()) {
        String line = file_handle.readStringUntil('\n');
        if (line.length() > 0 && !line.startsWith(";")) {
            count++;
        }
    }
    file_handle.seek(0);
    return count;
}

bool GcodeStreamer::send_next_line() {
    if (current_line >= total_lines) return false;
    
    String line;
    while (file_handle.available()) {
        line = file_handle.readStringUntil('\n');
        line.trim();
        if (line.length() > 0 && !line.startsWith(";")) {
            break;
        }
        line = "";
    }
    
    if (line.length() == 0) {
        current_line++;
        if (current_line >= total_lines) return false;
        return send_next_line();
    }
    
    if (g_cdc_serial && g_cdc_serial->connected()) {
        g_cdc_serial->println(line);
        in_flight.push_back(line);
        stats.sent_lines++;
        current_line++;
        return true;
    }
    
    return false;
}

uint8_t GcodeStreamer::in_flight_count() const {
    return in_flight.size();
}

void GcodeStreamer::check_timeouts() {
    uint32_t now = millis();
    for (auto it = in_flight.begin(); it != in_flight.end(); ) {
        if (now - stats.last_ack_time > config_.ack_timeout_ms) {
            stats.retry_count++;
            if (stats.retry_count < config_.max_retries) {
                if (g_cdc_serial && g_cdc_serial->connected()) {
                    g_cdc_serial->println(*it);
                    stats.sent_lines++;
                }
            } else {
                stats.error_count++;
                it = in_flight.erase(it);
            }
        } else {
            ++it;
        }
    }
}