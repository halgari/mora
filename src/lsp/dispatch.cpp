#include "mora/lsp/dispatch.h"

#include <stdexcept>

namespace mora::lsp {

void Dispatcher::on_request(std::string method, Handler h) {
    entries_[std::move(method)] = {std::move(h), true};
}

void Dispatcher::on_notification(std::string method, Handler h) {
    entries_[std::move(method)] = {std::move(h), false};
}

std::optional<nlohmann::json> Dispatcher::dispatch(Workspace& ws,
                                                   const nlohmann::json& message) {
    const bool has_id = message.contains("id");
    const nlohmann::json id = has_id ? message["id"] : nlohmann::json();

    auto method_it = message.find("method");
    if (method_it == message.end() || !method_it->is_string()) {
        if (has_id) {
            return make_error_response(id, ErrorCode::InvalidRequest,
                                       "missing or non-string \"method\"");
        }
        return std::nullopt;
    }

    auto entry_it = entries_.find(method_it->get<std::string>());
    if (entry_it == entries_.end()) {
        if (has_id) {
            return make_error_response(id, ErrorCode::MethodNotFound,
                                       "method not found");
        }
        return std::nullopt;
    }

    nlohmann::json params;
    if (auto p = message.find("params"); p != message.end()) params = *p;

    Result result;
    try {
        result = entry_it->second.handler(ws, params);
    } catch (const nlohmann::json::exception& e) {
        result = Error{ErrorCode::InvalidParams, e.what()};
    } catch (const std::exception& e) {
        result = Error{ErrorCode::InternalError, e.what()};
    }

    if (!entry_it->second.is_request) {
        // Notification — no response, error or success.
        return std::nullopt;
    }
    if (auto* err = std::get_if<Error>(&result)) {
        return make_error_response(id, err->code, err->message);
    }
    return make_success_response(id, std::get<nlohmann::json>(result));
}

nlohmann::json Dispatcher::make_error_response(const nlohmann::json& id,
                                               ErrorCode code,
                                               std::string_view message) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id.is_null() ? nlohmann::json() : id},
        {"error", {
            {"code", static_cast<int>(code)},
            {"message", std::string(message)},
        }},
    };
}

nlohmann::json Dispatcher::make_success_response(const nlohmann::json& id,
                                                 const nlohmann::json& result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result},
    };
}

} // namespace mora::lsp
