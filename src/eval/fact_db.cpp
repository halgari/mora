#include "mora/eval/fact_db.h"
#include <cassert>
#include <stdexcept>

namespace mora {

// ---------------------------------------------------------------------------
// FactDB
// ---------------------------------------------------------------------------

FactDB::FactDB(StringPool& pool) : pool_(pool) {}

void FactDB::add_fact(StringId relation, Tuple values) {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) {
        // Auto-vivify with Any columns matching the tuple's arity.
        std::vector<const Type*> col_types(values.size(), types::any());
        it = relations_.try_emplace(relation.index,
                                    std::move(col_types),
                                    std::vector<size_t>{}).first;
    }
    it->second.append(values);
}

void FactDB::configure_relation(StringId name, size_t arity,
                                 const std::vector<size_t>& indexes) {
    // Backwards compat: unknown column types → Any placeholders.
    std::vector<const Type*> col_types(arity, types::any());
    configure_relation(name, std::move(col_types), indexes);
}

void FactDB::configure_relation(StringId name,
                                 std::vector<const Type*> column_types,
                                 const std::vector<size_t>& indexes) {
    relations_.try_emplace(name.index,
                            std::move(column_types),
                            std::vector<size_t>(indexes));
}

void FactDB::merge_from(FactDB& other) {
    for (auto& [idx, rel] : other.relations_) {
        auto mine = relations_.find(idx);
        if (mine == relations_.end()) {
            // No local relation — auto-vivify with Any columns and absorb rows.
            auto rows = rel.materialize();
            std::vector<const Type*> auto_types(rel.arity(), types::any());
            relations_.try_emplace(idx, std::move(auto_types),
                                    std::vector<size_t>{});
            auto& dest = relations_.at(idx);
            for (auto& t : rows) dest.append(t);
        } else {
            mine->second.absorb(rel.materialize());
        }
    }
    other.relations_.clear();
}

std::vector<Tuple> FactDB::query(StringId relation, const Tuple& pattern) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return {};
    return it->second.query(pattern);
}

bool FactDB::has_fact(StringId relation, const Tuple& values) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return false;
    return it->second.contains(values);
}

size_t FactDB::fact_count(StringId relation) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return 0;
    return it->second.row_count();
}

size_t FactDB::fact_count() const {
    size_t n = 0;
    for (auto const& [_, rel] : relations_) n += rel.row_count();
    return n;
}

std::vector<Tuple> FactDB::get_relation(StringId relation) const {
    auto it = relations_.find(relation.index);
    if (it == relations_.end()) return {};
    return it->second.materialize();
}

std::vector<StringId> FactDB::all_relation_names() const {
    std::vector<StringId> out;
    out.reserve(relations_.size());
    for (auto const& [idx, _] : relations_) out.push_back(StringId{idx});
    return out;
}

} // namespace mora
