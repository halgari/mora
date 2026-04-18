#include "mora_skyrim_runtime/runtime.h"
#include "mora_skyrim_runtime/game_api.h"
#include "mora_parquet/snapshot_sink.h"

#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/fact_db.h"
#include "mora/ext/extension.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

// Create a unique temp directory and return its path. The directory is
// cleaned up automatically by the OS (or leftover for debugging).
fs::path make_temp_dir(const std::string& label) {
    auto root = fs::temp_directory_path() /
                ("mora-rt-" + label + "-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

// Helper: write a parquet snapshot from a FactDB into `dir`.
void write_snapshot(mora::StringPool& pool, mora::DiagBag& diags,
                    mora::FactDB& db, const fs::path& dir) {
    mora::ext::EmitCtx ctx{pool, diags, dir.string()};
    mora_parquet::ParquetSnapshotSink sink;
    sink.emit(ctx, db);
}

// ── Test A: end-to-end set+add dispatch ──────────────────────────────────

TEST(RuntimeApply, DispatchesSetAndAddFacts) {
    mora::StringPool pool;
    mora::DiagBag    sink_diags;
    mora::FactDB     db(pool);

    // skyrim/set(0x000A0B0C, :GoldValue, 100)
    auto set_rel = pool.intern("skyrim/set");
    db.configure_relation(set_rel, 3, {0});
    db.add_fact(set_rel, {
        mora::Value::make_formid(0x000A0B0Cu),
        mora::Value::make_keyword(pool.intern("GoldValue")),
        mora::Value::make_int(100),
    });

    // skyrim/add(0x000A0B0C, :Damage, 5)
    auto add_rel = pool.intern("skyrim/add");
    db.configure_relation(add_rel, 3, {0});
    db.add_fact(add_rel, {
        mora::Value::make_formid(0x000A0B0Cu),
        mora::Value::make_keyword(pool.intern("Damage")),
        mora::Value::make_int(5),
    });

    auto out_dir = make_temp_dir("set-add");
    write_snapshot(pool, sink_diags, db, out_dir);
    ASSERT_FALSE(sink_diags.has_errors())
        << sink_diags.all().front().message;

    mora::StringPool rt_pool;
    mora::DiagBag    rt_diags;
    mora_skyrim_runtime::MockGameAPI api;

    auto count = mora_skyrim_runtime::runtime_apply(out_dir, api, rt_pool, rt_diags);

    // Two missing ops (remove, multiply) produce warnings, not errors.
    EXPECT_FALSE(rt_diags.has_errors())
        << rt_diags.all().front().message;
    EXPECT_EQ(count, 2u);
    ASSERT_EQ(api.calls.size(), 2u);

    // Order: files are processed in op order (set, add, remove, multiply).
    // set row comes first.
    EXPECT_EQ(api.calls[0].op,     "set");
    EXPECT_EQ(api.calls[0].target, 0x000A0B0Cu);
    EXPECT_EQ(api.calls[0].field,  "GoldValue");
    EXPECT_EQ(api.calls[0].value.kind(), mora::Value::Kind::Int);
    EXPECT_EQ(api.calls[0].value.as_int(), 100);

    EXPECT_EQ(api.calls[1].op,     "add");
    EXPECT_EQ(api.calls[1].target, 0x000A0B0Cu);
    EXPECT_EQ(api.calls[1].field,  "Damage");
    EXPECT_EQ(api.calls[1].value.as_int(), 5);
}

// ── Test B: missing files are non-fatal ──────────────────────────────────

TEST(RuntimeApply, MissingFilesEmitWarningsNotErrors) {
    mora::StringPool pool;
    mora::DiagBag    sink_diags;
    mora::FactDB     db(pool);

    // Only set.parquet — the other three ops are absent.
    auto set_rel = pool.intern("skyrim/set");
    db.configure_relation(set_rel, 3, {0});
    db.add_fact(set_rel, {
        mora::Value::make_formid(0x0000AABBu),
        mora::Value::make_keyword(pool.intern("Health")),
        mora::Value::make_float(200.0),
    });

    auto out_dir = make_temp_dir("missing");
    write_snapshot(pool, sink_diags, db, out_dir);
    ASSERT_FALSE(sink_diags.has_errors());

    mora::StringPool rt_pool;
    mora::DiagBag    rt_diags;
    mora_skyrim_runtime::MockGameAPI api;

    auto count = mora_skyrim_runtime::runtime_apply(out_dir, api, rt_pool, rt_diags);

    // Three missing op files should produce 3 warnings, not errors.
    EXPECT_FALSE(rt_diags.has_errors());
    size_t missing_warnings = 0;
    for (const auto& d : rt_diags.all()) {
        if (d.code == "skyrim-runtime-missing") ++missing_warnings;
    }
    EXPECT_EQ(missing_warnings, 3u);

    // The set row is dispatched successfully.
    EXPECT_EQ(count, 1u);
    ASSERT_EQ(api.calls.size(), 1u);
    EXPECT_EQ(api.calls[0].op,    "set");
    EXPECT_EQ(api.calls[0].field, "Health");
    EXPECT_DOUBLE_EQ(api.calls[0].value.as_float(), 200.0);
}

// ── Test C: tagged heterogeneous col2 round-trip ─────────────────────────

TEST(RuntimeApply, TaggedHeterogeneousCol2RoundTrips) {
    mora::StringPool pool;
    mora::DiagBag    sink_diags;
    mora::FactDB     db(pool);

    // Build a set relation with mixed value kinds in col2
    auto set_rel = pool.intern("skyrim/set");
    db.configure_relation(set_rel, 3, {0});

    // Int row
    db.add_fact(set_rel, {
        mora::Value::make_formid(0x0001u),
        mora::Value::make_keyword(pool.intern("GoldValue")),
        mora::Value::make_int(42),
    });
    // Float row
    db.add_fact(set_rel, {
        mora::Value::make_formid(0x0002u),
        mora::Value::make_keyword(pool.intern("Speed")),
        mora::Value::make_float(1.5),
    });
    // String row
    db.add_fact(set_rel, {
        mora::Value::make_formid(0x0003u),
        mora::Value::make_keyword(pool.intern("Name")),
        mora::Value::make_string(pool.intern("Alduin")),
    });
    // FormID row
    db.add_fact(set_rel, {
        mora::Value::make_formid(0x0004u),
        mora::Value::make_keyword(pool.intern("Race")),
        mora::Value::make_formid(0xDEADBEEFu),
    });
    // Keyword row
    db.add_fact(set_rel, {
        mora::Value::make_formid(0x0005u),
        mora::Value::make_keyword(pool.intern("Faction")),
        mora::Value::make_keyword(pool.intern("BanditFaction")),
    });

    auto out_dir = make_temp_dir("tagged");
    write_snapshot(pool, sink_diags, db, out_dir);
    ASSERT_FALSE(sink_diags.has_errors())
        << sink_diags.all().front().message;

    mora::StringPool rt_pool;
    mora::DiagBag    rt_diags;
    mora_skyrim_runtime::MockGameAPI api;

    auto count = mora_skyrim_runtime::runtime_apply(out_dir, api, rt_pool, rt_diags);
    EXPECT_FALSE(rt_diags.has_errors());
    EXPECT_EQ(count, 5u);
    ASSERT_EQ(api.calls.size(), 5u);

    // Int
    EXPECT_EQ(api.calls[0].value.kind(), mora::Value::Kind::Int);
    EXPECT_EQ(api.calls[0].value.as_int(), 42);

    // Float
    EXPECT_EQ(api.calls[1].value.kind(), mora::Value::Kind::Float);
    EXPECT_DOUBLE_EQ(api.calls[1].value.as_float(), 1.5);

    // String
    EXPECT_EQ(api.calls[2].value.kind(), mora::Value::Kind::String);
    EXPECT_EQ(rt_pool.get(api.calls[2].value.as_string()), "Alduin");

    // FormID
    EXPECT_EQ(api.calls[3].value.kind(), mora::Value::Kind::FormID);
    EXPECT_EQ(api.calls[3].value.as_formid(), 0xDEADBEEFu);

    // Keyword
    EXPECT_EQ(api.calls[4].value.kind(), mora::Value::Kind::Keyword);
    EXPECT_EQ(rt_pool.get(api.calls[4].value.as_keyword()), "BanditFaction");
}

// ── Test D: homogeneous col2 with Int baseline ───────────────────────────

TEST(RuntimeApply, HomogeneousIntCol2Baseline) {
    mora::StringPool pool;
    mora::DiagBag    sink_diags;
    mora::FactDB     db(pool);

    auto multiply_rel = pool.intern("skyrim/multiply");
    db.configure_relation(multiply_rel, 3, {0});
    db.add_fact(multiply_rel, {
        mora::Value::make_formid(0xAAAAu),
        mora::Value::make_keyword(pool.intern("Damage")),
        mora::Value::make_int(2),
    });
    db.add_fact(multiply_rel, {
        mora::Value::make_formid(0xBBBBu),
        mora::Value::make_keyword(pool.intern("ArmorRating")),
        mora::Value::make_int(3),
    });

    auto out_dir = make_temp_dir("homoint");
    write_snapshot(pool, sink_diags, db, out_dir);
    ASSERT_FALSE(sink_diags.has_errors());

    mora::StringPool rt_pool;
    mora::DiagBag    rt_diags;
    mora_skyrim_runtime::MockGameAPI api;

    auto count = mora_skyrim_runtime::runtime_apply(out_dir, api, rt_pool, rt_diags);
    EXPECT_FALSE(rt_diags.has_errors());
    EXPECT_EQ(count, 2u);
    ASSERT_EQ(api.calls.size(), 2u);

    EXPECT_EQ(api.calls[0].op,     "multiply");
    EXPECT_EQ(api.calls[0].target, 0xAAAAu);
    EXPECT_EQ(api.calls[0].field,  "Damage");
    EXPECT_EQ(api.calls[0].value.as_int(), 2);

    EXPECT_EQ(api.calls[1].op,     "multiply");
    EXPECT_EQ(api.calls[1].target, 0xBBBBu);
    EXPECT_EQ(api.calls[1].field,  "ArmorRating");
    EXPECT_EQ(api.calls[1].value.as_int(), 3);
}

} // namespace
