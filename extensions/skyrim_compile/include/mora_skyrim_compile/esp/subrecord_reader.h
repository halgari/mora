#pragma once
#include "mora_skyrim_compile/esp/record_types.h"
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace mora {

struct Subrecord {
    RecordTag type;
    std::span<const uint8_t> data;
};

class SubrecordReader {
public:
    // Construct from record's data span and record flags
    SubrecordReader(std::span<const uint8_t> record_data, uint32_t flags);

    // Iterate: get next subrecord. Returns false when done.
    bool next(Subrecord& out);

    // Reset iteration to start
    void reset();

    // Find first subrecord with given 4-char tag. Returns empty span if not found.
    std::span<const uint8_t> find(const char* tag);

    // Find all subrecords with given tag (for repeating subrecords like SPLO, SNAM)
    std::vector<std::span<const uint8_t>> find_all(const char* tag);

private:
    std::span<const uint8_t> data_;
    std::vector<uint8_t> decompressed_; // buffer for compressed records
    size_t offset_ = 0;
    uint32_t xxxx_size_ = 0;
};

} // namespace mora
