#pragma once

#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/column.h"
#include "mora/data/value.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace mora {

// A BindingChunk is a slice of N rows × K bound variables. Each column
// carries the `Type*` and typed values of one variable. The `names_`
// vector positions each column with a variable identity (`StringId`).
// `index_of(name)` is the caller's way to look up which column holds
// a given variable.
//
// A BindingChunk is append-only during construction — operators build
// one in a staging phase (append rows until `kChunkSize` or source
// exhaustion), then emit it downstream.
class BindingChunk {
public:
    BindingChunk(std::vector<StringId>    var_names,
                 std::vector<const Type*> col_types);

    // Arity (number of bound variables in this chunk).
    size_t arity()     const { return columns_.size(); }
    size_t row_count() const;

    // Access a column by position. Downstream ops downcast the chunk's
    // Vector to a typed Vector when they need typed bulk access.
    Column&       column(size_t i)       { return *columns_[i]; }
    const Column& column(size_t i) const { return *columns_[i]; }

    // Lookup a column index by variable name. Returns -1 if absent.
    int index_of(StringId var_name) const;

    // Variable name at position i.
    StringId name_at(size_t i) const { return names_[i]; }
    const std::vector<StringId>& names() const { return names_; }

    // Append one row. The caller provides a Value per column, in
    // position order. Kind must match each column's hint (enforced by
    // Column::append).
    void append_row(const std::vector<Value>& row);

    // Build a Value representing the cell at (row, col). Used by
    // operators that emit individual rows to a downstream sink.
    Value cell(size_t row, size_t col) const;

private:
    std::vector<StringId>                  names_;
    std::vector<std::unique_ptr<Column>>   columns_;
    // For quick index_of; populated by ctor.
    std::unordered_map<uint32_t, size_t>   by_name_;
};

} // namespace mora
