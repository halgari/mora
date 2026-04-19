#include "mora/data/columnar_relation.h"

#include <cassert>
#include <stdexcept>
#include <utility>

namespace mora {

ColumnarRelation::ColumnarRelation(std::vector<const Type*> column_types,
                                     std::vector<size_t>       indexed_columns)
    : indexed_columns_(std::move(indexed_columns))
    , indexes_(indexed_columns_.size())
{
    columns_.reserve(column_types.size());
    for (const Type* t : column_types) {
        assert(t != nullptr && "ColumnarRelation: column type must not be nullptr");
        columns_.push_back(std::make_unique<Column>(t));
    }
}

size_t ColumnarRelation::row_count() const {
    return columns_.empty() ? 0 : columns_.front()->row_count();
}

void ColumnarRelation::index_row(uint32_t row) {
    for (size_t slot = 0; slot < indexed_columns_.size(); ++slot) {
        size_t const col = indexed_columns_[slot];
        Value const v = columns_[col]->at(row);
        uint64_t const h = v.hash();
        indexes_[slot][h].push_back(row);
    }
}

void ColumnarRelation::append(const Tuple& t) {
    if (t.size() != columns_.size()) {
        throw std::runtime_error("ColumnarRelation::append: tuple arity mismatch");
    }
    auto const row = static_cast<uint32_t>(row_count());
    for (size_t i = 0; i < t.size(); ++i) {
        columns_[i]->append(t[i]);
    }
    index_row(row);
}

void ColumnarRelation::absorb(std::vector<Tuple>&& incoming) {
    auto local = std::move(incoming);
    for (auto& t : local) append(t);
}

Tuple ColumnarRelation::row_at(size_t row) const {
    Tuple t;
    t.reserve(columns_.size());
    for (auto const& c : columns_) t.push_back(c->at(row));
    return t;
}

int ColumnarRelation::find_index_slot(size_t column) const {
    for (size_t slot = 0; slot < indexed_columns_.size(); ++slot) {
        if (indexed_columns_[slot] == column) {
            return static_cast<int>(slot);
        }
    }
    return -1;
}

std::vector<uint32_t> ColumnarRelation::lookup(size_t column,
                                                 const Value& key) const {
    int const slot = find_index_slot(column);
    if (slot < 0) return {};
    uint64_t const h = key.hash();
    auto it = indexes_[static_cast<size_t>(slot)].find(h);
    if (it == indexes_[static_cast<size_t>(slot)].end()) return {};
    // Verify each candidate against the key to filter hash collisions.
    std::vector<uint32_t> result;
    result.reserve(it->second.size());
    for (uint32_t const row : it->second) {
        if (columns_[column]->at(row) == key) {
            result.push_back(row);
        }
    }
    return result;
}

std::vector<Tuple> ColumnarRelation::query(const Tuple& pattern) const {
    // Find the first concrete column with a hash index.
    int best_slot = -1;
    size_t best_col = 0;
    for (size_t col = 0; col < pattern.size(); ++col) {
        if (!pattern[col].is_var()) {
            int const slot = find_index_slot(col);
            if (slot >= 0) {
                best_slot = slot;
                best_col = col;
                break;
            }
        }
    }

    std::vector<uint32_t> candidates;
    if (best_slot >= 0) {
        candidates = lookup(best_col, pattern[best_col]);
    } else {
        candidates.reserve(row_count());
        for (uint32_t i = 0; i < row_count(); ++i) candidates.push_back(i);
    }

    std::vector<Tuple> result;
    result.reserve(candidates.size());
    for (uint32_t const row : candidates) {
        Tuple t = row_at(row);
        bool ok = true;
        for (size_t col = 0; col < pattern.size(); ++col) {
            if (!pattern[col].matches(t[col])) {
                ok = false;
                break;
            }
        }
        if (ok) result.push_back(std::move(t));
    }
    return result;
}

bool ColumnarRelation::contains(const Tuple& values) const {
    return !query(values).empty();
}

std::vector<Tuple> ColumnarRelation::materialize() const {
    std::vector<Tuple> out;
    out.reserve(row_count());
    for (uint32_t i = 0; i < row_count(); ++i) out.push_back(row_at(i));
    return out;
}

} // namespace mora
