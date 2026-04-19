#include "mora_skyrim_compile/esp/subrecord_reader.h"
#include "mora_skyrim_compile/esp/record_types.h"
#include <cstring>
#include <stdexcept>
#include <string>
#include <zlib.h>

namespace mora {

SubrecordReader::SubrecordReader(std::span<const uint8_t> record_data, uint32_t flags) {
    if (flags & RecordFlags::COMPRESSED) {
        if (record_data.size() < 4) {
            throw std::runtime_error("SubrecordReader: compressed record too short for decompressed size header");
        }

        uint32_t decompressed_size = 0;
        std::memcpy(&decompressed_size, record_data.data(), sizeof(uint32_t));

        decompressed_.resize(decompressed_size);

        uLongf dest_len = decompressed_size;
        const Bytef* src = record_data.data() + 4;
        uLong const src_len = static_cast<uLong>(record_data.size() - 4);

        int const result = uncompress(decompressed_.data(), &dest_len, src, src_len);
        if (result != Z_OK) {
            throw std::runtime_error("SubrecordReader: zlib decompression failed (code " + std::to_string(result) + ")");
        }
        if (dest_len != decompressed_size) {
            throw std::runtime_error("SubrecordReader: decompressed size mismatch");
        }

        data_ = std::span<const uint8_t>(decompressed_.data(), decompressed_size);
    } else {
        data_ = record_data;
    }
}

bool SubrecordReader::next(Subrecord& out) {
    if (offset_ >= data_.size()) {
        return false;
    }

    if (offset_ + sizeof(RawSubrecordHeader) > data_.size()) {
        return false;
    }

    const RawSubrecordHeader* hdr = read_subrecord_header(data_.data() + offset_);

    // Handle XXXX extended size subrecord
    if (hdr->type == "XXXX" && hdr->data_size == 4) {
        if (offset_ + sizeof(RawSubrecordHeader) + 4 > data_.size()) {
            return false;
        }
        std::memcpy(&xxxx_size_, data_.data() + offset_ + sizeof(RawSubrecordHeader), sizeof(uint32_t));
        offset_ += sizeof(RawSubrecordHeader) + 4;
        return next(out);
    }

    uint32_t actual_size = 0;
    if (xxxx_size_ > 0) {
        actual_size = xxxx_size_;
        xxxx_size_ = 0;
    } else {
        actual_size = hdr->data_size;
    }

    if (offset_ + sizeof(RawSubrecordHeader) + actual_size > data_.size()) {
        return false;
    }

    out.type = hdr->type;
    out.data = data_.subspan(offset_ + sizeof(RawSubrecordHeader), actual_size);
    offset_ += sizeof(RawSubrecordHeader) + actual_size;
    return true;
}

void SubrecordReader::reset() {
    offset_ = 0;
    xxxx_size_ = 0;
}

std::span<const uint8_t> SubrecordReader::find(const char* tag) {
    reset();
    Subrecord sub;
    while (next(sub)) {
        if (sub.type == tag) {
            reset();
            return sub.data;
        }
    }
    reset();
    return {};
}

std::vector<std::span<const uint8_t>> SubrecordReader::find_all(const char* tag) {
    reset();
    std::vector<std::span<const uint8_t>> results;
    Subrecord sub;
    while (next(sub)) {
        if (sub.type == tag) {
            results.push_back(sub.data);
        }
    }
    reset();
    return results;
}

} // namespace mora
