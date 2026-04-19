#pragma once

#include "mora/ext/sink.h"

namespace mora_parquet {

// Sink that writes every relation in a FactDB to a parquet file tree.
//
// Layout: for a relation named "form/npc", the file is written to
//   <config>/form/npc.parquet
// For a relation named "plugin_exists", the file is
//   <config>/plugin_exists.parquet
// Intermediate directories are created as needed.
//
// Per-column type inference: each column's Arrow type is inferred from
// the first tuple's Value::Kind at that position. A later tuple whose
// kind differs triggers a warning diagnostic and the relation is skipped.
//
// List and Var values are not supported in v1. Relations containing
// either are skipped with a warning diagnostic.
//
// Thread safety: emit() is not thread-safe. The caller must not invoke
// emit() concurrently on the same sink instance with the same FactDB.
class ParquetSnapshotSink : public mora::ext::Sink {
public:
    std::string_view name() const override;
    void             emit(mora::ext::EmitCtx& ctx,
                           const mora::FactDB& db) override;
};

} // namespace mora_parquet
