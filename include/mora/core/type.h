#pragma once

#include <cstddef>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "mora/data/value.h"   // Value::Kind

namespace mora {

// A Type is a process-lifetime singleton identified by pointer.
// Physical types return themselves from `physical()`; nominal types
// return their underlying physical type. Types are never destroyed.
class Type {
public:
    virtual ~Type()                          = default;
    virtual std::string_view name() const    = 0;
    virtual const Type*      physical() const = 0;  // self for physical; underlying for nominal
    virtual size_t           element_bytes() const = 0;

    // The Value::Kind that naturally round-trips through a Column of
    // this Type. Physical types return the kind matching their payload
    // (Int32 → Int; String → String; Keyword → Keyword; Any → Var,
    // which means "no specific kind — use the cell's stored kind").
    // Nominal types may override to return a different kind (FormID
    // returns Kind::FormID even though its physical is Int32).
    virtual Value::Kind      kind_hint() const = 0;

    bool is_nominal() const { return physical() != this; }

protected:
    Type() = default;
};

// Built-in physical type singletons. Accessed via free functions to
// keep the Type class definition minimal.
namespace types {
    const Type* int32();
    const Type* int64();
    const Type* float64();
    const Type* boolean();
    const Type* string();    // interned StringId
    const Type* keyword();   // interned StringId with keyword sigil
    const Type* bytes();
    const Type* any();

    // Lookup by name. Physical names ("Int32", "String", ...) and any
    // nominal names previously registered via TypeRegistry are visible.
    // Returns nullptr on miss.
    const Type* get(std::string_view name);
}

// Process-singleton registry of nominal types. Extensions register
// domain-specific tags (e.g. "FormID") layered over a physical type.
// Registration is idempotent: registering the same name twice with
// the same physical returns the same pointer.
class TypeRegistry {
public:
    static TypeRegistry& instance();

    // Register with an explicit kind hint. Idempotent — same name returns the
    // existing pointer; the new kind_hint is ignored if an entry already exists.
    const Type* register_nominal(std::string_view name,
                                  const Type* physical,
                                  Value::Kind kind_hint);

    // Back-compat overload: defaults the kind hint to physical->kind_hint().
    const Type* register_nominal(std::string_view name, const Type* physical) {
        return register_nominal(name, physical, physical->kind_hint());
    }

    const Type* find(std::string_view name) const;

    TypeRegistry(const TypeRegistry&)            = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;

private:
    TypeRegistry();
    mutable std::shared_mutex mu_;
    std::unordered_map<std::string, const Type*> by_name_;  // physical + nominal
};

} // namespace mora
