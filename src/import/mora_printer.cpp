#include "mora/import/mora_printer.h"
#include <sstream>

namespace mora {

std::string MoraPrinter::print_expr(const Expr& expr) const {
    return std::visit([&](const auto& e) -> std::string {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, VariableExpr>) {
            return std::string(pool_.get(e.name));
        } else if constexpr (std::is_same_v<T, SymbolExpr>) {
            return ":" + std::string(pool_.get(e.name));
        } else if constexpr (std::is_same_v<T, IntLiteral>) {
            return std::to_string(e.value);
        } else if constexpr (std::is_same_v<T, FloatLiteral>) {
            // Use a reasonable representation
            std::ostringstream oss;
            oss << e.value;
            return oss.str();
        } else if constexpr (std::is_same_v<T, StringLiteral>) {
            return "\"" + std::string(pool_.get(e.value)) + "\"";
        } else if constexpr (std::is_same_v<T, DiscardExpr>) {
            return "_";
        } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            std::string op_str;
            switch (e.op) {
                case BinaryExpr::Op::Add:   op_str = "+";  break;
                case BinaryExpr::Op::Sub:   op_str = "-";  break;
                case BinaryExpr::Op::Mul:   op_str = "*";  break;
                case BinaryExpr::Op::Div:   op_str = "/";  break;
                case BinaryExpr::Op::Eq:    op_str = "=="; break;
                case BinaryExpr::Op::Neq:   op_str = "!="; break;
                case BinaryExpr::Op::Lt:    op_str = "<";  break;
                case BinaryExpr::Op::Gt:    op_str = ">";  break;
                case BinaryExpr::Op::LtEq:  op_str = "<="; break;
                case BinaryExpr::Op::GtEq:  op_str = ">="; break;
            }
            return print_expr(*e.left) + " " + op_str + " " + print_expr(*e.right);
        }
        return "";
    }, expr.data);
}

std::string MoraPrinter::print_fact_pattern(const FactPattern& fp) const {
    std::ostringstream oss;
    if (fp.negated) oss << "not ";
    oss << pool_.get(fp.name) << "(";
    for (size_t i = 0; i < fp.args.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << print_expr(fp.args[i]);
    }
    oss << ")";
    return oss.str();
}

std::string MoraPrinter::print_effect(const Effect& eff) const {
    std::ostringstream oss;
    oss << "=> " << pool_.get(eff.action) << "(";
    for (size_t i = 0; i < eff.args.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << print_expr(eff.args[i]);
    }
    oss << ")";
    return oss.str();
}

std::string MoraPrinter::print_rule(const Rule& rule) const {
    std::ostringstream oss;

    // Head: rule_name(Arg1, Arg2):
    oss << pool_.get(rule.name) << "(";
    for (size_t i = 0; i < rule.head_args.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << print_expr(rule.head_args[i]);
    }
    oss << "):\n";

    // Body clauses
    for (const auto& clause : rule.body) {
        std::visit([&](const auto& c) {
            using T = std::decay_t<decltype(c)>;
            if constexpr (std::is_same_v<T, FactPattern>) {
                oss << "    " << print_fact_pattern(c) << "\n";
            } else if constexpr (std::is_same_v<T, GuardClause>) {
                oss << "    | " << print_expr(*c.expr) << "\n";
            } else if constexpr (std::is_same_v<T, OrClause>) {
                oss << "    or:\n";
                for (const auto& branch : c.branches) {
                    oss << "        ";
                    for (size_t i = 0; i < branch.size(); ++i) {
                        if (i > 0) oss << ", ";
                        oss << print_fact_pattern(branch[i]);
                    }
                    oss << "\n";
                }
            } else if constexpr (std::is_same_v<T, InClause>) {
                oss << "    " << print_expr(*c.variable) << " in [";
                for (size_t i = 0; i < c.values.size(); ++i) {
                    if (i > 0) oss << ", ";
                    oss << print_expr(c.values[i]);
                }
                oss << "]\n";
            } else if constexpr (std::is_same_v<T, Effect>) {
                oss << "    " << print_effect(c) << "\n";
            } else if constexpr (std::is_same_v<T, ConditionalEffect>) {
                oss << "    if " << print_expr(*c.guard) << " "
                    << print_effect(c.effect) << "\n";
            }
        }, clause.data);
    }

    // Effects
    for (const auto& eff : rule.effects) {
        oss << "    " << print_effect(eff) << "\n";
    }

    // Conditional effects
    for (const auto& ceff : rule.conditional_effects) {
        oss << "    if " << print_expr(*ceff.guard) << " "
            << print_effect(ceff.effect) << "\n";
    }

    return oss.str();
}

std::string MoraPrinter::print_comment(const std::string& text) const {
    return "# " + text + "\n";
}

} // namespace mora
