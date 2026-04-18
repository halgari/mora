#include "mora/data/value.h"
#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>

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

Value Value::make_keyword(StringId s) {
    Value v;
    v.kind_ = Kind::Keyword;
    v.data_.string_index = s.index;
    return v;
}

Value Value::make_bool(bool b) {
    Value v;
    v.kind_ = Kind::Bool;
    v.data_.boolean = b;
    return v;
}

Value Value::make_list(std::vector<Value> items) {
    Value v;
    v.kind_ = Kind::List;
    v.list_ = std::make_shared<std::vector<Value>>(std::move(items));
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

StringId Value::as_keyword() const {
    assert(kind_ == Kind::Keyword);
    return StringId{data_.string_index};
}

bool Value::as_bool() const {
    assert(kind_ == Kind::Bool);
    return data_.boolean;
}

const std::vector<Value>& Value::as_list() const {
    assert(kind_ == Kind::List);
    return *list_;
}

bool Value::list_contains(const Value& needle) const {
    assert(kind_ == Kind::List);
    return std::ranges::any_of(*list_,
        [&](const Value& v) { return v == needle; });
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
        case Kind::String:  return data_.string_index  == other.data_.string_index;
        case Kind::Keyword: return data_.string_index  == other.data_.string_index;
        case Kind::Bool:    return data_.boolean       == other.data_.boolean;
        case Kind::List:   return *list_              == *other.list_;
    }
    return false;
}

bool Value::matches(const Value& other) const {
    if (is_var() || other.is_var()) return true;
    return *this == other;
}

// ---------------------------------------------------------------------------
// Value hash
// ---------------------------------------------------------------------------

uint64_t Value::hash() const {
    // Mix the kind_ tag into every hash so values that share a storage
    // layout but differ by kind (e.g. String vs Keyword both holding a
    // StringId.index) don't collide.
    const uint64_t kind_mix = static_cast<uint64_t>(kind_) * 2654435761ULL;
    switch (kind_) {
        case Kind::FormID: return std::hash<uint32_t>{}(data_.formid)      ^ kind_mix;
        case Kind::Int:    return std::hash<int64_t>{}(data_.integer)      ^ kind_mix;
        case Kind::Float:  return std::hash<double>{}(data_.floating)      ^ kind_mix;
        case Kind::String:  return std::hash<uint32_t>{}(data_.string_index) ^ kind_mix;
        case Kind::Keyword: return std::hash<uint32_t>{}(data_.string_index) ^ kind_mix;
        case Kind::Bool:    return std::hash<bool>{}(data_.boolean)          ^ kind_mix;
        case Kind::List: {
            // FNV-style mixing of element hashes
            uint64_t h = 14695981039346656037ULL; // FNV offset basis
            for (const Value& v : *list_) {
                h ^= v.hash();
                h *= 1099511628211ULL; // FNV prime
            }
            return h;
        }
        default:           return 0;
    }
}

} // namespace mora
