#include "gcode_streamer.h"
#include <EspUsbHost.h>

extern EspUsbHostCdcSerial* g_cdc_serial;

GcodeStreamer g_gcode_streamer;

void GcodeStreamer::begin(File& file, const StreamConfig& config) {
    // A completed/cancelled stream can be reused for the next job.  Only an
    // actively running or preparing stream must reject a new start.
    if (state == StreamState::PREPARING || state == StreamState::STREAMING ||
        state == StreamState::PAUSED) return;
    
    file_handle = file;
    config_ = config;
    in_flight.clear();
    current_line = 0;
    end_of_file = false;
    state = StreamState::PREPARING;
    
    file_handle.seek(0);
    // Do not scan the complete file here.  begin() is called from the
    // AsyncWebServer request callback, and a large G-code file can block that
    // task long enough to trigger the ESP task watchdog.  The main loop sends
    // a few lines at a time and reaches EOF incrementally instead.
    stats = StreamStats();
    total_lines = 0;
    stats.total_lines = 0;
    stats.start_time = millis();
    stats.last_ack_time = stats.start_time;
    
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
                if (end_of_file) {
                    bool success = stats.sent_lines > 0;
                    state = success ? StreamState::COMPLETED : StreamState::ERROR;
                    if (completion_cb_) {
                        completion_cb_(success, success ? "Print completed" :
                                       "G-code file has no executable lines");
                    }
                    if (progress_cb_) progress_cb_(stats);
                }
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
    // Marlin emits this while M190/M109/G4 and some motion commands are
    // still being processed.  It is not an acknowledgement, but it proves
    // the printer is alive and must extend the wait window instead of causing
    // the same command to be retried and queued repeatedly.
    if (response.startsWith("busy") || response.startsWith("echo:busy:")) {
        stats.last_ack_time = millis();
        return;
    }

    if (in_flight.empty()) return;
    
    for (auto it = in_flight.begin(); it != in_flight.end(); ) {
        if (response.startsWith("ok") || response.startsWith("OK")) {
            stats.acked_lines++;
            stats.last_ack_time = millis();
            stats.retry_count = 0;
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

bool GcodeStreamer::send_next_line() {
    if (end_of_file || !file_handle || !file_handle.available()) {
        end_of_file = true;
        return false;
    }
    
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
        end_of_file = true;
        return false;
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
