#pragma once
#include "mora/data/value.h"
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace mora {

class IndexedRelation {
public:
    // arity = number of columns, indexed_columns = which columns get hash indexes
    IndexedRelation(size_t arity, std::vector<size_t> indexed_columns);

    void add(Tuple tuple);

    // Lookup all tuples where the specified column equals key (uses hash index)
    std::vector<const Tuple*> lookup(size_t column, const Value& key) const;

    // Pattern query: concrete values must match, Var values are wildcards.
    // Uses the best available index for the first concrete column, then filters.
    std::vector<const Tuple*> query(const Tuple& pattern) const;

    // Check if an exact tuple exists
    bool contains(const Tuple& values) const;

    size_t size() const;
    size_t arity() const;
    const std::vector<Tuple>& all() const;

private:
    size_t arity_;
    std::vector<size_t> indexed_columns_;
    std::vector<Tuple> tuples_;
    // One index per indexed column: value_hash → [tuple_indices]
    std::vector<std::unordered_map<uint64_t, std::vector<uint32_t>>> indexes_;
    int find_index(size_t column) const; // returns index slot or -1
};

} // namespace mora
