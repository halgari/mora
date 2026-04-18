#include <gtest/gtest.h>
#include "mora/eval/evaluator.h"
#include "mora/eval/fact_db.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
// type_checker.h excluded in M2; deleted in M3

using namespace mora;

namespace {

struct Fixture {
    StringPool pool;
    DiagBag diags;

    Module parse_and_resolve(const std::string& src) {
        Lexer lexer(src, "t.mora", pool, diags);
        Parser parser(lexer, pool, diags);
        Module mod = parser.parse_module();
        mod.source = src;
        NameResolver resolver(pool, diags);
        resolver.resolve(mod);
        return mod;
    }
};

// Apply a single static rule that sets form/damage on a symbol weapon,
// then return the resulting int value from skyrim/set or -1 if no tuple landed.
// The rule needs at least one body FactPattern for the vectorized planner to
// accept it. We use form/npc(Anchor) with a seeded fact — the expr under test
// is the value arg of the effect.
int64_t eval_damage(Fixture& f, const std::string& body) {
    std::string src =
        "namespace t\n"
        "skyrim/set(:MyWeap, :Damage, " + body + "):\n"
        "    form/npc(Anchor)\n";
    auto mod = f.parse_and_resolve(src);
    FactDB db(f.pool);
    // Seed one npc fact so the rule fires once.
    db.add_fact(f.pool.intern("npc"), {Value::make_formid(0x001)});
    Evaluator ev(f.pool, f.diags, db);
    ev.set_symbol_formid(f.pool.intern("MyWeap"), 0x500);
    ev.evaluate_module(mod, db);
    auto rel_set = f.pool.intern("skyrim/set");
    const auto& tuples = db.get_relation(rel_set);
    // Find the tuple for formid 0x500
    for (const auto& t : tuples) {
        if (t.size() >= 3 && t[0].kind() == Value::Kind::FormID &&
            t[0].as_formid() == 0x500u) {
            if (t[2].kind() == Value::Kind::Int) return t[2].as_int();
        }
    }
    return -1;
}

} // namespace

TEST(BuiltinFns, Max) {
    Fixture f;
    EXPECT_EQ(eval_damage(f, "max(3, 5)"), 5);
    EXPECT_EQ(eval_damage(f, "max(7, 2)"), 7);
}

TEST(BuiltinFns, Min) {
    Fixture f;
    EXPECT_EQ(eval_damage(f, "min(3, 5)"), 3);
    EXPECT_EQ(eval_damage(f, "min(7, 2)"), 2);
}

TEST(BuiltinFns, Abs) {
    Fixture f;
    EXPECT_EQ(eval_damage(f, "abs(7)"), 7);
    // abs(0 - 9) avoids parser lack of unary minus
    EXPECT_EQ(eval_damage(f, "abs(0 - 9)"), 9);
}

TEST(BuiltinFns, Clamp) {
    Fixture f;
    EXPECT_EQ(eval_damage(f, "clamp(10, 0, 5)"), 5);
    EXPECT_EQ(eval_damage(f, "clamp(0 - 3, 0, 5)"), 0);
    EXPECT_EQ(eval_damage(f, "clamp(3, 0, 5)"), 3);
}

TEST(BuiltinFns, ArithmeticComposition) {
    Fixture f;
    // 10 * 4 + 5 * max(0, 4 - 1) = 40 + 15 = 55
    EXPECT_EQ(eval_damage(f, "10 * 4 + 5 * max(0, 4 - 1)"), 55);
    // 10 * 4 + 5 * max(0, 4 - 10) = 40 + 0 = 40
    EXPECT_EQ(eval_damage(f, "10 * 4 + 5 * max(0, 4 - 10)"), 40);
}

// TypeCheckerAcceptsKnownBuiltins, TypeCheckerRejectsUnknownBuiltin,
// and TypeCheckerRejectsWrongArity tests removed in M2 (TypeChecker excluded
// from build; deleted in M3). Builtin-fn validation will return via the
// vectorized evaluator in a later plan.
