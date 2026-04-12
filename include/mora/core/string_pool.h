#pragma once
#include <cstdint>
#include <deque>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mora {

struct StringId {
    uint32_t index = 0;
    bool operator==(const StringId& other) const = default;
    bool operator!=(const StringId& other) const = default;
    explicit operator bool() const { return index != 0; }
};

class StringPool {
public:
    StringPool();
    StringId intern(std::string_view str);
    std::string_view get(StringId id) const;
    size_t size() const;

private:
    // std::deque provides pointer/reference stability on push_back, which
    // keeps the string_view keys in lookup_ valid across insertions.
    std::deque<std::string> strings_;
    std::unordered_map<std::string_view, StringId> lookup_;
    mutable std::shared_mutex mu_;
};

} // namespace mora
