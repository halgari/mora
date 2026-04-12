#include <gtest/gtest.h>
#include "mora/import/ini_facts.h"
#include <cstdio>
#include <fstream>
#include <string>

class IniFactsTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;

    // Write content to a temp file, return the path.
    std::string write_temp(const std::string& suffix,
                           const std::string& content) {
        std::string path = "/tmp/ini_facts_test_" + suffix;
        std::ofstream f(path);
        f << content;
        f.close();
        return path;
    }
};

TEST_F(IniFactsTest, SpidSimpleKeyword) {
    auto path = write_temp("spid_simple.ini",
        "Keyword = ActorTypePoor|Brenuin\n");
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);
    uint32_t next_id = 1;
    auto count = mora::emit_spid_facts(path, db, pool, diags, next_id, {});

    EXPECT_EQ(count, 1u);
    EXPECT_EQ(next_id, 2u);

    // Check spid_dist fact
    auto dist_rel = pool.intern("spid_dist");
    EXPECT_EQ(db.fact_count(dist_rel), 1u);
    auto dists = db.get_relation(dist_rel);
    ASSERT_EQ(dists.size(), 1u);
    EXPECT_EQ(dists[0][0].as_int(), 1);
    EXPECT_EQ(pool.get(dists[0][1].as_string()), "keyword");
    EXPECT_EQ(pool.get(dists[0][2].as_string()), "ActorTypePoor");

    // Check spid_filter fact (Brenuin is a string filter)
    auto filter_rel = pool.intern("spid_filter");
    EXPECT_EQ(db.fact_count(filter_rel), 1u);
    auto filters = db.get_relation(filter_rel);
    ASSERT_EQ(filters.size(), 1u);
    EXPECT_EQ(filters[0][0].as_int(), 1);
    EXPECT_EQ(pool.get(filters[0][1].as_string()), "keyword");
    // The list should contain "Brenuin"
    auto& list = filters[0][2].as_list();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(pool.get(list[0].as_string()), "Brenuin");
}

TEST_F(IniFactsTest, SpidWithLevelRange) {
    auto path = write_temp("spid_level.ini",
        "Spell = FireBolt|NONE|NONE|25/50\n");
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);
    uint32_t next_id = 1;
    mora::emit_spid_facts(path, db, pool, diags, next_id, {});

    auto level_rel = pool.intern("spid_level");
    EXPECT_EQ(db.fact_count(level_rel), 1u);
    auto levels = db.get_relation(level_rel);
    ASSERT_EQ(levels.size(), 1u);
    EXPECT_EQ(levels[0][0].as_int(), 1);
    EXPECT_EQ(levels[0][1].as_int(), 25);
    EXPECT_EQ(levels[0][2].as_int(), 50);
}

TEST_F(IniFactsTest, SpidNoFilters) {
    auto path = write_temp("spid_none.ini",
        "Perk = LightFoot|NONE|NONE|NONE\n");
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);
    uint32_t next_id = 1;
    mora::emit_spid_facts(path, db, pool, diags, next_id, {});

    auto dist_rel = pool.intern("spid_dist");
    EXPECT_EQ(db.fact_count(dist_rel), 1u);

    // No filter or level facts should exist
    auto filter_rel = pool.intern("spid_filter");
    EXPECT_EQ(db.fact_count(filter_rel), 0u);
    auto exclude_rel = pool.intern("spid_exclude");
    EXPECT_EQ(db.fact_count(exclude_rel), 0u);
    auto level_rel = pool.intern("spid_level");
    EXPECT_EQ(db.fact_count(level_rel), 0u);
}

TEST_F(IniFactsTest, SpidExcludeFilter) {
    auto path = write_temp("spid_exclude.ini",
        "Keyword = ActorTypeWarrior|NONE|-NordRace\n");
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);
    uint32_t next_id = 1;
    mora::emit_spid_facts(path, db, pool, diags, next_id, {});

    auto exclude_rel = pool.intern("spid_exclude");
    EXPECT_EQ(db.fact_count(exclude_rel), 1u);
    auto excludes = db.get_relation(exclude_rel);
    ASSERT_EQ(excludes.size(), 1u);
    EXPECT_EQ(excludes[0][0].as_int(), 1);
    EXPECT_EQ(pool.get(excludes[0][1].as_string()), "form");
    auto& list = excludes[0][2].as_list();
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(pool.get(list[0].as_string()), "NordRace");
}

TEST_F(IniFactsTest, KidSimple) {
    auto path = write_temp("kid_simple.ini",
        "Keyword = WeapTypeGreatsword|Weapon\n");
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);
    uint32_t next_id = 1;
    auto count = mora::emit_kid_facts(path, db, pool, diags, next_id, {});

    EXPECT_EQ(count, 1u);
    auto dist_rel = pool.intern("kid_dist");
    EXPECT_EQ(db.fact_count(dist_rel), 1u);
    auto dists = db.get_relation(dist_rel);
    ASSERT_EQ(dists.size(), 1u);
    EXPECT_EQ(dists[0][0].as_int(), 1);
    EXPECT_EQ(pool.get(dists[0][1].as_string()), "WeapTypeGreatsword");
    EXPECT_EQ(pool.get(dists[0][2].as_string()), "weapon");
}

TEST_F(IniFactsTest, KidWithFilters) {
    auto path = write_temp("kid_filters.ini",
        "Keyword = ArmorHeavy|Armor|DaedricSmithing,EbonySmithing\n");
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);
    uint32_t next_id = 1;
    mora::emit_kid_facts(path, db, pool, diags, next_id, {});

    auto filter_rel = pool.intern("kid_filter");
    EXPECT_EQ(db.fact_count(filter_rel), 1u);
    auto filters = db.get_relation(filter_rel);
    ASSERT_EQ(filters.size(), 1u);
    auto& list = filters[0][2].as_list();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(pool.get(list[0].as_string()), "DaedricSmithing");
    EXPECT_EQ(pool.get(list[1].as_string()), "EbonySmithing");
}

TEST_F(IniFactsTest, KidUnknownType) {
    auto path = write_temp("kid_unknown.ini",
        "Keyword = SomeKeyword|FakeType\n");
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);
    uint32_t next_id = 1;
    auto count = mora::emit_kid_facts(path, db, pool, diags, next_id, {});

    EXPECT_EQ(count, 0u);
    EXPECT_EQ(diags.warning_count(), 1u);
    // next_rule_id should NOT have been incremented
    EXPECT_EQ(next_id, 1u);

    auto dist_rel = pool.intern("kid_dist");
    EXPECT_EQ(db.fact_count(dist_rel), 0u);
}

TEST_F(IniFactsTest, MultipleLines) {
    auto path = write_temp("spid_multi.ini",
        "Keyword = ActorTypePoor|Brenuin\n"
        "Spell = FireBolt|ActorTypeNPC\n"
        "Perk = LightFoot|NONE\n");
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);
    uint32_t next_id = 100;
    auto count = mora::emit_spid_facts(path, db, pool, diags, next_id, {});

    EXPECT_EQ(count, 3u);
    EXPECT_EQ(next_id, 103u);

    auto dist_rel = pool.intern("spid_dist");
    EXPECT_EQ(db.fact_count(dist_rel), 3u);
    auto dists = db.get_relation(dist_rel);
    ASSERT_EQ(dists.size(), 3u);
    // Verify sequential RuleIDs
    EXPECT_EQ(dists[0][0].as_int(), 100);
    EXPECT_EQ(dists[1][0].as_int(), 101);
    EXPECT_EQ(dists[2][0].as_int(), 102);
}
