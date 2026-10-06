#pragma once

#include <Arduino.h>
#include <FS.h>
#include <functional>
#include "command_scheduler.h"

enum class StreamState {
    IDLE,
    PREPARING,
    STREAMING,
    PAUSED,
    COMPLETED,
    ERROR,
    CANCELLED
};

struct StreamConfig {
    uint32_t max_in_flight = 3;
    uint32_t ack_timeout_ms = 5000;
    uint8_t max_retries = 3;
    uint16_t chunk_size = 512;
};

struct StreamStats {
    uint32_t total_lines = 0;
    uint32_t sent_lines = 0;
    uint32_t acked_lines = 0;
    uint32_t error_count = 0;
    uint32_t retry_count = 0;
    uint32_t start_time = 0;
    uint32_t last_ack_time = 0;
};

class GcodeStreamer {
public:
    using ProgressCallback = std::function<void(const StreamStats&)>;
    using CompletionCallback = std::function<void(bool success, const String& message)>;

    GcodeStreamer() {}

    void begin(File& file, const StreamConfig& config = StreamConfig());
    void set_callbacks(std::function<void(const StreamStats&)> progress_cb,
                       std::function<void(bool, const String&)> completion_cb);
    void handle_streaming();
    void handle_ack(const String& response);
    void pause();
    void resume();
    void cancel();
    StreamState get_state() const;
    const StreamStats& get_stats() const;
    bool is_active() const;
    void set_config(const StreamConfig& config) { config_ = config; }

private:
    StreamConfig config_;
    StreamStats stats;
    uint32_t total_lines = 0;
    std::vector<String> in_flight;
    size_t current_line = 0;
    File file_handle;

    std::function<void(const StreamStats&)> progress_cb_;
    std::function<void(bool, const String&)> completion_cb_;

    StreamState state = StreamState::IDLE;

    uint32_t count_total_lines();
    bool send_next_line();
    uint8_t in_flight_count() const;
    void check_timeouts();
};

extern GcodeStreamer g_gcode_streamer;