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
    Expr make_var(const char* name);
    Expr make_sym(const std::string& name);

    Clause make_fact(const std::string& fact_name,
                     std::vector<Expr> args, bool negated = false);

    void add_item_filters(const std::string& field, const std::string& item_var,
                          std::vector<Clause>& body);

    std::string resolve_symbol(const FormRef& ref) const;

    StringPool& pool_;
    DiagBag& diags_;
    const FormIdResolver* resolver_;
};

} // namespace mora
