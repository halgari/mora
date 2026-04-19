#include "mora/core/type.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace mora {
namespace {

template <const char* Name, size_t ElemBytes, Value::Kind K>
class PhysicalType : public Type {
public:
    std::string_view name() const override { return Name; }
    const Type*      physical() const override { return this; }
    size_t           element_bytes() const override { return ElemBytes; }
    Value::Kind      kind_hint() const override { return K; }
};

constexpr char kInt32Name[]   = "Int32";
constexpr char kInt64Name[]   = "Int64";
constexpr char kFloat64Name[] = "Float64";
constexpr char kBoolName[]    = "Bool";
constexpr char kStringName[]  = "String";
constexpr char kKeywordName[] = "Keyword";
constexpr char kBytesName[]   = "Bytes";
constexpr char kAnyName[]     = "Any";

using Int32Type_   = PhysicalType<kInt32Name,   sizeof(int32_t),  Value::Kind::Int>;
using Int64Type_   = PhysicalType<kInt64Name,   sizeof(int64_t),  Value::Kind::Int>;
using Float64Type_ = PhysicalType<kFloat64Name, sizeof(double),   Value::Kind::Float>;
using BoolType_    = PhysicalType<kBoolName,    sizeof(bool),     Value::Kind::Bool>;
using StringType_  = PhysicalType<kStringName,  sizeof(uint32_t), Value::Kind::String>;
using KeywordType_ = PhysicalType<kKeywordName, sizeof(uint32_t), Value::Kind::Keyword>;
using BytesType_   = PhysicalType<kBytesName,   0,                Value::Kind::Var>;  // no single kind
using AnyType_     = PhysicalType<kAnyName,     0,                Value::Kind::Var>;  // polymorphic

class NominalType : public Type {
public:
    NominalType(std::string n, const Type* physical, Value::Kind hint)
        : name_(std::move(n)), physical_(physical), kind_hint_(hint) {}
    std::string_view name() const override { return name_; }
    const Type*      physical() const override { return physical_; }
    size_t           element_bytes() const override {
        return physical_->element_bytes();
    }
    Value::Kind      kind_hint() const override { return kind_hint_; }
private:
    std::string name_;
    const Type* physical_;
    Value::Kind kind_hint_;
};

} // namespace

namespace types {

#define DEFINE_PHYSICAL(name, Class)                       \
    const Type* name() {                                   \
        static const Class instance;                       \
        return &instance;                                  \
    }

DEFINE_PHYSICAL(int32,   Int32Type_)
DEFINE_PHYSICAL(int64,   Int64Type_)
DEFINE_PHYSICAL(float64, Float64Type_)
DEFINE_PHYSICAL(boolean, BoolType_)
DEFINE_PHYSICAL(string,  StringType_)
DEFINE_PHYSICAL(keyword, KeywordType_)
DEFINE_PHYSICAL(bytes,   BytesType_)
DEFINE_PHYSICAL(any,     AnyType_)

#undef DEFINE_PHYSICAL

const Type* get(std::string_view name) {
    return TypeRegistry::instance().find(name);
}

} // namespace types

TypeRegistry::TypeRegistry() {
    by_name_.emplace(std::string(types::int32()->name()),   types::int32());
    by_name_.emplace(std::string(types::int64()->name()),   types::int64());
    by_name_.emplace(std::string(types::float64()->name()), types::float64());
    by_name_.emplace(std::string(types::boolean()->name()), types::boolean());
    by_name_.emplace(std::string(types::string()->name()),  types::string());
    by_name_.emplace(std::string(types::keyword()->name()), types::keyword());
    by_name_.emplace(std::string(types::bytes()->name()),   types::bytes());
    by_name_.emplace(std::string(types::any()->name()),     types::any());
}

TypeRegistry& TypeRegistry::instance() {
    static TypeRegistry inst;
    return inst;
}

const Type* TypeRegistry::register_nominal(std::string_view name,
                                            const Type* physical,
                                            Value::Kind kind_hint) {
    std::string const key(name);
    {
        std::shared_lock<std::shared_mutex> r(mu_);
        auto it = by_name_.find(key);
        if (it != by_name_.end()) {
            // Idempotent when physical matches; otherwise return existing
            // (do NOT overwrite; callers that race on names must agree).
            return it->second;
        }
    }
    std::unique_lock<std::shared_mutex> w(mu_);
    auto it = by_name_.find(key);
    if (it != by_name_.end()) return it->second;
    auto* t = new NominalType(key, physical, kind_hint);
    by_name_.emplace(key, t);
    return t;
}

const Type* TypeRegistry::find(std::string_view name) const {
    std::string const key(name);
    std::shared_lock<std::shared_mutex> r(mu_);
    auto it = by_name_.find(key);
    return it == by_name_.end() ? nullptr : it->second;
}

} // namespace mora
