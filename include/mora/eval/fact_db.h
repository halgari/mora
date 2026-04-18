#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/columnar_relation.h"
#include "mora/data/value.h"

namespace mora {

class FactDB {
public:
    explicit FactDB(StringPool& pool);

    void add_fact(StringId relation, Tuple values);

    // Arity-only overload — backwards compat. Column types default to Any.
    void configure_relation(StringId name, size_t arity, const std::vector<size_t>& indexes);

    // Typed overload — preferred. Drives ColumnarRelation allocation with
    // explicit per-column types. column_types.size() == arity.
    void configure_relation(StringId name,
                             std::vector<const Type*> column_types,
                             const std::vector<size_t>& indexes);

    void merge_from(FactDB& other);
    std::vector<Tuple> query(StringId relation, const Tuple& pattern) const;
    bool has_fact(StringId relation, const Tuple& values) const;
    size_t fact_count(StringId relation) const;
    size_t fact_count() const;

    // Returns the interned names of every relation that has at least
    // one configured relation slot (whether populated or empty). Used
    // by sinks to enumerate what to write.
    std::vector<StringId> all_relation_names() const;

    // Returns all tuples in the relation by value (was const-ref in Plan 10).
    // Callers using `const auto&` are fine — temp lifetime extension applies.
    std::vector<Tuple> get_relation(StringId relation) const;

private:
    StringPool& pool_;
    std::unordered_map<uint32_t, ColumnarRelation> relations_;
};

} // namespace mora
