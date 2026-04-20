#include "mora/eval/expr_eval.h"

#include <algorithm>

namespace mora {

Value resolve_expr(const Expr& expr,
                    const Bindings& bindings,
                    StringPool& pool,
                    const std::unordered_map<uint32_t, uint32_t>& symbols)
{
    return std::visit([&](const auto& e) -> Value {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, VariableExpr>) {
            auto it = bindings.find(e.name.index);
            if (it != bindings.end()) {
                return it->second;
            }
            return Value::make_var();
        } else if constexpr (std::is_same_v<T, SymbolExpr>) {
            auto it = symbols.find(e.name.index);
            if (it != symbols.end()) {
                return Value::make_formid(it->second);
            }
            return Value::make_var();
        } else if constexpr (std::is_same_v<T, EditorIdExpr>) {
            auto it = symbols.find(e.name.index);
            if (it == symbols.end()) {
                std::string lower(pool.get(e.name));
                for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                it = symbols.find(pool.intern(lower).index);
            }
            if (it != symbols.end()) {
                return Value::make_formid(it->second);
            }
            return Value::make_var();
        } else if constexpr (std::is_same_v<T, IntLiteral>) {
            return Value::make_int(e.value);
        } else if constexpr (std::is_same_v<T, FloatLiteral>) {
            return Value::make_float(e.value);
        } else if constexpr (std::is_same_v<T, StringLiteral>) {
            return Value::make_string(e.value);
        } else if constexpr (std::is_same_v<T, KeywordLiteral>) {
            // Try to resolve as a FormID symbol first (backward-compat with
            // legacy `:Symbol` usage). Fall back to an opaque keyword value.
            auto it = symbols.find(e.value.index);
            if (it != symbols.end()) {
                return Value::make_formid(it->second);
            }
            return Value::make_keyword(e.value);
        } else if constexpr (std::is_same_v<T, DiscardExpr>) {
            return Value::make_var();
        } else if constexpr (std::is_same_v<T, FormIdLiteral>) {
            return Value::make_formid(e.value);
        } else if constexpr (std::is_same_v<T, TaggedLiteralExpr>) {
            // Unreachable in a well-formed pipeline: reader expansion
            // should have replaced this node before evaluation. Return
            // an unbound var so downstream code bails without crashing.
            return Value::make_var();
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            Value const left  = resolve_expr(*e.left,  bindings, pool, symbols);
            Value const right = resolve_expr(*e.right, bindings, pool, symbols);
            // Arithmetic operations
            if (left.kind() == Value::Kind::Int && right.kind() == Value::Kind::Int) {
                switch (e.op) {
                    case BinaryExpr::Op::Add: return Value::make_int(left.as_int() + right.as_int());
                    case BinaryExpr::Op::Sub: return Value::make_int(left.as_int() - right.as_int());
                    case BinaryExpr::Op::Mul: return Value::make_int(left.as_int() * right.as_int());
                    case BinaryExpr::Op::Div: return Value::make_int(left.as_int() / right.as_int());
                    default: break;
                }
            }
            if (left.kind() == Value::Kind::Float || right.kind() == Value::Kind::Float) {
                double const l = (left.kind() == Value::Kind::Float) ? left.as_float() : static_cast<double>(left.as_int());
                double const r = (right.kind() == Value::Kind::Float) ? right.as_float() : static_cast<double>(right.as_int());
                switch (e.op) {
                    case BinaryExpr::Op::Add: return Value::make_float(l + r);
                    case BinaryExpr::Op::Sub: return Value::make_float(l - r);
                    case BinaryExpr::Op::Mul: return Value::make_float(l * r);
                    case BinaryExpr::Op::Div: return Value::make_float(l / r);
                    default: break;
                }
            }
            // Boolean comparison results
            if (left.kind() == Value::Kind::Int && right.kind() == Value::Kind::Int) {
                int64_t const l = left.as_int();
                int64_t const r = right.as_int();
                switch (e.op) {
                    case BinaryExpr::Op::Eq:   return Value::make_bool(l == r);
                    case BinaryExpr::Op::Neq:  return Value::make_bool(l != r);
                    case BinaryExpr::Op::Lt:   return Value::make_bool(l < r);
                    case BinaryExpr::Op::Gt:   return Value::make_bool(l > r);
                    case BinaryExpr::Op::LtEq: return Value::make_bool(l <= r);
                    case BinaryExpr::Op::GtEq: return Value::make_bool(l >= r);
                    default: break;
                }
            }
            return Value::make_var();
        } else if constexpr (std::is_same_v<T, CallExpr>) {
            // Evaluate all args first.
            std::vector<Value> vs;
            vs.reserve(e.args.size());
            for (const auto& a : e.args) vs.push_back(resolve_expr(a, bindings, pool, symbols));

            auto numeric = [&](const Value& v) -> double {
                if (v.kind() == Value::Kind::Float) return v.as_float();
                if (v.kind() == Value::Kind::Int)
                    return static_cast<double>(v.as_int());
                return 0.0;
            };
            auto any_float = [&]() {
                return std::ranges::any_of(vs, [](const auto& v) {
                    return v.kind() == Value::Kind::Float;
                });
            };
            auto make_num = [&](double d) {
                if (any_float()) return Value::make_float(d);
                return Value::make_int(static_cast<int64_t>(d));
            };

            std::string_view const name = pool.get(e.name);
            if (name == "max" && vs.size() == 2) {
                double const a = numeric(vs[0]);
                double const b = numeric(vs[1]);
                return make_num(a > b ? a : b);
            }
            if (name == "min" && vs.size() == 2) {
                double const a = numeric(vs[0]);
                double const b = numeric(vs[1]);
                return make_num(a < b ? a : b);
            }
            if (name == "abs" && vs.size() == 1) {
                double const a = numeric(vs[0]);
                return make_num(a < 0 ? -a : a);
            }
            if (name == "clamp" && vs.size() == 3) {
                double const x = numeric(vs[0]);
                double const lo = numeric(vs[1]);
                double const hi = numeric(vs[2]);
                return make_num(std::clamp(x, lo, hi));
            }
            // Unknown or arity-wrong built-in: return unbound var.
            return Value::make_var();
        } else {
            return Value::make_var();
        }
    }, expr.data);
}

bool evaluate_guard(const Expr& expr,
                     const Bindings& bindings,
                     StringPool& pool,
                     const std::unordered_map<uint32_t, uint32_t>& symbols)
{
    if (const auto* be = std::get_if<BinaryExpr>(&expr.data)) {
        Value const left  = resolve_expr(*be->left,  bindings, pool, symbols);
        Value const right = resolve_expr(*be->right, bindings, pool, symbols);

        // Compare based on types
        if (left.kind() == Value::Kind::Int && right.kind() == Value::Kind::Int) {
            int64_t const l = left.as_int();
            int64_t const r = right.as_int();
            switch (be->op) {
                case BinaryExpr::Op::Eq:   return l == r;
                case BinaryExpr::Op::Neq:  return l != r;
                case BinaryExpr::Op::Lt:   return l < r;
                case BinaryExpr::Op::Gt:   return l > r;
                case BinaryExpr::Op::LtEq: return l <= r;
                case BinaryExpr::Op::GtEq: return l >= r;
                default: break;
            }
        } else if (left.kind() == Value::Kind::Float && right.kind() == Value::Kind::Float) {
            double const l = left.as_float();
            double const r = right.as_float();
            switch (be->op) {
                case BinaryExpr::Op::Eq:   return l == r;
                case BinaryExpr::Op::Neq:  return l != r;
                case BinaryExpr::Op::Lt:   return l < r;
                case BinaryExpr::Op::Gt:   return l > r;
                case BinaryExpr::Op::LtEq: return l <= r;
                case BinaryExpr::Op::GtEq: return l >= r;
                default: break;
            }
        } else if (left.kind() == Value::Kind::Int && right.kind() == Value::Kind::Float) {
            double const l = static_cast<double>(left.as_int());
            double const r = right.as_float();
            switch (be->op) {
                case BinaryExpr::Op::Eq:   return l == r;
                case BinaryExpr::Op::Neq:  return l != r;
                case BinaryExpr::Op::Lt:   return l < r;
                case BinaryExpr::Op::Gt:   return l > r;
                case BinaryExpr::Op::LtEq: return l <= r;
                case BinaryExpr::Op::GtEq: return l >= r;
                default: break;
            }
        } else if (left.kind() == Value::Kind::Float && right.kind() == Value::Kind::Int) {
            double const l = left.as_float();
            double const r = static_cast<double>(right.as_int());
            switch (be->op) {
                case BinaryExpr::Op::Eq:   return l == r;
                case BinaryExpr::Op::Neq:  return l != r;
                case BinaryExpr::Op::Lt:   return l < r;
                case BinaryExpr::Op::Gt:   return l > r;
                case BinaryExpr::Op::LtEq: return l <= r;
                case BinaryExpr::Op::GtEq: return l >= r;
                default: break;
            }
        } else if (left.kind() == Value::Kind::FormID && right.kind() == Value::Kind::FormID) {
            switch (be->op) {
                case BinaryExpr::Op::Eq:  return left.as_formid() == right.as_formid();
                case BinaryExpr::Op::Neq: return left.as_formid() != right.as_formid();
                default: break;
            }
        }
    }
    return false;
}

} // namespace mora
