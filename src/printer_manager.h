#pragma once

#include <Arduino.h>
#include <vector>
#include <map>
#include <functional>
#include "command_scheduler.h"
#include "gcode_streamer.h"

enum class JobState {
    PENDING = 0,
    UPLOADING,
    VALIDATING,
    READY,
    STARTING,
    PRINTING,
    PAUSED,
    RESUMING,
    CANCELLING,
    COMPLETED,
    ERROR,
    CANCELLED
};

enum class PrinterManagerState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    IDLE,
    PREPARING,
    PRINTING,
    PAUSED,
    ERROR
};

class PrinterManager {
public:
    struct JobInfo {
        String id;
        String filename;
        String filepath;
        uint32_t file_size = 0;
        String mode;
        String status;
        uint32_t progress = 0;
        String error_message;
        uint32_t created_at = 0;
        uint32_t started_at = 0;
        uint32_t completed_at = 0;
    };

    using StateChangeCallback = std::function<void(const String& state)>;
    using ProgressCallback = std::function<void(uint32_t progress, const String& message)>;
    using JobCompleteCallback = std::function<void(bool success, const String& message)>;

    PrinterManager();

    void begin() {
        state = PrinterManagerState::DISCONNECTED;
        current_job_id = "";
    }

    void set_callbacks(std::function<void(const String&)> state_cb,
                       std::function<void(uint32_t, const String&)> progress_cb,
                       std::function<void(bool, const String&)> complete_cb) {
        state_cb_ = state_cb;
        progress_cb_ = progress_cb;
        complete_cb_ = complete_cb;
    }

    void set_usb_status(bool connected) {
        bool was_connected = usb_connected_;
        usb_connected_ = connected;
        
        if (connected && !was_connected) {
            state = PrinterManagerState::CONNECTED;
            notify_state_change("CONNECTED");
        } else if (!connected && was_connected) {
            state = PrinterManagerState::DISCONNECTED;
            cancel_current_job("USB disconnected");
        }
    }

    void handle_status_update(const String& status_data) {
        // Parse status updates from printer
    }

    void handle_progress_update(uint32_t progress, const String& message) {
        if (progress_cb_) progress_cb_(progress, message);
    }

    void handle_print_complete(bool success, const String& message) {
        if (current_job_id.length() > 0) {
            jobs[current_job_id].status = success ? "completed" : "error";
            jobs[current_job_id].error_message = success ? "" : message;
            jobs[current_job_id].completed_at = millis();
            current_job_id = "";
        }
        if (complete_cb_) complete_cb_(success, message);
    }

    void handle_error(const String& error) {
        if (current_job_id.length() > 0) {
            jobs[current_job_id].status = "error";
            jobs[current_job_id].error_message = error;
            jobs[current_job_id].completed_at = millis();
            current_job_id = "";
        }
        if (complete_cb_) complete_cb_(false, error);
    }

    String upload_file(const String& filename, const String& filepath, uint32_t size) {
        String job_id = "job_" + String(millis());
        
        jobs[job_id] = {
            job_id,
            extract_filename(filepath),
            filepath,
            size,
            "stream",
            "uploading",
            0,
            "",
            millis()
        };
        
        return job_id;
    }

    void update_upload_progress(const String& job_id, uint32_t progress) {
        if (jobs.find(job_id) != jobs.end()) {
            jobs[job_id].progress = progress;
        }
    }

    bool finalize_upload(const String& job_id, uint32_t file_size) {
        auto it = jobs.find(job_id);
        if (it == jobs.end()) return false;
        
        it->second.status = "ready";
        it->second.file_size = file_size;
        it->second.progress = 0;
        return true;
    }

    bool start_print(const String& job_id, const String& mode) {
        auto it = jobs.find(job_id);
        if (it == jobs.end()) return false;
        
        if (it->second.status != "ready") return false;
        
        it->second.status = "starting";
        it->second.mode = mode;
        it->second.started_at = millis();
        current_job_id = job_id;
        
        notify_state_change("STARTING");
        return true;
    }

    void pause_current() {
        if (current_job_id.length() > 0) {
            jobs[current_job_id].status = "paused";
            notify_state_change("PAUSED");
        }
    }

    void resume_current() {
        if (current_job_id.length() > 0) {
            jobs[current_job_id].status = "printing";
        }
    }

    void cancel_current_job(const String& reason = "Cancelled") {
        if (current_job_id.length() > 0) {
            auto it = jobs.find(current_job_id);
            if (it != jobs.end()) {
                it->second.status = "cancelled";
                it->second.error_message = reason;
                it->second.completed_at = millis();
            }
            current_job_id = "";
            notify_state_change("CANCELLED");
        }
    }

    void set_printer_status(const String& state_str) {
        if (state_str == "PRINTING") state = PrinterManagerState::PRINTING;
        else if (state_str == "PAUSED") state = PrinterManagerState::PAUSED;
        else if (state_str == "IDLE") state = PrinterManagerState::IDLE;
        else if (state_str == "ERROR") state = PrinterManagerState::ERROR;
    }

    void set_progress(uint32_t progress, const String& message = "") {
        if (current_job_id.length() > 0) {
            jobs[current_job_id].progress = progress;
        }
    }

    void print_complete(bool success, const String& message) {
        if (current_job_id.length() > 0) {
            jobs[current_job_id].status = success ? "completed" : "error";
            jobs[current_job_id].error_message = success ? "" : message;
            jobs[current_job_id].completed_at = millis();
            current_job_id = "";
        }
        if (complete_cb_) complete_cb_(success, message);
    }

    void handle_sd_print_progress() {
        // Handle SD print progress
    }

    String get_current_job_id() const { return current_job_id; }
    
    std::vector<String> list_jobs() const {
        std::vector<String> ids;
        for (const auto& pair : jobs) {
            ids.push_back(pair.first);
        }
        return ids;
    }

    bool get_job(const String& job_id, struct JobInfo& job) const {
        auto it = jobs.find(job_id);
        if (it == jobs.end()) return false;
        
        const auto& j = it->second;
        job = {j.id, j.filename, j.filepath, j.file_size, j.mode, j.status, 
               j.progress, j.error_message, j.created_at, j.started_at, j.completed_at};
        return true;
    }

    bool is_printing() const {
        return state == PrinterManagerState::PRINTING;
    }

    bool is_paused() const {
        return state == PrinterManagerState::PAUSED;
    }

    bool is_idle() const {
        return state == PrinterManagerState::IDLE || state == PrinterManagerState::CONNECTED;
    }

    void set_state_callback(std::function<void(const String&)> cb) { state_cb_ = cb; }
    void set_progress_callback(std::function<void(uint32_t, const String&)> cb) { progress_cb_ = cb; }
    void set_complete_callback(std::function<void(bool, const String&)> cb) { complete_cb_ = cb; }

private:
    struct Job {
        String id;
        String filename;
        String filepath;
        uint32_t file_size = 0;
        String mode;
        String status;
        uint32_t progress = 0;
        String error_message;
        uint32_t created_at = 0;
        uint32_t started_at = 0;
        uint32_t completed_at = 0;
    };

    std::map<String, Job> jobs;
    String current_job_id;
    PrinterManagerState state = PrinterManagerState::DISCONNECTED;
    bool usb_connected_ = false;

    std::function<void(const String&)> state_cb_;
    std::function<void(uint32_t, const String&)> progress_cb_;
    std::function<void(bool, const String&)> complete_cb_;

    void notify_state_change(const String& state) {
        if (state_cb_) state_cb_(state);
    }

    String extract_filename(const String& path) {
        int idx = path.lastIndexOf('/');
        if (idx >= 0) return path.substring(idx + 1);
        return path;
    }
};

extern PrinterManager g_printer_manager;