#pragma once

#include "mora/core/type.h"
#include "mora/data/value.h"
#include "mora/data/vector.h"

#include <memory>
#include <vector>

namespace mora {

// A Column is Type + chunks. Each chunk is a typed Vector holding up
// to kChunkSize rows. Appends allocate a new chunk lazily when the
// current one fills. The Column owns its chunks.
//
// No random-access `set` — columns are append-only by design. Updates
// happen at the relation level by rewriting affected rows.
class Column {
public:
    explicit Column(const Type* type);

    const Type* type()        const { return type_; }
    size_t      row_count()   const;
    size_t      chunk_count() const { return chunks_.size(); }

    // Access a chunk by index. Callers that know the type downcast
    // (e.g. `static_cast<const Int32Vector&>(col.chunk(0))`).
    Vector&       chunk(size_t i)       { return *chunks_[i]; }
    const Vector& chunk(size_t i) const { return *chunks_[i]; }

    // Append a Value, routed by this column's Type. For physical
    // types, the Value's kind must match (Int32 column accepts Int
    // values, etc.); mismatches throw via assertion in debug. For
    // nominal types, the physical match determines routing (FormID
    // nominal over Int32 accepts FormID or Int values).
    //
    // AnyVector columns accept any kind.
    void append(const Value& v);

    // Row-level read. Slow path — rebuilds a Value from the column's
    // typed storage. Plan 12's vectorized evaluator avoids this.
    Value at(size_t row) const;

private:
    const Type*                        type_;
    std::vector<std::unique_ptr<Vector>> chunks_;

    Vector& ensure_writable_chunk();
    std::unique_ptr<Vector> make_chunk() const;
};

} // namespace mora
