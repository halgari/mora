#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mora {

using KeywordId = uint32_t;

class KeywordInterner {
public:
    KeywordId intern(std::string_view s);
    std::string_view name(KeywordId id) const;
    size_t size() const { return names_.size(); }
private:
    std::unordered_map<std::string, KeywordId> ids_;
    std::vector<std::string> names_;
};

} // namespace mora
