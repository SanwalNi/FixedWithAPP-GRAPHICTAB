#pragma once
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

struct DeviceInfo {
    std::string serial;
    std::string state;
    std::string model;
};

class ADBManager {
private:
    std::string adb_path;
    std::atomic<bool> is_streaming{false};
    std::thread stream_thread;

    // Handles for forced cleanup of streaming process (BUG-20 fix)
    // FIX: guard with a mutex - these were read/written from two threads
    // (stop_getevent_stream and the streaming thread) without synchronization.
    std::mutex handles_mutex;
    HANDLE stream_process_handle = nullptr;
    HANDLE stream_read_pipe = nullptr;

    std::string run_adb_command(const std::string& args);

    // BUG-21: Validate that a string is safe for command-line use
    static bool is_safe_arg(const std::string& s);

public:
    ADBManager();
    ~ADBManager();

    bool find_adb();
    void set_adb_path(const std::string& path);
    std::string get_adb_path() const { return adb_path; }

    bool is_available();
    std::vector<DeviceInfo> list_devices();
    bool reverse_port(const std::string& serial, int port);
    bool forward_port(const std::string& serial, int port);
    bool open_url_on_device(const std::string& serial, const std::string& url);
    std::vector<std::string> detect_touch_devices(const std::string& serial);

    // Direct kernel getevent streaming
    bool start_getevent_stream(
        const std::string& serial, 
        const std::string& input_device,
        std::function<void(uint16_t type, uint16_t code, int32_t value)> on_event
    );
    void stop_getevent_stream();
};
