#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/effect_facts.h"      // populate_effect_facts
#include "mora/eval/fact_db.h"
#include "mora/eval/patch_set.h"         // PatchSet, FieldOp
#include "mora/ext/extension.h"
#include "mora/model/relations.h"        // FieldId
#include "mora_parquet/register.h"
#include "mora_skyrim_compile/register.h"

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>

#include <filesystem>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

TEST(CliParquetSink, DispatchesConfiguredSink) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    // Populate the DB as if sema + eval already happened.
    auto rel = pool.intern("plugin_exists");
    db.configure_relation(rel, /*arity*/ 1, /*indexed*/ {0});
    db.add_fact(rel, {mora::Value::make_string(pool.intern("Skyrim.esm"))});
    db.add_fact(rel, {mora::Value::make_string(pool.intern("Update.esm"))});

    // Simulate what cmd_compile does: register sinks on an
    // ExtensionContext, then dispatch those named by --sink.
    mora::ext::ExtensionContext ctx;
    mora_parquet::register_parquet(ctx);

    auto out_dir = fs::temp_directory_path() /
                   ("mora-cli-sink-" + std::to_string(getpid()));
    fs::remove_all(out_dir);

    std::unordered_map<std::string, std::string> sink_configs = {
        {"parquet.snapshot", out_dir.string()},
    };

    for (const auto& sink : ctx.sinks()) {
        auto it = sink_configs.find(std::string(sink->name()));
        if (it == sink_configs.end()) continue;
        mora::ext::EmitCtx ectx{pool, diags, it->second};
        sink->emit(ectx, db);
    }

    ASSERT_FALSE(diags.has_errors())
        << (diags.all().empty() ? "(no diag)" : diags.all().front().message);

    auto file = out_dir / "plugin_exists.parquet";
    ASSERT_TRUE(fs::exists(file)) << file.string();

    auto infile = *arrow::io::ReadableFile::Open(file.string());
    std::unique_ptr<parquet::arrow::FileReader> reader;
    ASSERT_TRUE(parquet::arrow::OpenFile(
        infile, arrow::default_memory_pool(), &reader).ok());
    std::shared_ptr<arrow::Table> table;
    ASSERT_TRUE(reader->ReadTable(&table).ok());
    ASSERT_EQ(table->num_rows(), 2);
    ASSERT_EQ(table->num_columns(), 1);
}

TEST(CliParquetSink, NoSinkConfiguredProducesNoFiles) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    auto rel = pool.intern("plugin_exists");
    db.configure_relation(rel, /*arity*/ 1, /*indexed*/ {0});
    db.add_fact(rel, {mora::Value::make_string(pool.intern("Skyrim.esm"))});

    mora::ext::ExtensionContext ctx;
    mora_parquet::register_parquet(ctx);

    // Empty sink_configs map — no sink is dispatched.
    std::unordered_map<std::string, std::string> sink_configs;

    auto out_dir = fs::temp_directory_path() /
                   ("mora-cli-sink-empty-" + std::to_string(getpid()));
    fs::remove_all(out_dir);

    for (const auto& sink : ctx.sinks()) {
        auto it = sink_configs.find(std::string(sink->name()));
        if (it == sink_configs.end()) continue;
        mora::ext::EmitCtx ectx{pool, diags, it->second};
        sink->emit(ectx, db);
    }

    EXPECT_FALSE(diags.has_errors());
    EXPECT_FALSE(fs::exists(out_dir));
}

TEST(CliParquetSink, OutputOnlyFilterEmitsOnlyFlaggedRelations) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    // Register the real Skyrim bridge + the three is_output relations.
    mora::ext::ExtensionContext ctx;
    mora_skyrim_compile::register_skyrim(ctx);
    mora_parquet::register_parquet(ctx);

    // Non-output relation — should NOT be written when output-only.
    auto form_npc = pool.intern("npc");
    db.configure_relation(form_npc, /*arity*/ 1, /*indexed*/ {0});
    db.add_fact(form_npc, {mora::Value::make_formid(0x0100)});

    // Output relation (skyrim/set, arity 3 per register_skyrim) not
    // populated — the output-only path emits an empty parquet file.

    auto out_dir = fs::temp_directory_path() /
                   ("mora-output-only-" + std::to_string(getpid()));
    fs::remove_all(out_dir);

    std::unordered_map<std::string, std::string> sink_configs = {
        {"parquet.snapshot", out_dir.string() + "?output-only"},
    };

    for (const auto& sink : ctx.sinks()) {
        auto it = sink_configs.find(std::string(sink->name()));
        if (it == sink_configs.end()) continue;
        mora::ext::EmitCtx emit_ctx{pool, diags, it->second, &ctx};
        sink->emit(emit_ctx, db);
    }

    ASSERT_FALSE(diags.has_errors())
        << (diags.all().empty() ? "(no diag)" : diags.all().front().message);

    // npc should NOT be written.
    EXPECT_FALSE(fs::exists(out_dir / "npc.parquet"));

    // The three is_output relations should all be written (even empty).
    EXPECT_TRUE(fs::exists(out_dir / "skyrim" / "set.parquet"));
    EXPECT_TRUE(fs::exists(out_dir / "skyrim" / "add.parquet"));
    EXPECT_TRUE(fs::exists(out_dir / "skyrim" / "remove.parquet"));
}

TEST(CliParquetSink, EffectFactsBridgeRoundTripThroughTaggedColumns) {
    // Builds a synthetic PatchBuffer with three different value types
    // (Int, Float, FormID), runs the Plan 5 bridge to populate the
    // skyrim/set FactDB relation, dispatches the parquet sink with
    // output-only, then reads skyrim/set.parquet back and verifies the
    // tagged-column layout carries the three rows with correct kinds +
    // values.
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    mora::ext::ExtensionContext ctx;
    mora_skyrim_compile::register_skyrim(ctx);
    mora_parquet::register_parquet(ctx);

    mora::PatchSet ps;
    auto const form = uint32_t{0x000ABCDE};

    ps.add_patch(form, mora::FieldId::GoldValue, mora::FieldOp::Set,
                 mora::Value::make_int(750),
                 mora::StringId{}, /*priority*/ 0);

    double const weight = 2.5;
    ps.add_patch(form, mora::FieldId::Weight, mora::FieldOp::Set,
                 mora::Value::make_float(weight),
                 mora::StringId{}, /*priority*/ 0);

    ps.add_patch(form, mora::FieldId::Race, mora::FieldOp::Set,
                 mora::Value::make_formid(0x01337F),
                 mora::StringId{}, /*priority*/ 0);

    // String case — previously broken under the PatchBuffer path
    // because PatchValueType::StringIndex encoded a byte offset, not
    // a StringPool index. Plan 7's typed ResolvedPatchSet path carries
    // the StringId directly, so the string round-trips.
    ps.add_patch(form, mora::FieldId::Name, mora::FieldOp::Set,
                 mora::Value::make_string(pool.intern("Skeever")),
                 mora::StringId{}, /*priority*/ 0);

    mora::populate_effect_facts(ps.resolve(), db, pool);

    // Dispatch the parquet sink.
    auto out_dir = fs::temp_directory_path() /
                   ("mora-bridge-roundtrip-" + std::to_string(getpid()));
    fs::remove_all(out_dir);

    std::unordered_map<std::string, std::string> sink_configs = {
        {"parquet.snapshot", out_dir.string() + "?output-only"},
    };
    for (const auto& sink : ctx.sinks()) {
        auto it = sink_configs.find(std::string(sink->name()));
        if (it == sink_configs.end()) continue;
        mora::ext::EmitCtx ectx{pool, diags, it->second, &ctx};
        sink->emit(ectx, db);
    }

    ASSERT_FALSE(diags.has_errors())
        << (diags.all().empty() ? "(no diag)" : diags.all().front().message);

    auto file = out_dir / "skyrim" / "set.parquet";
    ASSERT_TRUE(fs::exists(file)) << file.string();

    // Read back and verify. skyrim/set schema: (FormID, Keyword, ANY).
    // Columns 0 and 1 are homogeneous; column 2 expands into six
    // tagged sub-columns. Expected Arrow fields:
    //   col0 uint32, col1 utf8,
    //   col2_kind utf8, col2_formid uint32, col2_int int64,
    //   col2_float float64, col2_string utf8, col2_bool bool.
    auto infile = *arrow::io::ReadableFile::Open(file.string());
    std::unique_ptr<parquet::arrow::FileReader> reader;
    ASSERT_TRUE(parquet::arrow::OpenFile(
        infile, arrow::default_memory_pool(), &reader).ok());
    std::shared_ptr<arrow::Table> table;
    ASSERT_TRUE(reader->ReadTable(&table).ok());

    ASSERT_EQ(table->num_columns(), 8);
    ASSERT_EQ(table->num_rows(), 4);

    auto kind_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(2)->chunk(0));
    auto int_col = std::static_pointer_cast<arrow::Int64Array>(
        table->column(4)->chunk(0));
    auto float_col = std::static_pointer_cast<arrow::DoubleArray>(
        table->column(5)->chunk(0));
    auto string_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(6)->chunk(0));
    auto field_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(1)->chunk(0));

    // Order depends on PatchSet iteration. Scan all rows and match by the
    // field-keyword column rather than asserting a specific row order —
    // robust to future sort changes.
    bool seen_int = false, seen_float = false, seen_formid = false;
    bool seen_string = false;
    for (int64_t i = 0; i < table->num_rows(); ++i) {
        auto field = field_col->GetString(i);
        auto kind  = kind_col->GetString(i);

        if (field == "GoldValue") {
            EXPECT_EQ(kind, "Int");
            EXPECT_FALSE(int_col->IsNull(i));
            EXPECT_EQ(int_col->Value(i), 750);
            seen_int = true;
        } else if (field == "Weight") {
            EXPECT_EQ(kind, "Float");
            EXPECT_FALSE(float_col->IsNull(i));
            EXPECT_DOUBLE_EQ(float_col->Value(i), 2.5);
            seen_float = true;
        } else if (field == "Race") {
            EXPECT_EQ(kind, "FormID");
            // Arrow 7.0.0 may widen UInt32 to Int64 on read-back when
            // the compute library isn't linked. Accept either type
            // via schema inspection rather than a hardcoded cast.
            auto col3_type = table->column(3)->type()->id();
            if (col3_type == arrow::Type::UINT32) {
                auto formid_col = std::static_pointer_cast<arrow::UInt32Array>(
                    table->column(3)->chunk(0));
                EXPECT_FALSE(formid_col->IsNull(i));
                EXPECT_EQ(formid_col->Value(i), 0x01337Fu);
            } else {
                auto formid_col = std::static_pointer_cast<arrow::Int64Array>(
                    table->column(3)->chunk(0));
                EXPECT_FALSE(formid_col->IsNull(i));
                EXPECT_EQ(formid_col->Value(i), 0x01337F);
            }
            seen_formid = true;
        } else if (field == "Name") {
            EXPECT_EQ(kind, "String");
            EXPECT_FALSE(string_col->IsNull(i));
            EXPECT_EQ(string_col->GetString(i), "Skeever");
            seen_string = true;
        }
    }

    EXPECT_TRUE(seen_int);
    EXPECT_TRUE(seen_float);
    EXPECT_TRUE(seen_formid);
    EXPECT_TRUE(seen_string);
}

} // namespace
