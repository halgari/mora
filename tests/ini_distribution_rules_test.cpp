#include <gtest/gtest.h>
#include "mora/import/ini_distribution_rules.h"
#include "mora/import/ini_facts.h"
#include "mora/eval/evaluator.h"
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_set.h"

class IniDistributionRulesTest : public ::testing::Test {
protected:
    mora::StringPool pool;
    mora::DiagBag diags;
};

TEST_F(IniDistributionRulesTest, ModuleHasExpectedRuleCount) {
    auto mod = mora::build_ini_distribution_rules(pool);

    // 5 dist_types * 2 filter_kinds = 10 SPID rules
    // 11 item_types = 11 KID rules
    // Total = 21
    EXPECT_EQ(mod.rules.size(), 21u);
}

TEST_F(IniDistributionRulesTest, SpidRuleNamesAreCorrect) {
    auto mod = mora::build_ini_distribution_rules(pool);

    // Check a few expected rule names
    bool found_kw_by_kw = false;
    bool found_spell_by_form = false;
    for (const auto& rule : mod.rules) {
        auto name = pool.get(rule.name);
        if (name == "_spid_keyword_by_keyword") found_kw_by_kw = true;
        if (name == "_spid_spell_by_form") found_spell_by_form = true;
    }
    EXPECT_TRUE(found_kw_by_kw);
    EXPECT_TRUE(found_spell_by_form);
}

TEST_F(IniDistributionRulesTest, KidRuleNamesAreCorrect) {
    auto mod = mora::build_ini_distribution_rules(pool);

    bool found_weapon = false;
    bool found_soul_gem = false;
    for (const auto& rule : mod.rules) {
        auto name = pool.get(rule.name);
        if (name == "_kid_weapon") found_weapon = true;
        if (name == "_kid_soul_gem") found_soul_gem = true;
    }
    EXPECT_TRUE(found_weapon);
    EXPECT_TRUE(found_soul_gem);
}

// Integration test: SPID keyword distribution by keyword filter.
//
// Setup:
//   npc(0x100), npc(0x200)
//   has_keyword(0x100, 0xA01)  -- NPC 1 has keyword A01
//   has_keyword(0x200, 0xA02)  -- NPC 2 has keyword A02
//   spid_dist(1, "keyword", 0xBEEF)  -- distribute keyword 0xBEEF
//   spid_filter(1, "keyword", [0xA01, 0xA02])  -- to NPCs with A01 or A02
//
// Expected: both NPCs get add_keyword(NPC, 0xBEEF)
TEST_F(IniDistributionRulesTest, SpidKeywordByKeywordDistribution) {
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);

    // NPCs
    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x100)});
    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x200)});

    // Keywords on NPCs
    db.add_fact(pool.intern("has_keyword"), {
        mora::Value::make_formid(0x100),
        mora::Value::make_formid(0xA01),
    });
    db.add_fact(pool.intern("has_keyword"), {
        mora::Value::make_formid(0x200),
        mora::Value::make_formid(0xA02),
    });

    // SPID distribution: keyword 0xBEEF to NPCs matching keyword filter
    db.add_fact(pool.intern("spid_dist"), {
        mora::Value::make_int(1),
        mora::Value::make_string(pool.intern("keyword")),
        mora::Value::make_formid(0xBEEF),
    });

    // Filter: match NPCs with keyword 0xA01 or 0xA02
    db.add_fact(pool.intern("spid_filter"), {
        mora::Value::make_int(1),
        mora::Value::make_string(pool.intern("keyword")),
        mora::Value::make_list({
            mora::Value::make_formid(0xA01),
            mora::Value::make_formid(0xA02),
        }),
    });

    // Build the generic rules module
    auto mod = mora::build_ini_distribution_rules(pool);

    mora::Evaluator evaluator(pool, diags, db);
    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    // Both NPCs should get the keyword patch
    EXPECT_EQ(resolved.patch_count(), 2u);

    auto p1 = resolved.get_patches_for(0x100);
    ASSERT_EQ(p1.size(), 1u);
    EXPECT_EQ(p1[0].field, mora::FieldId::Keywords);
    EXPECT_EQ(p1[0].op, mora::FieldOp::Add);
    EXPECT_EQ(p1[0].value.as_formid(), 0xBEEFu);

    auto p2 = resolved.get_patches_for(0x200);
    ASSERT_EQ(p2.size(), 1u);
    EXPECT_EQ(p2[0].value.as_formid(), 0xBEEFu);
}

// Test: SPID spell distribution by keyword filter.
// Only NPC 0x100 has the matching keyword.
TEST_F(IniDistributionRulesTest, SpidSpellByKeywordDistribution) {
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);

    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x100)});
    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x200)});

    db.add_fact(pool.intern("has_keyword"), {
        mora::Value::make_formid(0x100),
        mora::Value::make_formid(0xA01),
    });

    // Distribute spell 0xSP01 to NPCs with keyword 0xA01
    db.add_fact(pool.intern("spid_dist"), {
        mora::Value::make_int(1),
        mora::Value::make_string(pool.intern("spell")),
        mora::Value::make_formid(0x5901),
    });
    db.add_fact(pool.intern("spid_filter"), {
        mora::Value::make_int(1),
        mora::Value::make_string(pool.intern("keyword")),
        mora::Value::make_list({mora::Value::make_formid(0xA01)}),
    });

    auto mod = mora::build_ini_distribution_rules(pool);
    mora::Evaluator evaluator(pool, diags, db);
    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    // Only NPC 0x100 should get the spell
    EXPECT_EQ(resolved.patch_count(), 1u);
    auto p1 = resolved.get_patches_for(0x100);
    ASSERT_EQ(p1.size(), 1u);
    EXPECT_EQ(p1[0].field, mora::FieldId::Spells);
    EXPECT_EQ(p1[0].op, mora::FieldOp::Add);
    EXPECT_EQ(p1[0].value.as_formid(), 0x5901u);

    EXPECT_TRUE(resolved.get_patches_for(0x200).empty());
}

// Test: KID weapon distribution by keyword filter.
//
// Setup:
//   weapon(0x300), weapon(0x400)
//   has_keyword(0x300, 0xB01)
//   has_keyword(0x400, 0xB02)
//   kid_dist(1, 0xCCC, "weapon")  -- assign keyword 0xCCC to weapons
//   kid_filter(1, "keyword", [0xB01])  -- only weapons with keyword 0xB01
//
// Expected: only weapon 0x300 gets add_keyword(0x300, 0xCCC)
TEST_F(IniDistributionRulesTest, KidWeaponByKeywordDistribution) {
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);

    db.add_fact(pool.intern("weapon"), {mora::Value::make_formid(0x300)});
    db.add_fact(pool.intern("weapon"), {mora::Value::make_formid(0x400)});

    db.add_fact(pool.intern("has_keyword"), {
        mora::Value::make_formid(0x300),
        mora::Value::make_formid(0xB01),
    });
    db.add_fact(pool.intern("has_keyword"), {
        mora::Value::make_formid(0x400),
        mora::Value::make_formid(0xB02),
    });

    // KID: distribute keyword 0xCCC to weapons matching keyword 0xB01
    db.add_fact(pool.intern("kid_dist"), {
        mora::Value::make_int(1),
        mora::Value::make_formid(0xCCC),
        mora::Value::make_string(pool.intern("weapon")),
    });
    db.add_fact(pool.intern("kid_filter"), {
        mora::Value::make_int(1),
        mora::Value::make_string(pool.intern("keyword")),
        mora::Value::make_list({mora::Value::make_formid(0xB01)}),
    });

    auto mod = mora::build_ini_distribution_rules(pool);
    mora::Evaluator evaluator(pool, diags, db);
    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    // Only weapon 0x300 should get the keyword
    EXPECT_EQ(resolved.patch_count(), 1u);
    auto p1 = resolved.get_patches_for(0x300);
    ASSERT_EQ(p1.size(), 1u);
    EXPECT_EQ(p1[0].field, mora::FieldId::Keywords);
    EXPECT_EQ(p1[0].op, mora::FieldOp::Add);
    EXPECT_EQ(p1[0].value.as_formid(), 0xCCCu);

    EXPECT_TRUE(resolved.get_patches_for(0x400).empty());
}

// Test: no matching filter produces no patches.
// NPC has keyword 0xA01 but filter requires 0xA99.
TEST_F(IniDistributionRulesTest, NoMatchingFilterProducesNoPatches) {
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);

    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x100)});
    db.add_fact(pool.intern("has_keyword"), {
        mora::Value::make_formid(0x100),
        mora::Value::make_formid(0xA01),
    });

    db.add_fact(pool.intern("spid_dist"), {
        mora::Value::make_int(1),
        mora::Value::make_string(pool.intern("keyword")),
        mora::Value::make_formid(0xBEEF),
    });
    db.add_fact(pool.intern("spid_filter"), {
        mora::Value::make_int(1),
        mora::Value::make_string(pool.intern("keyword")),
        mora::Value::make_list({mora::Value::make_formid(0xA99)}),
    });

    auto mod = mora::build_ini_distribution_rules(pool);
    mora::Evaluator evaluator(pool, diags, db);
    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    EXPECT_EQ(resolved.patch_count(), 0u);
}

// Test: multiple SPID rules firing for different dist types.
// Two rules: one distributes a keyword, another distributes a spell,
// both to the same NPC via the same keyword filter.
TEST_F(IniDistributionRulesTest, MultipleDistTypesFireIndependently) {
    mora::FactDB db(pool);
    mora::configure_ini_relations(db, pool);

    db.add_fact(pool.intern("npc"), {mora::Value::make_formid(0x100)});
    db.add_fact(pool.intern("has_keyword"), {
        mora::Value::make_formid(0x100),
        mora::Value::make_formid(0xA01),
    });

    // Rule 1: distribute keyword 0xBEEF
    db.add_fact(pool.intern("spid_dist"), {
        mora::Value::make_int(1),
        mora::Value::make_string(pool.intern("keyword")),
        mora::Value::make_formid(0xBEEF),
    });
    db.add_fact(pool.intern("spid_filter"), {
        mora::Value::make_int(1),
        mora::Value::make_string(pool.intern("keyword")),
        mora::Value::make_list({mora::Value::make_formid(0xA01)}),
    });

    // Rule 2: distribute spell 0x5901
    db.add_fact(pool.intern("spid_dist"), {
        mora::Value::make_int(2),
        mora::Value::make_string(pool.intern("spell")),
        mora::Value::make_formid(0x5901),
    });
    db.add_fact(pool.intern("spid_filter"), {
        mora::Value::make_int(2),
        mora::Value::make_string(pool.intern("keyword")),
        mora::Value::make_list({mora::Value::make_formid(0xA01)}),
    });

    auto mod = mora::build_ini_distribution_rules(pool);
    mora::Evaluator evaluator(pool, diags, db);
    auto patch_set = evaluator.evaluate_static(mod);
    auto resolved = patch_set.resolve();

    // NPC 0x100 should get both a keyword and a spell patch
    auto patches = resolved.get_patches_for(0x100);
    ASSERT_EQ(patches.size(), 2u);

    bool has_keyword_patch = false;
    bool has_spell_patch = false;
    for (const auto& p : patches) {
        if (p.field == mora::FieldId::Keywords && p.value.as_formid() == 0xBEEFu) {
            has_keyword_patch = true;
        }
        if (p.field == mora::FieldId::Spells && p.value.as_formid() == 0x5901u) {
            has_spell_patch = true;
        }
    }
    EXPECT_TRUE(has_keyword_patch);
    EXPECT_TRUE(has_spell_patch);
}
