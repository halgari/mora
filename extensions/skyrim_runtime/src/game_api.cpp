#include "mora_skyrim_runtime/game_api.h"

namespace mora_skyrim_runtime {

void MockGameAPI::set(uint32_t t, std::string_view f, const mora::Value& v) {
    calls.push_back({"set", t, std::string(f), v});
}

void MockGameAPI::add(uint32_t t, std::string_view f, const mora::Value& v) {
    calls.push_back({"add", t, std::string(f), v});
}

void MockGameAPI::remove(uint32_t t, std::string_view f, const mora::Value& v) {
    calls.push_back({"remove", t, std::string(f), v});
}

void MockGameAPI::multiply(uint32_t t, std::string_view f, const mora::Value& v) {
    calls.push_back({"multiply", t, std::string(f), v});
}

} // namespace mora_skyrim_runtime
