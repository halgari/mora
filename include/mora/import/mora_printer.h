#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include <string>

namespace mora {

class MoraPrinter {
public:
    explicit MoraPrinter(StringPool& pool) : pool_(pool) {}

    std::string print_rule(const Rule& rule) const;
    std::string print_comment(const std::string& text) const;

private:
    std::string print_expr(const Expr& expr) const;
    std::string print_fact_pattern(const FactPattern& fp) const;
    std::string print_effect(const Effect& eff) const;

    StringPool& pool_;
};

} // namespace mora
