#include "mora/sema/keyword_intern.h"

namespace mora {

KeywordId KeywordInterner::intern(std::string_view s) {
    std::string key{s};
    auto it = ids_.find(key);
    if (it != ids_.end()) return it->second;
    KeywordId id = static_cast<KeywordId>(names_.size());
    names_.push_back(key);
    ids_[key] = id;
    return id;
}

std::string_view KeywordInterner::name(KeywordId id) const {
    return names_[id];
}

} // namespace mora
