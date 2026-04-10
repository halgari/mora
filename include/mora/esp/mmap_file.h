#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace mora {

class MmapFile {
public:
    explicit MmapFile(const std::string& path);
    ~MmapFile();
    MmapFile(const MmapFile&) = delete;
    MmapFile& operator=(const MmapFile&) = delete;
    MmapFile(MmapFile&& other) noexcept;
    MmapFile& operator=(MmapFile&& other) noexcept;

    std::span<const uint8_t> span() const;
    std::span<const uint8_t> span(size_t offset, size_t length) const;
    size_t size() const { return size_; }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
};

} // namespace mora
