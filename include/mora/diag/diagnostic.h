#pragma once
#include "mora/core/source_location.h"
#include <mutex>
#include <string>
#include <vector>

namespace mora {

enum class DiagLevel { Error, Warning, Note };

struct Diagnostic {
    DiagLevel level = DiagLevel::Error;
    std::string code;
    std::string message;
    SourceSpan span;
    std::string source_line;
    std::vector<std::string> notes;
};

class DiagBag {
public:
    static constexpr size_t kMaxErrors   = 50;
    static constexpr size_t kMaxWarnings = 50;

    void error(const std::string& code, const std::string& msg,
               const SourceSpan& span, const std::string& source_line);
    void warning(const std::string& code, const std::string& msg,
                 const SourceSpan& span, const std::string& source_line);
    void add(Diagnostic diag);

    size_t error_count() const { return error_count_; }
    size_t warning_count() const { return warning_count_; }
    bool has_errors() const { return error_count_ > 0; }
    bool at_error_limit() const { return error_count_ >= kMaxErrors; }
    const std::vector<Diagnostic>& all() const { return diags_; }

private:
    std::vector<Diagnostic> diags_;
    size_t error_count_ = 0;
    size_t warning_count_ = 0;
    mutable std::mutex mu_;
};

} // namespace mora
