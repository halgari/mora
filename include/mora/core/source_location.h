#pragma once
#include <cstdint>
#include <string>

namespace mora {

struct SourceSpan {
    std::string file;
    uint32_t start_line = 0;
    uint32_t start_col = 0;
    uint32_t end_line = 0;
    uint32_t end_col = 0;
};

SourceSpan merge_spans(const SourceSpan& a, const SourceSpan& b);

} // namespace mora
