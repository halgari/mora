#include "mora/sema/type_checker.h"
#include "mora/data/form_model.h"
#include "mora/model/builtin_fns.h"
#include "mora/model/relations.h"
#include "mora/model/validate.h"
#include <variant>

namespace mora {

namespace {
const char* verb_name_str(VerbKind v) {
    switch (v) {
        case VerbKind::Set:    return "set";
        case VerbKind::Add:    return "add";
        case VerbKind::Sub:    return "sub";
        case VerbKind::Remove: return "remove";
    }
    return "?";
}

model::VerbKind to_model_verb(VerbKind v) {
    switch (v) {
        case VerbKind::Set:    return model::VerbKind::Set;
        case VerbKind::Add:    return model::VerbKind::Add;
        case VerbKind::Sub:    return model::VerbKind::Sub;
        case VerbKind::Remove: return model::VerbKind::Remove;
    }
    return model::VerbKind::Set;
}
} // namespace

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
    current_mod_ = &mod;
    for (Rule& rule : mod.rules) {
        if (diags_.at_error_limit()) break;
        check_rule(rule);
    }
    current_mod_ = nullptr;
}

std::string TypeChecker::source_line(const SourceSpan& span) const {
    if (current_mod_) return current_mod_->get_line(span.start_line);
    return "";
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
            } else if constexpr (std::is_same_v<NodeT, InClause>) {
                if (node.variable) check_guard(*node.variable);
            } else if constexpr (std::is_same_v<NodeT, OrClause>) {
                for (const auto& branch : node.branches) {
                    for (const auto& fp : branch) check_fact_pattern(fp);
                }
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

    // ── Rule-kind constraints (shape-only; mostly dormant until Plan 3) ──
    // event/* relations may only appear in 'on' rules.
    auto check_event_usage = [&](const FactPattern& fp) {
        std::string_view const ns = pool_.get(fp.qualifier);
        if (ns == "event" && rule.kind != RuleKind::On) {
            diags_.error("E036",
                "event/* relations are only allowed in 'on' rules",
                fp.span, source_line(fp.span));
        }
    };
    for (const Clause& clause : rule.body) {
        if (auto* fp = std::get_if<FactPattern>(&clause.data)) {
            check_event_usage(*fp);
        } else if (auto* oc = std::get_if<OrClause>(&clause.data)) {
            for (const auto& branch : oc->branches)
                for (const auto& fp : branch) check_event_usage(fp);
        }
    }

    // Maintain rules: all effects must be retractable.
    if (rule.kind == RuleKind::Maintain) {
        auto check_retractable = [&](const Effect& e) {
            std::string_view const ns = pool_.get(e.namespace_);
            std::string_view const nm = pool_.get(e.name);
            if (ns.empty()) return;
            const auto* rel = model::find_relation(
                ns, nm, model::kRelations, model::kRelationCount);
            if (!rel) return;
            // Scalar / Countable are naturally reversible (set-prev-value bookkeeping).
            if (rel->type.ctor == model::TypeCtor::Scalar ||
                rel->type.ctor == model::TypeCtor::Countable) {
                return;
            }
            if (rel->retract_handler == model::HandlerId::None) {
                diags_.error("E037",
                    std::string("effect '") + std::string(ns) + "/" +
                        std::string(nm) +
                        "' is not retractable and cannot be used in a maintain rule",
                    e.span, source_line(e.span));
            }
        };
        for (const Effect& e : rule.effects) check_retractable(e);
        for (const ConditionalEffect& ce : rule.conditional_effects) {
            check_retractable(ce.effect);
        }
        for (const Clause& clause : rule.body) {
            if (auto* e = std::get_if<Effect>(&clause.data)) check_retractable(*e);
            else if (auto* ce = std::get_if<ConditionalEffect>(&clause.data))
                check_retractable(ce->effect);
        }
    }

    check_unused_variables(rule);
}

// ---------------------------------------------------------------------------
// check_fact_pattern
// ---------------------------------------------------------------------------

void TypeChecker::check_fact_pattern(const FactPattern& pattern) {
    // ── New: namespaced-relation validation against kRelations ──
    std::string_view const fp_ns = pool_.get(pattern.qualifier);
    std::string_view const fp_nm = pool_.get(pattern.name);
    if (!fp_ns.empty()) {
        const model::RelationEntry* rel = model::find_relation(
            fp_ns, fp_nm, model::kRelations, model::kRelationCount);

        // Built-in namespaces: if not in kRelations, flag as unknown.
        static const auto is_builtin_ns = [](std::string_view ns) {
            return ns == "form" || ns == "ref" || ns == "player"
                || ns == "world" || ns == "event";
        };

        if (!rel && is_builtin_ns(fp_ns)) {
            diags_.error("E023",
                std::string("unknown relation '") +
                    std::string(fp_ns) + "/" + std::string(fp_nm) + "'",
                pattern.span, source_line(pattern.span));
            return;
        }

        if (rel) {
            if (pattern.args.size() != rel->arg_count) {
                diags_.error("E020",
                    std::string("relation '") +
                        std::string(fp_ns) + "/" + std::string(fp_nm) +
                        "' expects " + std::to_string(rel->arg_count) +
                        " args, got " + std::to_string(pattern.args.size()),
                    pattern.span, source_line(pattern.span));
            }
            // Mark variable arguments as used / bind if unknown. Recurse into
            // non-trivial arg exprs so built-in call nodes get validated.
            for (const Expr& arg : pattern.args) {
                if (auto* var = std::get_if<VariableExpr>(&arg.data)) {
                    MoraType const existing = lookup_variable(var->name);
                    if (existing.kind == TypeKind::Unknown) {
                        bind_variable(var->name, existing, arg.span);
                    } else {
                        var_used_.insert(var->name.index);
                    }
                } else if (std::get_if<DiscardExpr>(&arg.data)) {
                    // skip
                } else {
                    (void)infer_expr_type(arg);
                }
            }
            return;
        }
    }

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
                     pattern.span, source_line(pattern.span));
        return;
    }

    // Check each argument
    for (size_t i = 0; i < pattern.args.size(); ++i) {
        const Expr& arg = pattern.args[i];
        MoraType const expected = sig->param_types[i];

        if (auto* var = std::get_if<VariableExpr>(&arg.data)) {
            MoraType const existing = lookup_variable(var->name);
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
                                 arg.span, source_line(arg.span));
                }
            }
        } else if (std::get_if<DiscardExpr>(&arg.data)) {
            // Skip discard
        } else {
            // Literal or symbol — infer type and check
            MoraType const actual = infer_expr_type(arg);
            if (actual.kind != TypeKind::Unknown &&
                actual.kind != TypeKind::Error &&
                !actual.is_subtype_of(expected)) {
                diags_.error("E022",
                             std::string("type mismatch: expected ") +
                                 expected.to_string() + " but got " +
                                 actual.to_string(),
                             arg.span, source_line(arg.span));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// check_effect
// ---------------------------------------------------------------------------

void TypeChecker::check_effect(const Effect& effect) {
    // ── New: namespaced-relation validation against kRelations ──
    std::string_view const eff_ns = pool_.get(effect.namespace_);
    std::string_view const eff_nm = pool_.get(effect.name);
    if (!eff_ns.empty()) {
        const model::RelationEntry* rel = model::find_relation(
            eff_ns, eff_nm, model::kRelations, model::kRelationCount);
        if (rel) {
            // Verb vs type-constructor legality
            if (!model::is_legal_verb(to_model_verb(effect.verb), rel->type)) {
                diags_.error("E035",
                    std::string("verb '") + verb_name_str(effect.verb) +
                        "' is not legal for relation '" +
                        std::string(eff_ns) + "/" + std::string(eff_nm) +
                        "' (constructor: " +
                        std::string(model::ctor_spec(rel->type.ctor).name) + ")",
                    effect.span, source_line(effect.span));
            }
            // Arity check against kRelations
            if (effect.args.size() != rel->arg_count) {
                diags_.error("E020",
                    std::string("arity mismatch for '") +
                        std::string(eff_ns) + "/" + std::string(eff_nm) +
                        "': expected " + std::to_string(rel->arg_count) +
                        " arguments, got " + std::to_string(effect.args.size()),
                    effect.span, source_line(effect.span));
            }
            // Mark variable arguments as used so unused-variable checks pass,
            // and recurse into non-trivial arg exprs so built-in call nodes get
            // validated (arity, known name).
            for (const Expr& arg : effect.args) {
                if (auto* var = std::get_if<VariableExpr>(&arg.data)) {
                    var_used_.insert(var->name.index);
                } else if (std::get_if<DiscardExpr>(&arg.data)) {
                    // skip
                } else {
                    (void)infer_expr_type(arg);
                }
            }
            // Skip legacy lookup_fact / component-compat flow: this effect is
            // fully described by kRelations.
            return;
        }
    }

    const FactSignature* sig = resolver_.lookup_fact(effect.name);
    if (!sig) return;

    if (effect.args.size() != sig->param_types.size()) {
        diags_.error("E020",
                     std::string("arity mismatch for '") +
                         std::string(pool_.get(effect.name)) +
                         "': expected " +
                         std::to_string(sig->param_types.size()) +
                         " arguments, got " +
                         std::to_string(effect.args.size()),
                     effect.span, source_line(effect.span));
        return;
    }

    // Type-check each argument (same logic as check_fact_pattern)
    for (size_t i = 0; i < effect.args.size(); ++i) {
        const Expr& arg = effect.args[i];
        MoraType const expected = sig->param_types[i];

        if (auto* var = std::get_if<VariableExpr>(&arg.data)) {
            var_used_.insert(var->name.index);
            MoraType const existing = lookup_variable(var->name);
            if (existing.kind != TypeKind::Unknown &&
                !existing.is_subtype_of(expected) &&
                !expected.is_subtype_of(existing)) {
                diags_.error("E030",
                             std::string("type mismatch in effect '") +
                                 std::string(pool_.get(effect.name)) +
                                 "': variable '" +
                                 std::string(pool_.get(var->name)) +
                                 "' is " + existing.to_string() +
                                 " but effect expects " + expected.to_string(),
                             arg.span, source_line(arg.span));
            }
        } else if (std::get_if<DiscardExpr>(&arg.data)) {
            // skip
        } else {
            MoraType const actual = infer_expr_type(arg);
            if (actual.kind != TypeKind::Unknown &&
                actual.kind != TypeKind::Error &&
                !actual.is_subtype_of(expected)) {
                diags_.error("E031",
                             std::string("type mismatch in effect '") +
                                 std::string(pool_.get(effect.name)) +
                                 "': expected " + expected.to_string() +
                                 " but got " + actual.to_string(),
                             arg.span, source_line(arg.span));
            }
        }
    }

    // Component compatibility check: verify the first argument's form type
    // actually has the component required by this effect.
    if (effect.args.empty()) return;
    MoraType first_type = MoraType::make(TypeKind::Unknown);
    if (auto* var = std::get_if<VariableExpr>(&effect.args[0].data)) {
        first_type = lookup_variable(var->name);
    }
    if (!first_type.is_formid() || first_type.kind == TypeKind::FormID) return;

    auto action_name = pool_.get(effect.name);
    namespace m = model;

    // Check scalar fields
    for (size_t i = 0; i < m::kFieldCount; i++) {
        if (m::kFields[i].set_action && action_name == m::kFields[i].set_action) {
            if (!m::type_has_component(first_type.kind, m::kFields[i].component_idx)) {
                diags_.error("E034",
                             std::string("effect '") + std::string(action_name) +
                                 "' is not valid for " + first_type.to_string() +
                                 " (available on: " +
                                 m::form_types_with_component(m::kFields[i].component_idx) + ")",
                             effect.args[0].span, source_line(effect.args[0].span));
            }
            return;
        }
    }

    // Check form arrays
    for (size_t i = 0; i < m::kFormArrayCount; i++) {
        auto& fa = m::kFormArrays[i];
        if ((fa.add_action && action_name == fa.add_action) ||
            (fa.remove_action && action_name == fa.remove_action)) {
            if (!m::type_has_component(first_type.kind, fa.component_idx)) {
                diags_.error("E034",
                             std::string("effect '") + std::string(action_name) +
                                 "' is not valid for " + first_type.to_string() +
                                 " (available on: " +
                                 m::form_types_with_component(fa.component_idx) + ")",
                             effect.args[0].span, source_line(effect.args[0].span));
            }
            return;
        }
    }

    // Check flags
    for (size_t i = 0; i < m::kFlagCount; i++) {
        if (m::kFlags[i].set_action && action_name == m::kFlags[i].set_action) {
            if (!m::type_has_component(first_type.kind, m::kFlags[i].component_idx)) {
                diags_.error("E034",
                             std::string("effect '") + std::string(action_name) +
                                 "' is not valid for " + first_type.to_string() +
                                 " (available on: " +
                                 m::form_types_with_component(m::kFlags[i].component_idx) + ")",
                             effect.args[0].span, source_line(effect.args[0].span));
            }
            return;
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
        } else if constexpr (std::is_same_v<NodeT, CallExpr>) {
            // Validate name/arity; recurse into args so vars get marked used.
            std::string_view const nm = pool_.get(node.name);
            const model::BuiltinFn* b = model::find_builtin(nm);
            if (!b) {
                diags_.error("E040",
                             std::string("unknown function '") + std::string(nm) + "'",
                             expr.span, source_line(expr.span));
            } else if (node.args.size() != b->arity) {
                diags_.error("E041",
                             std::string("function '") + std::string(nm) +
                                 "' expects " + std::to_string(b->arity) +
                                 " argument(s), got " +
                                 std::to_string(node.args.size()),
                             expr.span, source_line(expr.span));
            }
            for (const auto& a : node.args) check_guard(a);
        } else if constexpr (std::is_same_v<NodeT, BinaryExpr>) {
            if (node.left) check_guard(*node.left);
            if (node.right) check_guard(*node.right);

            MoraType const left = node.left ? infer_expr_type(*node.left)
                                      : MoraType::make(TypeKind::Unknown);
            MoraType const right = node.right ? infer_expr_type(*node.right)
                                        : MoraType::make(TypeKind::Unknown);

            bool const is_arith = (node.op == BinaryExpr::Op::Add ||
                             node.op == BinaryExpr::Op::Sub ||
                             node.op == BinaryExpr::Op::Mul ||
                             node.op == BinaryExpr::Op::Div);

            bool const is_cmp = (node.op == BinaryExpr::Op::Lt ||
                           node.op == BinaryExpr::Op::Gt ||
                           node.op == BinaryExpr::Op::LtEq ||
                           node.op == BinaryExpr::Op::GtEq);

            if (is_arith) {
                if (left.kind != TypeKind::Unknown && !left.is_numeric()) {
                    diags_.error("E032",
                                 "arithmetic operator requires numeric type, got " +
                                     left.to_string(),
                                 expr.span, source_line(expr.span));
                }
                if (right.kind != TypeKind::Unknown && !right.is_numeric()) {
                    diags_.error("E032",
                                 "arithmetic operator requires numeric type, got " +
                                     right.to_string(),
                                 expr.span, source_line(expr.span));
                }
            }

            if (is_cmp) {
                if (left.kind != TypeKind::Unknown && !left.is_numeric() &&
                    right.kind != TypeKind::Unknown && !right.is_numeric()) {
                    diags_.error("E033",
                                 "comparison requires numeric types, got " +
                                     left.to_string() + " and " + right.to_string(),
                                 expr.span, source_line(expr.span));
                }
            }
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
        } else if constexpr (std::is_same_v<NodeT, KeywordLiteral>) {
            return MoraType::make(TypeKind::Unknown);
        } else if constexpr (std::is_same_v<NodeT, SymbolExpr>) {
            return MoraType::make(TypeKind::Unknown);
        } else if constexpr (std::is_same_v<NodeT, DiscardExpr>) {
            return MoraType::make(TypeKind::Unknown);
        } else if constexpr (std::is_same_v<NodeT, CallExpr>) {
            // Validate name/arity; infer result type from arg types.
            std::string_view const nm = pool_.get(node.name);
            const model::BuiltinFn* b = model::find_builtin(nm);
            if (!b) {
                diags_.error("E040",
                             std::string("unknown function '") + std::string(nm) + "'",
                             expr.span, source_line(expr.span));
                for (const auto& a : node.args) (void)infer_expr_type(a);
                return MoraType::make(TypeKind::Unknown);
            }
            if (node.args.size() != b->arity) {
                diags_.error("E041",
                             std::string("function '") + std::string(nm) +
                                 "' expects " + std::to_string(b->arity) +
                                 " argument(s), got " +
                                 std::to_string(node.args.size()),
                             expr.span, source_line(expr.span));
            }
            bool any_float = false;
            for (const auto& a : node.args) {
                MoraType const t = infer_expr_type(a);
                if (t.kind == TypeKind::Float) any_float = true;
            }
            return MoraType::make(any_float ? TypeKind::Float : TypeKind::Int);
        } else if constexpr (std::is_same_v<NodeT, BinaryExpr>) {
            MoraType const left = node.left ? infer_expr_type(*node.left)
                                      : MoraType::make(TypeKind::Unknown);
            MoraType const right = node.right ? infer_expr_type(*node.right)
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

void TypeChecker::check_unused_variables([[maybe_unused]] const Rule& rule) {
    for (const auto& [id, span] : var_def_spans_) {
        if (var_used_.contains(id)) continue;

        // Look up the name to check for "_" prefix
        StringId sid;
        sid.index = id;
        std::string_view const name = pool_.get(sid);
        if (name == "_" || (!name.empty() && name[0] == '_')) continue;

        diags_.warning("W007",
                       std::string("unused variable '") +
                           std::string(name) + "'",
                       span, source_line(span));
    }
}

} // namespace mora
