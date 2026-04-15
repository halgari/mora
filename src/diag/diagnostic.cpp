#include "mora/diag/diagnostic.h"
#include "mora/lsp/uri.h"

namespace mora {

void DiagBag::error(const std::string& code, const std::string& msg,
                    const SourceSpan& span, const std::string& source_line) {
    Diagnostic d;
    d.level = DiagLevel::Error;
    d.code = code;
    d.message = msg;
    d.span = span;
    d.source_line = source_line;
    add(std::move(d));
}

void DiagBag::warning(const std::string& code, const std::string& msg,
                      const SourceSpan& span, const std::string& source_line) {
    Diagnostic d;
    d.level = DiagLevel::Warning;
    d.code = code;
    d.message = msg;
    d.span = span;
    d.source_line = source_line;
    add(std::move(d));
}

void DiagBag::add(Diagnostic diag) {
    std::lock_guard lock(mu_);
    if (diag.level == DiagLevel::Error) {
        ++error_count_;
        if (error_count_ > kMaxErrors) return;
    } else if (diag.level == DiagLevel::Warning) {
        ++warning_count_;
        if (warning_count_ > kMaxWarnings) return;
    }
    diags_.push_back(std::move(diag));
}

std::vector<Diagnostic> DiagBag::drain_for_uri(std::string_view uri) {
    std::string path = lsp::path_from_uri(uri);
    std::vector<Diagnostic> out;
    std::lock_guard<std::mutex> g(mu_);
    auto it = diags_.begin();
    while (it != diags_.end()) {
        if (it->span.file == path) {
            if (it->level == DiagLevel::Error)        --error_count_;
            else if (it->level == DiagLevel::Warning) --warning_count_;
            out.push_back(std::move(*it));
            it = diags_.erase(it);
        } else {
            ++it;
        }
    }
    return out;
}

} // namespace mora
