#include "mora/ast/ast.h"

namespace mora {

void Module::build_line_index() const {
    if (!line_offsets_.empty()) return;
    line_offsets_.push_back(0); // line 1 starts at offset 0
    for (size_t i = 0; i < source.size(); i++) {
        if (source[i] == '\n') {
            line_offsets_.push_back(i + 1);
        }
    }
}

std::string Module::get_line(uint32_t line) const {
    if (line == 0 || source.empty()) return "";
    build_line_index();

    uint32_t idx = line - 1; // 1-indexed → 0-indexed
    if (idx >= line_offsets_.size()) return "";

    size_t start = line_offsets_[idx];
    size_t end = (idx + 1 < line_offsets_.size())
        ? line_offsets_[idx + 1]
        : source.size();

    // Strip trailing newline if present
    if (end > start && source[end - 1] == '\n') --end;
    if (end > start && source[end - 1] == '\r') --end;

    return source.substr(start, end - start);
}

} // namespace mora
