#include "mora/core/source_location.h"

namespace mora {

SourceSpan merge_spans(const SourceSpan& a, const SourceSpan& b) {
    return SourceSpan{
        a.file,
        a.start_line,
        a.start_col,
        b.end_line,
        b.end_col
    };
}

} // namespace mora
