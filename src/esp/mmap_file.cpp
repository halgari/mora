#include "mora/esp/mmap_file.h"

#include <stdexcept>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace mora {

MmapFile::MmapFile(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        throw std::runtime_error("MmapFile: failed to open file: " + path);
    }

    struct stat st{};
    if (::fstat(fd, &st) == -1) {
        ::close(fd);
        throw std::runtime_error("MmapFile: failed to stat file: " + path);
    }
    size_ = static_cast<size_t>(st.st_size);

    if (size_ == 0) {
        ::close(fd);
        data_ = nullptr;
        return;
    }

    void* mapped = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);

    if (mapped == MAP_FAILED) {
        throw std::runtime_error("MmapFile: mmap failed for file: " + path);
    }

    data_ = static_cast<const uint8_t*>(mapped);
}

MmapFile::~MmapFile() {
    if (data_ != nullptr) {
        ::munmap(const_cast<uint8_t*>(data_), size_);
        data_ = nullptr;
        size_ = 0;
    }
}

MmapFile::MmapFile(MmapFile&& other) noexcept
    : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
}

MmapFile& MmapFile::operator=(MmapFile&& other) noexcept {
    if (this != &other) {
        if (data_ != nullptr) {
            ::munmap(const_cast<uint8_t*>(data_), size_);
        }
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

std::span<const uint8_t> MmapFile::span() const {
    return {data_, size_};
}

std::span<const uint8_t> MmapFile::span(size_t offset, size_t length) const {
    if (offset + length > size_) {
        throw std::runtime_error("MmapFile::span: offset+length exceeds file size");
    }
    return {data_ + offset, length};
}

} // namespace mora
