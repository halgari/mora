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
    relations_[relation.index].push_back(std::move(values));
}

std::vector<Tuple> FactDB::query(StringId relation, const Tuple& pattern) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return {};

    std::vector<Tuple> results;
    for (const Tuple& tuple : it->second) {
        if (tuple.size() != pattern.size()) continue;
        bool match = true;
        for (size_t i = 0; i < pattern.size(); ++i) {
            if (!pattern[i].matches(tuple[i])) {
                match = false;
                break;
            }
        }
        if (match) results.push_back(tuple);
    }
    return results;
}

bool FactDB::has_fact(StringId relation, const Tuple& values) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return false;

    for (const Tuple& tuple : it->second) {
        if (tuple.size() != values.size()) continue;
        bool match = true;
        for (size_t i = 0; i < values.size(); ++i) {
            if (!values[i].matches(tuple[i])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

size_t FactDB::fact_count(StringId relation) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return 0;
    return it->second.size();
}

size_t FactDB::fact_count() const {
    size_t total = 0;
    for (const auto& [key, tuples] : relations_) {
        total += tuples.size();
    }
    return total;
}

const std::vector<Tuple>& FactDB::get_relation(StringId relation) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return empty_;
    return it->second;
}

} // namespace mora
