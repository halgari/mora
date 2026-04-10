#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include <cstdint>
#include <ostream>
#include <vector>

namespace mora {

enum class TriggerKind : uint8_t {
    OnDataLoaded  = 0,
    OnNpcLoad     = 1,
    OnCellChange  = 2,
    OnEquip       = 3,
    OnQuestUpdate = 4,
};

class RtWriter {
public:
    explicit RtWriter(StringPool& pool);
    void write(std::ostream& out, const std::vector<const Rule*>& rules);

private:
    TriggerKind infer_trigger(const Rule& rule) const;

    void write_u8(std::ostream& out, uint8_t v);
    void write_u16(std::ostream& out, uint16_t v);
    void write_u32(std::ostream& out, uint32_t v);

    StringPool& pool_;

    // Interned StringIds for instance fact names used in trigger inference
    StringId id_current_location_;
    StringId id_current_cell_;
    StringId id_equipped_;
    StringId id_quest_stage_;
    StringId id_current_level_;
    StringId id_is_alive_;
};

} // namespace mora
