#include "mora_parquet/snapshot_sink.h"

#include "mora/core/string_pool.h"
#include "mora/data/value.h"
#include "mora/eval/fact_db.h"
#include "mora/ext/extension.h"

#include <arrow/api.h>
#include <arrow/io/file.h>
#include <parquet/arrow/writer.h>

#include <fmt/format.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
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
        case mora::Value::Kind::String:  return arrow::utf8();
        case mora::Value::Kind::Keyword: return arrow::utf8();
        case mora::Value::Kind::Bool:    return arrow::boolean();
        case mora::Value::Kind::Var:
        case mora::Value::Kind::List:    return nullptr;
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
        case K::Keyword: {
            arrow::StringBuilder b;
            ARROW_RETURN_NOT_OK(b.Reserve(tuples.size()));
            for (const auto& t : tuples) {
                auto sv = pool.get(t[col].as_keyword());
                ARROW_RETURN_NOT_OK(b.Append(sv.data(), sv.size()));
            }
            std::shared_ptr<arrow::Array> out; ARROW_RETURN_NOT_OK(b.Finish(&out));
            return out;
        }
        case K::Var:
        case K::List:
            return arrow::Status::NotImplemented("unsupported kind for parquet emit");
    }
    return arrow::Status::UnknownError("unreachable");
}

// Names used in the `_kind` string column to identify which typed
// sub-column holds the row's value. Keep this list in sync with
// Value::Kind.
const char* kind_name_for(mora::Value::Kind k) {
    switch (k) {
        case mora::Value::Kind::FormID:  return "FormID";
        case mora::Value::Kind::Int:     return "Int";
        case mora::Value::Kind::Float:   return "Float";
        case mora::Value::Kind::String:  return "String";
        case mora::Value::Kind::Keyword: return "Keyword";
        case mora::Value::Kind::Bool:    return "Bool";
        case mora::Value::Kind::Var:
        case mora::Value::Kind::List:    return "Unsupported";
    }
    return "Unknown";
}

// Build the six Arrow arrays that make up a tagged-column group for a
// heterogeneous column at position `col`. Order: kind, formid, int,
// float, string, bool. Exactly one of the last five is non-null per
// row, selected by the kind. `Keyword` values land in `_string` with
// the kind tag set to "Keyword" (Plan 6 uses six sub-columns, not
// seven — keyword payload is an interned string either way).
//
// Precondition: every tuple has at least `col + 1` values. Only Bool /
// FormID / Int / Float / String / Keyword kinds are expected here —
// unsupported kinds (Var/List) trigger a relation-level skip before
// this function is called.
arrow::Result<std::vector<std::shared_ptr<arrow::Array>>>
build_tagged_columns(const std::vector<mora::Tuple>& tuples,
                     std::size_t col,
                     const mora::StringPool& pool) {
    arrow::StringBuilder  b_kind;
    arrow::UInt32Builder  b_formid;
    arrow::Int64Builder   b_int;
    arrow::DoubleBuilder  b_float;
    arrow::StringBuilder  b_string;
    arrow::BooleanBuilder b_bool;

    ARROW_RETURN_NOT_OK(b_kind.Reserve(tuples.size()));
    ARROW_RETURN_NOT_OK(b_formid.Reserve(tuples.size()));
    ARROW_RETURN_NOT_OK(b_int.Reserve(tuples.size()));
    ARROW_RETURN_NOT_OK(b_float.Reserve(tuples.size()));
    ARROW_RETURN_NOT_OK(b_string.Reserve(tuples.size()));
    ARROW_RETURN_NOT_OK(b_bool.Reserve(tuples.size()));

    for (const auto& t : tuples) {
        const auto& v = t[col];
        const auto k = v.kind();
        const auto* name = kind_name_for(k);
        ARROW_RETURN_NOT_OK(b_kind.Append(name, std::strlen(name)));

        switch (k) {
            case mora::Value::Kind::FormID: {
                b_formid.UnsafeAppend(v.as_formid());
                ARROW_RETURN_NOT_OK(b_int.AppendNull());
                ARROW_RETURN_NOT_OK(b_float.AppendNull());
                ARROW_RETURN_NOT_OK(b_string.AppendNull());
                ARROW_RETURN_NOT_OK(b_bool.AppendNull());
                break;
            }
            case mora::Value::Kind::Int: {
                ARROW_RETURN_NOT_OK(b_formid.AppendNull());
                b_int.UnsafeAppend(v.as_int());
                ARROW_RETURN_NOT_OK(b_float.AppendNull());
                ARROW_RETURN_NOT_OK(b_string.AppendNull());
                ARROW_RETURN_NOT_OK(b_bool.AppendNull());
                break;
            }
            case mora::Value::Kind::Float: {
                ARROW_RETURN_NOT_OK(b_formid.AppendNull());
                ARROW_RETURN_NOT_OK(b_int.AppendNull());
                b_float.UnsafeAppend(v.as_float());
                ARROW_RETURN_NOT_OK(b_string.AppendNull());
                ARROW_RETURN_NOT_OK(b_bool.AppendNull());
                break;
            }
            case mora::Value::Kind::String: {
                ARROW_RETURN_NOT_OK(b_formid.AppendNull());
                ARROW_RETURN_NOT_OK(b_int.AppendNull());
                ARROW_RETURN_NOT_OK(b_float.AppendNull());
                auto sv = pool.get(v.as_string());
                ARROW_RETURN_NOT_OK(b_string.Append(sv.data(), sv.size()));
                ARROW_RETURN_NOT_OK(b_bool.AppendNull());
                break;
            }
            case mora::Value::Kind::Keyword: {
                // Keyword payload shares the _string column. The _kind
                // tag is "Keyword" so consumers can tell them apart.
                ARROW_RETURN_NOT_OK(b_formid.AppendNull());
                ARROW_RETURN_NOT_OK(b_int.AppendNull());
                ARROW_RETURN_NOT_OK(b_float.AppendNull());
                auto sv = pool.get(v.as_keyword());
                ARROW_RETURN_NOT_OK(b_string.Append(sv.data(), sv.size()));
                ARROW_RETURN_NOT_OK(b_bool.AppendNull());
                break;
            }
            case mora::Value::Kind::Bool: {
                ARROW_RETURN_NOT_OK(b_formid.AppendNull());
                ARROW_RETURN_NOT_OK(b_int.AppendNull());
                ARROW_RETURN_NOT_OK(b_float.AppendNull());
                ARROW_RETURN_NOT_OK(b_string.AppendNull());
                b_bool.UnsafeAppend(v.as_bool());
                break;
            }
            case mora::Value::Kind::Var:
            case mora::Value::Kind::List:
                return arrow::Status::Invalid(
                    "build_tagged_columns: unsupported kind reached "
                    "(Var/List); upstream filter missed one");
        }
    }

    std::vector<std::shared_ptr<arrow::Array>> out;
    out.reserve(6);
    for (auto* builder : std::array<arrow::ArrayBuilder*, 6>{
             &b_kind, &b_formid, &b_int, &b_float, &b_string, &b_bool}) {
        std::shared_ptr<arrow::Array> arr;
        ARROW_RETURN_NOT_OK(builder->Finish(&arr));
        out.push_back(std::move(arr));
    }
    return out;
}

// Translate a relation name like "form/npc" into a filesystem-safe
// relative path like "form/npc.parquet". Namespace separators in mora
// relation names are always '/', matching filesystem separators.
fs::path parquet_path_for(std::string_view rel_name, const fs::path& root) {
    return root / (std::string(rel_name) + ".parquet");
}

// Parsed form of the sink's config string.
struct ParsedConfig {
    fs::path root;
    bool     output_only = false;
};

// Parse `<path>[?<flags>]` where <flags> is k[=v](&k[=v])*. Only recognizes
// the boolean-presence flag `output-only` in Plan 4; unknown flags are
// reported via `unknown_flags` so the caller can diagnose.
ParsedConfig parse_config(std::string_view raw,
                           std::vector<std::string>& unknown_flags) {
    ParsedConfig cfg;
    auto q = raw.find('?');
    cfg.root = fs::path(std::string(raw.substr(0, q)));
    if (q == std::string_view::npos) return cfg;

    std::string_view flags = raw.substr(q + 1);
    while (!flags.empty()) {
        auto amp = flags.find('&');
        std::string_view chunk = flags.substr(0, amp);
        std::string_view key = chunk;
        auto eq = chunk.find('=');
        if (eq != std::string_view::npos) {
            key = chunk.substr(0, eq);
        }
        if (key == "output-only") {
            cfg.output_only = true;
        } else {
            unknown_flags.emplace_back(key);
        }
        if (amp == std::string_view::npos) break;
        flags = flags.substr(amp + 1);
    }
    return cfg;
}

// Emit a shape-only, zero-row parquet file for a relation that has no
// tuples — used when output-only mode is active and we want a stable
// downstream file set. Columns are all utf8 placeholders; real types
// arrive in Plan 5+ once ColumnSpec carries type info.
void emit_empty_parquet(std::string_view rel_name,
                         std::size_t column_count,
                         const fs::path& root,
                         mora::DiagBag& diags) {
    std::vector<std::shared_ptr<arrow::Field>> fields;
    fields.reserve(column_count);
    for (std::size_t c = 0; c < column_count; ++c) {
        fields.push_back(arrow::field(fmt::format("col{}", c),
                                       arrow::utf8()));
    }
    std::vector<std::shared_ptr<arrow::Array>> columns;
    columns.reserve(column_count);
    for (std::size_t c = 0; c < column_count; ++c) {
        arrow::StringBuilder b;
        std::shared_ptr<arrow::Array> out;
        (void)b.Finish(&out);
        columns.push_back(out);
    }
    auto empty_schema = arrow::schema(fields);
    auto empty_table  = arrow::Table::Make(empty_schema, columns);

    auto out_path = parquet_path_for(rel_name, root);
    std::error_code mk_ec;
    fs::create_directories(out_path.parent_path(), mk_ec);
    if (mk_ec) {
        diags.error("parquet-mkdir",
            fmt::format("parquet.snapshot: cannot create dir {}: {}",
                        out_path.parent_path().string(), mk_ec.message()),
            mora::SourceSpan{}, "");
        return;
    }
    auto outfile = arrow::io::FileOutputStream::Open(out_path.string());
    if (!outfile.ok()) {
        diags.error("parquet-open",
            fmt::format("parquet.snapshot: cannot open {} for writing: {}",
                        out_path.string(), outfile.status().ToString()),
            mora::SourceSpan{}, "");
        return;
    }
    (void)parquet::arrow::WriteTable(
        *empty_table, arrow::default_memory_pool(), *outfile,
        /*chunk_size*/ 64 * 1024);
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
            "--sink parquet.snapshot=<out-dir>[?output-only]",
            mora::SourceSpan{}, "");
        return;
    }

    std::vector<std::string> unknown_flags;
    ParsedConfig cfg = parse_config(ctx.config, unknown_flags);
    for (const auto& f : unknown_flags) {
        ctx.diags.warning("parquet-unknown-flag",
            fmt::format("parquet.snapshot: unknown flag '{}' ignored", f),
            mora::SourceSpan{}, "");
    }

    // When output-only is requested, we need the ExtensionContext to
    // discover which relations are flagged is_output. Without a context
    // pointer, there's no way to know.
    std::optional<std::unordered_set<std::string>> output_names;
    if (cfg.output_only) {
        if (ctx.extension == nullptr) {
            ctx.diags.error("parquet-output-only",
                "parquet.snapshot: 'output-only' requires an ExtensionContext; "
                "not available in this invocation",
                mora::SourceSpan{}, "");
            return;
        }
        output_names.emplace();
        for (const auto& schema : ctx.extension->schemas()) {
            if (schema.is_output) output_names->insert(schema.name);
        }
    }

    fs::path root = cfg.root;
    std::error_code ec;
    fs::create_directories(root, ec);
    if (ec) {
        ctx.diags.error("parquet-mkdir",
            fmt::format("parquet.snapshot: cannot create output dir {}: {}",
                        root.string(), ec.message()),
            mora::SourceSpan{}, "");
        return;
    }

    // In output-only mode also iterate every declared output relation
    // whose name isn't in the FactDB yet — we emit an empty parquet
    // file for it so downstream tooling sees a stable file set.
    std::unordered_set<uint32_t> seen;
    for (auto rel_id : db.all_relation_names()) {
        const auto rel_name = ctx.pool.get(rel_id);
        seen.insert(rel_id.index);
        if (output_names.has_value() &&
            !output_names->contains(std::string(rel_name))) {
            continue;  // filtered out by output-only
        }
        const auto& tuples = db.get_relation(rel_id);
        if (tuples.empty() && !output_names.has_value()) continue;

        if (tuples.empty()) {
            // Output-only mode: we were asked to emit even for empty
            // output relations. Look up the schema to get the column
            // count. If no schema is registered, skip.
            const auto* schema = ctx.extension->find_schema(std::string(rel_name));
            if (schema == nullptr) {
                ctx.diags.warning("parquet-skip-empty-no-schema",
                    fmt::format("parquet.snapshot: skipping empty relation '{}' "
                                "— no schema registered",
                                rel_name),
                    mora::SourceSpan{}, "");
                continue;
            }
            emit_empty_parquet(rel_name, schema->columns.size(), root, ctx.diags);
            continue;
        }

        const std::size_t arity = tuples.front().size();

        // Arity consistency — a hard shape error; skip whole relation.
        bool arity_mismatch = false;
        for (const auto& t : tuples) {
            if (t.size() != arity) { arity_mismatch = true; break; }
        }
        if (arity_mismatch) {
            ctx.diags.warning("parquet-skip-arity-mismatch",
                fmt::format("parquet.snapshot: skipping relation '{}' — "
                            "tuples have inconsistent arity",
                            rel_name),
                mora::SourceSpan{}, "");
            continue;
        }

        // Per-column kind sets. Size 1 = homogeneous (single typed
        // Arrow column). Size > 1 = heterogeneous (six tagged fields).
        std::vector<std::unordered_set<mora::Value::Kind>> col_kinds(arity);
        for (std::size_t c = 0; c < arity; ++c) {
            for (const auto& t : tuples) {
                col_kinds[c].insert(t[c].kind());
            }
        }

        // Unsupported-kind check: any Var or List anywhere kills the
        // whole relation (tagged encoding doesn't cover those).
        bool skip_unsupported = false;
        for (std::size_t c = 0; c < arity && !skip_unsupported; ++c) {
            for (auto k : col_kinds[c]) {
                if (k == mora::Value::Kind::Var || k == mora::Value::Kind::List) {
                    skip_unsupported = true;
                    break;
                }
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

        // Build Arrow schema + columns. For each input column:
        //  - Homogeneous (col_kinds[c].size() == 1): emit one typed
        //    field named "col{c}" via build_column.
        //  - Heterogeneous: expand into six tagged sub-columns named
        //    "col{c}_kind", "col{c}_formid", "col{c}_int",
        //    "col{c}_float", "col{c}_string", "col{c}_bool".
        std::vector<std::shared_ptr<arrow::Field>> fields;
        std::vector<std::shared_ptr<arrow::Array>> columns;

        bool column_failed = false;
        for (std::size_t c = 0; c < arity; ++c) {
            if (col_kinds[c].size() == 1) {
                const auto k = *col_kinds[c].begin();
                fields.push_back(arrow::field(fmt::format("col{}", c),
                                               arrow_type_for(k)));
                auto col = build_column(tuples, c, k, ctx.pool);
                if (!col.ok()) {
                    ctx.diags.error("parquet-build-column",
                        fmt::format("parquet.snapshot: failed to build "
                                    "column {} of relation '{}': {}",
                                    c, rel_name, col.status().ToString()),
                        mora::SourceSpan{}, "");
                    column_failed = true;
                    break;
                }
                columns.push_back(*col);
            } else {
                fields.push_back(arrow::field(
                    fmt::format("col{}_kind",    c), arrow::utf8()));
                fields.push_back(arrow::field(
                    fmt::format("col{}_formid",  c), arrow::uint32()));
                fields.push_back(arrow::field(
                    fmt::format("col{}_int",     c), arrow::int64()));
                fields.push_back(arrow::field(
                    fmt::format("col{}_float",   c), arrow::float64()));
                fields.push_back(arrow::field(
                    fmt::format("col{}_string",  c), arrow::utf8()));
                fields.push_back(arrow::field(
                    fmt::format("col{}_bool",    c), arrow::boolean()));
                auto tagged = build_tagged_columns(tuples, c, ctx.pool);
                if (!tagged.ok()) {
                    ctx.diags.error("parquet-build-column",
                        fmt::format("parquet.snapshot: failed to build "
                                    "tagged column {} of relation '{}': {}",
                                    c, rel_name, tagged.status().ToString()),
                        mora::SourceSpan{}, "");
                    column_failed = true;
                    break;
                }
                for (auto& arr : *tagged) {
                    columns.push_back(std::move(arr));
                }
            }
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

    // In output-only mode: any declared output relation that wasn't in
    // the FactDB (no slot configured) still needs an empty parquet
    // file so downstream sees a predictable file set. Re-emit via the
    // same empty-file logic used above.
    if (output_names.has_value()) {
        for (const auto& schema : ctx.extension->schemas()) {
            if (!schema.is_output) continue;
            auto name_id = ctx.pool.intern(schema.name);
            if (seen.count(name_id.index)) continue;  // already handled
            emit_empty_parquet(schema.name, schema.columns.size(), root, ctx.diags);
        }
    }
}

} // namespace mora_parquet
