#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mora::harness {

// Command handler: receives the full command line (e.g. "dump weapons"),
// returns a JSON response string.
using CommandHandler = std::function<std::string(const std::string& command)>;

class TcpListener {
public:
    explicit TcpListener(uint16_t port);
    ~TcpListener();

    // Register a handler for a command prefix (e.g. "dump", "status", "quit").
    void on(const std::string& prefix, CommandHandler handler);

    // Start listening in a background thread. Non-blocking.
    bool start();

    // Stop the listener and close all sockets.
    void stop();

    bool running() const { return running_; }

private:
    void listen_loop();
    void handle_connection(void* client_socket);
    std::string dispatch(const std::string& command);

    uint16_t port_;
    bool running_ = false;

#ifdef _WIN32
    void* listen_socket_ = nullptr;
    void* thread_ = nullptr;
#endif

    struct HandlerEntry {
        std::string prefix;
        CommandHandler handler;
    };
    std::vector<HandlerEntry> handlers_;
};

} // namespace mora::harness
