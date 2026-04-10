#include "mora/sema/type_checker.h"
#include <variant>

namespace mora {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

TypeChecker::TypeChecker(StringPool& pool, DiagBag& diags,
                         const NameResolver& resolver)
    : pool_(pool), diags_(diags), resolver_(resolver) {}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TypeChecker::check(Module& mod) {
    for (Rule& rule : mod.rules) {
        check_rule(rule);
    }
}

// ---------------------------------------------------------------------------
// check_rule
// ---------------------------------------------------------------------------

void TypeChecker::check_rule(Rule& rule) {
    // Reset per-rule state
    var_types_.clear();
    var_used_.clear();
    var_def_spans_.clear();

    // Bind head arguments as variables (marked used since they're in the head)
    for (const Expr& arg : rule.head_args) {
        if (auto* var = std::get_if<VariableExpr>(&arg.data)) {
            bind_variable(var->name, MoraType::make(TypeKind::Unknown), arg.span);
            var_used_.insert(var->name.index);
        }
    }

    // Walk body clauses
    for (const Clause& clause : rule.body) {
        std::visit([&](const auto& node) {
            using NodeT = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<NodeT, FactPattern>) {
                check_fact_pattern(node);
            } else if constexpr (std::is_same_v<NodeT, GuardClause>) {
                if (node.expr) check_guard(*node.expr);
            } else if constexpr (std::is_same_v<NodeT, Effect>) {
                check_effect(node);
            } else if constexpr (std::is_same_v<NodeT, ConditionalEffect>) {
                if (node.guard) check_guard(*node.guard);
                check_effect(node.effect);
            }
        }, clause.data);
    }

    // Walk top-level effects
    for (const Effect& eff : rule.effects) {
        check_effect(eff);
    }

    // Walk conditional effects
    for (const ConditionalEffect& ce : rule.conditional_effects) {
        if (ce.guard) check_guard(*ce.guard);
        check_effect(ce.effect);
    }

    check_unused_variables(rule);
}

// ---------------------------------------------------------------------------
// check_fact_pattern
// ---------------------------------------------------------------------------

void TypeChecker::check_fact_pattern(const FactPattern& pattern) {
    const FactSignature* sig = resolver_.lookup_fact(pattern.name);
    if (!sig) return; // already reported by NameResolver

    // Arity check
    if (pattern.args.size() != sig->param_types.size()) {
        diags_.error("E020",
                     std::string("arity mismatch for '") +
                         std::string(pool_.get(pattern.name)) +
                         "': expected " +
                         std::to_string(sig->param_types.size()) +
                         " arguments, got " +
                         std::to_string(pattern.args.size()),
                     pattern.span, "");
        return;
    }

    // Check each argument
    for (size_t i = 0; i < pattern.args.size(); ++i) {
        const Expr& arg = pattern.args[i];
        MoraType expected = sig->param_types[i];

        if (auto* var = std::get_if<VariableExpr>(&arg.data)) {
            MoraType existing = lookup_variable(var->name);
            if (existing.kind == TypeKind::Unknown) {
                // First occurrence — bind to expected type (not "used" yet)
                bind_variable(var->name, expected, arg.span);
            } else {
                // Already bound — this is a use of the variable
                var_used_.insert(var->name.index);
                if (!existing.is_subtype_of(expected) &&
                    !expected.is_subtype_of(existing)) {
                    diags_.error("E021",
                                 std::string("type mismatch for variable '") +
                                     std::string(pool_.get(var->name)) +
                                     "': bound as " + existing.to_string() +
                                     " but used as " + expected.to_string(),
                                 arg.span, "");
                }
            }
        } else if (std::get_if<DiscardExpr>(&arg.data)) {
            // Skip discard
        } else {
            // Literal or symbol — infer type and check
            MoraType actual = infer_expr_type(arg);
            if (actual.kind != TypeKind::Unknown &&
                actual.kind != TypeKind::Error &&
                !actual.is_subtype_of(expected)) {
                diags_.error("E022",
                             std::string("type mismatch: expected ") +
                                 expected.to_string() + " but got " +
                                 actual.to_string(),
                             arg.span, "");
            }
        }
    }
}

// ---------------------------------------------------------------------------
// check_effect
// ---------------------------------------------------------------------------

void TypeChecker::check_effect(const Effect& effect) {
    const FactSignature* sig = resolver_.lookup_fact(effect.action);
    if (!sig) return;

    if (effect.args.size() != sig->param_types.size()) {
        diags_.error("E020",
                     std::string("arity mismatch for '") +
                         std::string(pool_.get(effect.action)) +
                         "': expected " +
                         std::to_string(sig->param_types.size()) +
                         " arguments, got " +
                         std::to_string(effect.args.size()),
                     effect.span, "");
        return;
    }

    // Mark variables as used
    for (const Expr& arg : effect.args) {
        if (auto* var = std::get_if<VariableExpr>(&arg.data)) {
            var_used_.insert(var->name.index);
        }
    }
}

// ---------------------------------------------------------------------------
// check_guard — recursively mark variables as used
// ---------------------------------------------------------------------------

void TypeChecker::check_guard(const Expr& expr) {
    std::visit([&](const auto& node) {
        using NodeT = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<NodeT, VariableExpr>) {
            var_used_.insert(node.name.index);
        } else if constexpr (std::is_same_v<NodeT, BinaryExpr>) {
            if (node.left) check_guard(*node.left);
            if (node.right) check_guard(*node.right);
        }
    }, expr.data);
}

// ---------------------------------------------------------------------------
// infer_expr_type
// ---------------------------------------------------------------------------

MoraType TypeChecker::infer_expr_type(const Expr& expr) {
    return std::visit([&](const auto& node) -> MoraType {
        using NodeT = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<NodeT, VariableExpr>) {
            var_used_.insert(node.name.index);
            return lookup_variable(node.name);
        } else if constexpr (std::is_same_v<NodeT, IntLiteral>) {
            return MoraType::make(TypeKind::Int);
        } else if constexpr (std::is_same_v<NodeT, FloatLiteral>) {
            return MoraType::make(TypeKind::Float);
        } else if constexpr (std::is_same_v<NodeT, StringLiteral>) {
            return MoraType::make(TypeKind::String);
        } else if constexpr (std::is_same_v<NodeT, SymbolExpr>) {
            return MoraType::make(TypeKind::Unknown);
        } else if constexpr (std::is_same_v<NodeT, DiscardExpr>) {
            return MoraType::make(TypeKind::Unknown);
        } else if constexpr (std::is_same_v<NodeT, BinaryExpr>) {
            MoraType left = node.left ? infer_expr_type(*node.left)
                                      : MoraType::make(TypeKind::Unknown);
            MoraType right = node.right ? infer_expr_type(*node.right)
                                        : MoraType::make(TypeKind::Unknown);

            switch (node.op) {
                case BinaryExpr::Op::Eq:
                case BinaryExpr::Op::Neq:
                case BinaryExpr::Op::Lt:
                case BinaryExpr::Op::Gt:
                case BinaryExpr::Op::LtEq:
                case BinaryExpr::Op::GtEq:
                    return MoraType::make(TypeKind::Bool);
                case BinaryExpr::Op::Add:
                case BinaryExpr::Op::Sub:
                case BinaryExpr::Op::Mul:
                case BinaryExpr::Op::Div:
                    if (left.kind == TypeKind::Float ||
                        right.kind == TypeKind::Float) {
                        return MoraType::make(TypeKind::Float);
                    }
                    return MoraType::make(TypeKind::Int);
            }
            return MoraType::make(TypeKind::Unknown);
        } else {
            return MoraType::make(TypeKind::Unknown);
        }
    }, expr.data);
}

// ---------------------------------------------------------------------------
// Variable management
// ---------------------------------------------------------------------------

void TypeChecker::bind_variable(StringId name, MoraType type,
                                const SourceSpan& span) {
    auto it = var_types_.find(name.index);
    if (it == var_types_.end()) {
        var_types_.emplace(name.index, type);
        var_def_spans_.emplace(name.index, span);
    } else if (it->second.kind == TypeKind::Unknown) {
        it->second = type;
    }
}

MoraType TypeChecker::lookup_variable(StringId name) const {
    auto it = var_types_.find(name.index);
    if (it == var_types_.end()) return MoraType::make(TypeKind::Unknown);
    return it->second;
}

void TypeChecker::check_unused_variables(const Rule& rule) {
    for (const auto& [id, span] : var_def_spans_) {
        if (var_used_.count(id)) continue;

        // Look up the name to check for "_" prefix
        StringId sid;
        sid.index = id;
        std::string_view name = pool_.get(sid);
        if (name == "_" || (!name.empty() && name[0] == '_')) continue;

        diags_.warning("W007",
                       std::string("unused variable '") +
                           std::string(name) + "'",
                       span, "");
    }
}

} // namespace mora
