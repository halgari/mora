#include "mora/eval/fact_db.h"
#include <cassert>
#include <stdexcept>

namespace mora {

// ---------------------------------------------------------------------------
// FactDB
// ---------------------------------------------------------------------------

const std::vector<Tuple> FactDB::empty_;

FactDB::FactDB(StringPool& pool) : pool_(pool) {}

void FactDB::add_fact(StringId relation, Tuple values) {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) {
        size_t arity = values.size();
        auto [ins, ok] = relations_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(relation.index),
            std::forward_as_tuple(arity, std::vector<size_t>{0})
        );
        ins->second.add(std::move(values));
    } else {
        it->second.add(std::move(values));
    }
}

void FactDB::configure_relation(StringId name, size_t arity, const std::vector<size_t>& indexes) {
    relations_.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(name.index),
        std::forward_as_tuple(arity, indexes)
    );
}

void FactDB::merge_from(FactDB& other) {
    for (auto& [rel_id, other_rel] : other.relations_) {
        auto it = relations_.find(rel_id);
        if (it == relations_.end()) {
            // Relation doesn't exist in target — move the whole thing
            relations_.emplace(rel_id, std::move(other_rel));
        } else {
            // Relation exists — absorb tuples (move + index rebuild)
            // We need to steal the tuples vector from other_rel.
            // IndexedRelation doesn't expose a mutable all(), so use absorb.
            it->second.absorb(std::move(other_rel.mutable_tuples()));
        }
    }
}

std::vector<Tuple> FactDB::query(StringId relation, const Tuple& pattern) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return {};

    std::vector<const Tuple*> const ptrs = it->second.query(pattern);
    std::vector<Tuple> results;
    results.reserve(ptrs.size());
    for (const Tuple* tp : ptrs) {
        results.push_back(*tp);
    }
    return results;
}

bool FactDB::has_fact(StringId relation, const Tuple& values) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return false;
    return it->second.contains(values);
}

size_t FactDB::fact_count(StringId relation) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return 0;
    return it->second.size();
}

size_t FactDB::fact_count() const {
    size_t total = 0;
    for (const auto& [key, rel] : relations_) {
        total += rel.size();
    }
    return total;
}

const std::vector<Tuple>& FactDB::get_relation(StringId relation) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return empty_;
    return it->second.all();
}

} // namespace mora
