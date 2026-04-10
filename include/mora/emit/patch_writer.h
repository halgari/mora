#pragma once
#include "mora/eval/patch_set.h"
#include "mora/core/string_pool.h"
#include <ostream>

namespace mora {

class PatchWriter {
public:
    explicit PatchWriter(StringPool& pool) : pool_(pool) {}
    void write(std::ostream& out, const ResolvedPatchSet& patches,
               uint64_t load_order_hash, uint64_t source_hash);
private:
    void write_u8(std::ostream& out, uint8_t v);
    void write_u16(std::ostream& out, uint16_t v);
    void write_u32(std::ostream& out, uint32_t v);
    void write_u64(std::ostream& out, uint64_t v);
    void write_i64(std::ostream& out, int64_t v);
    void write_f64(std::ostream& out, double v);
    void write_value(std::ostream& out, const Value& v);
    StringPool& pool_;
};

} // namespace mora
