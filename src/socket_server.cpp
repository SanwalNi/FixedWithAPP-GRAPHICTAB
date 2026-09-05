#include "socket_server.hpp"
#include "input_injector.hpp"
#include "embedded_client.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstring>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <cstdio>

// --- Lightweight SHA-1 & Base64 implementation for WebSocket handshake ---
namespace crypto {
    inline uint32_t rol(uint32_t value, size_t bits) {
        return (value << bits) | (value >> (32 - bits));
    }

    inline uint32_t blk(const uint32_t block[16], size_t i) {
        return rol(block[(i + 13) & 15] ^ block[(i + 8) & 15] ^ block[(i + 2) & 15] ^ block[i & 15], 1);
    }

    inline void R0(const uint32_t block[16], uint32_t v, uint32_t& w, uint32_t x, uint32_t y, uint32_t& z, size_t i) {
        z += ((w & (x ^ y)) ^ y) + block[i] + 0x5a827999 + rol(v, 5);
        w = rol(w, 30);
    }

    inline void R1(uint32_t block[16], uint32_t v, uint32_t& w, uint32_t x, uint32_t y, uint32_t& z, size_t i) {
        block[i & 15] = blk(block, i);
        z += ((w & (x ^ y)) ^ y) + block[i & 15] + 0x5a827999 + rol(v, 5);
        w = rol(w, 30);
    }

    inline void R2(uint32_t block[16], uint32_t v, uint32_t& w, uint32_t x, uint32_t y, uint32_t& z, size_t i) {
        block[i & 15] = blk(block, i);
        z += (w ^ x ^ y) + block[i & 15] + 0x6ed9eba1 + rol(v, 5);
        w = rol(w, 30);
    }

    inline void R3(uint32_t block[16], uint32_t v, uint32_t& w, uint32_t x, uint32_t y, uint32_t& z, size_t i) {
        block[i & 15] = blk(block, i);
        z += (((w | x) & y) | (w & x)) + block[i & 15] + 0x8f1bbcdc + rol(v, 5);
        w = rol(w, 30);
    }

    inline void R4(uint32_t block[16], uint32_t v, uint32_t& w, uint32_t x, uint32_t y, uint32_t& z, size_t i) {
        block[i & 15] = blk(block, i);
        z += (w ^ x ^ y) + block[i & 15] + 0xca62c1d6 + rol(v, 5);
        w = rol(w, 30);
    }

    std::vector<uint8_t> sha1(const std::string& input) {
        uint32_t digest[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };
        uint64_t total_bits = input.size() * 8;

        std::vector<uint8_t> data(input.begin(), input.end());
        data.push_back(0x80);
        while ((data.size() % 64) != 56) {
            data.push_back(0x00);
        }
        for (int i = 7; i >= 0; --i) {
            data.push_back(static_cast<uint8_t>((total_bits >> (i * 8)) & 0xFF));
        }

        for (size_t chunk = 0; chunk < data.size(); chunk += 64) {
            uint32_t block[16];
            for (size_t i = 0; i < 16; ++i) {
                block[i] = (static_cast<uint32_t>(data[chunk + i * 4]) << 24) |
                           (static_cast<uint32_t>(data[chunk + i * 4 + 1]) << 16) |
                           (static_cast<uint32_t>(data[chunk + i * 4 + 2]) << 8) |
                           (static_cast<uint32_t>(data[chunk + i * 4 + 3]));
            }

            uint32_t a = digest[0], b = digest[1], c = digest[2], d = digest[3], e = digest[4];

            R0(block, a, b, c, d, e, 0); R0(block, e, a, b, c, d, 1); R0(block, d, e, a, b, c, 2); R0(block, c, d, e, a, b, 3);
            R0(block, b, c, d, e, a, 4); R0(block, a, b, c, d, e, 5); R0(block, e, a, b, c, d, 6); R0(block, d, e, a, b, c, 7);
            R0(block, c, d, e, a, b, 8); R0(block, b, c, d, e, a, 9); R0(block, a, b, c, d, e, 10); R0(block, e, a, b, c, d, 11);
            R0(block, d, e, a, b, c, 12); R0(block, c, d, e, a, b, 13); R0(block, b, c, d, e, a, 14); R0(block, a, b, c, d, e, 15);
            R1(block, e, a, b, c, d, 16); R1(block, d, e, a, b, c, 17); R1(block, c, d, e, a, b, 18); R1(block, b, c, d, e, a, 19);
            R2(block, a, b, c, d, e, 20); R2(block, e, a, b, c, d, 21); R2(block, d, e, a, b, c, 22); R2(block, c, d, e, a, b, 23);
            R2(block, b, c, d, e, a, 24); R2(block, a, b, c, d, e, 25); R2(block, e, a, b, c, d, 26); R2(block, d, e, a, b, c, 27);
            R2(block, c, d, e, a, b, 28); R2(block, b, c, d, e, a, 29); R2(block, a, b, c, d, e, 30); R2(block, e, a, b, c, d, 31);
            R2(block, d, e, a, b, c, 32); R2(block, c, d, e, a, b, 33); R2(block, b, c, d, e, a, 34); R2(block, a, b, c, d, e, 35);
            R2(block, e, a, b, c, d, 36); R2(block, d, e, a, b, c, 37); R2(block, c, d, e, a, b, 38); R2(block, b, c, d, e, a, 39);
            R3(block, a, b, c, d, e, 40); R3(block, e, a, b, c, d, 41); R3(block, d, e, a, b, c, 42); R3(block, c, d, e, a, b, 43);
            R3(block, b, c, d, e, a, 44); R3(block, a, b, c, d, e, 45); R3(block, e, a, b, c, d, 46); R3(block, d, e, a, b, c, 47);
            R3(block, c, d, e, a, b, 48); R3(block, b, c, d, e, a, 49); R3(block, a, b, c, d, e, 50); R3(block, e, a, b, c, d, 51);
            R3(block, d, e, a, b, c, 52); R3(block, c, d, e, a, b, 53); R3(block, b, c, d, e, a, 54); R3(block, a, b, c, d, e, 55);
            R3(block, e, a, b, c, d, 56); R3(block, d, e, a, b, c, 57); R3(block, c, d, e, a, b, 58); R3(block, b, c, d, e, a, 59);
            R4(block, a, b, c, d, e, 60); R4(block, e, a, b, c, d, 61); R4(block, d, e, a, b, c, 62); R4(block, c, d, e, a, b, 63);
            R4(block, b, c, d, e, a, 64); R4(block, a, b, c, d, e, 65); R4(block, e, a, b, c, d, 66); R4(block, d, e, a, b, c, 67);
            R4(block, c, d, e, a, b, 68); R4(block, b, c, d, e, a, 69); R4(block, a, b, c, d, e, 70); R4(block, e, a, b, c, d, 71);
            R4(block, d, e, a, b, c, 72); R4(block, c, d, e, a, b, 73); R4(block, b, c, d, e, a, 74); R4(block, a, b, c, d, e, 75);
            R4(block, e, a, b, c, d, 76); R4(block, d, e, a, b, c, 77); R4(block, c, d, e, a, b, 78); R4(block, b, c, d, e, a, 79);

            digest[0] += a; digest[1] += b; digest[2] += c; digest[3] += d; digest[4] += e;
        }

        std::vector<uint8_t> result(20);
        for (size_t i = 0; i < 5; ++i) {
            result[i * 4] = static_cast<uint8_t>((digest[i] >> 24) & 0xFF);
            result[i * 4 + 1] = static_cast<uint8_t>((digest[i] >> 16) & 0xFF);
            result[i * 4 + 2] = static_cast<uint8_t>((digest[i] >> 8) & 0xFF);
            result[i * 4 + 3] = static_cast<uint8_t>(digest[i] & 0xFF);
        }
        return result;
    }

    std::string base64_encode(const std::vector<uint8_t>& input) {
        static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        int val = 0, valb = -6;
        for (uint8_t c : input) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                out.push_back(tbl[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) out.push_back(tbl[((val << 8) >> (valb + 8)) & 0x3F]);
        while (out.size() % 4) out.push_back('=');
        return out;
    }
}

std::string SocketServer::generate_ws_accept_key(const std::string& client_key) {
    static const std::string magic_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = client_key + magic_guid;
    auto hash = crypto::sha1(combined);
    return crypto::base64_encode(hash);
}

// Send a WebSocket frame (server -> client, unmasked per RFC 6455 §5.1)
void SocketServer::send_ws_frame(SOCKET s, uint8_t opcode, const uint8_t* payload, size_t len) {
    // Calculate header size to pre-reserve and avoid GCC -Warray-bounds false positive
    size_t header_size = 2;
    if (len >= 126 && len <= 65535) header_size += 2;
    else if (len > 65535) header_size += 8;

    std::vector<uint8_t> frame;
    frame.reserve(header_size + len);

    frame.push_back(0x80 | opcode); // FIN + opcode
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
        }
    }
    if (payload && len > 0) {
        frame.insert(frame.end(), payload, payload + len);
    }
    send(s, reinterpret_cast<const char*>(frame.data()), static_cast<int>(frame.size()), 0);
}

SocketServer::SocketServer(int listen_port, const std::string& web_root, std::shared_ptr<InputInjector> input_injector)
    : port(listen_port), web_root_dir(web_root), injector(input_injector) {
    // WSAStartup is now called in main() before any networking
    // Cache the embedded HTML once at construction
    const char* raw = GET_EMBEDDED_HTML();
    cached_embedded_html = std::string(raw);
    cached_html_length = cached_embedded_html.size();
}

SocketServer::~SocketServer() {
    stop();
    // WSACleanup is now called in main() after server shutdown
}

bool SocketServer::start() {
    listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        return false;
    }

    int opt = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(static_cast<u_short>(port));

    if (bind(listen_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        closesocket(listen_socket);
        listen_socket = INVALID_SOCKET;
        return false;
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(listen_socket);
        listen_socket = INVALID_SOCKET;
        return false;
    }

    is_running.store(true);
    server_thread = std::thread(&SocketServer::run_server, this);

    stats_thread = std::thread([this]() {
        while (is_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            uint32_t count = packet_counter.exchange(0);
            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                stats.packets_per_sec = count;
            }
        }
    });

    return true;
}

void SocketServer::stop() {
    if (!is_running.load()) return;
    is_running.store(false);

    // Close listen socket to unblock accept()
    if (listen_socket != INVALID_SOCKET) {
        closesocket(listen_socket);
        listen_socket = INVALID_SOCKET;
    }

    // Force-close all active client sockets to unblock their recv() calls
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (SOCKET s : active_client_sockets) {
            closesocket(s);
        }
        active_client_sockets.clear();
    }

    // Join server thread
    if (server_thread.joinable()) {
        server_thread.join();
    }

    // Join all client threads
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto& slot : client_threads) {
            if (slot.first.joinable()) {
                slot.first.join();
            }
        }
        client_threads.clear();
        for (auto& t : finished_client_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        finished_client_threads.clear();
    }

    if (stats_thread.joinable()) {
        stats_thread.join();
    }
}

SocketServer::ServerStats SocketServer::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex);
    return stats;
}

void SocketServer::register_client_socket(SOCKET s) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    active_client_sockets.insert(s);
}

void SocketServer::unregister_client_socket(SOCKET s) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    active_client_sockets.erase(s);
}

void SocketServer::run_server() {
    while (is_running.load()) {
        sockaddr_in client_addr{};
        int addr_len = sizeof(client_addr);
        SOCKET client_socket = accept(listen_socket, (sockaddr*)&client_addr, &addr_len);

        if (client_socket == INVALID_SOCKET) {
            if (!is_running.load()) break;
            continue;
        }

        int nodelay = 1;
        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);

        register_client_socket(client_socket);

        // Store threads for clean shutdown (no detach)
        {
            auto done_flag = std::make_shared<std::atomic<bool>>(false);
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                std::thread t([this, client_socket, ip = std::string(ip_str), done_flag]() {
                    handle_client(client_socket, ip);
                    unregister_client_socket(client_socket);
                    done_flag->store(true);
                });
                client_threads.emplace_back(std::make_pair(std::move(t), done_flag));
            }

            // FIX (memory growth): finished client threads were never removed, so
            // every phone reconnect grew the vector. Reap finished threads here.
            std::vector<std::thread> graveyard;
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                client_threads.erase(
                    std::remove_if(client_threads.begin(), client_threads.end(),
                        [&graveyard](std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>& slot) {
                            if (slot.second->load()) {
                                graveyard.push_back(std::move(slot.first));
                                return true;
                            }
                            return false;
                        }),
                    client_threads.end());
            }
            for (auto& t : graveyard) {
                if (t.joinable()) t.join();
            }
        }
    }
}

static std::string to_lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

void SocketServer::handle_client(SOCKET client_socket, const std::string& client_ip) {
    char buffer[4096];
    int bytes_read = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        closesocket(client_socket);
        return;
    }

    buffer[bytes_read] = '\0';
    std::string request_str(buffer, bytes_read);
    std::string lower_req = to_lower_str(request_str);

    // Check if this is a WebSocket upgrade request (case-insensitive)
    if (lower_req.find("upgrade: websocket") != std::string::npos || lower_req.find("upgrade:websocket") != std::string::npos) {
        size_t key_pos = lower_req.find("sec-websocket-key:");
        if (key_pos != std::string::npos) {
            size_t key_start = key_pos + 18;
            while (key_start < request_str.size() && (request_str[key_start] == ' ' || request_str[key_start] == '\t')) {
                key_start++;
            }
            size_t key_end = request_str.find("\r\n", key_start);
            if (key_end == std::string::npos) key_end = request_str.find("\n", key_start);
            
            std::string client_key = request_str.substr(key_start, key_end - key_start);
            while (!client_key.empty() && (client_key.back() == ' ' || client_key.back() == '\r')) {
                client_key.pop_back();
            }

            std::string accept_key = generate_ws_accept_key(client_key);
            std::ostringstream response;
            response << "HTTP/1.1 101 Switching Protocols\r\n"
                     << "Upgrade: websocket\r\n"
                     << "Connection: Upgrade\r\n"
                     << "Sec-WebSocket-Accept: " << accept_key << "\r\n\r\n";

            std::string resp_str = response.str();
            send(client_socket, resp_str.data(), static_cast<int>(resp_str.size()), 0);

            // FIX: only ONE connection may inject input at a time. When the phone
            // browser reconnects, the old socket can linger briefly; two live
            // injectors produced doubled/stuttered cursor movement.
            bool expected_ws = false;
            if (!input_client_active.compare_exchange_strong(expected_ws, true)) {
                const uint8_t close_code[2] = {0x03, 0xE8}; // 1000 = normal closure
                send_ws_frame(client_socket, 0x8, close_code, 2);
                closesocket(client_socket);
                return;
            }
            injector->reset_state(); // start every (re)connection with clean stroke state

            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                stats.is_client_connected = true;
                stats.client_ip = client_ip;
            }

            handle_websocket_frames(client_socket);

            {
                std::lock_guard<std::mutex> lock(stats_mutex);
                stats.is_client_connected = false;
                stats.client_ip = "";
            }
            input_client_active.store(false);
            injector->reset_state();
            closesocket(client_socket);
            return;
        }
    }

    // Check if this is a standard HTTP request for static files
    if (request_str.rfind("GET ", 0) == 0 || request_str.rfind("HEAD ", 0) == 0) {
        handle_http_request(client_socket, request_str);
        closesocket(client_socket);
        return;
    }

    // Otherwise, treat as raw binary TCP stream
    bool expected_raw = false;
    if (!input_client_active.compare_exchange_strong(expected_raw, true)) {
        closesocket(client_socket);
        return;
    }
    injector->reset_state();
    {
        std::lock_guard<std::mutex> lock(stats_mutex);
        stats.is_client_connected = true;
        stats.client_ip = client_ip;
    }
    
    if (bytes_read == sizeof(TouchPacket)) {
        TouchPacket packet;
        std::memcpy(&packet, buffer, sizeof(TouchPacket));
        injector->process_packet(packet);
        packet_counter++;
        {
            std::lock_guard<std::mutex> lock(stats_mutex);
            stats.packets_received++;
        }
    }

    handle_raw_binary_stream(client_socket);

    {
        std::lock_guard<std::mutex> lock(stats_mutex);
        stats.is_client_connected = false;
        stats.client_ip = "";
    }
    input_client_active.store(false);
    injector->reset_state();
    closesocket(client_socket);
}

// Helper: receive exactly n bytes, return false on error/disconnect
// FIX: WSAETIMEDOUT must be retried (not treated as disconnect) — SO_RCVTIMEO fires
// inside recv_exact during mask/payload reads when drawing slowly, causing phantom
// disconnects mid-stroke which send a TOUCH_UP and break the line.
static bool recv_exact(SOCKET s, char* buf, int n) {
    int total = 0;
    while (total < n) {
        int r = recv(s, buf + total, n - total, 0);
        if (r == 0) return false; // Clean close
        if (r < 0) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT || err == WSAEINTR) continue; // Retry on timeout/interrupt
            return false; // Real error — disconnect
        }
        total += r;
    }
    return true;
}

// Helper: check if path is safe (no traversal beyond root)
static bool is_safe_path(const std::string& web_root, const std::string& requested_path) {
    try {
        std::filesystem::path root = std::filesystem::weakly_canonical(web_root);
        std::filesystem::path full = std::filesystem::weakly_canonical(web_root + requested_path);
        // Verify that the canonical full path starts with the canonical root
        auto root_str = root.string();
        auto full_str = full.string();
        if (full_str.size() < root_str.size()) return false;
        if (full_str.compare(0, root_str.size(), root_str) != 0) return false;
        // FIX: a bare prefix match also accepted sibling directories such as
        // "web_client_evil". Require a path separator (or exact root) after it.
        if (full_str.size() == root_str.size()) return true;
        char next_char = full_str[root_str.size()];
        return next_char == '\\' || next_char == '/';
    } catch (...) {
        return false;
    }
}

bool SocketServer::handle_http_request(SOCKET client_socket, const std::string& request_str) {
    // BUG-02 FIX: Safely parse the path
    size_t space1 = request_str.find(' ');
    if (space1 == std::string::npos) return false;
    size_t path_start = space1 + 1;
    size_t path_end = request_str.find(' ', path_start);
    if (path_end == std::string::npos) return false;

    std::string path = request_str.substr(path_start, path_end - path_start);
    if (path == "/" || path.empty() || path.rfind("/?", 0) == 0) {
        path = "/index.html";
    }

    size_t q_pos = path.find('?');
    if (q_pos != std::string::npos) {
        path = path.substr(0, q_pos);
    }

    // If root or index.html is requested, serve the cached embedded web app
    if (path == "/index.html" || path == "/") {
        char header[512];
        int header_len = std::snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %zu\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "Access-Control-Allow-Origin: *\r\n\r\n",
            cached_html_length);
        send(client_socket, header, header_len, 0);
        send(client_socket, cached_embedded_html.data(), static_cast<int>(cached_html_length), 0);
        return true;
    }

    // BUG-10 FIX: Reject path traversal attempts
    if (path.find("..") != std::string::npos || !is_safe_path(web_root_dir, path)) {
        const char* resp = "HTTP/1.1 403 Forbidden\r\nContent-Length: 9\r\n\r\nForbidden";
        send(client_socket, resp, static_cast<int>(strlen(resp)), 0);
        return true;
    }

    // Try loading file from web_root_dir on disk
    std::string full_path = web_root_dir + path;
    std::ifstream file(full_path, std::ios::binary);

    if (file.is_open()) {
        std::ostringstream ss;
        ss << file.rdbuf();
        std::string content = ss.str();

        const char* content_type = "text/html";
        if (path.ends_with(".css")) content_type = "text/css";
        else if (path.ends_with(".js")) content_type = "application/javascript";
        else if (path.ends_with(".json")) content_type = "application/json";
        else if (path.ends_with(".png")) content_type = "image/png";
        else if (path.ends_with(".svg")) content_type = "image/svg+xml";
        else if (path.ends_with(".ico")) content_type = "image/x-icon";
        else if (path.ends_with(".woff2")) content_type = "font/woff2";

        char header[512];
        int header_len = std::snprintf(header, sizeof(header),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: %s\r\n"
            "Content-Length: %zu\r\n"
            "Cache-Control: no-cache, no-store, must-revalidate\r\n"
            "Access-Control-Allow-Origin: *\r\n\r\n",
            content_type, content.size());
        send(client_socket, header, header_len, 0);
        send(client_socket, content.data(), static_cast<int>(content.size()), 0);
        return true;
    }

    // BUG-22 FIX: Return proper 404 for unknown files
    const char* not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
    send(client_socket, not_found, static_cast<int>(strlen(not_found)), 0);
    return true;
}

void SocketServer::handle_websocket_frames(SOCKET client_socket) {
    static constexpr size_t MAX_PAYLOAD_SIZE = 65536; // 64KB max per frame

    // Server-side ping for RTT latency measurement
    auto last_ping_time = std::chrono::steady_clock::now();
    bool ping_in_flight = false;
    auto ping_sent_at = std::chrono::steady_clock::now();
    auto last_frame_time = std::chrono::steady_clock::now(); // FIX: idle watchdog

    // FIX: Set recv timeout ONCE outside the loop (not every iteration = wasted syscalls).
    // 500ms gives the ping check interval slack without false-disconnecting on slow strokes.
    // recv_exact now retries on WSAETIMEDOUT so partial reads survive slow drawing too.
    {
        DWORD timeout_ms = 500;
        setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
    }

    while (is_running.load()) {
        // Periodically send a WebSocket ping for RTT measurement (every 2 seconds)
        auto now = std::chrono::steady_clock::now();
        // FIX: a lost pong left ping_in_flight stuck true forever, permanently
        // disabling latency measurement. Recover after 4 seconds.
        if (ping_in_flight && std::chrono::duration_cast<std::chrono::milliseconds>(now - ping_sent_at).count() >= 4000) {
            ping_in_flight = false;
        }
        if (!ping_in_flight && std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ping_time).count() >= 2000) {
            // Send ping frame (opcode 0x9) with 8-byte timestamp payload
            uint8_t ping_payload[8];
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
            std::memcpy(ping_payload, &us, 8);
            send_ws_frame(client_socket, 0x9, ping_payload, 8);
            ping_sent_at = now;
            ping_in_flight = true;
            last_ping_time = now;
        }

        uint8_t hdr[2];
        int bytes_read = recv(client_socket, (char*)hdr, 2, 0);
        if (bytes_read == 0) break; // Clean close
        if (bytes_read < 0) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) {
                // FIX: a silently-dead connection (phone slept / cable unplug)
                // used to spin here FOREVER, holding the input slot and blocking
                // new connections. Live browsers pong our pings every ~2s, so
                // 15s of total silence reliably means the client is gone.
                auto silent_for = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - last_frame_time).count();
                if (silent_for > 15000) break;
                continue; // Just a timeout, retry
            }
            break; // Real error
        }
        last_frame_time = std::chrono::steady_clock::now();
        if (bytes_read < 2) {
            // Partial read — try to get the second byte
            if (!recv_exact(client_socket, (char*)&hdr[1], 1)) break;
        }

        uint8_t opcode = hdr[0] & 0x0F;
        bool is_masked = (hdr[1] & 0x80) != 0;
        uint64_t payload_len = hdr[1] & 0x7F;

        // Handle close frame — BUG-11 FIX: send close frame back per RFC 6455
        if (opcode == 0x8) {
            send_ws_frame(client_socket, 0x8, nullptr, 0);
            break;
        }

        // Read extended length — BUG-04 & BUG-05 FIX: properly assign and check recv
        if (payload_len == 126) {
            uint8_t ext[2];
            if (!recv_exact(client_socket, (char*)ext, 2)) break;
            payload_len = (static_cast<uint16_t>(ext[0]) << 8) | ext[1];
        } else if (payload_len == 127) {
            uint8_t ext[8];
            if (!recv_exact(client_socket, (char*)ext, 8)) break;
            payload_len = 0;
            for (int i = 0; i < 8; ++i) {
                payload_len = (payload_len << 8) | ext[i];
            }
        }

        // BUG-23 FIX: Reject oversized payloads
        if (payload_len > MAX_PAYLOAD_SIZE) {
            break; // Protocol violation — close connection
        }

        uint8_t mask_key[4] = {0};
        if (is_masked) {
            if (!recv_exact(client_socket, (char*)mask_key, 4)) break;
        }

        std::vector<uint8_t> payload(static_cast<size_t>(payload_len));
        if (payload_len > 0) {
            if (!recv_exact(client_socket, (char*)payload.data(), static_cast<int>(payload_len))) break;

            if (is_masked) {
                for (size_t i = 0; i < payload_len; ++i) {
                    payload[i] ^= mask_key[i % 4];
                }
            }
        }

        // BUG-12 FIX: Handle ping — respond with pong
        if (opcode == 0x9) {
            send_ws_frame(client_socket, 0xA, payload.data(), payload.size());
            continue;
        }

        // Handle pong — measure RTT for latency
        if (opcode == 0xA) {
            if (ping_in_flight) {
                auto pong_received = std::chrono::steady_clock::now();
                double rtt_ms = std::chrono::duration<double, std::milli>(pong_received - ping_sent_at).count();
                {
                    std::lock_guard<std::mutex> lock(stats_mutex);
                    stats.current_latency_ms = rtt_ms / 2.0; // One-way estimate
                }
                ping_in_flight = false;
            }
            continue;
        }

        if (opcode == 0x2) { // Binary Frame
            if (payload.size() == sizeof(TouchPacket)) {
                TouchPacket packet;
                std::memcpy(&packet, payload.data(), sizeof(TouchPacket));

                injector->process_packet(packet);
                packet_counter++;
                {
                    std::lock_guard<std::mutex> lock(stats_mutex);
                    stats.packets_received++;
                }
            }
        }
    }
}

void SocketServer::handle_raw_binary_stream(SOCKET client_socket) {
    TouchPacket packet;
    // FIX: add a receive timeout so a vanished raw client cannot block forever.
    DWORD timeout_ms = 5000;
    setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms));
    while (is_running.load()) {
        int bytes_read = recv(client_socket, (char*)&packet, sizeof(TouchPacket), MSG_WAITALL);
        if (bytes_read != sizeof(TouchPacket)) break;

        injector->process_packet(packet);
        packet_counter++;
        {
            std::lock_guard<std::mutex> lock(stats_mutex);
            stats.packets_received++;
        }
    }
}
