#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "protocol.hpp"
#include <thread>
#include <atomic>
#include <functional>
#include <string>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include <set>
#include <chrono>

class InputInjector;

class SocketServer {
public:
    struct ServerStats {
        uint32_t packets_received = 0;
        uint32_t packets_per_sec = 0;
        double current_latency_ms = 0.0;
        bool is_client_connected = false;
        std::string client_ip = "";
    };

private:
    int port;
    std::string web_root_dir;
    std::shared_ptr<InputInjector> injector;
    
    SOCKET listen_socket = INVALID_SOCKET;
    std::atomic<bool> is_running{false};
    std::thread server_thread;
    std::thread stats_thread;

    // Thread-safe stats access
    mutable std::mutex stats_mutex;
    ServerStats stats;
    std::atomic<uint32_t> packet_counter{0};

    // Track client threads for clean shutdown (no detach)
    std::mutex clients_mutex;
    // FIX: each thread carries a done flag so finished threads can be reaped
    // (previously the vector grew unboundedly with every phone reconnect).
    std::vector<std::pair<std::thread, std::shared_ptr<std::atomic<bool>>>> client_threads;
    std::vector<std::thread> finished_client_threads;
    std::set<SOCKET> active_client_sockets;

    // FIX: only ONE connection may inject input at a time (reconnect races
    // previously caused doubled/stuttered cursor movement).
    std::atomic<bool> input_client_active{false};

    // Cached embedded HTML (avoid 30KB copy per request)
    std::string cached_embedded_html;
    size_t cached_html_length = 0;

    void run_server();
    void handle_client(SOCKET client_socket, const std::string& client_ip);
    bool handle_http_request(SOCKET client_socket, const std::string& request_str);
    void handle_websocket_frames(SOCKET client_socket);
    void handle_raw_binary_stream(SOCKET client_socket);
    void register_client_socket(SOCKET s);
    void unregister_client_socket(SOCKET s);

    // Helpers for WebSocket Handshake (RFC 6455)
    static std::string generate_ws_accept_key(const std::string& client_key);
    static void send_ws_frame(SOCKET s, uint8_t opcode, const uint8_t* payload, size_t len);

public:
    SocketServer(int listen_port, const std::string& web_root, std::shared_ptr<InputInjector> input_injector);
    ~SocketServer();

    bool start();
    void stop();

    ServerStats get_stats() const;
};
