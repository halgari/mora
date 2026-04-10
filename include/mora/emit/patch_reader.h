#pragma once
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <istream>
#include <optional>
#include <vector>

namespace mora {

struct PatchFile {
    uint16_t version;
    uint64_t load_order_hash;
    uint64_t source_hash;
    std::vector<ResolvedPatch> patches;
};

class PatchReader {
public:
    explicit PatchReader(StringPool& pool) : pool_(pool) {}
    std::optional<PatchFile> read(std::istream& in);
private:
    bool read_u8(std::istream& in, uint8_t& v);
    bool read_u16(std::istream& in, uint16_t& v);
    bool read_u32(std::istream& in, uint32_t& v);
    bool read_u64(std::istream& in, uint64_t& v);
    bool read_i64(std::istream& in, int64_t& v);
    bool read_f64(std::istream& in, double& v);
    bool read_value(std::istream& in, Value& v);
    StringPool& pool_;
};

} // namespace mora
