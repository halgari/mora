#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include "mora/sema/type_checker.h"
#include <gtest/gtest.h>

using namespace mora;

// Helper: parse + resolve + type-check, return collected errors.
static std::vector<std::string> check_source(const char* src) {
    StringPool pool;
    DiagBag diag;
    Lexer lex(src, "test.mora", pool, diag);
    Parser p(lex, pool, diag);
    Module mod = p.parse_module();
    NameResolver nr(pool, diag);
    nr.resolve(mod);
    TypeChecker tc(pool, diag, nr);
    tc.check(mod);
    std::vector<std::string> out;
    for (const auto& d : diag.all()) {
        if (d.level == DiagLevel::Error) out.push_back(d.message);
    }
    return out;
}

TEST(VerbLegality, SetOnScalarOk) {
    // form/name is Scalar(String), verb=set is legal.
    auto errs = check_source(
        "namespace x.y\nr(F):\n    form/npc(F)\n    => set form/name(F, \"Nazeem\")\n");
    EXPECT_EQ(errs.size(), 0u) << "first: " << (errs.empty() ? "" : errs[0]);
}

TEST(VerbLegality, SetOnCountableOk) {
    // form/damage is Countable(Int), verb=set is legal.
    auto errs = check_source(
        "namespace x.y\nr(W):\n    form/weapon(W)\n    => set form/damage(W, 20)\n");
    EXPECT_EQ(errs.size(), 0u) << "first: " << (errs.empty() ? "" : errs[0]);
}

TEST(VerbLegality, AddOnCountableOk) {
    // form/damage is Countable, verb=add is legal.
    auto errs = check_source(
        "namespace x.y\nr(W):\n    form/weapon(W)\n    => add form/damage(W, 5)\n");
    EXPECT_EQ(errs.size(), 0u) << "first: " << (errs.empty() ? "" : errs[0]);
}

TEST(VerbLegality, AddOnPlainScalarFails) {
    // form/name is Scalar(String), verb=add is NOT legal.
    auto errs = check_source(
        "namespace x.y\nr(F):\n    form/npc(F)\n    => add form/name(F, \"Nazeem\")\n");
    EXPECT_GE(errs.size(), 1u);
}

TEST(VerbLegality, RemoveOnSetOk) {
    // form/keyword is Set, verb=remove is legal.
    auto errs = check_source(
        "namespace x.y\nr(W):\n    form/weapon(W)\n    => remove form/keyword(W, @Iron)\n");
    EXPECT_EQ(errs.size(), 0u) << "first: " << (errs.empty() ? "" : errs[0]);
}

TEST(VerbLegality, SetOnSetFails) {
    // form/keyword is Set, verb=set is NOT legal.
    auto errs = check_source(
        "namespace x.y\nr(W):\n    form/weapon(W)\n    => set form/keyword(W, @Iron)\n");
    EXPECT_GE(errs.size(), 1u);
}

TEST(TypeCheck, UnknownRelationErrors) {
    // form/nonexistent does not exist.
    auto errs = check_source(
        "namespace x.y\nr(F):\n    form/nonexistent(F)\n");
    EXPECT_GE(errs.size(), 1u);
}

TEST(TypeCheck, ArityMismatchErrors) {
    // form/weapon is unary; two args should error.
    auto errs = check_source(
        "namespace x.y\nr(F):\n    form/weapon(F, F)\n");
    EXPECT_GE(errs.size(), 1u);
}

TEST(MaintainRules, MaintainOnNonRetractableFails) {
    // player/notification has apply_handler but no retract_handler →
    // attempting to use it in 'maintain' should error.
    auto errs = check_source(
        "namespace x.y\n"
        "maintain r(P):\n"
        "    form/npc(P)\n"
        "    => add player/notification(P, \"hi\")\n");
    EXPECT_GE(errs.size(), 1u);
}

TEST(MaintainRules, MaintainUsingEventFails) {
    auto errs = check_source(
        "namespace x.y\n"
        "maintain r(R, L):\n"
        "    event/entered_location(R, L)\n");
    EXPECT_GE(errs.size(), 1u);
}

TEST(MaintainRules, MaintainOnRetractableSetSucceeds) {
    auto errs = check_source(
        "namespace x.y\n"
        "maintain r(R):\n"
        "    ref/base_form(R, Base)\n"
        "    form/faction(Base, @BanditFaction)\n"
        "    => add ref/keyword(R, @DangerMark)\n");
    EXPECT_EQ(errs.size(), 0u) << "first: " << (errs.empty() ? "" : errs[0]);
}

TEST(OnRules, OnUsingEventSucceeds) {
    auto errs = check_source(
        "namespace x.y\n"
        "on r(P, Loc):\n"
        "    event/entered_location(P, Loc)\n"
        "    => add player/gold(P, 100)\n");
    EXPECT_EQ(errs.size(), 0u) << "first: " << (errs.empty() ? "" : errs[0]);
}
