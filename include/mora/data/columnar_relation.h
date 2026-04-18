#pragma once

#include "mora/core/type.h"
#include "mora/data/column.h"
#include "mora/data/value.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace mora {

// A relation stored as N Columns (one per arg) plus optional hash
// indexes on selected columns. Append-only — rows are added, never
// updated in place.
//
// Plan 11 introduces this class alongside IndexedRelation. Plan 11 M2
// switches FactDB to use it and deletes IndexedRelation.
class ColumnarRelation {
public:
    // `column_types[i]` drives the ith Column's Type (and thus its
    // typed chunk allocation). `indexed_columns` names columns that
    // get a hash-map index alongside the Column.
    ColumnarRelation(std::vector<const Type*> column_types,
                     std::vector<size_t>       indexed_columns);

    size_t arity()     const { return columns_.size(); }
    size_t row_count() const;

    // Append one tuple. The tuple's arity must match `arity()`.
    // Values are routed to columns via `Column::append(Value)`.
    void append(const Tuple& t);

    // Bulk-move append: move tuples from `incoming` and rebuild
    // indexes for the newly-appended range only.
    void absorb(std::vector<Tuple>&& incoming);

    // Read a column by index. Callers that know the physical type
    // may downcast into the typed vector chunks.
    const Column& column(size_t i) const { return *columns_[i]; }

    // Build a materialized Tuple for row `row`. O(arity) Column::at
    // calls. The slow path — Plan 12's vectorized evaluator avoids
    // this entirely.
    Tuple row_at(size_t row) const;

    // Pattern-matching query. Returns matching tuples by value.
    // Uses the best available index; falls back to full scan.
    std::vector<Tuple> query(const Tuple& pattern) const;

    bool contains(const Tuple& values) const;

    // Full materialization — rebuilds a Tuple per row. Used by the
    // FactDB tuple-based shim. Plan 12 callers should read columns
    // directly instead.
    std::vector<Tuple> materialize() const;

private:
    std::vector<std::unique_ptr<Column>> columns_;
    std::vector<size_t>                   indexed_columns_;
    // One hash map per indexed-column entry. Maps Value::hash() to
    // the row indices whose cell in that column hashes to this value.
    std::vector<std::unordered_map<uint64_t, std::vector<uint32_t>>> indexes_;

    int find_index_slot(size_t column) const;
    std::vector<uint32_t> lookup(size_t column, const Value& key) const;
    void index_row(uint32_t row);
};

} // namespace mora
