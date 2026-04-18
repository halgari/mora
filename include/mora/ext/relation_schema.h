#pragma once

#include <any>
#include <string>
#include <vector>

namespace mora::ext {

// A relation column description. Pool-agnostic: names are std::string
// and the caller interns them into its own pool at use time. Column
// type info is intentionally absent in Plan 4 — it lands alongside the
// vectorized evaluator work. `indexed` tracks whether the column
// should be hash-indexed for lookup.
struct ColumnSpec {
    std::string name;
    bool        indexed = false;
};

// A relation schema registered by an extension. Pool-agnostic — names
// (both relation and column names) are plain strings.
//
// `is_output` flags relations whose facts represent side effects meant
// to be consumed by downstream stages (currently: parquet sink's
// `output-only` filter). The evaluator writes effect facts directly
// into these relations (skyrim/set, skyrim/add, ...).
//
// `ext_metadata` is an opaque escape hatch: an extension may attach
// its own state (e.g. EspSource for Skyrim) keyed by a type the
// extension owns. Core code treats it as `std::any` and does not
// interpret it.
struct RelationSchema {
    std::string             name;
    std::vector<ColumnSpec> columns;
    bool                    is_output = false;
    std::any                ext_metadata;
};

} // namespace mora::ext
