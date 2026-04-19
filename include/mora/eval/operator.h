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

    // The set of variable names this operator binds in its output.
    // Used by the planner to compute shared vars for JoinOp construction.
    virtual const std::vector<StringId>& output_var_names() const = 0;
};

} // namespace mora
