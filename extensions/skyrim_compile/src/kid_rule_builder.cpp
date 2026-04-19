#include "mora_skyrim_compile/kid_rule_builder.h"

#include <fmt/format.h>

namespace mora_skyrim_compile {

namespace {

mora::Expr make_var(mora::StringPool& pool, std::string_view name,
                     const mora::SourceSpan& span) {
    mora::Expr e;
    e.span = span;
    e.data = mora::VariableExpr{pool.intern(name), span};
    return e;
}

mora::Expr make_keyword(mora::StringPool& pool, std::string_view text,
                         const mora::SourceSpan& span) {
    // KeywordLiteral stores the text including the leading colon — same
    // convention the lexer follows (see src/lexer/lexer.cpp `lex_colon`).
    mora::Expr e;
    e.span = span;
    e.data = mora::KeywordLiteral{pool.intern(text), span};
    return e;
}

mora::Expr make_editor_id(mora::StringPool& pool, std::string_view name,
                           const mora::SourceSpan& span) {
    // EditorIdExpr stores the name WITHOUT a leading '@' — the lexer
    // strips it (see src/lexer/lexer.cpp around the `@` branch).
    mora::Expr e;
    e.span = span;
    e.data = mora::EditorIdExpr{pool.intern(name), span};
    return e;
}

mora::FactPattern make_fact_pattern(
    mora::StringPool& pool,
    std::string_view qualifier,
    std::string_view name,
    std::vector<mora::Expr> args,
    bool negated,
    const mora::SourceSpan& span)
{
    mora::FactPattern fp;
    fp.qualifier = pool.intern(qualifier);
    fp.name      = pool.intern(name);
    fp.args      = std::move(args);
    fp.negated   = negated;
    fp.span      = span;
    return fp;
}

mora::Clause clause_from_fact(mora::FactPattern fp) {
    mora::Clause c;
    c.span = fp.span;
    c.data = std::move(fp);
    return c;
}

// Append `form/<item_type>(X)` to body.
void append_type_clause(std::vector<mora::Clause>& body,
                         mora::StringPool& pool,
                         std::string_view item_type,
                         std::string_view subject_var,
                         const mora::SourceSpan& span)
{
    std::vector<mora::Expr> args;
    args.push_back(make_var(pool, subject_var, span));
    body.push_back(clause_from_fact(
        make_fact_pattern(pool, "form", item_type, std::move(args),
                           /*negated*/ false, span)));
}

// Append `form/keyword(X, @kw_name)` to body.
void append_keyword_clause(std::vector<mora::Clause>& body,
                            mora::StringPool& pool,
                            std::string_view subject_var,
                            std::string_view keyword_editor_id,
                            const mora::SourceSpan& span)
{
    std::vector<mora::Expr> args;
    args.push_back(make_var(pool, subject_var, span));
    args.push_back(make_editor_id(pool, keyword_editor_id, span));
    body.push_back(clause_from_fact(
        make_fact_pattern(pool, "form", "keyword", std::move(args),
                           /*negated*/ false, span)));
}

// Append `[not] form/enchanted_with(X, V)` to body. `V` is a fresh
// throwaway variable so the planner sees a valid binding — the
// evaluator uses negation-as-failure on existence; using a fresh
// VariableExpr (not DiscardExpr — the planner rejects DiscardExpr in
// args; see is_simple_arg_expr in src/eval/rule_planner.cpp) keeps the
// scan well-formed and the value column unused.
void append_enchanted_with(std::vector<mora::Clause>& body,
                            mora::StringPool& pool,
                            std::string_view subject_var,
                            std::string_view fresh_var,
                            bool negated,
                            const mora::SourceSpan& span)
{
    std::vector<mora::Expr> args;
    args.push_back(make_var(pool, subject_var, span));
    args.push_back(make_var(pool, fresh_var, span));
    body.push_back(clause_from_fact(
        make_fact_pattern(pool, "form", "enchanted_with", std::move(args),
                           negated, span)));
}

mora::Rule make_skyrim_add_rule(
    mora::StringPool& pool,
    std::string_view subject_var,
    std::string_view target_editor_id,
    const mora::SourceSpan& span)
{
    mora::Rule rule;
    rule.kind      = mora::RuleKind::Static;
    rule.qualifier = pool.intern("skyrim");
    rule.name      = pool.intern("add");
    rule.span      = span;

    // Head: (X, :Keyword, @TargetEditorId)
    rule.head_args.push_back(make_var(pool, subject_var, span));
    rule.head_args.push_back(make_keyword(pool, ":Keyword", span));
    rule.head_args.push_back(make_editor_id(pool, target_editor_id, span));
    return rule;
}

} // namespace

std::string compose_synthetic_editor_id(uint32_t formid) {
    // Lowercase 8-hex format keeps the generated name stable across
    // runs and avoids any case-folding surprises in the evaluator's
    // case-insensitive symbol lookup. Real Skyrim EditorIDs never
    // start with `__` so collisions are impossible.
    return fmt::format("__kid_formid_{:08x}", formid);
}

std::vector<mora::Rule> build_rules(
    const ResolvedRef&                                     target,
    std::string_view                                       item_type,
    const std::vector<std::vector<ResolvedRef>>&           filter_groups,
    bool                                                   trait_e,
    bool                                                   trait_neg_e,
    const mora::SourceSpan&                                source_span,
    mora::StringPool&                                      pool)
{
    std::vector<mora::Rule> out;
    if (item_type.empty() || target.editor_id.empty()) return out;

    // How many rules: one per OR-group, or one if no filter at all.
    size_t const n_rules = filter_groups.empty() ? size_t{1} : filter_groups.size();

    // Counter for fresh variable names across all rules from this line
    // — distinct names per rule keep the planner from interpreting
    // duplicate StringIds as same-variable equality filters.
    size_t fresh_id = 0;

    for (size_t gi = 0; gi < n_rules; ++gi) {
        mora::Rule rule = make_skyrim_add_rule(
            pool, "X", target.editor_id, source_span);

        append_type_clause(rule.body, pool, item_type, "X", source_span);

        if (trait_e) {
            std::string fresh = fmt::format("_anon_{}", fresh_id++);
            append_enchanted_with(rule.body, pool, "X", fresh,
                                    /*negated*/ false, source_span);
        }
        if (trait_neg_e) {
            std::string fresh = fmt::format("_anon_{}", fresh_id++);
            append_enchanted_with(rule.body, pool, "X", fresh,
                                    /*negated*/ true, source_span);
        }

        if (!filter_groups.empty()) {
            for (const ResolvedRef& member : filter_groups[gi]) {
                append_keyword_clause(rule.body, pool, "X", member.editor_id,
                                       source_span);
            }
        }

        out.push_back(std::move(rule));
    }

    return out;
}

} // namespace mora_skyrim_compile
