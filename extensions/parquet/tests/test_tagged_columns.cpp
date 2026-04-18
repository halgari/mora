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
                ("mora-tagged-" + label + "-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

// Helper: read a parquet file into an arrow::Table.
std::shared_ptr<arrow::Table> read_table(const fs::path& path) {
    auto infile = *arrow::io::ReadableFile::Open(path.string());
    std::unique_ptr<parquet::arrow::FileReader> reader;
    auto open = parquet::arrow::OpenFile(
        infile, arrow::default_memory_pool(), &reader);
    EXPECT_TRUE(open.ok()) << open.ToString();
    std::shared_ptr<arrow::Table> table;
    EXPECT_TRUE(reader->ReadTable(&table).ok());
    return table;
}

TEST(ParquetTaggedColumns, HeterogeneousValueColumnEmitsSixSubColumns) {
    // Relation: effect/set(entity: FormID, field: Keyword, value: ANY)
    // Value column is heterogeneous across rows: Int, Float, String, FormID.
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    auto rel = pool.intern("effect/set");
    db.configure_relation(rel, /*arity*/ 3, /*indexed*/ {0});

    db.add_fact(rel, {
        mora::Value::make_formid(0x0100),
        mora::Value::make_keyword(pool.intern("GoldValue")),
        mora::Value::make_int(100),
    });
    db.add_fact(rel, {
        mora::Value::make_formid(0x0101),
        mora::Value::make_keyword(pool.intern("Damage")),
        mora::Value::make_float(12.5),
    });
    db.add_fact(rel, {
        mora::Value::make_formid(0x0102),
        mora::Value::make_keyword(pool.intern("Name")),
        mora::Value::make_string(pool.intern("Skeever")),
    });
    db.add_fact(rel, {
        mora::Value::make_formid(0x0103),
        mora::Value::make_keyword(pool.intern("Race")),
        mora::Value::make_formid(0x01337F),
    });

    auto out_dir = make_temp_dir("roundtrip");
    mora::ext::EmitCtx ctx{pool, diags, out_dir.string()};
    mora_parquet::ParquetSnapshotSink sink;
    sink.emit(ctx, db);

    ASSERT_FALSE(diags.has_errors())
        << (diags.all().empty() ? "(no diag)" : diags.all().front().message);

    auto file = out_dir / "effect" / "set.parquet";
    ASSERT_TRUE(fs::exists(file)) << file.string();

    auto table = read_table(file);
    ASSERT_NE(table, nullptr);

    // Expected fields: col0 (uint32), col1 (utf8),
    // col2_kind (utf8), col2_formid (uint32),
    // col2_int (int64), col2_float (float64),
    // col2_string (utf8), col2_bool (bool).
    ASSERT_EQ(table->num_columns(), 8);
    EXPECT_EQ(table->schema()->field(0)->name(), "col0");
    EXPECT_EQ(table->schema()->field(1)->name(), "col1");
    EXPECT_EQ(table->schema()->field(2)->name(), "col2_kind");
    EXPECT_EQ(table->schema()->field(3)->name(), "col2_formid");
    EXPECT_EQ(table->schema()->field(4)->name(), "col2_int");
    EXPECT_EQ(table->schema()->field(5)->name(), "col2_float");
    EXPECT_EQ(table->schema()->field(6)->name(), "col2_string");
    EXPECT_EQ(table->schema()->field(7)->name(), "col2_bool");

    ASSERT_EQ(table->num_rows(), 4);

    auto kind_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(2)->chunk(0));
    // Arrow 7.0.0 round-trips uint32 Parquet columns as INT64 on read.
    // Accept either type for the formid sub-column.
    auto formid_chunk = table->column(3)->chunk(0);
    auto int_col = std::static_pointer_cast<arrow::Int64Array>(
        table->column(4)->chunk(0));
    auto float_col = std::static_pointer_cast<arrow::DoubleArray>(
        table->column(5)->chunk(0));
    auto string_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(6)->chunk(0));
    auto bool_col = std::static_pointer_cast<arrow::BooleanArray>(
        table->column(7)->chunk(0));

    // Row 0: Int 100
    EXPECT_EQ(kind_col->GetString(0), "Int");
    EXPECT_TRUE(formid_chunk->IsNull(0));
    EXPECT_FALSE(int_col->IsNull(0));
    EXPECT_EQ(int_col->Value(0), 100);
    EXPECT_TRUE(float_col->IsNull(0));
    EXPECT_TRUE(string_col->IsNull(0));
    EXPECT_TRUE(bool_col->IsNull(0));

    // Row 1: Float 12.5
    EXPECT_EQ(kind_col->GetString(1), "Float");
    EXPECT_TRUE(formid_chunk->IsNull(1));
    EXPECT_TRUE(int_col->IsNull(1));
    EXPECT_FALSE(float_col->IsNull(1));
    EXPECT_DOUBLE_EQ(float_col->Value(1), 12.5);
    EXPECT_TRUE(string_col->IsNull(1));
    EXPECT_TRUE(bool_col->IsNull(1));

    // Row 2: String "Skeever"
    EXPECT_EQ(kind_col->GetString(2), "String");
    EXPECT_TRUE(formid_chunk->IsNull(2));
    EXPECT_TRUE(int_col->IsNull(2));
    EXPECT_TRUE(float_col->IsNull(2));
    EXPECT_FALSE(string_col->IsNull(2));
    EXPECT_EQ(string_col->GetString(2), "Skeever");
    EXPECT_TRUE(bool_col->IsNull(2));

    // Row 3: FormID 0x01337F
    EXPECT_EQ(kind_col->GetString(3), "FormID");
    EXPECT_FALSE(formid_chunk->IsNull(3));
    // Arrow 7.0.0 reads uint32 parquet columns back as INT64.
    if (formid_chunk->type()->id() == arrow::Type::UINT32) {
        auto fc = std::static_pointer_cast<arrow::UInt32Array>(formid_chunk);
        EXPECT_EQ(fc->Value(3), 0x01337Fu);
    } else {
        auto fc = std::static_pointer_cast<arrow::Int64Array>(formid_chunk);
        EXPECT_EQ(fc->Value(3), 0x01337F);
    }
    EXPECT_TRUE(int_col->IsNull(3));
    EXPECT_TRUE(float_col->IsNull(3));
    EXPECT_TRUE(string_col->IsNull(3));
    EXPECT_TRUE(bool_col->IsNull(3));
}

TEST(ParquetTaggedColumns, HomogeneousColumnStaysTyped) {
    // Regression: ensure the homogeneous fast path still emits a single
    // typed column named "col0" (NOT "col0_kind" etc.) when every tuple
    // in a column shares the same kind.
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    auto rel = pool.intern("form/npc");
    db.configure_relation(rel, /*arity*/ 2, /*indexed*/ {0});

    db.add_fact(rel, {
        mora::Value::make_formid(0x0100),
        mora::Value::make_string(pool.intern("Alice")),
    });
    db.add_fact(rel, {
        mora::Value::make_formid(0x0101),
        mora::Value::make_string(pool.intern("Bob")),
    });

    auto out_dir = make_temp_dir("homogeneous");
    mora::ext::EmitCtx ctx{pool, diags, out_dir.string()};
    mora_parquet::ParquetSnapshotSink sink;
    sink.emit(ctx, db);

    ASSERT_FALSE(diags.has_errors());
    auto table = read_table(out_dir / "form" / "npc.parquet");

    ASSERT_EQ(table->num_columns(), 2);
    EXPECT_EQ(table->schema()->field(0)->name(), "col0");
    // Arrow 7.0.0 round-trips Parquet INT32+UINT_32 as INT64 on read.
    {
        auto col0_type = table->schema()->field(0)->type()->id();
        EXPECT_TRUE(col0_type == arrow::Type::UINT32 || col0_type == arrow::Type::INT64)
            << "expected UINT32 or INT64 for FormID col, got type id " << (int)col0_type;
    }
    EXPECT_EQ(table->schema()->field(1)->name(), "col1");
    EXPECT_EQ(table->schema()->field(1)->type()->id(), arrow::Type::STRING);
}

TEST(ParquetTaggedColumns, KeywordInHeterogeneousColumnTaggedAsKeyword) {
    // Special case: Value::Kind::Keyword shares storage with String but
    // is a distinct kind. In a tagged column the _kind tag must be
    // "Keyword" while the payload lands in _string. Consumers rely on
    // the kind tag to tell them apart.
    mora::StringPool pool;
    mora::DiagBag diags;
    mora::FactDB db(pool);

    auto rel = pool.intern("kw/test");
    db.configure_relation(rel, /*arity*/ 1, /*indexed*/ {});

    db.add_fact(rel, {mora::Value::make_string(pool.intern("plain"))});
    db.add_fact(rel, {mora::Value::make_keyword(pool.intern("tagged"))});

    auto out_dir = make_temp_dir("keyword");
    mora::ext::EmitCtx ctx{pool, diags, out_dir.string()};
    mora_parquet::ParquetSnapshotSink sink;
    sink.emit(ctx, db);

    ASSERT_FALSE(diags.has_errors());
    auto table = read_table(out_dir / "kw" / "test.parquet");

    ASSERT_EQ(table->num_columns(), 6);   // 6 tagged sub-columns, no base col
    EXPECT_EQ(table->schema()->field(0)->name(), "col0_kind");
    EXPECT_EQ(table->schema()->field(4)->name(), "col0_string");

    auto kind_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(0)->chunk(0));
    auto string_col = std::static_pointer_cast<arrow::StringArray>(
        table->column(4)->chunk(0));

    EXPECT_EQ(kind_col->GetString(0), "String");
    EXPECT_EQ(string_col->GetString(0), "plain");

    EXPECT_EQ(kind_col->GetString(1), "Keyword");
    EXPECT_EQ(string_col->GetString(1), "tagged");
}

} // namespace
