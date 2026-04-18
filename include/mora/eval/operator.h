#pragma once

#include "mora/eval/binding_chunk.h"

#include <memory>
#include <optional>

namespace mora {

// Pull-model iterator. Each call to next_chunk() produces the next
// BindingChunk (up to kChunkSize rows) or nullopt when the stream is
// exhausted.
class Operator {
public:
    virtual ~Operator()                              = default;
    virtual std::optional<BindingChunk> next_chunk() = 0;
};

} // namespace mora
