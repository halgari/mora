#pragma once
#include "mora/diag/diagnostic.h"
#include "mora/cli/terminal.h"
#include <string>

namespace mora {

class DiagRenderer {
public:
    explicit DiagRenderer(bool use_color = true) : color_(use_color) {}
    std::string render(const Diagnostic& diag) const;
    std::string render_all(const DiagBag& bag) const;

private:
    bool color_;
    std::string level_str(DiagLevel level) const;
};

} // namespace mora
