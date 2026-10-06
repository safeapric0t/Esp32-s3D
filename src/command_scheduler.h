#pragma once

#include <Arduino.h>
#include <vector>
#include <functional>
#include "web_server.h"

struct PrintJob {
    String id;
    String filename;
    String filepath;
    uint32_t file_size = 0;
    String mode;  // "stream" or "sd"
    uint32_t created_at = 0;
    String status = "pending";  // pending, ready, printing, paused, completed, error, cancelled
    String error_message;
};

enum class CommandPriority {
    EMERGENCY = 0,      // M112, M999 - Emergency stop
    PRINT_CONTROL = 1,  // Pause, Resume, Cancel
    MANUAL = 2,         // Jog, Home, Manual G-code
    STATUS = 2,         // M105, M114 - Status queries
    PRINT_STREAM = 3    // G-code streaming (lowest priority during print)
};

struct PrinterCommand {
    String command;
    CommandPriority priority;
    uint32_t sequence = 0;
    uint32_t timestamp = 0;
    bool requires_ack = true;
    uint8_t retry_count = 0;
    uint32_t sent_time = 0;
    std::function<void(bool success, String response)> callback;
    bool sent = false;
    bool completed = false;
    String response;
};

class CommandScheduler {
public:
    CommandScheduler() : next_sequence(1) {}

    void add_command(PrinterCommand cmd) {
        cmd.sequence = next_sequence++;
        cmd.timestamp = millis();
        queue.push_back(cmd);
        sort_queue();
    }

    bool get_next_ready_command(PrinterCommand& cmd) {
        // First, check for retryable commands
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (it->sent && !it->completed && it->requires_ack) {
                if (millis() - it->sent_time > ACK_TIMEOUT_MS) {
                    if (it->retry_count < MAX_RETRIES) {
                        it->retry_count++;
                        it->sent = false;
                        it->sent_time = 0;
                    } else {
                        // Max retries exceeded
                        it->completed = true;
                        if (it->callback) it->callback(false, "ACK timeout");
                        continue;
                    }
                }
            }
            
            // Find next unsent command
            if (!it->sent && !it->completed) {
                cmd = *it;
                it->sent = true;
                it->sent_time = millis();
                return true;
            }
        }
        
        // No ready commands
        return false;
    }

    void mark_ack_received(uint32_t sequence, const String& response) {
        for (auto& cmd : queue) {
            if (cmd.sequence == sequence && cmd.sent && !cmd.completed) {
                cmd.completed = true;
                cmd.response = response;
                if (cmd.callback) cmd.callback(true, response);
                break;
            }
        }
        cleanup_completed();
    }

    void mark_error(uint32_t sequence, const String& error) {
        for (auto& cmd : queue) {
            if (cmd.sequence == sequence && cmd.sent && !cmd.completed) {
                cmd.completed = true;
                if (cmd.callback) cmd.callback(false, error);
                break;
            }
        }
        cleanup_completed();
    }

    void clear_print_stream_commands() {
        queue.erase(
            std::remove_if(queue.begin(), queue.end(),
                [](const PrinterCommand& cmd) {
                    return cmd.priority == CommandPriority::PRINT_STREAM;
                }),
            queue.end()
        );
    }

    void clear_all_except_emergency() {
        queue.erase(
            std::remove_if(queue.begin(), queue.end(),
                [](const PrinterCommand& cmd) {
                    return cmd.priority != CommandPriority::EMERGENCY;
                }),
            queue.end()
        );
    }

    size_t pending_count() const {
        return std::count_if(queue.begin(), queue.end(),
            [](const PrinterCommand& c) { return !c.completed && !c.sent; });
    }

    size_t in_flight_count() const {
        return std::count_if(queue.begin(), queue.end(),
            [](const PrinterCommand& c) { return c.sent && !c.completed; });
    }

    void clear() { queue.clear(); }

private:
    static const uint32_t ACK_TIMEOUT_MS = 5000;
    static const uint8_t MAX_RETRIES = 3;
    std::vector<PrinterCommand> queue;
    uint32_t next_sequence = 1;

    void sort_queue() {
        std::sort(queue.begin(), queue.end(),
            [](const PrinterCommand& a, const PrinterCommand& b) {
                if (a.priority != b.priority) return a.priority < b.priority;
                return a.sequence < b.sequence;
            });
    }

    void cleanup_completed() {
        queue.erase(
            std::remove_if(queue.begin(), queue.end(),
                [](const PrinterCommand& c) { return c.completed; }),
            queue.end()
        );
    }
};

extern CommandScheduler g_command_scheduler;