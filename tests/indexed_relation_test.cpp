#include <gtest/gtest.h>
#include "mora/data/indexed_relation.h"

TEST(IndexedRelationTest, AddAndScan) {
    mora::IndexedRelation rel(2, {0});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x300)});
    rel.add({mora::Value::make_formid(0x999), mora::Value::make_formid(0x200)});
    EXPECT_EQ(rel.size(), 3u);
}

TEST(IndexedRelationTest, IndexedLookup) {
    mora::IndexedRelation rel(2, {0});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x300)});
    rel.add({mora::Value::make_formid(0x999), mora::Value::make_formid(0x200)});
    auto results = rel.lookup(0, mora::Value::make_formid(0x100));
    ASSERT_EQ(results.size(), 2u);
}

TEST(IndexedRelationTest, IndexedLookupMiss) {
    mora::IndexedRelation rel(2, {0});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    auto results = rel.lookup(0, mora::Value::make_formid(0x999));
    EXPECT_TRUE(results.empty());
}

TEST(IndexedRelationTest, MultiColumnIndex) {
    mora::IndexedRelation rel(2, {0, 1});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x300)});
    auto results = rel.lookup(1, mora::Value::make_formid(0x200));
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ((*results[0])[0].as_formid(), 0x100u);
}

TEST(IndexedRelationTest, PatternQuery) {
    mora::IndexedRelation rel(2, {0});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x300)});
    rel.add({mora::Value::make_formid(0x999), mora::Value::make_formid(0x200)});
    mora::Tuple pattern = {mora::Value::make_formid(0x100), mora::Value::make_var()};
    auto results = rel.query(pattern);
    ASSERT_EQ(results.size(), 2u);
}

TEST(IndexedRelationTest, FullScanWhenNoIndex) {
    mora::IndexedRelation rel(2, {});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    rel.add({mora::Value::make_formid(0x999), mora::Value::make_formid(0x300)});
    mora::Tuple pattern = {mora::Value::make_formid(0x100), mora::Value::make_var()};
    auto results = rel.query(pattern);
    ASSERT_EQ(results.size(), 1u);
}

TEST(IndexedRelationTest, ExistenceCheck) {
    mora::IndexedRelation rel(2, {0, 1});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)});
    EXPECT_TRUE(rel.contains({mora::Value::make_formid(0x100), mora::Value::make_formid(0x200)}));
    EXPECT_FALSE(rel.contains({mora::Value::make_formid(0x100), mora::Value::make_formid(0x999)}));
}

TEST(IndexedRelationTest, SingleColumnRelation) {
    mora::IndexedRelation rel(1, {0});
    rel.add({mora::Value::make_formid(0x100)});
    rel.add({mora::Value::make_formid(0x200)});
    EXPECT_EQ(rel.size(), 2u);
    auto results = rel.query({mora::Value::make_var()});
    EXPECT_EQ(results.size(), 2u);
}

TEST(IndexedRelationTest, IntValues) {
    mora::IndexedRelation rel(2, {0});
    rel.add({mora::Value::make_formid(0x100), mora::Value::make_int(25)});
    rel.add({mora::Value::make_formid(0x200), mora::Value::make_int(10)});
    auto results = rel.lookup(0, mora::Value::make_formid(0x100));
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ((*results[0])[1].as_int(), 25);
}
