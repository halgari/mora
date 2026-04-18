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

    if (phys == types::int32()) {
        // Int32 column accepts Int (narrowed) or FormID (already 32-bit nominal).
        auto& c = static_cast<Int32Vector&>(chunk);
        if (v.kind() == Value::Kind::FormID) c.append(static_cast<int32_t>(v.as_formid()));
        else                                  c.append(static_cast<int32_t>(v.as_int()));
    } else if (phys == types::int64()) {
        auto& c = static_cast<Int64Vector&>(chunk);
        c.append(v.as_int());
    } else if (phys == types::float64()) {
        auto& c = static_cast<Float64Vector&>(chunk);
        c.append(v.as_float());
    } else if (phys == types::boolean()) {
        auto& c = static_cast<BoolVector&>(chunk);
        c.append(v.as_bool());
    } else if (phys == types::string()) {
        auto& c = static_cast<StringVector&>(chunk);
        c.append(v.as_string());
    } else if (phys == types::keyword()) {
        auto& c = static_cast<KeywordVector&>(chunk);
        c.append(v.as_keyword());
    } else if (phys == types::any()) {
        auto& c = static_cast<AnyVector&>(chunk);
        c.append(v);
    } else {
        throw std::runtime_error("Column::append: unsupported physical type");
    }
}

Value Column::at(size_t row) const {
    size_t const chunk_idx = row / kChunkSize;
    size_t const row_idx   = row % kChunkSize;
    auto const& chunk = *chunks_[chunk_idx];
    auto const* phys  = type_->physical();

    if (phys == types::int32()) {
        auto const& c = static_cast<const Int32Vector&>(chunk);
        // Nominal FormID-over-Int32 decodes to FormID; raw Int32 decodes to Int.
        // For v1 we decode according to the nominal name — "FormID" → FormID kind.
        // (Refine via a nominal_to_kind hint in a later plan if needed.)
        if (type_->is_nominal() && type_->name() == "FormID") {
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
