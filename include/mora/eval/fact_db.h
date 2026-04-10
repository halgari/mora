#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "mora/core/string_pool.h"
#include "mora/data/indexed_relation.h"
#include "mora/data/value.h"

namespace mora {

class FactDB {
public:
    explicit FactDB(StringPool& pool);

    void add_fact(StringId relation, Tuple values);
    void configure_relation(StringId name, size_t arity, std::vector<size_t> indexes);
    std::vector<Tuple> query(StringId relation, const Tuple& pattern) const;
    bool has_fact(StringId relation, const Tuple& values) const;
    size_t fact_count(StringId relation) const;
    size_t fact_count() const;
    const std::vector<Tuple>& get_relation(StringId relation) const;

private:
    StringPool& pool_;
    std::unordered_map<uint32_t, IndexedRelation> relations_;
    static const std::vector<Tuple> empty_;
};

} // namespace mora
