#include "mora/lsp/symbol_index.h"
#include "mora/ast/ast.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include <limits>
#include <unordered_map>

namespace mora::lsp {

namespace {

// Forward declaration.
void walk_expr(const mora::Expr& expr,
               const mora::TypeChecker& tc,
               std::unordered_map<uint32_t, mora::SourceSpan>& bindings_seen,
               std::vector<SymbolEntry>& out);

void emit_variable(const mora::VariableExpr& var,
                   const mora::SourceSpan& span,
                   std::unordered_map<uint32_t, mora::SourceSpan>& bindings_seen,
                   std::vector<SymbolEntry>& out) {
    SymbolEntry e;
    e.span = span;
    e.name = var.name;
    // StringId keyed by .index (not .value — StringId has no .value field)
    auto def_it = bindings_seen.find(var.name.index);
    if (def_it == bindings_seen.end()) {
        e.kind = SymbolKind::VariableBinding;
        e.binding_span = span;
        bindings_seen.emplace(var.name.index, span);
    } else {
        e.kind = SymbolKind::VariableUse;
        e.binding_span = def_it->second;
    }
    out.push_back(e);
}

void walk_expr(const mora::Expr& expr,
               const mora::TypeChecker& tc,
               std::unordered_map<uint32_t, mora::SourceSpan>& bindings_seen,
               std::vector<SymbolEntry>& out) {
    // VariableExpr (plan called it mora::Variable)
    if (const auto* var = std::get_if<mora::VariableExpr>(&expr.data)) {
        emit_variable(*var, var->span, bindings_seen, out);
    }
    // EditorIdExpr (plan called it mora::AtomLiteral)
    else if (const auto* atom = std::get_if<mora::EditorIdExpr>(&expr.data)) {
        SymbolEntry e;
        e.kind = SymbolKind::Atom;
        e.span = atom->span;
        e.name = atom->name;
        out.push_back(e);
    }
    // BinaryExpr: recurse into both sides
    else if (const auto* bin = std::get_if<mora::BinaryExpr>(&expr.data)) {
        if (bin->left)  walk_expr(*bin->left,  tc, bindings_seen, out);
        if (bin->right) walk_expr(*bin->right, tc, bindings_seen, out);
    }
    // CallExpr: recurse into args
    else if (const auto* call = std::get_if<mora::CallExpr>(&expr.data)) {
        for (const auto& arg : call->args) {
            walk_expr(arg, tc, bindings_seen, out);
        }
    }
    // Other expression kinds (IntLiteral, FloatLiteral, StringLiteral, SymbolExpr,
    // DiscardExpr) carry no sub-expressions to walk.
}

// Approximate name span from FactPattern: the name starts at the pattern's
// span start. We don't have a dedicated name_span field (ast.h only has span).
// We treat the name as occupying the start of the full pattern span.
mora::SourceSpan fact_name_span(const mora::FactPattern& fp,
                                const mora::StringPool& pool) {
    // The name portion begins at fp.span.start_*. Length = interned string length.
    std::string_view name_str = pool.get(fp.name);
    if (name_str.empty()) return fp.span;
    mora::SourceSpan s;
    s.file       = fp.span.file;
    s.start_line = fp.span.start_line;
    s.start_col  = fp.span.start_col;
    s.end_line   = fp.span.start_line;
    // end_col is inclusive last char (mora convention: start_col + len - 1).
    s.end_col    = fp.span.start_col + static_cast<uint32_t>(name_str.size()) - 1;
    return s;
}

void walk_effect(const mora::Effect& eff,
                 const mora::TypeChecker& tc,
                 std::unordered_map<uint32_t, mora::SourceSpan>& bindings_seen,
                 std::vector<SymbolEntry>& out) {
    for (const auto& arg : eff.args) {
        walk_expr(arg, tc, bindings_seen, out);
    }
}

void walk_clause(const mora::Clause& clause,
                 const mora::NameResolver& resolver,
                 const mora::TypeChecker& tc,
                 const mora::StringPool& pool,
                 std::unordered_map<uint32_t, mora::SourceSpan>& bindings_seen,
                 std::vector<SymbolEntry>& out) {
    if (const auto* fp = std::get_if<mora::FactPattern>(&clause.data)) {
        // Emit a Relation (or RuleCall) entry for fp->name.
        // fp->qualifier holds the namespace prefix (e.g. "form" in form/npc).
        // There is no separate name_span — approximate from fp->span start.
        SymbolEntry e;
        // Distinguish: if this fact is registered as a user rule, emit RuleCall;
        // otherwise emit Relation.
        const FactSignature* sig = resolver.lookup_fact(fp->name);
        bool is_rule = sig != nullptr && resolver.rules().count(fp->name.index) > 0;
        e.kind = is_rule ? SymbolKind::RuleCall : SymbolKind::Relation;
        e.span = fact_name_span(*fp, pool);
        e.name = fp->name;
        // qualifier is the namespace prefix (e.g. StringId for "form")
        if (fp->qualifier.index != 0) {
            e.ns_path = std::string(pool.get(fp->qualifier));
        }
        out.push_back(e);

        for (const auto& arg : fp->args) {
            walk_expr(arg, tc, bindings_seen, out);
        }
    }
    else if (const auto* guard = std::get_if<mora::GuardClause>(&clause.data)) {
        if (guard->expr) walk_expr(*guard->expr, tc, bindings_seen, out);
    }
    else if (const auto* or_cl = std::get_if<mora::OrClause>(&clause.data)) {
        // OrClause has branches of FactPattern vectors.
        for (const auto& branch : or_cl->branches) {
            for (const auto& fp2 : branch) {
                // Inline fact pattern handling for or-branches.
                SymbolEntry e;
                const FactSignature* sig2 = resolver.lookup_fact(fp2.name);
                bool is_rule2 = sig2 != nullptr && resolver.rules().count(fp2.name.index) > 0;
                e.kind = is_rule2 ? SymbolKind::RuleCall : SymbolKind::Relation;
                e.span = fact_name_span(fp2, pool);
                e.name = fp2.name;
                if (fp2.qualifier.index != 0) {
                    e.ns_path = std::string(pool.get(fp2.qualifier));
                }
                out.push_back(e);
                for (const auto& arg : fp2.args) {
                    walk_expr(arg, tc, bindings_seen, out);
                }
            }
        }
    }
    else if (const auto* in_cl = std::get_if<mora::InClause>(&clause.data)) {
        if (in_cl->variable) walk_expr(*in_cl->variable, tc, bindings_seen, out);
        for (const auto& val : in_cl->values) {
            walk_expr(val, tc, bindings_seen, out);
        }
    }
    else if (const auto* eff = std::get_if<mora::Effect>(&clause.data)) {
        walk_effect(*eff, tc, bindings_seen, out);
    }
    else if (const auto* cond_eff = std::get_if<mora::ConditionalEffect>(&clause.data)) {
        if (cond_eff->guard) walk_expr(*cond_eff->guard, tc, bindings_seen, out);
        walk_effect(cond_eff->effect, tc, bindings_seen, out);
    }
}

} // anonymous namespace

void SymbolIndex::build(const mora::Module& mod,
                        const mora::NameResolver& resolver,
                        const mora::TypeChecker& tc,
                        const mora::StringPool& pool) {
    entries_.clear();

    // 1. Namespace declaration.
    if (mod.ns) {
        SymbolEntry e;
        e.kind = SymbolKind::Namespace;
        e.span = mod.ns->span;
        e.name = mod.ns->name;
        entries_.push_back(e);
    }

    // 2. Per rule: head + variables in head args + body clauses + effects.
    for (const auto& rule : mod.rules) {
        // Rule head entry — use rule.span (no separate name_span in ast.h).
        SymbolEntry head;
        head.kind = SymbolKind::RuleHead;
        head.span = rule.span;
        head.name = rule.name;
        entries_.push_back(head);

        // Per-rule binding tracker (keyed by StringId::index).
        std::unordered_map<uint32_t, mora::SourceSpan> bindings_seen;

        // Head args: variables here are always bindings (first occurrences).
        for (const auto& arg : rule.head_args) {
            walk_expr(arg, tc, bindings_seen, entries_);
        }

        // Body clauses.
        for (const auto& cl : rule.body) {
            walk_clause(cl, resolver, tc, pool, bindings_seen, entries_);
        }

        // Top-level effects.
        for (const auto& eff : rule.effects) {
            walk_effect(eff, tc, bindings_seen, entries_);
        }

        // Conditional effects.
        for (const auto& cond_eff : rule.conditional_effects) {
            if (cond_eff.guard) walk_expr(*cond_eff.guard, tc, bindings_seen, entries_);
            walk_effect(cond_eff.effect, tc, bindings_seen, entries_);
        }
    }
}

const SymbolEntry* SymbolIndex::find_at(uint32_t line, uint32_t col) const {
    const SymbolEntry* best = nullptr;
    uint64_t best_size = std::numeric_limits<uint64_t>::max();
    for (const auto& e : entries_) {
        if (line < e.span.start_line || line > e.span.end_line) continue;
        if (line == e.span.start_line && col < e.span.start_col) continue;
        if (line == e.span.end_line   && col > e.span.end_col)   continue;
        // Compute "size" = (end_line - start_line) * 2^32 + (end_col - start_col)
        // so multi-line spans always rank as larger than single-line spans.
        uint64_t size =
            (static_cast<uint64_t>(e.span.end_line - e.span.start_line) << 32) +
            static_cast<uint64_t>(e.span.end_col - e.span.start_col);
        if (size < best_size) {
            best = &e;
            best_size = size;
        }
    }
    return best;
}

} // namespace mora::lsp
