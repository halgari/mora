#include "mora_parquet/snapshot_sink.h"

#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/eval/fact_db.h"

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/writer.h>

#include <fmt/format.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace mora_parquet {

namespace {

namespace fs = std::filesystem;

// Translate one mora::Value::Kind into an Arrow type. Returns nullptr
// for List / Var, which callers interpret as "skip this relation".
std::shared_ptr<arrow::DataType> arrow_type_for(mora::Value::Kind k) {
    switch (k) {
        case mora::Value::Kind::FormID: return arrow::uint32();
        case mora::Value::Kind::Int:    return arrow::int64();
        case mora::Value::Kind::Float:  return arrow::float64();
        case mora::Value::Kind::String: return arrow::utf8();
        case mora::Value::Kind::Bool:   return arrow::boolean();
        case mora::Value::Kind::Var:
        case mora::Value::Kind::List:   return nullptr;
    }
    return nullptr;
}

// Build an Arrow array for a single column. Precondition: every tuple
// has at least `col + 1` values; every value at position `col` has kind
// equal to `kind`. Caller enforces both invariants before calling.
arrow::Result<std::shared_ptr<arrow::Array>>
build_column(const std::vector<mora::Tuple>& tuples,
             std::size_t col, mora::Value::Kind kind,
             const mora::StringPool& pool) {
    using K = mora::Value::Kind;
    switch (kind) {
        case K::FormID: {
            arrow::UInt32Builder b;
            ARROW_RETURN_NOT_OK(b.Reserve(tuples.size()));
            for (const auto& t : tuples) b.UnsafeAppend(t[col].as_formid());
            std::shared_ptr<arrow::Array> out; ARROW_RETURN_NOT_OK(b.Finish(&out));
            return out;
        }
        case K::Int: {
            arrow::Int64Builder b;
            ARROW_RETURN_NOT_OK(b.Reserve(tuples.size()));
            for (const auto& t : tuples) b.UnsafeAppend(t[col].as_int());
            std::shared_ptr<arrow::Array> out; ARROW_RETURN_NOT_OK(b.Finish(&out));
            return out;
        }
        case K::Float: {
            arrow::DoubleBuilder b;
            ARROW_RETURN_NOT_OK(b.Reserve(tuples.size()));
            for (const auto& t : tuples) b.UnsafeAppend(t[col].as_float());
            std::shared_ptr<arrow::Array> out; ARROW_RETURN_NOT_OK(b.Finish(&out));
            return out;
        }
        case K::String: {
            arrow::StringBuilder b;
            ARROW_RETURN_NOT_OK(b.Reserve(tuples.size()));
            for (const auto& t : tuples) {
                auto sv = pool.get(t[col].as_string());
                ARROW_RETURN_NOT_OK(b.Append(sv.data(), sv.size()));
            }
            std::shared_ptr<arrow::Array> out; ARROW_RETURN_NOT_OK(b.Finish(&out));
            return out;
        }
        case K::Bool: {
            arrow::BooleanBuilder b;
            ARROW_RETURN_NOT_OK(b.Reserve(tuples.size()));
            for (const auto& t : tuples) b.UnsafeAppend(t[col].as_bool());
            std::shared_ptr<arrow::Array> out; ARROW_RETURN_NOT_OK(b.Finish(&out));
            return out;
        }
        case K::Var:
        case K::List:
            return arrow::Status::NotImplemented("unsupported kind for parquet emit");
    }
    return arrow::Status::UnknownError("unreachable");
}

// Translate a relation name like "form/npc" into a filesystem-safe
// relative path like "form/npc.parquet". Namespace separators in mora
// relation names are always '/', matching filesystem separators.
fs::path parquet_path_for(std::string_view rel_name, const fs::path& root) {
    return root / (std::string(rel_name) + ".parquet");
}

} // namespace

std::string_view ParquetSnapshotSink::name() const {
    return "parquet.snapshot";
}

void ParquetSnapshotSink::emit(mora::ext::EmitCtx& ctx,
                                const mora::FactDB& db) {
    if (ctx.config.empty()) {
        ctx.diags.error("parquet-config",
            "parquet.snapshot requires a config path: "
            "--sink parquet.snapshot=<out-dir>",
            mora::SourceSpan{}, "");
        return;
    }

    fs::path root(ctx.config);
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        ctx.diags.error("parquet-mkdir",
            fmt::format("parquet.snapshot: cannot create output dir {}: {}",
                        root.string(), ec.message()),
            mora::SourceSpan{}, "");
        return;
    }

    for (auto rel_id : db.all_relation_names()) {
        const auto rel_name = ctx.pool.get(rel_id);
        const auto& tuples = db.get_relation(rel_id);
        if (tuples.empty()) continue;

        const std::size_t arity = tuples.front().size();
        std::vector<mora::Value::Kind> kinds(arity);
        for (std::size_t c = 0; c < arity; ++c) {
            kinds[c] = tuples.front()[c].kind();
        }

        // Skip relations whose first row contains List / Var or whose
        // later rows disagree on column kind.
        bool skip_unsupported = false;
        for (std::size_t c = 0; c < arity; ++c) {
            if (arrow_type_for(kinds[c]) == nullptr) {
                skip_unsupported = true;
                break;
            }
        }
        if (skip_unsupported) {
            ctx.diags.warning("parquet-skip-unsupported-kind",
                fmt::format("parquet.snapshot: skipping relation '{}' — "
                            "column contains List or Var value (not supported in v1)",
                            rel_name),
                mora::SourceSpan{}, "");
            continue;
        }
        bool skip_heterogeneous = false;
        for (const auto& t : tuples) {
            if (t.size() != arity) { skip_heterogeneous = true; break; }
            for (std::size_t c = 0; c < arity; ++c) {
                if (t[c].kind() != kinds[c]) { skip_heterogeneous = true; break; }
            }
            if (skip_heterogeneous) break;
        }
        if (skip_heterogeneous) {
            ctx.diags.warning("parquet-skip-heterogeneous",
                fmt::format("parquet.snapshot: skipping relation '{}' — "
                            "tuples have inconsistent arity or per-column kinds",
                            rel_name),
                mora::SourceSpan{}, "");
            continue;
        }

        // Build Arrow schema + columns.
        std::vector<std::shared_ptr<arrow::Field>> fields;
        std::vector<std::shared_ptr<arrow::Array>> columns;
        fields.reserve(arity);
        columns.reserve(arity);

        bool column_failed = false;
        for (std::size_t c = 0; c < arity; ++c) {
            fields.push_back(arrow::field(fmt::format("col{}", c),
                                           arrow_type_for(kinds[c])));
            auto col = build_column(tuples, c, kinds[c], ctx.pool);
            if (!col.ok()) {
                ctx.diags.error("parquet-build-column",
                    fmt::format("parquet.snapshot: failed to build column {} "
                                "of relation '{}': {}",
                                c, rel_name, col.status().ToString()),
                    mora::SourceSpan{}, "");
                column_failed = true;
                break;
            }
            columns.push_back(*col);
        }
        if (column_failed) continue;

        auto schema = arrow::schema(fields);
        auto table  = arrow::Table::Make(schema, columns);

        auto out_path = parquet_path_for(rel_name, root);
        std::error_code mk_ec;
        fs::create_directories(out_path.parent_path(), mk_ec);
        if (mk_ec) {
            ctx.diags.error("parquet-mkdir",
                fmt::format("parquet.snapshot: cannot create dir {}: {}",
                            out_path.parent_path().string(), mk_ec.message()),
                mora::SourceSpan{}, "");
            continue;
        }

        auto outfile = arrow::io::FileOutputStream::Open(out_path.string());
        if (!outfile.ok()) {
            ctx.diags.error("parquet-open",
                fmt::format("parquet.snapshot: cannot open {} for writing: {}",
                            out_path.string(), outfile.status().ToString()),
                mora::SourceSpan{}, "");
            continue;
        }

        // Default writer properties; no compression, no dictionary encoding
        // tweaks. Plan 3 gets correctness; performance tuning lands later.
        auto write_status = parquet::arrow::WriteTable(
            *table, arrow::default_memory_pool(), *outfile,
            /*chunk_size*/ 64 * 1024);
        if (!write_status.ok()) {
            ctx.diags.error("parquet-write",
                fmt::format("parquet.snapshot: WriteTable failed for {}: {}",
                            out_path.string(), write_status.ToString()),
                mora::SourceSpan{}, "");
            continue;
        }
    }
}

} // namespace mora_parquet
