#include "mora_skyrim_compile/esp/mmap_file.h"

#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace mora {

#ifdef _WIN32

MmapFile::MmapFile(const std::string& path) {
    HANDLE file = ::CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                                nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "MmapFile: failed to open: %s\n", path.c_str());
        return;
    }

    LARGE_INTEGER fsize{};
    if (!::GetFileSizeEx(file, &fsize)) {
        ::CloseHandle(file);
        return;
    }
    size_ = static_cast<size_t>(fsize.QuadPart);

    if (size_ == 0) {
        ::CloseHandle(file);
        return;
    }

    HANDLE mapping = ::CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        ::CloseHandle(file);
        size_ = 0;
        return;
    }

    void* mapped = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    ::CloseHandle(mapping);
    ::CloseHandle(file);

    if (mapped == nullptr) {
        size_ = 0;
        return;
    }

    data_ = static_cast<const uint8_t*>(mapped);
}

MmapFile::~MmapFile() {
    if (data_ != nullptr) {
        ::UnmapViewOfFile(const_cast<uint8_t*>(data_));
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
            ::UnmapViewOfFile(const_cast<uint8_t*>(data_));
        }
        data_ = other.data_;
        size_ = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

#else // POSIX

MmapFile::MmapFile(const std::string& path) {
    int const fd = ::open(path.c_str(), O_RDONLY);
    if (fd == -1) {
        std::fprintf(stderr, "MmapFile: failed to open: %s\n", path.c_str());
        return;
    }

    struct stat st{};
    if (::fstat(fd, &st) == -1) {
        ::close(fd);
        return;
    }
    size_ = static_cast<size_t>(st.st_size);

    if (size_ == 0) {
        ::close(fd);
        return;
    }

    void* mapped = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);

    if (mapped == MAP_FAILED) {
        size_ = 0;
        return;
    }

    ::madvise(mapped, size_, MADV_SEQUENTIAL);
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

#endif // _WIN32

std::span<const uint8_t> MmapFile::span() const {
    return {data_, size_};
}

std::span<const uint8_t> MmapFile::span(size_t offset, size_t length) const {
    if (offset + length > size_) {
        return {};
    }
    return {data_ + offset, length};
}

} // namespace mora
