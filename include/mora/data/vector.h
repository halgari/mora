#pragma once

#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/data/value.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace mora {

inline constexpr size_t kChunkSize = 2048;

// A Vector owns a single chunk of like-typed rows. It knows its Type
// (identity, not content) and its logical size. Typed access is via
// downcast; the base exposes just type(), size(), capacity(), and a
// reserve hook.
class Vector {
public:
    virtual ~Vector() = default;
    virtual const Type* type() const = 0;
    virtual size_t      size() const = 0;
    virtual size_t      capacity() const { return kChunkSize; }
    virtual void        reserve(size_t n) = 0;
    // No Value-level read/write on the base — callers downcast.
};

// Concrete typed vectors ----------------------------------------------
// Each holds a std::vector<T> underneath, pre-reserved to kChunkSize.
// `append(T)` pushes; `get(size_t)` returns the cell by value. No mutation
// of existing rows — vectors are write-once-per-row for now.

class Int32Vector : public Vector {
public:
    Int32Vector();
    const Type* type() const override;
    size_t      size() const override { return data_.size(); }
    void        reserve(size_t n) override;
    void        append(int32_t v);
    int32_t     get(size_t i) const { return data_[i]; }
    // Raw span for vectorized consumers (Plan 12+).
    const int32_t* data() const { return data_.data(); }
private:
    std::vector<int32_t> data_;
};

class Int64Vector : public Vector {
public:
    Int64Vector();
    const Type* type() const override;
    size_t      size() const override { return data_.size(); }
    void        reserve(size_t n) override;
    void        append(int64_t v);
    int64_t     get(size_t i) const { return data_[i]; }
    const int64_t* data() const { return data_.data(); }
private:
    std::vector<int64_t> data_;
};

class Float64Vector : public Vector {
public:
    Float64Vector();
    const Type* type() const override;
    size_t      size() const override { return data_.size(); }
    void        reserve(size_t n) override;
    void        append(double v);
    double      get(size_t i) const { return data_[i]; }
    const double* data() const { return data_.data(); }
private:
    std::vector<double> data_;
};

class BoolVector : public Vector {
public:
    BoolVector();
    const Type* type() const override;
    size_t      size() const override { return size_; }
    void        reserve(size_t n) override;
    void        append(bool v);
    bool        get(size_t i) const;
private:
    // Plain std::vector<uint8_t> keeps each row addressable without
    // the std::vector<bool> proxy surprises. Upgrade to a bit-packed
    // layout later if profiling demands it.
    std::vector<uint8_t> data_;
    size_t size_ = 0;
};

class StringVector : public Vector {
public:
    StringVector();
    const Type* type() const override;
    size_t      size() const override { return data_.size(); }
    void        reserve(size_t n) override;
    void        append(StringId id);
    StringId    get(size_t i) const { return data_[i]; }
private:
    std::vector<StringId> data_;
};

class KeywordVector : public Vector {
public:
    KeywordVector();
    const Type* type() const override;
    size_t      size() const override { return data_.size(); }
    void        reserve(size_t n) override;
    void        append(StringId id);
    StringId    get(size_t i) const { return data_[i]; }
private:
    std::vector<StringId> data_;
};

class BytesVector : public Vector {
public:
    BytesVector();
    const Type* type() const override;
    size_t      size() const override { return offsets_.size() - 1; }
    void        reserve(size_t n) override;
    void        append(const uint8_t* bytes, size_t n);
    // Returns a pointer to row `i`'s bytes plus its length. Pointer
    // is invalidated on the next append.
    const uint8_t* data(size_t i, size_t* out_len) const;
private:
    std::vector<uint8_t> bytes_;
    std::vector<size_t>  offsets_{0};  // [0, off1, off2, ...]; size = N+1
};

// AnyVector — polymorphic. Stores a kind tag + one payload column per
// physical type. Only the payload matching each row's kind is valid;
// the others are don't-care. Built for the Plan 6 tagged-column
// encoding idea, realized at the Vector level.
//
// `append(Value)` routes by `v.kind()`. `get(i)` returns a Value.
//
// This is the slow path. Prefer typed vectors when the column type
// is known statically.
class AnyVector : public Vector {
public:
    AnyVector();
    const Type* type() const override;
    size_t      size() const override { return kinds_.size(); }
    void        reserve(size_t n) override;

    void   append(const Value& v);
    Value  get(size_t i) const;
    Value::Kind kind_at(size_t i) const { return kinds_[i]; }

private:
    std::vector<Value::Kind> kinds_;
    // Per-kind payload columns. For each row i, exactly one entry
    // matches kinds_[i]; positions in other columns are placeholders.
    // Tracking positions per-kind is cheaper than a universal payload
    // union; typed consumers just index into the matching column.
    std::vector<int64_t>  int_payloads_;      // indexed by seq of Int/FormID rows
    std::vector<double>   float_payloads_;
    std::vector<uint32_t> string_payloads_;   // indexed by seq of String/Keyword rows
    std::vector<uint8_t>  bool_payloads_;
    // List payloads store the shared_ptr from Value::make_list so that
    // list data survives across column boundaries without copying.
    std::vector<Value>    list_payloads_;     // indexed by seq of List rows
    // Dense position-in-kind map: for row i, which index into the
    // matching payload column to read.
    std::vector<uint32_t> payload_idx_;
};

} // namespace mora
