#include "mora_skyrim_runtime/runtime.h"
#include "mora_skyrim_runtime/game_api.h"

#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/diag/diagnostic.h"

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/reader.h>

#include <fmt/format.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>

namespace mora_skyrim_runtime {

namespace fs = std::filesystem;

namespace {

// Returns the Arrow schema field index for the given name, or -1 if not found.
int find_field(const std::shared_ptr<arrow::Schema>& schema, const std::string& name) {
    return schema->GetFieldIndex(name);
}

// Attempt to get a StringArray chunk from a table column. Handles both
// Utf8 (STRING) and LargeUtf8 (LARGE_STRING) by returning the chunk's
// StringArray directly. Returns nullptr if the type is unexpected.
std::shared_ptr<arrow::StringArray> get_string_array(
    const std::shared_ptr<arrow::ChunkedArray>& col)
{
    if (col->num_chunks() == 0) return nullptr;
    auto chunk = col->chunk(0);
    if (chunk->type()->id() == arrow::Type::STRING) {
        return std::static_pointer_cast<arrow::StringArray>(chunk);
    }
    return nullptr;
}

// Struct holding decoded column arrays for a single parquet table.
struct Col2Homogeneous {
    std::shared_ptr<arrow::Array> array;
    arrow::Type::type             type_id;
};

struct Col2Tagged {
    std::shared_ptr<arrow::StringArray>  kind_arr;
    std::shared_ptr<arrow::Array>        formid_arr;  // may be Int64 or UInt32
    std::shared_ptr<arrow::Int64Array>   int_arr;
    std::shared_ptr<arrow::DoubleArray>  float_arr;
    std::shared_ptr<arrow::StringArray>  string_arr;
    std::shared_ptr<arrow::BooleanArray> bool_arr;
};

// Decode value from a homogeneous col2 at row r.
mora::Value decode_homogeneous(const Col2Homogeneous& h, int64_t r,
                                mora::StringPool& pool) {
    switch (h.type_id) {
        case arrow::Type::UINT32: {
            auto arr = std::static_pointer_cast<arrow::UInt32Array>(h.array);
            return mora::Value::make_formid(arr->Value(r));
        }
        case arrow::Type::INT32: {
            // Arrow 7.0.0 reads Parquet INT32+UINT_32 as INT32 in some cases;
            // treat it as FormID per the sink's encoding contract.
            auto arr = std::static_pointer_cast<arrow::Int32Array>(h.array);
            return mora::Value::make_formid(static_cast<uint32_t>(arr->Value(r)));
        }
        case arrow::Type::INT64: {
            auto arr = std::static_pointer_cast<arrow::Int64Array>(h.array);
            return mora::Value::make_int(arr->Value(r));
        }
        case arrow::Type::DOUBLE: {
            auto arr = std::static_pointer_cast<arrow::DoubleArray>(h.array);
            return mora::Value::make_float(arr->Value(r));
        }
        case arrow::Type::STRING: {
            auto arr = std::static_pointer_cast<arrow::StringArray>(h.array);
            auto sv = arr->GetString(r);
            return mora::Value::make_string(pool.intern(sv));
        }
        case arrow::Type::BOOL: {
            auto arr = std::static_pointer_cast<arrow::BooleanArray>(h.array);
            return mora::Value::make_bool(arr->Value(r));
        }
        default:
            // Fallback — return a placeholder int.
            return mora::Value::make_int(0);
    }
}

// Decode value from tagged col2 sub-columns at row r.
mora::Value decode_tagged(const Col2Tagged& t, int64_t r, mora::StringPool& pool) {
    auto kind_str = t.kind_arr->GetString(r);

    if (kind_str == "FormID") {
        if (!t.formid_arr || t.formid_arr->IsNull(r)) return mora::Value::make_formid(0);
        if (t.formid_arr->type()->id() == arrow::Type::UINT32) {
            auto arr = std::static_pointer_cast<arrow::UInt32Array>(t.formid_arr);
            return mora::Value::make_formid(arr->Value(r));
        } else {
            // Arrow 7.0.0 round-trips uint32 as INT64 on read
            auto arr = std::static_pointer_cast<arrow::Int64Array>(t.formid_arr);
            return mora::Value::make_formid(static_cast<uint32_t>(arr->Value(r)));
        }
    } else if (kind_str == "Int") {
        if (!t.int_arr || t.int_arr->IsNull(r)) return mora::Value::make_int(0);
        return mora::Value::make_int(t.int_arr->Value(r));
    } else if (kind_str == "Float") {
        if (!t.float_arr || t.float_arr->IsNull(r)) return mora::Value::make_float(0.0);
        return mora::Value::make_float(t.float_arr->Value(r));
    } else if (kind_str == "String") {
        if (!t.string_arr || t.string_arr->IsNull(r)) return mora::Value::make_string(pool.intern(""));
        return mora::Value::make_string(pool.intern(t.string_arr->GetString(r)));
    } else if (kind_str == "Keyword") {
        if (!t.string_arr || t.string_arr->IsNull(r)) return mora::Value::make_keyword(pool.intern(""));
        return mora::Value::make_keyword(pool.intern(t.string_arr->GetString(r)));
    } else if (kind_str == "Bool") {
        if (!t.bool_arr || t.bool_arr->IsNull(r)) return mora::Value::make_bool(false);
        return mora::Value::make_bool(t.bool_arr->Value(r));
    }
    // Unknown kind — return placeholder
    return mora::Value::make_int(0);
}

// Process one parquet file for the given op. Returns number of rows dispatched.
size_t process_op_file(const fs::path& path,
                        const std::string& op,
                        GameAPI& api,
                        mora::StringPool& pool,
                        mora::DiagBag& diags) {
    // Open file
    auto infile_result = arrow::io::ReadableFile::Open(path.string());
    if (!infile_result.ok()) {
        diags.error("skyrim-runtime-open",
            fmt::format("skyrim_runtime: cannot open {}: {}",
                        path.string(), infile_result.status().ToString()),
            mora::SourceSpan{}, "");
        return 0;
    }
    auto infile = *infile_result;

    std::unique_ptr<parquet::arrow::FileReader> reader;
    auto open_status = parquet::arrow::OpenFile(
        infile, arrow::default_memory_pool(), &reader);
    if (!open_status.ok()) {
        diags.error("skyrim-runtime-open",
            fmt::format("skyrim_runtime: cannot open parquet file {}: {}",
                        path.string(), open_status.ToString()),
            mora::SourceSpan{}, "");
        return 0;
    }

    std::shared_ptr<arrow::Table> table;
    auto read_status = reader->ReadTable(&table);
    if (!read_status.ok()) {
        diags.error("skyrim-runtime-read",
            fmt::format("skyrim_runtime: ReadTable failed for {}: {}",
                        path.string(), read_status.ToString()),
            mora::SourceSpan{}, "");
        return 0;
    }

    auto schema = table->schema();

    // Find col0 (FormID — UInt32 or INT64 depending on Arrow version)
    int col0_idx = find_field(schema, "col0");
    if (col0_idx < 0) {
        diags.error("skyrim-runtime-schema",
            fmt::format("skyrim_runtime: missing col0 in {}", path.string()),
            mora::SourceSpan{}, "");
        return 0;
    }
    auto col0_chunked = table->column(col0_idx);
    if (col0_chunked->num_chunks() == 0) return 0;
    auto col0_chunk = col0_chunked->chunk(0);
    auto col0_type  = col0_chunk->type()->id();
    if (col0_type != arrow::Type::UINT32 && col0_type != arrow::Type::INT64 &&
        col0_type != arrow::Type::INT32) {
        diags.error("skyrim-runtime-schema",
            fmt::format("skyrim_runtime: col0 has unexpected type in {}",
                        path.string()),
            mora::SourceSpan{}, "");
        return 0;
    }

    // Find col1 (field keyword — Utf8)
    int col1_idx = find_field(schema, "col1");
    if (col1_idx < 0) {
        diags.error("skyrim-runtime-schema",
            fmt::format("skyrim_runtime: missing col1 in {}", path.string()),
            mora::SourceSpan{}, "");
        return 0;
    }
    auto col1_arr = get_string_array(table->column(col1_idx));
    if (!col1_arr) {
        diags.error("skyrim-runtime-schema",
            fmt::format("skyrim_runtime: col1 is not Utf8 in {}", path.string()),
            mora::SourceSpan{}, "");
        return 0;
    }

    // Detect col2 encoding: homogeneous ("col2") or tagged ("col2_kind" + ...)
    int col2_idx      = find_field(schema, "col2");
    int col2_kind_idx = find_field(schema, "col2_kind");

    bool is_tagged = (col2_kind_idx >= 0);

    // Decode col2 into one of two holders
    Col2Homogeneous col2_homo{};
    Col2Tagged      col2_tag{};

    if (is_tagged) {
        // Tagged encoding
        if (col2_kind_idx < 0 || col2_kind_idx >= (int)table->num_columns()) {
            diags.error("skyrim-runtime-schema",
                fmt::format("skyrim_runtime: col2_kind missing in {}", path.string()),
                mora::SourceSpan{}, "");
            return 0;
        }
        col2_tag.kind_arr = std::static_pointer_cast<arrow::StringArray>(
            table->column(col2_kind_idx)->chunk(0));

        auto load_col = [&](const std::string& name) -> std::shared_ptr<arrow::Array> {
            int idx = find_field(schema, name);
            if (idx < 0 || table->column(idx)->num_chunks() == 0) return nullptr;
            return table->column(idx)->chunk(0);
        };

        col2_tag.formid_arr = load_col("col2_formid");
        auto int_chunk      = load_col("col2_int");
        auto float_chunk    = load_col("col2_float");
        auto string_chunk   = load_col("col2_string");
        auto bool_chunk     = load_col("col2_bool");

        if (int_chunk && int_chunk->type()->id() == arrow::Type::INT64)
            col2_tag.int_arr = std::static_pointer_cast<arrow::Int64Array>(int_chunk);
        if (float_chunk && float_chunk->type()->id() == arrow::Type::DOUBLE)
            col2_tag.float_arr = std::static_pointer_cast<arrow::DoubleArray>(float_chunk);
        if (string_chunk && string_chunk->type()->id() == arrow::Type::STRING)
            col2_tag.string_arr = std::static_pointer_cast<arrow::StringArray>(string_chunk);
        if (bool_chunk && bool_chunk->type()->id() == arrow::Type::BOOL)
            col2_tag.bool_arr = std::static_pointer_cast<arrow::BooleanArray>(bool_chunk);
    } else {
        if (col2_idx < 0) {
            diags.error("skyrim-runtime-schema",
                fmt::format("skyrim_runtime: no col2 or col2_kind in {}", path.string()),
                mora::SourceSpan{}, "");
            return 0;
        }
        if (table->column(col2_idx)->num_chunks() == 0) return 0;
        col2_homo.array   = table->column(col2_idx)->chunk(0);
        col2_homo.type_id = col2_homo.array->type()->id();
    }

    int64_t num_rows = table->num_rows();
    size_t dispatched = 0;

    for (int64_t r = 0; r < num_rows; ++r) {
        // Decode FormID from col0
        uint32_t formid = 0;
        if (col0_type == arrow::Type::UINT32) {
            formid = std::static_pointer_cast<arrow::UInt32Array>(col0_chunk)->Value(r);
        } else if (col0_type == arrow::Type::INT32) {
            formid = static_cast<uint32_t>(
                std::static_pointer_cast<arrow::Int32Array>(col0_chunk)->Value(r));
        } else {
            formid = static_cast<uint32_t>(
                std::static_pointer_cast<arrow::Int64Array>(col0_chunk)->Value(r));
        }

        // Decode field name from col1
        std::string field = col1_arr->GetString(r);

        // Decode value from col2
        mora::Value val = is_tagged
            ? decode_tagged(col2_tag, r, pool)
            : decode_homogeneous(col2_homo, r, pool);

        // Dispatch
        if (op == "set") {
            api.set(formid, field, val);
        } else if (op == "add") {
            api.add(formid, field, val);
        } else if (op == "remove") {
            api.remove(formid, field, val);
        } else if (op == "multiply") {
            api.multiply(formid, field, val);
        }
        ++dispatched;
    }

    return dispatched;
}

} // namespace

size_t runtime_apply(const std::filesystem::path& parquet_dir,
                      GameAPI&                     api,
                      mora::StringPool&            pool,
                      mora::DiagBag&               diags) {
    constexpr std::array<const char*, 4> kOps = {"set", "add", "remove", "multiply"};
    size_t total = 0;

    for (const char* op : kOps) {
        fs::path p = parquet_dir / "skyrim" / (std::string(op) + ".parquet");
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            diags.warning("skyrim-runtime-missing",
                fmt::format("skyrim_runtime: missing parquet file {} — skipping op '{}'",
                            p.string(), op),
                mora::SourceSpan{}, "");
            continue;
        }
        total += process_op_file(p, op, api, pool, diags);
    }

    return total;
}

} // namespace mora_skyrim_runtime
