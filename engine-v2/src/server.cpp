#include "server.hpp"
#include "input_injector.hpp"

#include <chrono>
#include <cstring>
#include <iostream>

TcpServer::TcpServer(int port, std::shared_ptr<InputInjector> injector)
    : port_(port), injector_(std::move(injector)) {}

TcpServer::~TcpServer() { stop(); }

bool TcpServer::start() {
    listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCKET) {
        std::cout << "[!] socket() failed: " << WSAGetLastError() << "\n";
        return false;
    }
    BOOL reuse = TRUE;
    setsockopt(listen_socket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // USB tunnel only - no LAN exposure
    addr.sin_port = htons((u_short)port_);

    if (bind(listen_socket_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cout << "[!] bind() failed on port " << port_ << ": " << WSAGetLastError() << "\n";
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
        return false;
    }
    if (listen(listen_socket_, 4) == SOCKET_ERROR) {
        std::cout << "[!] listen() failed: " << WSAGetLastError() << "\n";
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
        return false;
    }

    running_.store(true);
    accept_thread_ = std::thread(&TcpServer::accept_loop, this);

    // packets-per-second sampler
    std::thread([this]() {
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            std::lock_guard<std::mutex> lock(stats_mutex_);
            packets_per_sec_.store(window_packets_);
            window_packets_ = 0;
        }
    }).detach();

    return true;
}

void TcpServer::stop() {
    if (!running_.exchange(false)) return;
    if (listen_socket_ != INVALID_SOCKET) {
        closesocket(listen_socket_);
        listen_socket_ = INVALID_SOCKET;
    }
    {
        std::lock_guard<std::mutex> lock(client_mutex_);
        if (client_socket_ != INVALID_SOCKET) {
            closesocket(client_socket_); // unblocks the client loop
            client_socket_ = INVALID_SOCKET;
        }
    }
    if (accept_thread_.joinable()) accept_thread_.join();
}

TcpServer::Stats TcpServer::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    Stats s;
    s.packets_per_sec = packets_per_sec_.load();
    s.total_packets = total_packets_;
    s.client_connected = input_client_active_.load();
    return s;
}

void TcpServer::push_config(const TouchPacket& packet) {
    std::lock_guard<std::mutex> lock(client_mutex_);
    if (client_socket_ != INVALID_SOCKET) {
        send(client_socket_, (const char*)&packet, sizeof(packet), 0);
    }
}

void TcpServer::accept_loop() {
    while (running_.load()) {
        sockaddr_in client_addr{};
        int len = sizeof(client_addr);
        SOCKET client = accept(listen_socket_, (sockaddr*)&client_addr, &len);
        if (client == INVALID_SOCKET) {
            if (running_.load()) continue;
            break;
        }

        // One injecting client at a time.
        bool expected = false;
        if (!input_client_active_.compare_exchange_strong(expected, true)) {
            closesocket(client);
            continue;
        }

        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            client_socket_ = client;
        }
        injector_->reset_state();

        client_loop(client);

        {
            std::lock_guard<std::mutex> lock(client_mutex_);
            client_socket_ = INVALID_SOCKET;
        }
        input_client_active_.store(false);
        injector_->reset_state();
    }
}

bool TcpServer::recv_exact(SOCKET s, char* buf, int n) {
    int off = 0;
    while (off < n) {
        int r = recv(s, buf + off, n - off, 0);
        if (r == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err == WSAETIMEDOUT) continue; // slow drawing, not a disconnect
            return false;
        }
        if (r == 0) return false;
        off += r;
    }
    return true;
}

void TcpServer::client_loop(SOCKET client) {
    // Latency-critical socket settings.
    BOOL nodelay = TRUE;
    setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay, sizeof(nodelay));
    DWORD timeout = 4000; // ms - watchdog for vanished clients
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    TouchPacket packet{};
    while (running_.load()) {
        if (!recv_exact(client, (char*)&packet, sizeof(packet))) break;

        auto type = (PacketType)packet.packet_type;
        if (type == PacketType::PING) {
            // App latency probe: echo the timestamp straight back.
            TouchPacket pong{};
            pong.packet_type = (uint8_t)PacketType::PONG;
            pong.client_time = packet.client_time;
            send(client, (const char*)&pong, sizeof(pong), 0);
            continue;
        }

        injector_->process_packet(packet);

        std::lock_guard<std::mutex> lock(stats_mutex_);
        total_packets_++;
        window_packets_++;
    }
    closesocket(client);
}
