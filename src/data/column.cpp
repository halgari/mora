// src/data/column.cpp
#include "mora/data/column.h"

#include <cassert>
#include <stdexcept>

namespace mora {

Column::Column(const Type* type) : type_(type) {
    assert(type_ != nullptr);
}

size_t Column::row_count() const {
    size_t n = 0;
    for (auto const& c : chunks_) n += c->size();
    return n;
}

std::unique_ptr<Vector> Column::make_chunk() const {
    auto const* phys = type_->physical();
    if (phys == types::int32())   return std::make_unique<Int32Vector>();
    if (phys == types::int64())   return std::make_unique<Int64Vector>();
    if (phys == types::float64()) return std::make_unique<Float64Vector>();
    if (phys == types::boolean()) return std::make_unique<BoolVector>();
    if (phys == types::string())  return std::make_unique<StringVector>();
    if (phys == types::keyword()) return std::make_unique<KeywordVector>();
    if (phys == types::bytes())   return std::make_unique<BytesVector>();
    if (phys == types::any())     return std::make_unique<AnyVector>();
    throw std::runtime_error("Column::make_chunk: unknown physical type");
}

Vector& Column::ensure_writable_chunk() {
    if (chunks_.empty() || chunks_.back()->size() >= kChunkSize) {
        chunks_.push_back(make_chunk());
    }
    return *chunks_.back();
}

void Column::append(const Value& v) {
    auto& chunk = ensure_writable_chunk();
    auto const* phys = type_->physical();
    auto const  hint = type_->kind_hint();

    // AnyVector columns accept any kind.
    if (phys == types::any()) {
        auto& c = static_cast<AnyVector&>(chunk);
        c.append(v);
        return;
    }

    // For typed columns, the value's kind must match the column's hint.
    // Exception: Int32 columns with kind_hint == Int accept FormID (they
    // share the same 32-bit payload). This preserves the Plan 10
    // permissive behavior for untagged Int32 columns used by tests.
    if (hint != Value::Kind::Var && v.kind() != hint) {
        bool const int32_formid_compat =
            (phys == types::int32()) &&
            (hint == Value::Kind::Int) &&
            (v.kind() == Value::Kind::FormID);
        if (!int32_formid_compat) {
            throw std::runtime_error(
                std::string("Column::append: kind mismatch — column '") +
                std::string(type_->name()) +
                "' expects " + std::to_string(static_cast<int>(hint)) +
                " got " + std::to_string(static_cast<int>(v.kind())));
        }
    }

    if (phys == types::int32()) {
        auto& c = static_cast<Int32Vector&>(chunk);
        if (v.kind() == Value::Kind::FormID) {
            c.append(static_cast<int32_t>(v.as_formid()));
        } else {
            c.append(static_cast<int32_t>(v.as_int()));
        }
    } else if (phys == types::int64()) {
        static_cast<Int64Vector&>(chunk).append(v.as_int());
    } else if (phys == types::float64()) {
        static_cast<Float64Vector&>(chunk).append(v.as_float());
    } else if (phys == types::boolean()) {
        static_cast<BoolVector&>(chunk).append(v.as_bool());
    } else if (phys == types::string()) {
        static_cast<StringVector&>(chunk).append(v.as_string());
    } else if (phys == types::keyword()) {
        static_cast<KeywordVector&>(chunk).append(v.as_keyword());
    } else {
        throw std::runtime_error("Column::append: unsupported physical type");
    }
}

Value Column::at(size_t row) const {
    size_t const chunk_idx = row / kChunkSize;
    size_t const row_idx   = row % kChunkSize;
    auto const& chunk = *chunks_[chunk_idx];
    auto const* phys  = type_->physical();
    auto const  hint  = type_->kind_hint();

    if (phys == types::int32()) {
        auto const& c = static_cast<const Int32Vector&>(chunk);
        if (hint == Value::Kind::FormID) {
            return Value::make_formid(static_cast<uint32_t>(c.get(row_idx)));
        }
        return Value::make_int(c.get(row_idx));
    }
    if (phys == types::int64()) {
        auto const& c = static_cast<const Int64Vector&>(chunk);
        return Value::make_int(c.get(row_idx));
    }
    if (phys == types::float64()) {
        auto const& c = static_cast<const Float64Vector&>(chunk);
        return Value::make_float(c.get(row_idx));
    }
    if (phys == types::boolean()) {
        auto const& c = static_cast<const BoolVector&>(chunk);
        return Value::make_bool(c.get(row_idx));
    }
    if (phys == types::string()) {
        auto const& c = static_cast<const StringVector&>(chunk);
        return Value::make_string(c.get(row_idx));
    }
    if (phys == types::keyword()) {
        auto const& c = static_cast<const KeywordVector&>(chunk);
        return Value::make_keyword(c.get(row_idx));
    }
    if (phys == types::any()) {
        auto const& c = static_cast<const AnyVector&>(chunk);
        return c.get(row_idx);
    }
    throw std::runtime_error("Column::at: unsupported physical type");
}

} // namespace mora
