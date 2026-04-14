#include "mora/harness/npc_dumper.h"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace mora::harness {

// NPC (TESNPC, FormType 0x2B) layout. TESFullName is at +0x0D8 on TESNPC.
static constexpr size_t kFormIdOffset   = 0x14;
static constexpr size_t kFullNameOffset = 0x0D8 + 0x08; // TESFullName.fullName

void read_npc_fields(const void* form, NpcData& out) {
    auto base = static_cast<const char*>(form);

    std::memcpy(&out.formid, base + kFormIdOffset, sizeof(out.formid));

    void* name_ptr = nullptr;
    std::memcpy(&name_ptr, base + kFullNameOffset, sizeof(name_ptr));
    if (name_ptr) {
        out.name = static_cast<const char*>(name_ptr);
    }
}

static std::string format_hex(uint32_t val) {
    std::ostringstream ss;
    ss << "0x" << std::setfill('0') << std::setw(8) << std::uppercase << std::hex << val;
    return ss.str();
}

static std::string escape_json_string(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string npc_to_jsonl(const NpcData& data) {
    std::ostringstream ss;
    ss << "{\"formid\":\"" << format_hex(data.formid) << "\"";
    ss << ",\"name\":\"" << escape_json_string(data.name) << "\"}";
    return ss.str();
}

void write_npcs_jsonl(std::vector<NpcData>& npcs, std::ostream& out) {
    std::sort(npcs.begin(), npcs.end(),
              [](const NpcData& a, const NpcData& b) {
                  return a.formid < b.formid;
              });
    for (const auto& n : npcs) {
        out << npc_to_jsonl(n) << "\n";
    }
}

} // namespace mora::harness
