#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include "mora/core/string_pool.h"

namespace mora {

class Value {
public:
    enum class Kind { Var, FormID, Int, Float, String, Bool };

    static Value make_var();
    static Value make_formid(uint32_t id);
    static Value make_int(int64_t i);
    static Value make_float(double f);
    static Value make_string(StringId s);
    static Value make_bool(bool b);

    Kind kind() const { return kind_; }
    bool is_var() const { return kind_ == Kind::Var; }

    uint32_t as_formid() const;
    int64_t  as_int()    const;
    double   as_float()  const;
    StringId as_string() const;
    bool     as_bool()   const;

    // Returns true if either value is Var, or both are equal concrete values.
    bool matches(const Value& other) const;

    bool operator==(const Value& other) const;

private:
    Kind kind_ = Kind::Var;

    union Data {
        uint32_t formid;
        int64_t  integer;
        double   floating;
        uint32_t string_index; // StringId::index
        bool     boolean;

        Data() : integer(0) {}
    } data_;
};

using Tuple = std::vector<Value>;

class FactDB {
public:
    explicit FactDB(StringPool& pool);

    void add_fact(StringId relation, Tuple values);
    std::vector<Tuple> query(StringId relation, const Tuple& pattern) const;
    bool has_fact(StringId relation, const Tuple& values) const;
    size_t fact_count(StringId relation) const;
    size_t fact_count() const;
    const std::vector<Tuple>& get_relation(StringId relation) const;

private:
    StringPool& pool_;
    std::unordered_map<uint32_t, std::vector<Tuple>> relations_;
    static const std::vector<Tuple> empty_;
};

} // namespace mora
