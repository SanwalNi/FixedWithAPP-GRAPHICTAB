#pragma once
// ============================================================================
//  Minimal ADB wrapper: device detection, reverse tunnel, APK install, launch.
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>

class Adb {
public:
    bool find_adb() {
        // 1. Already known?
        if (!adb_path_.empty()) return true;
        // 2. PATH lookup
        char path_buf[MAX_PATH];
        if (SearchPathA(nullptr, "adb.exe", nullptr, MAX_PATH, path_buf, nullptr)) {
            adb_path_ = path_buf;
            return true;
        }
        // 3. Known locations
        const char* candidates[] = {
            "E:\\platform-tools-latest-windows\\platform-tools\\adb.exe",
            "E:\\Android\\Sdk\\platform-tools\\adb.exe",
            "C:\\platform-tools\\adb.exe",
            "D:\\platform-tools\\adb.exe",
            "C:\\Android\\platform-tools\\adb.exe",
        };
        for (const char* c : candidates) {
            if (GetFileAttributesA(c) != INVALID_FILE_ATTRIBUTES) {
                adb_path_ = c;
                return true;
            }
        }
        // 4. LOCALAPPDATA SDK path
        char env[MAX_PATH];
        if (GetEnvironmentVariableA("LOCALAPPDATA", env, MAX_PATH)) {
            std::string p = std::string(env) + "\\Android\\Sdk\\platform-tools\\adb.exe";
            if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
                adb_path_ = p;
                return true;
            }
        }
        return false;
    }

    bool available() const { return !adb_path_.empty(); }
    std::string path() const { return adb_path_; }

    std::vector<std::string> list_devices() {
        std::vector<std::string> out;
        std::string res = run("devices");
        size_t pos = 0;
        while ((pos = res.find("\n", pos)) != std::string::npos) {
            size_t next = res.find("\n", pos + 1);
            if (next == std::string::npos) break;
            std::string line = res.substr(pos + 1, next - pos - 1);
            pos = next;
            if (line.find("device") != std::string::npos &&
                line.find("offline") == std::string::npos &&
                line.find("unauthorized") == std::string::npos) {
                size_t tab = line.find('\t');
                if (tab != std::string::npos) out.push_back(line.substr(0, tab));
            }
        }
        return out;
    }

    bool reverse(int port) {
        std::string res = run("reverse tcp:" + std::to_string(port) + " tcp:" + std::to_string(port));
        return res.find("error") == std::string::npos && res.find("failed") == std::string::npos;
    }

    bool is_app_installed() {
        std::string res = run("shell pm path com.alamy.tablet");
        return res.find("package:") != std::string::npos;
    }

    bool install_apk(const std::string& apk_path) {
        if (GetFileAttributesA(apk_path.c_str()) == INVALID_FILE_ATTRIBUTES) return false;
        std::string res = run("install -r -g \"" + apk_path + "\"", 120000);
        return res.find("Success") != std::string::npos;
    }

    bool launch_app() {
        std::string res = run(
            "shell am start -n com.alamy.tablet/.MainActivity "
            "--activity-clear-top --activity-clear-when-task-reset");
        return res.find("Starting") != std::string::npos ||
               res.find("starting") != std::string::npos;
    }

    static bool is_safe_arg(const std::string& s) {
        for (char c : s) {
            if (c == '&' || c == '|' || c == ';' || c == '<' || c == '>' ||
                c == '"' || c == '\'' || c == '%' || c == '`') return false;
        }
        return true;
    }

private:
    std::string run(const std::string& args, DWORD timeout_ms = 20000) {
        if (adb_path_.empty()) return "";
        std::string cmd = "\"" + adb_path_ + "\" " + args;

        SECURITY_ATTRIBUTES sa{sizeof(sa)};
        sa.bInheritHandle = TRUE;
        HANDLE read_end = nullptr, write_end = nullptr;
        if (!CreatePipe(&read_end, &write_end, &sa, 0)) return "";

        SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdOutput = write_end;
        si.hStdError = write_end;

        PROCESS_INFORMATION pi{};
        std::vector<char> cmd_buf(cmd.begin(), cmd.end());
        cmd_buf.push_back('\0');

        if (!CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, TRUE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(read_end);
            CloseHandle(write_end);
            return "";
        }
        CloseHandle(write_end);

        std::string output;
        char buf[4096];
        DWORD read = 0;
        BOOL done = FALSE;
        while (!done) {
            DWORD wait = WaitForSingleObject(pi.hProcess, 50);
            while (PeekNamedPipe(read_end, nullptr, 0, nullptr, &read, nullptr) && read > 0) {
                DWORD got = 0;
                if (ReadFile(read_end, buf, sizeof(buf), &got, nullptr) && got > 0) {
                    output.append(buf, got);
                } else break;
            }
            if (wait == WAIT_OBJECT_0) done = TRUE;
            else if (wait == WAIT_FAILED) { done = TRUE; }
        }
        // final drain
        while (PeekNamedPipe(read_end, nullptr, 0, nullptr, &read, nullptr) && read > 0) {
            DWORD got = 0;
            if (ReadFile(read_end, buf, sizeof(buf), &got, nullptr) && got > 0) {
                output.append(buf, got);
            } else break;
        }
        CloseHandle(read_end);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return output;
    }

    std::string adb_path_;
};
