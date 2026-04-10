#include "mora/ast/ast.h"

namespace mora {

std::string Module::get_line(uint32_t line) const {
    if (line == 0 || source.empty()) return "";
    uint32_t current = 1;
    size_t start = 0;
    for (size_t i = 0; i < source.size(); i++) {
        if (current == line) {
            // Find end of this line
            size_t end = source.find('\n', i);
            if (end == std::string::npos) end = source.size();
            return source.substr(i, end - i);
        }
        if (source[i] == '\n') {
            current++;
            start = i + 1;
        }
    }
    if (current == line) {
        return source.substr(start);
    }
    return "";
}

} // namespace mora
