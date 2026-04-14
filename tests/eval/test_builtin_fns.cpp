#include <gtest/gtest.h>
#include "mora/eval/evaluator.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_set.h"
#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"

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
// then return the resulting int patch value or -1 if no patch landed.
int64_t eval_damage(Fixture& f, const std::string& body) {
    std::string src =
        "namespace t\n"
        "r():\n"
        "    => set form/damage(:MyWeap, " + body + ")\n";
    auto mod = f.parse_and_resolve(src);
    FactDB db(f.pool);
    Evaluator ev(f.pool, f.diags, db);
    ev.set_symbol_formid(f.pool.intern("MyWeap"), 0x500);
    auto ps = ev.evaluate_static(mod);
    auto resolved = ps.resolve();
    auto patches = resolved.get_patches_for(0x500);
    if (patches.empty()) return -1;
    return patches[0].value.as_int();
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

TEST(BuiltinFns, TypeCheckerAcceptsKnownBuiltins) {
    Fixture f;
    auto mod = f.parse_and_resolve(
        "namespace t\n"
        "r(W):\n"
        "    => set form/damage(W, max(1, min(abs(0 - 3), clamp(10, 0, 7))))\n");
    NameResolver nr(f.pool, f.diags);
    nr.resolve(mod);
    TypeChecker tc(f.pool, f.diags, nr);
    tc.check(mod);
    for (const auto& d : f.diags.all()) {
        if (d.level == DiagLevel::Error) ADD_FAILURE() << d.message;
    }
    EXPECT_FALSE(f.diags.has_errors());
}

TEST(BuiltinFns, TypeCheckerRejectsUnknownBuiltin) {
    Fixture f;
    auto mod = f.parse_and_resolve(
        "namespace t\n"
        "r(W):\n"
        "    => set form/damage(W, nosuchfn(1, 2))\n");
    NameResolver nr(f.pool, f.diags);
    nr.resolve(mod);
    TypeChecker tc(f.pool, f.diags, nr);
    tc.check(mod);
    bool saw_e040 = false;
    for (const auto& d : f.diags.all()) {
        if (d.level == DiagLevel::Error && d.code == "E040") saw_e040 = true;
    }
    EXPECT_TRUE(saw_e040);
}

TEST(BuiltinFns, TypeCheckerRejectsWrongArity) {
    Fixture f;
    auto mod = f.parse_and_resolve(
        "namespace t\n"
        "r(W):\n"
        "    => set form/damage(W, max(1))\n");
    NameResolver nr(f.pool, f.diags);
    nr.resolve(mod);
    TypeChecker tc(f.pool, f.diags, nr);
    tc.check(mod);
    bool saw_e041 = false;
    for (const auto& d : f.diags.all()) {
        if (d.level == DiagLevel::Error && d.code == "E041") saw_e041 = true;
    }
    EXPECT_TRUE(saw_e041);
}
