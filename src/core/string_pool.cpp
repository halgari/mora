#include "mora/core/string_pool.h"

namespace mora {

StringPool::StringPool() {
    strings_.emplace_back("");
    lookup_[strings_[0]] = StringId{0};
}

StringId StringPool::intern(std::string_view str) {
    // Fast path: already interned (shared/read lock).
    {
        std::shared_lock lock(mu_);
        auto it = lookup_.find(str);
        if (it != lookup_.end()) {
            return it->second;
        }
    }

    // Slow path: insert (exclusive/write lock).
    std::unique_lock lock(mu_);
    // Re-check after acquiring write lock.
    auto it = lookup_.find(str);
    if (it != lookup_.end()) {
        return it->second;
    }

    strings_.push_back(std::string(str));
    StringId id{static_cast<uint32_t>(strings_.size() - 1)};
    lookup_[strings_.back()] = id;
    return id;
}

std::string_view StringPool::get(StringId id) const {
    std::shared_lock lock(mu_);
    return strings_[id.index];
}

size_t StringPool::size() const {
    std::shared_lock lock(mu_);
    return strings_.size();
}

} // namespace mora
