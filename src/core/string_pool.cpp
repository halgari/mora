#include "mora/core/string_pool.h"

namespace mora {

StringPool::StringPool() {
    // Reserve index 0 with empty string as the null/sentinel entry.
    strings_.emplace_back("");
    lookup_[strings_[0]] = StringId{0};
}

StringId StringPool::intern(std::string_view str) {
    auto it = lookup_.find(str);
    if (it != lookup_.end()) {
        return it->second;
    }

    // std::deque::push_back never invalidates references/pointers to existing
    // elements, so the string_view keys already stored in lookup_ stay valid.
    strings_.push_back(std::string(str));
    StringId id{static_cast<uint32_t>(strings_.size() - 1)};
    lookup_[strings_.back()] = id;
    return id;
}

std::string_view StringPool::get(StringId id) const {
    return strings_[id.index];
}

} // namespace mora
