#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <vector>

namespace mora {

class Arena {
public:
    explicit Arena(size_t chunk_size = 4096);
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    Arena(Arena&&) = default;
    Arena& operator=(Arena&&) = default;

    template <typename T, typename... Args>
    T* alloc(Args&&... args) {
        void* mem = alloc_raw(sizeof(T), alignof(T));
        return new (mem) T(std::forward<Args>(args)...);
    }

    void reset();
    size_t bytes_allocated() const;

private:
    void* alloc_raw(size_t size, size_t align);

    struct Chunk {
        std::unique_ptr<std::byte[]> data;
        size_t size;
    };

    size_t chunk_size_;
    std::vector<Chunk> chunks_;
    size_t current_offset_ = 0;
    size_t total_allocated_ = 0;
};

} // namespace mora
