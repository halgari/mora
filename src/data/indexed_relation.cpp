#include "mora/data/indexed_relation.h"

namespace mora {

IndexedRelation::IndexedRelation(size_t arity, std::vector<size_t> indexed_columns)
    : arity_(arity)
    , indexed_columns_(std::move(indexed_columns))
    , indexes_(indexed_columns_.size())
{}

void IndexedRelation::add(Tuple tuple) {
    auto idx = static_cast<uint32_t>(tuples_.size());
    tuples_.push_back(std::move(tuple));

    for (size_t slot = 0; slot < indexed_columns_.size(); ++slot) {
        size_t col = indexed_columns_[slot];
        uint64_t h = tuples_.back()[col].hash();
        indexes_[slot][h].push_back(idx);
    }
}

int IndexedRelation::find_index(size_t column) const {
    for (size_t slot = 0; slot < indexed_columns_.size(); ++slot) {
        if (indexed_columns_[slot] == column)
            return static_cast<int>(slot);
    }
    return -1;
}

std::vector<const Tuple*> IndexedRelation::lookup(size_t column, const Value& key) const {
    int slot = find_index(column);
    if (slot < 0)
        return {};

    uint64_t h = key.hash();
    auto it = indexes_[static_cast<size_t>(slot)].find(h);
    if (it == indexes_[static_cast<size_t>(slot)].end())
        return {};

    std::vector<const Tuple*> result;
    result.reserve(it->second.size());
    for (uint32_t i : it->second) {
        // Guard against hash collisions: verify the value actually matches
        if (tuples_[i][column] == key)
            result.push_back(&tuples_[i]);
    }
    return result;
}

std::vector<const Tuple*> IndexedRelation::query(const Tuple& pattern) const {
    // Find the first concrete column that has a hash index
    int best_slot = -1;
    size_t best_col = 0;
    for (size_t col = 0; col < pattern.size(); ++col) {
        if (!pattern[col].is_var()) {
            int slot = find_index(col);
            if (slot >= 0) {
                best_slot = slot;
                best_col = col;
                break;
            }
        }
    }

    std::vector<const Tuple*> candidates;

    if (best_slot >= 0) {
        // Use the index to get candidates for the best column
        candidates = lookup(best_col, pattern[best_col]);
    } else {
        // Full scan: gather all tuples
        candidates.reserve(tuples_.size());
        for (const Tuple& t : tuples_)
            candidates.push_back(&t);
    }

    // Filter candidates against the full pattern
    std::vector<const Tuple*> result;
    for (const Tuple* tp : candidates) {
        bool ok = true;
        for (size_t col = 0; col < pattern.size(); ++col) {
            if (!pattern[col].matches((*tp)[col])) {
                ok = false;
                break;
            }
        }
        if (ok)
            result.push_back(tp);
    }
    return result;
}

bool IndexedRelation::contains(const Tuple& values) const {
    return !query(values).empty();
}

size_t IndexedRelation::size() const {
    return tuples_.size();
}

size_t IndexedRelation::arity() const {
    return arity_;
}

const std::vector<Tuple>& IndexedRelation::all() const {
    return tuples_;
}

} // namespace mora
