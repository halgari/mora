#include "mora/harness/tcp_listener.h"

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace mora::harness {

TcpListener::TcpListener(uint16_t port) : port_(port) {}

TcpListener::~TcpListener() {
    stop();
}

void TcpListener::on(const std::string& prefix, CommandHandler handler) {
    handlers_.push_back({prefix, std::move(handler)});
}

bool TcpListener::start() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) { WSACleanup(); return false; }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port_);

    if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return false;
    }

    if (listen(sock, 1) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return false;
    }

    listen_socket_ = reinterpret_cast<void*>(sock);
    running_ = true;
    thread_ = std::make_unique<std::thread>(&TcpListener::listen_loop, this);

    return true;
}

void TcpListener::stop() {
    running_ = false;
    if (listen_socket_) {
        closesocket(reinterpret_cast<SOCKET>(listen_socket_));
        listen_socket_ = nullptr;
    }
    if (thread_) {
        if (thread_->joinable()) thread_->join();
        thread_.reset();
    }
    WSACleanup();
}

void TcpListener::listen_loop() {
    SOCKET sock = reinterpret_cast<SOCKET>(listen_socket_);
    while (running_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeval tv{1, 0};

        int ready = select(0, &fds, nullptr, nullptr, &tv);
        if (ready <= 0) continue;

        SOCKET client = accept(sock, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;

        handle_connection(reinterpret_cast<void*>(client));
    }
}

void TcpListener::handle_connection(void* client_socket) {
    SOCKET client = reinterpret_cast<SOCKET>(client_socket);
    std::string buffer;
    char chunk[1024];

    while (running_) {
        int n = recv(client, chunk, sizeof(chunk), 0);
        if (n <= 0) break;

        buffer.append(chunk, n);

        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            std::string response = dispatch(line) + "\n";
            send(client, response.c_str(), static_cast<int>(response.size()), 0);

            if (line == "quit") {
                closesocket(client);
                running_ = false;
                return;
            }
        }
    }

    closesocket(client);
}

std::string TcpListener::dispatch(const std::string& command) {
    for (const auto& entry : handlers_) {
        if (command == entry.prefix ||
            command.substr(0, entry.prefix.size() + 1) == entry.prefix + " ") {
            return entry.handler(command);
        }
    }
    return R"({"ok":false,"error":"unknown command"})";
}

} // namespace mora::harness

#else
// Linux stub — TCP listener is Windows-only (runs inside Skyrim)
namespace mora::harness {

TcpListener::TcpListener(uint16_t) : port_(0) {}
TcpListener::~TcpListener() {}
void TcpListener::on(const std::string&, const CommandHandler&) {}
bool TcpListener::start() { return false; }
void TcpListener::stop() {}
std::string TcpListener::dispatch(const std::string&) { return ""; }
void TcpListener::listen_loop() {}
void TcpListener::handle_connection(void*) {}

} // namespace mora::harness
#endif
