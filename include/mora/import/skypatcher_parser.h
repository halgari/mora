#pragma once

#include "mora/ast/ast.h"
#include "mora/import/ini_common.h"
#include "mora/import/skypatcher_schema.h"
#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"
#include <string>
#include <vector>

namespace mora {

class SkyPatcherParser {
public:
    SkyPatcherParser(StringPool& pool, DiagBag& diags,
                      const FormIdResolver* resolver = nullptr);

    // Parse a single line for a known schema
    std::vector<Rule> parse_line(const std::string& line,
                                  const RecordSchema& schema,
                                  const std::string& filename, int line_num);

    // Overload: determine schema from record type string
    std::vector<Rule> parse_line(const std::string& line,
                                  const std::string& record_type,
                                  const std::string& filename, int line_num);

    // Parse a file (determines record type from parent directory name)
    std::vector<Rule> parse_file(const std::string& path);

    // Parse all SkyPatcher config directories under a base path
    std::vector<Rule> parse_directory(const std::string& base_path);

private:
    // Split "key1=val1:key2=val2" into pairs (keys lowercased)
    struct KVPair { std::string key; std::string value; };
    static std::vector<KVPair> split_kv_pairs(const std::string& line);

    // Split comma-separated values
    static std::vector<std::string> split_commas(const std::string& value);

    // Strip ~tildes~ from a name
    static std::string strip_tildes(const std::string& value);

    // Parse SkyPatcher FormRef: "Plugin.esp|HEXID" or "EditorID"
    FormRef parse_formref(const std::string& text) const;
    std::string resolve_sym(const FormRef& ref) const;

    // ── Generic schema-driven dispatch ───────────────────────────────
    void emit_filter(Rule& rule, FilterKind kind, const std::string& value,
                     const std::string& var);
    void emit_operation(Rule& rule, OpKind kind, SkyField field,
                        const std::string& value, const std::string& var,
                        const SourceSpan& span);

    // ── AST helpers (delegating to ini_common free functions) ────────
    Expr var(const std::string& name) { return mora::make_var(pool_, name.c_str()); }
    Expr sym(std::string_view name) { return mora::make_sym(pool_, name); }
    Expr intlit(int64_t v) { return mora::make_int(v); }
    Expr floatlit(double v) { return mora::make_float(v); }
    Clause fact(std::string_view n, std::vector<Expr> a, bool neg = false) {
        return mora::make_fact(pool_, n, std::move(a), neg);
    }

    // Determine record type from directory path
    static std::string type_from_path(const std::string& path);

    StringPool& pool_;
    DiagBag& diags_;
    const FormIdResolver* resolver_;
};

} // namespace mora
