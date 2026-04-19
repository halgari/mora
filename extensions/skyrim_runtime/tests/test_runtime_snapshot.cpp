#include "mora_skyrim_runtime/game_api.h"
#include "mora_skyrim_runtime/runtime_snapshot.h"

#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/fact_db.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

// Seed a FactDB with a small set of representative effect facts
// across all four op relations, mixing value kinds.
void seed_effect_facts(mora::FactDB& db, mora::StringPool& pool) {
    auto const* formid_t = mora::TypeRegistry::instance().register_nominal(
        "FormID", mora::types::int32(), mora::Value::Kind::FormID);

    for (const char* name :
         {"skyrim/set", "skyrim/add", "skyrim/remove", "skyrim/multiply"}) {
        db.configure_relation(
            pool.intern(name),
            std::vector<const mora::Type*>{
                formid_t, mora::types::keyword(), mora::types::any()},
            /*indexed*/ std::vector<size_t>{0});
    }

    auto kw = [&](const char* s) { return mora::Value::make_keyword(pool.intern(s)); };

    // skyrim/set: int, float, keyword, string values
    db.add_fact(pool.intern("skyrim/set"), {
        mora::Value::make_formid(0x000A0B0C),
        kw("GoldValue"),
        mora::Value::make_int(100),
    });
    db.add_fact(pool.intern("skyrim/set"), {
        mora::Value::make_formid(0x000A0B0D),
        kw("Damage"),
        mora::Value::make_float(12.5),
    });
    db.add_fact(pool.intern("skyrim/set"), {
        mora::Value::make_formid(0x000A0B0E),
        kw("Name"),
        mora::Value::make_string(pool.intern("Skeever")),
    });

    // skyrim/add: FormID-valued Keyword pushes
    db.add_fact(pool.intern("skyrim/add"), {
        mora::Value::make_formid(0x000A0B0F),
        kw("Keyword"),
        mora::Value::make_formid(0x00013746),
    });

    // skyrim/remove
    db.add_fact(pool.intern("skyrim/remove"), {
        mora::Value::make_formid(0x000A0B10),
        kw("Keyword"),
        mora::Value::make_formid(0x00013747),
    });

    // skyrim/multiply: float factor
    db.add_fact(pool.intern("skyrim/multiply"), {
        mora::Value::make_formid(0x000A0B11),
        kw("Damage"),
        mora::Value::make_float(1.5),
    });
}

TEST(RuntimeSnapshot, RoundTripThroughMockGameAPI) {
    namespace fs = std::filesystem;
    auto tmpdir = fs::temp_directory_path() /
        ("mora-rt-snap-" + std::to_string(::getpid()));
    fs::create_directories(tmpdir);
    auto out_path = tmpdir / "mora_runtime.bin";

    mora::StringPool pool;
    mora::DiagBag    diags;
    mora::FactDB     db(pool);

    seed_effect_facts(db, pool);

    ASSERT_TRUE(mora_skyrim_runtime::write_snapshot(out_path, db, pool, diags))
        << "write_snapshot failed";
    ASSERT_FALSE(diags.has_errors()) << "write diags: "
        << diags.error_count() << " errors";

    // Separate pool for the read side — exercises the string-interning
    // round-trip without relying on shared pool identity.
    mora::StringPool read_pool;
    mora::DiagBag    read_diags;
    auto snap = mora_skyrim_runtime::read_snapshot(out_path, read_diags);
    ASSERT_TRUE(snap.has_value());
    EXPECT_FALSE(read_diags.has_errors());
    EXPECT_EQ(snap->rows.size(), 6u);

    mora_skyrim_runtime::MockGameAPI api;
    auto applied = mora_skyrim_runtime::apply_snapshot(*snap, api, read_pool);
    EXPECT_EQ(applied, 6u);
    ASSERT_EQ(api.calls.size(), 6u);

    // Spot-check each call by op + field name.
    auto find_call = [&](const std::string& op, const std::string& field) {
        for (auto& c : api.calls) {
            if (c.op == op && c.field == field) return &c;
        }
        return static_cast<mora_skyrim_runtime::MockCall*>(nullptr);
    };

    auto* set_gold = find_call("set", "GoldValue");
    ASSERT_NE(set_gold, nullptr);
    EXPECT_EQ(set_gold->target, 0x000A0B0Cu);
    EXPECT_EQ(set_gold->value.kind(), mora::Value::Kind::Int);
    EXPECT_EQ(set_gold->value.as_int(), 100);

    auto* set_dmg = find_call("set", "Damage");
    ASSERT_NE(set_dmg, nullptr);
    EXPECT_EQ(set_dmg->value.kind(), mora::Value::Kind::Float);
    EXPECT_DOUBLE_EQ(set_dmg->value.as_float(), 12.5);

    auto* set_name = find_call("set", "Name");
    ASSERT_NE(set_name, nullptr);
    EXPECT_EQ(set_name->value.kind(), mora::Value::Kind::String);
    EXPECT_EQ(read_pool.get(set_name->value.as_string()), "Skeever");

    auto* add_kw = find_call("add", "Keyword");
    ASSERT_NE(add_kw, nullptr);
    EXPECT_EQ(add_kw->value.kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(add_kw->value.as_formid(), 0x00013746u);

    auto* rem_kw = find_call("remove", "Keyword");
    ASSERT_NE(rem_kw, nullptr);
    EXPECT_EQ(rem_kw->value.kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(rem_kw->value.as_formid(), 0x00013747u);

    auto* mul_dmg = find_call("multiply", "Damage");
    ASSERT_NE(mul_dmg, nullptr);
    EXPECT_EQ(mul_dmg->value.kind(), mora::Value::Kind::Float);
    EXPECT_DOUBLE_EQ(mul_dmg->value.as_float(), 1.5);

    std::error_code ec;
    std::filesystem::remove_all(tmpdir, ec);
}

TEST(RuntimeSnapshot, MissingFileEmitsDiagnostic) {
    mora::DiagBag diags;
    auto snap = mora_skyrim_runtime::read_snapshot(
        "/tmp/nonexistent-mora-snap-zzz.bin", diags);
    EXPECT_FALSE(snap.has_value());
    EXPECT_TRUE(diags.has_errors());
}

TEST(RuntimeSnapshot, EmptyFactDBProducesEmptySnapshot) {
    namespace fs = std::filesystem;
    auto tmpdir = fs::temp_directory_path() /
        ("mora-rt-snap-empty-" + std::to_string(::getpid()));
    fs::create_directories(tmpdir);
    auto out_path = tmpdir / "mora_runtime.bin";

    mora::StringPool pool;
    mora::DiagBag    diags;
    mora::FactDB     db(pool);

    EXPECT_TRUE(mora_skyrim_runtime::write_snapshot(out_path, db, pool, diags));
    EXPECT_FALSE(diags.has_errors());

    mora::DiagBag read_diags;
    auto snap = mora_skyrim_runtime::read_snapshot(out_path, read_diags);
    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(snap->rows.size(), 0u);
    EXPECT_EQ(snap->string_pool.size(), 0u);

    std::error_code ec;
    std::filesystem::remove_all(tmpdir, ec);
}

} // namespace
