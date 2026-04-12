#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include "mora/import/ini_common.h"
#include <string>
#include <vector>

namespace mora {

class KidParser {
public:
    KidParser(StringPool& pool, DiagBag& diags,
              const FormIdResolver* resolver = nullptr);

    std::vector<Rule> parse_line(const std::string& line,
                                 const std::string& filename, int line_num);
    std::vector<Rule> parse_string(const std::string& content,
                                   const std::string& filename);
    std::vector<Rule> parse_file(const std::string& path);

private:
    Expr make_var(const char* name) { return mora::make_var(pool_, name); }
    Expr make_sym(std::string_view name) { return mora::make_sym(pool_, name); }
    Clause make_fact(std::string_view n, std::vector<Expr> a, bool neg = false) {
        return mora::make_fact(pool_, n, std::move(a), neg);
    }
    std::string resolve_symbol(const FormRef& ref) const {
        return mora::resolve_symbol(ref, resolver_);
    }

    void add_item_filters(const std::string& field, const std::string& item_var,
                          std::vector<Clause>& body);

    StringPool& pool_;
    DiagBag& diags_;
    const FormIdResolver* resolver_;
};

} // namespace mora
