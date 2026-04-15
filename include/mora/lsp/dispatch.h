#pragma once
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

#include <nlohmann/json.hpp>

namespace mora::lsp {

class Workspace;

// Standard JSON-RPC error codes.
enum class ErrorCode : int {
    ParseError      = -32700,
    InvalidRequest  = -32600,
    MethodNotFound  = -32601,
    InvalidParams   = -32602,
    InternalError   = -32603,
    ServerNotInitialized = -32002,
    RequestCancelled     = -32800,
};

struct Error {
    ErrorCode code;
    std::string message;
};

// A handler returns either a result JSON value (for requests) or an error.
// For notifications the runtime ignores the success value.
using Result = std::variant<nlohmann::json, Error>;

// Signature for a request/notification handler.
using Handler = std::function<Result(Workspace&, const nlohmann::json& params)>;

class Dispatcher {
public:
    // Register a request handler — its Result is sent back as the response.
    void on_request(std::string method, Handler h);
    // Register a notification handler — Result is discarded.
    void on_notification(std::string method, Handler h);

    // Dispatch a parsed message. The optional `id` tells you whether this
    // is a request (id present) or notification (id absent). Returns the
    // response JSON to send back, or std::nullopt for notifications.
    std::optional<nlohmann::json> dispatch(Workspace& ws,
                                           const nlohmann::json& message);

private:
    struct Entry { Handler handler; bool is_request; };
    std::unordered_map<std::string, Entry> entries_;

    static nlohmann::json make_error_response(const nlohmann::json& id,
                                              ErrorCode code,
                                              std::string_view message);
    static nlohmann::json make_success_response(const nlohmann::json& id,
                                                const nlohmann::json& result);
};

} // namespace mora::lsp
