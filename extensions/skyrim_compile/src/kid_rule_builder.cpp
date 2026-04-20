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

mora::Expr make_tagged_form(mora::StringPool& pool, std::string_view payload,
                             const mora::SourceSpan& span) {
    // TaggedLiteralExpr("form", payload) — the `#form` reader globalizes
    // it to a FormIdLiteral during the reader-expansion phase.
    mora::Expr e;
    e.span = span;
    e.data = mora::TaggedLiteralExpr{
        pool.intern("form"), pool.intern(payload), span};
    return e;
}

// Build the right Expr node for a ResolvedRef: EditorIdExpr for
// EditorID refs (and wildcard expansions), TaggedLiteralExpr for
// FormID refs (`0xNNN@Plugin.ext` payload).
mora::Expr expr_from_ref(mora::StringPool& pool, const ResolvedRef& ref,
                          const mora::SourceSpan& span) {
    if (ref.is_tagged_form()) {
        return make_tagged_form(pool, ref.tagged_payload, span);
    }
    return make_editor_id(pool, ref.editor_id, span);
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

// Append `form/keyword(X, <member>)` to body. <member> is built from
// the ResolvedRef — EditorIdExpr for EditorID refs, TaggedLiteralExpr
// for FormID refs.
void append_keyword_clause(std::vector<mora::Clause>& body,
                            mora::StringPool& pool,
                            std::string_view subject_var,
                            const ResolvedRef& member,
                            const mora::SourceSpan& span)
{
    std::vector<mora::Expr> args;
    args.push_back(make_var(pool, subject_var, span));
    args.push_back(expr_from_ref(pool, member, span));
    body.push_back(clause_from_fact(
        make_fact_pattern(pool, "form", "keyword", std::move(args),
                           /*negated*/ false, span)));
}

// Append one trait clause: `[not] <ev.qualifier>/<ev.name>(X, _freshN...)`.
// Each fresh var is a distinct VariableExpr (not DiscardExpr — the
// planner rejects DiscardExpr in args; see is_simple_arg_expr in
// src/eval/rule_planner.cpp) so the scan stays well-formed and the
// value columns remain unused by downstream logic.
void append_trait_clause(std::vector<mora::Clause>& body,
                          mora::StringPool& pool,
                          std::string_view subject_var,
                          const TraitEvidence& ev,
                          bool negated,
                          size_t& fresh_id,
                          const mora::SourceSpan& span)
{
    std::vector<mora::Expr> args;
    args.push_back(make_var(pool, subject_var, span));
    for (int i = 0; i < ev.fresh_var_count; ++i) {
        std::string const fresh = fmt::format("_anon_{}", fresh_id++);
        args.push_back(make_var(pool, fresh, span));
    }
    body.push_back(clause_from_fact(
        make_fact_pattern(pool, ev.qualifier, ev.name, std::move(args),
                           negated, span)));
}

mora::Rule make_skyrim_add_rule(
    mora::StringPool& pool,
    std::string_view subject_var,
    const ResolvedRef& target,
    const mora::SourceSpan& span)
{
    mora::Rule rule;
    rule.kind      = mora::RuleKind::Static;
    rule.qualifier = pool.intern("skyrim");
    rule.name      = pool.intern("add");
    rule.span      = span;

    // Head: (X, :Keyword, <target>)
    rule.head_args.push_back(make_var(pool, subject_var, span));
    rule.head_args.push_back(make_keyword(pool, ":Keyword", span));
    rule.head_args.push_back(expr_from_ref(pool, target, span));
    return rule;
}

} // namespace

std::vector<mora::Rule> build_rules(
    const ResolvedRef&                                     target,
    std::string_view                                       item_type,
    const std::vector<std::vector<ResolvedRef>>&           filter_groups,
    const std::vector<TraitRef>&                           traits,
    const mora::SourceSpan&                                source_span,
    mora::StringPool&                                      pool)
{
    std::vector<mora::Rule> out;
    if (item_type.empty() ||
        (!target.is_editor_id() && !target.is_tagged_form())) {
        return out;
    }

    // How many rules: one per OR-group, or one if no filter at all.
    size_t const n_rules = filter_groups.empty() ? size_t{1} : filter_groups.size();

    // Counter for fresh variable names across all rules from this line
    // — distinct names per rule keep the planner from interpreting
    // duplicate StringIds as same-variable equality filters.
    size_t fresh_id = 0;

    for (size_t gi = 0; gi < n_rules; ++gi) {
        mora::Rule rule = make_skyrim_add_rule(
            pool, "X", target, source_span);

        append_type_clause(rule.body, pool, item_type, "X", source_span);

        for (const TraitRef& t : traits) {
            append_trait_clause(rule.body, pool, "X", t.evidence, t.negated,
                                 fresh_id, source_span);
        }

        if (!filter_groups.empty()) {
            for (const ResolvedRef& member : filter_groups[gi]) {
                append_keyword_clause(rule.body, pool, "X", member,
                                       source_span);
            }
        }

        out.push_back(std::move(rule));
    }

    return out;
}

} // namespace mora_skyrim_compile
