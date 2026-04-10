#include "mora/eval/fact_db.h"
#include <cassert>
#include <stdexcept>

namespace mora {

// ---------------------------------------------------------------------------
// Value factory methods
// ---------------------------------------------------------------------------

Value Value::make_var() {
    Value v;
    v.kind_ = Kind::Var;
    return v;
}

Value Value::make_formid(uint32_t id) {
    Value v;
    v.kind_ = Kind::FormID;
    v.data_.formid = id;
    return v;
}

Value Value::make_int(int64_t i) {
    Value v;
    v.kind_ = Kind::Int;
    v.data_.integer = i;
    return v;
}

Value Value::make_float(double f) {
    Value v;
    v.kind_ = Kind::Float;
    v.data_.floating = f;
    return v;
}

Value Value::make_string(StringId s) {
    Value v;
    v.kind_ = Kind::String;
    v.data_.string_index = s.index;
    return v;
}

Value Value::make_bool(bool b) {
    Value v;
    v.kind_ = Kind::Bool;
    v.data_.boolean = b;
    return v;
}

// ---------------------------------------------------------------------------
// Value accessors
// ---------------------------------------------------------------------------

uint32_t Value::as_formid() const {
    assert(kind_ == Kind::FormID);
    return data_.formid;
}

int64_t Value::as_int() const {
    assert(kind_ == Kind::Int);
    return data_.integer;
}

double Value::as_float() const {
    assert(kind_ == Kind::Float);
    return data_.floating;
}

StringId Value::as_string() const {
    assert(kind_ == Kind::String);
    return StringId{data_.string_index};
}

bool Value::as_bool() const {
    assert(kind_ == Kind::Bool);
    return data_.boolean;
}

// ---------------------------------------------------------------------------
// Value equality and matching
// ---------------------------------------------------------------------------

bool Value::operator==(const Value& other) const {
    if (kind_ != other.kind_) return false;
    switch (kind_) {
        case Kind::Var:    return true; // all Vars are equal to each other
        case Kind::FormID: return data_.formid        == other.data_.formid;
        case Kind::Int:    return data_.integer       == other.data_.integer;
        case Kind::Float:  return data_.floating      == other.data_.floating;
        case Kind::String: return data_.string_index  == other.data_.string_index;
        case Kind::Bool:   return data_.boolean       == other.data_.boolean;
    }
    return false;
}

bool Value::matches(const Value& other) const {
    if (is_var() || other.is_var()) return true;
    return *this == other;
}

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
