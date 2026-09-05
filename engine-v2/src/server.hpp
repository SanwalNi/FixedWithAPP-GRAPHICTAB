#pragma once
// ============================================================================
//  Alamy Engine v2 - TCP server
//  Raw 16-byte binary packet stream (no HTTP, no WebSocket framing).
//  Handles PING/PONG latency probes and bidirectional CONFIG_SYNC.
// ============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "protocol.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class InputInjector;

class TcpServer {
public:
    struct Stats {
        uint32_t packets_per_sec = 0;
        uint64_t total_packets = 0;
        bool client_connected = false;
    };

    TcpServer(int port, std::shared_ptr<InputInjector> injector);
    ~TcpServer();

    bool start();
    void stop();
    Stats get_stats() const;

    /** Send a CONFIG_SYNC packet to the connected app (hotkey changes). */
    void push_config(const TouchPacket& packet);

private:
    void accept_loop();
    void client_loop(SOCKET client);
    static bool recv_exact(SOCKET s, char* buf, int n);

    int port_;
    std::shared_ptr<InputInjector> injector_;
    SOCKET listen_socket_ = INVALID_SOCKET;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;

    // active raw client (for push_config); one injecting client at a time
    std::mutex client_mutex_;
    SOCKET client_socket_ = INVALID_SOCKET;
    std::atomic<bool> input_client_active_{false};

    mutable std::mutex stats_mutex_;
    uint64_t total_packets_ = 0;
    uint32_t window_packets_ = 0;
    std::atomic<uint32_t> packets_per_sec_{0};
};
