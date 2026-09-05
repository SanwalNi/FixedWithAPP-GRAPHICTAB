#include "adb_manager.hpp"
#include <iostream>
#include <sstream>
#include <array>
#include <fstream>
#include <filesystem>

ADBManager::ADBManager() {
    find_adb();
}

ADBManager::~ADBManager() {
    stop_getevent_stream();
}

// BUG-21 FIX: Reject strings containing shell metacharacters
bool ADBManager::is_safe_arg(const std::string& s) {
    for (char c : s) {
        if (c == ';' || c == '&' || c == '|' || c == '`' || c == '$' ||
            c == '(' || c == ')' || c == '{' || c == '}' || c == '\n' ||
            c == '\r' || c == '\'' || c == '>' || c == '<') {
            return false;
        }
    }
    return true;
}

bool ADBManager::find_adb() {
    // 1. Check if adb is in PATH
    char buffer[MAX_PATH];
    DWORD res = SearchPathA(NULL, "adb.exe", NULL, MAX_PATH, buffer, NULL);
    if (res > 0) {
        adb_path = buffer;
        return true;
    }

    // 2. Check standard Android SDK platform-tools locations
    std::vector<std::string> search_candidates;
    
    char* local_appdata = nullptr;
    size_t len = 0;
    if (_dupenv_s(&local_appdata, &len, "LOCALAPPDATA") == 0 && local_appdata != nullptr) {
        search_candidates.push_back(std::string(local_appdata) + "\\Android\\Sdk\\platform-tools\\adb.exe");
        free(local_appdata);
    }

    search_candidates.push_back("E:\\platform-tools-latest-windows\\platform-tools\\adb.exe");
    search_candidates.push_back("C:\\platform-tools\\adb.exe");
    search_candidates.push_back("D:\\platform-tools\\adb.exe");
    search_candidates.push_back("C:\\Android\\platform-tools\\adb.exe");

    for (const auto& candidate : search_candidates) {
        if (std::filesystem::exists(candidate)) {
            adb_path = candidate;
            return true;
        }
    }

    adb_path = "adb.exe"; // Fallback to PATH
    return false;
}

void ADBManager::set_adb_path(const std::string& path) {
    adb_path = path;
}

bool ADBManager::is_available() {
    std::string out = run_adb_command("version");
    return out.find("Android Debug Bridge") != std::string::npos;
}

std::string ADBManager::run_adb_command(const std::string& args) {
    std::string cmd = "\"" + adb_path + "\" " + args;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe, hWritePipe;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        return "";
    }
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(STARTUPINFOA);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};
    std::string cmd_copy = cmd;

    BOOL success = CreateProcessA(
        NULL,
        cmd_copy.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    CloseHandle(hWritePipe);

    if (!success) {
        CloseHandle(hReadPipe);
        return "";
    }

    std::string output;
    char buffer[1024];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[bytesRead] = '\0';
        output += buffer;
    }

    CloseHandle(hReadPipe);

    // BUG-19 FIX: Check WaitForSingleObject return and TerminateProcess on timeout
    DWORD wait_result = WaitForSingleObject(pi.hProcess, 5000); // 5 second timeout
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000); // Brief wait for termination
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return output;
}

std::vector<DeviceInfo> ADBManager::list_devices() {
    std::vector<DeviceInfo> devices;
    std::string output = run_adb_command("devices -l");
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty() || line.find("List of devices attached") != std::string::npos || line.find("* daemon") != std::string::npos) {
            continue;
        }

        std::istringstream line_stream(line);
        std::string serial, state;
        if (line_stream >> serial >> state) {
            DeviceInfo dev;
            dev.serial = serial;
            dev.state = state;

            std::string token;
            while (line_stream >> token) {
                if (token.find("model:") == 0) {
                    dev.model = token.substr(6);
                }
            }
            if (dev.model.empty()) {
                dev.model = "Android Device";
            }
            devices.push_back(dev);
        }
    }

    return devices;
}

bool ADBManager::reverse_port(const std::string& serial, int port) {
    // BUG-21: Validate serial
    if (!serial.empty() && !is_safe_arg(serial)) return false;

    std::string args;
    if (!serial.empty()) {
        args += "-s " + serial + " ";
    }
    args += "reverse tcp:" + std::to_string(port) + " tcp:" + std::to_string(port);
    std::string res = run_adb_command(args);
    return res.find("error") == std::string::npos;
}

bool ADBManager::forward_port(const std::string& serial, int port) {
    if (!serial.empty() && !is_safe_arg(serial)) return false;

    std::string args;
    if (!serial.empty()) {
        args += "-s " + serial + " ";
    }
    args += "forward tcp:" + std::to_string(port) + " tcp:" + std::to_string(port);
    std::string res = run_adb_command(args);
    return res.find("error") == std::string::npos;
}

bool ADBManager::open_url_on_device(const std::string& serial, const std::string& url) {
    // BUG-21: Validate both serial and url
    if (!serial.empty() && !is_safe_arg(serial)) return false;
    if (!is_safe_arg(url)) return false;

    std::string args;
    if (!serial.empty()) {
        args += "-s " + serial + " ";
    }
    args += "shell am start -a android.intent.action.VIEW -d \"" + url + "\"";
    std::string res = run_adb_command(args);
    return res.find("Starting: Intent") != std::string::npos;
}

std::vector<std::string> ADBManager::detect_touch_devices(const std::string& serial) {
    std::vector<std::string> input_nodes;
    if (!serial.empty() && !is_safe_arg(serial)) return input_nodes;

    std::string args;
    if (!serial.empty()) {
        args += "-s " + serial + " ";
    }
    args += "shell getevent -pl";
    std::string output = run_adb_command(args);
    
    std::istringstream stream(output);
    std::string line;
    std::string current_device;
    bool is_touch = false;

    while (std::getline(stream, line)) {
        if (line.find("add device") != std::string::npos) {
            if (is_touch && !current_device.empty()) {
                input_nodes.push_back(current_device);
            }
            size_t colon = line.rfind(":");
            if (colon != std::string::npos) {
                current_device = line.substr(colon + 2);
                while (!current_device.empty() && (current_device.back() == '\r' || current_device.back() == ' ')) {
                    current_device.pop_back();
                }
            }
            is_touch = false;
        } else if (line.find("ABS_MT_POSITION_X") != std::string::npos || line.find("TOUCH") != std::string::npos) {
            is_touch = true;
        }
    }

    if (is_touch && !current_device.empty()) {
        input_nodes.push_back(current_device);
    }

    return input_nodes;
}

bool ADBManager::start_getevent_stream(
    const std::string& serial, 
    const std::string& input_device,
    std::function<void(uint16_t type, uint16_t code, int32_t value)> on_event
) {
    stop_getevent_stream();

    if (!serial.empty() && !is_safe_arg(serial)) return false;
    if (!is_safe_arg(input_device)) return false;

    is_streaming.store(true);
    stream_thread = std::thread([this, serial, input_device, on_event]() {
        std::string cmd = "\"" + adb_path + "\" ";
        if (!serial.empty()) {
            cmd += "-s " + serial + " ";
        }
        cmd += "exec-out getevent -r " + input_device;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;

        HANDLE hReadPipe, hWritePipe;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) return;
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(STARTUPINFOA);
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        si.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi{};
        std::string cmd_copy = cmd;

        if (!CreateProcessA(NULL, cmd_copy.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(hReadPipe);
            CloseHandle(hWritePipe);
            return;
        }

        CloseHandle(hWritePipe);

        // Store handles for forced cleanup in stop_getevent_stream() (BUG-20 fix)
        {
            std::lock_guard<std::mutex> lock(handles_mutex);
            stream_process_handle = pi.hProcess;
            stream_read_pipe = hReadPipe;
        }

        // FIX: stop_getevent_stream() may run before the thread registers its
        // handles - without this check the thread would block in ReadFile
        // forever and stop()'s join() would hang.
        if (!is_streaming.load()) {
            TerminateProcess(pi.hProcess, 0);
            WaitForSingleObject(pi.hProcess, 2000);
            CloseHandle(hReadPipe);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            std::lock_guard<std::mutex> lock(handles_mutex);
            stream_process_handle = nullptr;
            stream_read_pipe = nullptr;
            return;
        }

        // Linux input_event struct (64-bit Android kernel):
        // struct input_event {
        //    struct timeval time; // 16 bytes (8 bytes sec + 8 bytes usec)
        //    uint16_t type;       // 2 bytes
        //    uint16_t code;       // 2 bytes
        //    int32_t value;       // 4 bytes
        // }; Total = 24 bytes (or 16 bytes on 32-bit kernel)
        
        #pragma pack(push, 1)
        struct LinuxInputEvent64 {
            uint64_t tv_sec;
            uint64_t tv_usec;
            uint16_t type;
            uint16_t code;
            int32_t value;
        };
        #pragma pack(pop)

        LinuxInputEvent64 ev;
        DWORD bytesRead;

        while (is_streaming.load()) {
            if (ReadFile(hReadPipe, &ev, sizeof(ev), &bytesRead, NULL) && bytesRead == sizeof(ev)) {
                on_event(ev.type, ev.code, ev.value);
            } else {
                break;
            }
        }

        // Cleanup - the thread owns its handle copies; stop() must never close
        // them (previously BOTH closed the same handles: double-CloseHandle race
        // that could close an unrelated handle reused by the OS).
        {
            std::lock_guard<std::mutex> lock(handles_mutex);
            stream_read_pipe = nullptr;
            stream_process_handle = nullptr;
        }
        CloseHandle(hReadPipe);
        TerminateProcess(pi.hProcess, 0);
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    });

    return true;
}

// BUG-20 FIX: Force-close the pipe and terminate the process before joining
void ADBManager::stop_getevent_stream() {
    is_streaming.store(false);

    // Terminate the ADB process so the thread's blocking ReadFile unblocks.
    // FIX: never close the pipe/process handles here - the streaming thread owns
    // them and closes them in its cleanup. Closing a handle another thread is
    // using caused double-CloseHandle races (thread closed the same values again).
    HANDLE proc = nullptr;
    {
        std::lock_guard<std::mutex> lock(handles_mutex);
        proc = stream_process_handle;
    }
    if (proc) {
        TerminateProcess(proc, 0);
        WaitForSingleObject(proc, 2000);
    }

    if (stream_thread.joinable()) {
        stream_thread.join();
    }

    // Fallback: if the thread exited before registering its handles (should not
    // normally happen), make sure nothing leaks.
    std::lock_guard<std::mutex> lock(handles_mutex);
    if (stream_process_handle) {
        CloseHandle(stream_process_handle);
        stream_process_handle = nullptr;
    }
}
