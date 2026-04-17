#include "mora/core/arena.h"
#include <algorithm>
#include <cassert>
#include <memory>

namespace mora {

Arena::Arena(size_t chunk_size)
    : chunk_size_(chunk_size) {}

Arena::~Arena() = default;

void* Arena::alloc_raw(size_t size, size_t align) {
    // Try to fit into the current (last) chunk first.
    if (!chunks_.empty()) {
        Chunk const& current = chunks_.back();
        void* ptr = current.data.get() + current_offset_;
        size_t space = current.size - current_offset_;
        if (std::align(align, size, ptr, space)) {
            size_t const used = static_cast<std::byte*>(ptr) - current.data.get();
            current_offset_ = used + size;
            total_allocated_ += size;
            return ptr;
        }
    }

    // Need a new chunk. Make it large enough for worst-case alignment + size.
    size_t new_chunk_size = chunk_size_;
    new_chunk_size = std::max(size + align, new_chunk_size);

    Chunk const& chunk = chunks_.emplace_back(Chunk{
        std::make_unique<std::byte[]>(new_chunk_size),
        new_chunk_size
    });
    current_offset_ = 0;

    void* ptr = chunk.data.get();
    size_t space = chunk.size;
    void* aligned = std::align(align, size, ptr, space);
    assert(aligned != nullptr && "Alignment failed on fresh chunk");

    size_t const used = static_cast<std::byte*>(aligned) - chunk.data.get();
    current_offset_ = used + size;
    total_allocated_ += size;
    return aligned;
}

void Arena::reset() {
    chunks_.clear();
    current_offset_ = 0;
    total_allocated_ = 0;
}

size_t Arena::bytes_allocated() const {
    return total_allocated_;
}

} // namespace mora
