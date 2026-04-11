#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include "mora/import/ini_common.h"
#include <string>
#include <vector>

namespace mora {

class SpidParser {
public:
    SpidParser(StringPool& pool, DiagBag& diags,
               const FormIdResolver* resolver = nullptr);

    std::vector<Rule> parse_line(const std::string& line,
                                 const std::string& filename, int line_num);
    std::vector<Rule> parse_string(const std::string& content,
                                   const std::string& filename);
    std::vector<Rule> parse_file(const std::string& path);

private:
    Expr make_var(const char* name);
    Expr make_sym(const std::string& name);
    Expr make_int(int64_t value);

    Clause make_fact(const std::string& fact_name,
                     std::vector<Expr> args, bool negated = false);
    Clause make_guard(BinaryExpr::Op op,
                      Expr left, Expr right);

    void add_string_filters(const std::string& field,
                            std::vector<Clause>& body);
    void add_form_filters(const std::string& field,
                          std::vector<Clause>& body);
    void add_level_filters(const std::string& field,
                           std::vector<Clause>& body);

    // Resolve a FormRef to a clean symbol name (EditorID if available, else hex)
    std::string resolve_symbol(const FormRef& ref) const;

    StringPool& pool_;
    DiagBag& diags_;
    const FormIdResolver* resolver_;
};

} // namespace mora
