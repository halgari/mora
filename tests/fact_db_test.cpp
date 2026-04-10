#include <gtest/gtest.h>
#include "mora/eval/fact_db.h"
#include "mora/core/string_pool.h"

class FactDBTest : public ::testing::Test {
protected:
    mora::StringPool pool;
};

TEST_F(FactDBTest, AddAndQueryFact) {
    mora::FactDB db(pool);
    auto npc_rel = pool.intern("npc");
    db.add_fact(npc_rel, {mora::Value::make_formid(0x0003B547)});
    auto results = db.query(npc_rel, {mora::Value::make_var()});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0][0].as_formid(), 0x0003B547u);
}

TEST_F(FactDBTest, QueryWithFilter) {
    mora::FactDB db(pool);
    auto has_kw = pool.intern("has_keyword");
    db.add_fact(has_kw, {mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    db.add_fact(has_kw, {mora::Value::make_formid(0x100), mora::Value::make_formid(0x300)});
    db.add_fact(has_kw, {mora::Value::make_formid(0x999), mora::Value::make_formid(0x200)});
    auto results = db.query(has_kw, {mora::Value::make_formid(0x100), mora::Value::make_var()});
    ASSERT_EQ(results.size(), 2u);
}

TEST_F(FactDBTest, QueryNoResults) {
    mora::FactDB db(pool);
    auto npc_rel = pool.intern("npc");
    EXPECT_TRUE(db.query(npc_rel, {mora::Value::make_var()}).empty());
}

TEST_F(FactDBTest, MultipleRelations) {
    mora::FactDB db(pool);
    auto npc    = pool.intern("npc");
    auto weapon = pool.intern("weapon");
    db.add_fact(npc,    {mora::Value::make_formid(0x100)});
    db.add_fact(weapon, {mora::Value::make_formid(0x200)});
    EXPECT_EQ(db.query(npc,    {mora::Value::make_var()}).size(), 1u);
    EXPECT_EQ(db.query(weapon, {mora::Value::make_var()}).size(), 1u);
}

TEST_F(FactDBTest, IntAndStringValues) {
    mora::FactDB db(pool);
    auto base_level = pool.intern("base_level");
    auto name_rel   = pool.intern("name");
    db.add_fact(base_level, {mora::Value::make_formid(0x100), mora::Value::make_int(25)});
    db.add_fact(name_rel,   {mora::Value::make_formid(0x100), mora::Value::make_string(pool.intern("Bandit"))});

    auto results = db.query(base_level, {mora::Value::make_formid(0x100), mora::Value::make_var()});
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0][1].as_int(), 25);

    auto name_results = db.query(name_rel, {mora::Value::make_formid(0x100), mora::Value::make_var()});
    ASSERT_EQ(name_results.size(), 1u);
    EXPECT_EQ(pool.get(name_results[0][1].as_string()), "Bandit");
}

TEST_F(FactDBTest, NegationQuery) {
    mora::FactDB db(pool);
    auto has_kw = pool.intern("has_keyword");
    db.add_fact(has_kw, {mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    EXPECT_TRUE(db.has_fact(has_kw,  {mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)}));
    EXPECT_FALSE(db.has_fact(has_kw, {mora::Value::make_formid(0x100), mora::Value::make_formid(0x999)}));
}

TEST_F(FactDBTest, FactCount) {
    mora::FactDB db(pool);
    auto npc = pool.intern("npc");
    db.add_fact(npc, {mora::Value::make_formid(0x100)});
    db.add_fact(npc, {mora::Value::make_formid(0x200)});
    EXPECT_EQ(db.fact_count(npc), 2u);
    EXPECT_EQ(db.fact_count(),    2u);
}
