#pragma once
#include <cstdint>
#include <memory>
#include "mora/core/string_pool.h"
#include <vector>

namespace mora {

class Value {
public:
    enum class Kind { Var, FormID, Int, Float, String, Bool, List };

    static Value make_var();
    static Value make_formid(uint32_t id);
    static Value make_int(int64_t i);
    static Value make_float(double f);
    static Value make_string(StringId s);
    static Value make_bool(bool b);
    static Value make_list(std::vector<Value> items);

    Kind kind() const { return kind_; }
    bool is_var() const { return kind_ == Kind::Var; }

    uint32_t as_formid() const;
    int64_t  as_int()    const;
    double   as_float()  const;
    StringId as_string() const;
    bool     as_bool()   const;
    const std::vector<Value>& as_list() const;

    bool list_contains(const Value& needle) const;

    // Returns true if either value is Var, or both are equal concrete values.
    bool matches(const Value& other) const;

    bool operator==(const Value& other) const;

    uint64_t hash() const;

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

    // Stored outside the union so Value remains trivially copyable for non-list kinds.
    // Null when kind_ != Kind::List.
    std::shared_ptr<std::vector<Value>> list_;
};

using Tuple = std::vector<Value>;

} // namespace mora
