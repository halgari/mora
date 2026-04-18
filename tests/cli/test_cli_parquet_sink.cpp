#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/fact_db.h"
#include "mora/ext/extension.h"
#include "mora_parquet/register.h"

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

} // namespace
