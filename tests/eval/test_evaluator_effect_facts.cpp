#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/evaluator.h"
#include "mora/eval/fact_db.h"
#include "mora/lexer/lexer.h"
#include "mora/parser/parser.h"
#include "mora/sema/name_resolver.h"
// type_checker.h excluded in M2; deleted in M3

#include <gtest/gtest.h>

namespace {

mora::Module parse_and_resolve(mora::StringPool& pool,
                                mora::DiagBag& diags,
                                const std::string& source) {
    mora::Lexer lexer(source, "test.mora", pool, diags);
    mora::Parser parser(lexer, pool, diags);
    auto mod = parser.parse_module();
    mora::NameResolver resolver(pool, diags);
    resolver.resolve(mod);
    return mod;
}

TEST(EvaluatorEffectFacts, SetProducesSkyrimSetTuple) {
    mora::StringPool pool;
    mora::DiagBag diags;

    // Minimal program: seed a form/npc fact (registered relation), fire a
    // single qualified-head rule to produce a skyrim/set tuple.
    // form/npc is in the schema registry so the name resolver accepts it.
    std::string const source =
        "skyrim/set(NPC, :GoldValue, 100):\n"
        "    form/npc(NPC)\n";

    auto mod = parse_and_resolve(pool, diags, source);
    for (const auto& d : diags.all()) {
        if (d.level == mora::DiagLevel::Error)
            ADD_FAILURE() << "parse/resolve error: " << d.message;
    }
    ASSERT_FALSE(diags.has_errors()) << "parse/resolve errors";

    mora::FactDB db(pool);
    // Seed an npc fact. The evaluator scans both db_ and derived_facts_.
    // We use pool.intern("npc") because evaluator_test.cpp does the same; the
    // parsed FactPattern has qualifier "form" and name "npc" — the evaluator looks
    // up by name only (pattern.name = pool.intern("npc")).
    db.add_fact(pool.intern("npc"), mora::Tuple{
        mora::Value::make_formid(0x000DEFEA)});

    mora::Evaluator evaluator(pool, diags, db);
    evaluator.evaluate_module(mod, db);

    auto rel_set = pool.intern("skyrim/set");
    const auto& tuples = db.get_relation(rel_set);
    ASSERT_EQ(tuples.size(), 1U);

    const auto& t = tuples.front();
    ASSERT_EQ(t.size(), 3U);
    EXPECT_EQ(t[0].kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(t[0].as_formid(), 0x000DEFEAu);
    EXPECT_EQ(t[1].kind(), mora::Value::Kind::Keyword);
    EXPECT_EQ(pool.get(t[1].as_keyword()), "GoldValue");
    EXPECT_EQ(t[2].kind(), mora::Value::Kind::Int);
    EXPECT_EQ(t[2].as_int(), 100);
}

} // namespace
