#include "mora/data/vector.h"

namespace mora {

// -- Int32Vector ------------------------------------------------------
Int32Vector::Int32Vector() { data_.reserve(kChunkSize); }
const Type* Int32Vector::type() const { return types::int32(); }
void Int32Vector::reserve(size_t n) { data_.reserve(n); }
void Int32Vector::append(int32_t v) { data_.push_back(v); }

// -- Int64Vector ------------------------------------------------------
Int64Vector::Int64Vector() { data_.reserve(kChunkSize); }
const Type* Int64Vector::type() const { return types::int64(); }
void Int64Vector::reserve(size_t n) { data_.reserve(n); }
void Int64Vector::append(int64_t v) { data_.push_back(v); }

// -- Float64Vector ----------------------------------------------------
Float64Vector::Float64Vector() { data_.reserve(kChunkSize); }
const Type* Float64Vector::type() const { return types::float64(); }
void Float64Vector::reserve(size_t n) { data_.reserve(n); }
void Float64Vector::append(double v) { data_.push_back(v); }

// -- BoolVector -------------------------------------------------------
BoolVector::BoolVector() { data_.reserve(kChunkSize); }
const Type* BoolVector::type() const { return types::boolean(); }
void BoolVector::reserve(size_t n) { data_.reserve(n); }
void BoolVector::append(bool v) {
    data_.push_back(v ? 1 : 0);
    ++size_;
}
bool BoolVector::get(size_t i) const { return data_[i] != 0; }

// -- StringVector -----------------------------------------------------
StringVector::StringVector() { data_.reserve(kChunkSize); }
const Type* StringVector::type() const { return types::string(); }
void StringVector::reserve(size_t n) { data_.reserve(n); }
void StringVector::append(StringId id) { data_.push_back(id); }

// -- KeywordVector ----------------------------------------------------
KeywordVector::KeywordVector() { data_.reserve(kChunkSize); }
const Type* KeywordVector::type() const { return types::keyword(); }
void KeywordVector::reserve(size_t n) { data_.reserve(n); }
void KeywordVector::append(StringId id) { data_.push_back(id); }

// -- BytesVector ------------------------------------------------------
BytesVector::BytesVector() {
    bytes_.reserve(kChunkSize * 16);  // rough — average 16 bytes/row
    offsets_.reserve(kChunkSize + 1);
}
const Type* BytesVector::type() const { return types::bytes(); }
void BytesVector::reserve(size_t n) {
    bytes_.reserve(n * 16);
    offsets_.reserve(n + 1);
}
void BytesVector::append(const uint8_t* bytes, size_t n) {
    bytes_.insert(bytes_.end(), bytes, bytes + n);
    offsets_.push_back(bytes_.size());
}
const uint8_t* BytesVector::data(size_t i, size_t* out_len) const {
    size_t const start = offsets_[i];
    size_t const end   = offsets_[i + 1];
    *out_len = end - start;
    return bytes_.data() + start;
}

// -- AnyVector --------------------------------------------------------
AnyVector::AnyVector() {
    kinds_.reserve(kChunkSize);
    payload_idx_.reserve(kChunkSize);
}
const Type* AnyVector::type() const { return types::any(); }
void AnyVector::reserve(size_t n) {
    kinds_.reserve(n);
    payload_idx_.reserve(n);
}

void AnyVector::append(const Value& v) {
    auto const kind = v.kind();
    kinds_.push_back(kind);
    switch (kind) {
        case Value::Kind::Int:
            payload_idx_.push_back(static_cast<uint32_t>(int_payloads_.size()));
            int_payloads_.push_back(v.as_int());
            break;
        case Value::Kind::FormID:
            payload_idx_.push_back(static_cast<uint32_t>(int_payloads_.size()));
            int_payloads_.push_back(static_cast<int64_t>(v.as_formid()));
            break;
        case Value::Kind::Float:
            payload_idx_.push_back(static_cast<uint32_t>(float_payloads_.size()));
            float_payloads_.push_back(v.as_float());
            break;
        case Value::Kind::String:
            payload_idx_.push_back(static_cast<uint32_t>(string_payloads_.size()));
            string_payloads_.push_back(v.as_string().index);
            break;
        case Value::Kind::Keyword:
            payload_idx_.push_back(static_cast<uint32_t>(string_payloads_.size()));
            string_payloads_.push_back(v.as_keyword().index);
            break;
        case Value::Kind::Bool:
            payload_idx_.push_back(static_cast<uint32_t>(bool_payloads_.size()));
            bool_payloads_.push_back(v.as_bool() ? 1 : 0);
            break;
        case Value::Kind::Var:
        case Value::Kind::List:
            // Not expected in FactDB columns; drop to placeholder.
            payload_idx_.push_back(0);
            break;
    }
}

Value AnyVector::get(size_t i) const {
    auto const kind = kinds_[i];
    auto const idx  = payload_idx_[i];
    switch (kind) {
        case Value::Kind::Int:     return Value::make_int(int_payloads_[idx]);
        case Value::Kind::FormID:  return Value::make_formid(
                                       static_cast<uint32_t>(int_payloads_[idx]));
        case Value::Kind::Float:   return Value::make_float(float_payloads_[idx]);
        case Value::Kind::String:  return Value::make_string(StringId{string_payloads_[idx]});
        case Value::Kind::Keyword: return Value::make_keyword(StringId{string_payloads_[idx]});
        case Value::Kind::Bool:    return Value::make_bool(bool_payloads_[idx] != 0);
        case Value::Kind::Var:     return Value::make_var();
        case Value::Kind::List:    return Value::make_var();  // not supported
    }
    return Value::make_var();
}

} // namespace mora
