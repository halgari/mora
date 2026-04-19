#include "mora/parser/parser.h"
#include "mora/lexer/lexer.h"
#include "mora/sema/name_resolver.h"
#include "mora/eval/phase_classifier.h"
#include <gtest/gtest.h>

using namespace mora;

static Module parse_src(StringPool& pool, DiagBag& diags, const char* src) {
    Lexer lex(src, "test.mora", pool, diags);
    Parser p(lex, pool, diags);
    return p.parse_module();
}

TEST(PhaseClassifierV2, UnannotatedWithOnlyFormIsStatic) {
    StringPool pool;
    DiagBag diag;
    auto m = parse_src(pool, diag,
        "namespace x.y\nskyrim/set(W, :Damage, 20):\n    form/weapon(W)\n");
    PhaseClassifier pc(pool, diag);
    auto classes = pc.classify(m);
    EXPECT_EQ(classes.at(0), PhaseClass::Static);
}

TEST(PhaseClassifierV2, MaintainAnnotationHonored) {
    StringPool pool;
    DiagBag diag;
    auto m = parse_src(pool, diag,
        "namespace x.y\nmaintain r(W):\n    form/weapon(W)\n");
    PhaseClassifier pc(pool, diag);
    auto classes = pc.classify(m);
    EXPECT_EQ(classes.at(0), PhaseClass::Maintain);
}

TEST(PhaseClassifierV2, OnAnnotationHonored) {
    StringPool pool;
    DiagBag diag;
    auto m = parse_src(pool, diag,
        "namespace x.y\non r(W):\n    form/weapon(W)\n");
    PhaseClassifier pc(pool, diag);
    auto classes = pc.classify(m);
    EXPECT_EQ(classes.at(0), PhaseClass::On);
}
