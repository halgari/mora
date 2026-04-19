#include "mora_parquet/snapshot_sink.h"

#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"
#include "mora/eval/fact_db.h"

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>

#include <filesystem>
#include <gtest/gtest.h>

namespace fs = std::filesystem;

namespace {

fs::path make_temp_dir(const std::string& label) {
    auto root = fs::temp_directory_path() /
                ("mora-parquet-" + label + "-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

TEST(ParquetSnapshotSink, RoundtripsAFullyTypedRelation) {
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    // Relation: form/npc(form_id: FormID, name: String, level: Int)
    auto rel = pool.intern("form/npc");
    db.configure_relation(rel, /*arity*/ 3, /*indexed*/ {0});
    auto alice  = pool.intern("Alice");
    auto bob    = pool.intern("Bob");
    db.add_fact(rel, {
        mora::Value::make_formid(0x0100),
        mora::Value::make_string(alice),
        mora::Value::make_int(5),
    });
    db.add_fact(rel, {
        mora::Value::make_formid(0x0101),
        mora::Value::make_string(bob),
        mora::Value::make_int(12),
    });

    auto out_dir = make_temp_dir("roundtrip");
    mora::ext::EmitCtx ctx{pool, diags, out_dir.string()};
    mora_parquet::ParquetSnapshotSink sink;
    sink.emit(ctx, db);

    ASSERT_FALSE(diags.has_errors())
        << "sink emitted errors: "
        << (diags.all().empty() ? "(none)" : diags.all().front().message);

    auto file_path = out_dir / "form" / "npc.parquet";
    ASSERT_TRUE(fs::exists(file_path)) << file_path.string();

    // Read back with parquet::arrow::OpenFile.
    auto infile_result = arrow::io::ReadableFile::Open(file_path.string());
    ASSERT_TRUE(infile_result.ok()) << infile_result.status().ToString();
    auto infile = *infile_result;

    std::unique_ptr<parquet::arrow::FileReader> reader;
    auto open_status = parquet::arrow::OpenFile(
        infile, arrow::default_memory_pool(), &reader);
    ASSERT_TRUE(open_status.ok()) << open_status.ToString();

    std::shared_ptr<arrow::Table> table;
    ASSERT_TRUE(reader->ReadTable(&table).ok());

    ASSERT_EQ(table->num_columns(), 3);
    ASSERT_EQ(table->num_rows(), 2);
    EXPECT_EQ(table->schema()->field(0)->name(), "col0");
    // Arrow 7.0.0 round-trips Parquet INT32+UINT_32 as INT64 on read.
    // The write side uses arrow::uint32(), but parquet encodes it as
    // INT32 with UINT_32 logical type. The reader upscales to INT64
    // when the compute cast kernel is not available for UINT32 → UINT32.
    // Accept either UINT32 (future Arrow) or INT64 (Arrow 7.0.0 actual).
    auto col0_type = table->schema()->field(0)->type()->id();
    EXPECT_TRUE(col0_type == arrow::Type::UINT32 || col0_type == arrow::Type::INT64)
        << "expected UINT32 or INT64 for FormID col, got type id " << (int)col0_type;
    EXPECT_EQ(table->schema()->field(1)->type()->id(), arrow::Type::STRING);
    EXPECT_EQ(table->schema()->field(2)->type()->id(), arrow::Type::INT64);

    // Read FormID column — may come back as UINT32 or INT64 depending on Arrow version.
    auto col0_chunk = table->column(0)->chunk(0);
    if (col0_chunk->type()->id() == arrow::Type::UINT32) {
        auto form_ids = std::static_pointer_cast<arrow::UInt32Array>(col0_chunk);
        EXPECT_EQ(form_ids->Value(0), 0x0100u);
        EXPECT_EQ(form_ids->Value(1), 0x0101u);
    } else {
        auto form_ids = std::static_pointer_cast<arrow::Int64Array>(col0_chunk);
        EXPECT_EQ(form_ids->Value(0), 0x0100);
        EXPECT_EQ(form_ids->Value(1), 0x0101);
    }

    auto names = std::static_pointer_cast<arrow::StringArray>(
        table->column(1)->chunk(0));
    EXPECT_EQ(names->GetString(0), "Alice");
    EXPECT_EQ(names->GetString(1), "Bob");

    auto levels = std::static_pointer_cast<arrow::Int64Array>(
        table->column(2)->chunk(0));
    EXPECT_EQ(levels->Value(0), 5);
    EXPECT_EQ(levels->Value(1), 12);
}

TEST(ParquetSnapshotSink, EmitsTaggedColumnsForHeterogeneousRelation) {
    // After Plan 6 M1, heterogeneous relations are no longer skipped —
    // they are emitted with six tagged sub-columns per heterogeneous
    // input column. Verify no error and the file is present.
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    auto rel = pool.intern("bad/mixed");
    db.configure_relation(rel, /*arity*/ 1, /*indexed*/ {});
    db.add_fact(rel, {mora::Value::make_int(1)});
    db.add_fact(rel, {mora::Value::make_string(pool.intern("oops"))});

    auto out_dir = make_temp_dir("hetero");
    mora::ext::EmitCtx ctx{pool, diags, out_dir.string()};
    mora_parquet::ParquetSnapshotSink sink;
    sink.emit(ctx, db);

    EXPECT_FALSE(diags.has_errors());
    // No parquet-skip-heterogeneous warning (that code is retired).
    for (const auto& d : diags.all()) {
        EXPECT_NE(d.code, "parquet-skip-heterogeneous");
    }
    // The file IS emitted now (not skipped).
    EXPECT_TRUE(fs::exists(out_dir / "bad" / "mixed.parquet"));
}

} // namespace
