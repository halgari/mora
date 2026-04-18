#include "mora_skyrim_runtime/runtime_snapshot.h"

#include "mora_skyrim_runtime/game_api.h"

#include "mora/data/columnar_relation.h"
#include "mora/diag/diagnostic.h"

#include <array>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>

namespace mora_skyrim_runtime {

namespace {

// ── Local string-pool helper for the writer ───────────────────────────
class LocalStringPool {
public:
    // Intern a string into the pool; returns its byte offset.
    uint32_t intern(std::string_view s) {
        auto it = by_string_.find(std::string(s));
        if (it != by_string_.end()) return it->second;
        uint32_t offset = static_cast<uint32_t>(bytes_.size());
        uint32_t len = static_cast<uint32_t>(s.size());
        // Append u32 length + raw bytes
        bytes_.insert(bytes_.end(),
                      reinterpret_cast<const uint8_t*>(&len),
                      reinterpret_cast<const uint8_t*>(&len) + sizeof(len));
        bytes_.insert(bytes_.end(), s.begin(), s.end());
        by_string_.emplace(std::string(s), offset);
        return offset;
    }
    const std::vector<uint8_t>& bytes() const { return bytes_; }
private:
    std::unordered_map<std::string, uint32_t> by_string_;
    std::vector<uint8_t>                       bytes_;
};

// Safe read of u32 length + bytes from string_pool at `offset`.
// Returns empty view if the offset or length would overrun the buffer.
std::string_view read_string_at(const std::vector<uint8_t>& pool, uint32_t offset) {
    if (offset + sizeof(uint32_t) > pool.size()) return {};
    uint32_t len = 0;
    std::memcpy(&len, pool.data() + offset, sizeof(len));
    if (offset + sizeof(uint32_t) + len > pool.size()) return {};
    return std::string_view(
        reinterpret_cast<const char*>(pool.data() + offset + sizeof(uint32_t)),
        len);
}

Op op_for_relation(std::string_view rel_name) {
    if (rel_name == "skyrim/set")      return Op::Set;
    if (rel_name == "skyrim/add")      return Op::Add;
    if (rel_name == "skyrim/remove")   return Op::Remove;
    if (rel_name == "skyrim/multiply") return Op::Multiply;
    return Op::Set;  // default — caller filters by name before reaching here
}

// Translate Value::Kind → ValueKind (differing int ordering).
ValueKind kind_for(mora::Value::Kind k) {
    switch (k) {
        case mora::Value::Kind::Int:     return ValueKind::Int;
        case mora::Value::Kind::Float:   return ValueKind::Float;
        case mora::Value::Kind::FormID:  return ValueKind::FormID;
        case mora::Value::Kind::String:  return ValueKind::String;
        case mora::Value::Kind::Keyword: return ValueKind::Keyword;
        case mora::Value::Kind::Bool:    return ValueKind::Bool;
        case mora::Value::Kind::Var:
        case mora::Value::Kind::List:
        default:                          return ValueKind::Int;  // defensive
    }
}

} // anonymous

// ── LoadedSnapshot::get_string ────────────────────────────────────────
std::string_view LoadedSnapshot::get_string(uint32_t offset) const {
    return read_string_at(string_pool, offset);
}

// ── write_snapshot ────────────────────────────────────────────────────
bool write_snapshot(const std::filesystem::path& out_path,
                     const mora::FactDB&           db,
                     const mora::StringPool&       pool,
                     mora::DiagBag&                diags)
{
    LocalStringPool                 sp;
    std::vector<SnapshotRow>        rows;

    // Scan the four effect relations in Set / Add / Remove / Multiply order so
    // the runtime applies them in a predictable sequence.
    constexpr std::array<std::string_view, 4> rel_names = {
        "skyrim/set", "skyrim/add", "skyrim/remove", "skyrim/multiply",
    };
    // `pool` is const, so we can't intern into it. Use a mutable local pool
    // for lookups; the StringPool API we need is `get(StringId)` which is const.
    auto& mpool = const_cast<mora::StringPool&>(pool);

    for (auto rel_name : rel_names) {
        auto rel_id = mpool.intern(std::string(rel_name));
        const auto* rel = db.get_relation_columnar(rel_id);
        if (rel == nullptr) continue;
        if (rel->row_count() == 0) continue;
        if (rel->arity() != 3) {
            diags.error("runtime-snapshot-arity",
                std::string("runtime_snapshot: relation '") +
                    std::string(rel_name) + "' has arity " +
                    std::to_string(rel->arity()) + "; expected 3",
                mora::SourceSpan{}, "");
            return false;
        }

        Op const op = op_for_relation(rel_name);

        for (size_t r = 0; r < rel->row_count(); ++r) {
            auto const target = rel->column(0).at(r);
            auto const field  = rel->column(1).at(r);
            auto const value  = rel->column(2).at(r);

            if (target.kind() != mora::Value::Kind::FormID) {
                // Skip malformed rows (mirrors EffectAppendOp's silent drop).
                continue;
            }
            if (field.kind() != mora::Value::Kind::Keyword &&
                field.kind() != mora::Value::Kind::String) {
                continue;
            }

            auto field_sv = (field.kind() == mora::Value::Kind::Keyword)
                ? pool.get(field.as_keyword())
                : pool.get(field.as_string());

            SnapshotRow row{};
            row.op                  = static_cast<uint8_t>(op);
            row.value_kind          = static_cast<uint8_t>(kind_for(value.kind()));
            row.target_formid       = target.as_formid();
            row.field_string_offset = sp.intern(field_sv);

            switch (value.kind()) {
                case mora::Value::Kind::Int:
                    row.value_payload = static_cast<uint64_t>(value.as_int());
                    break;
                case mora::Value::Kind::Float: {
                    double d = value.as_float();
                    std::memcpy(&row.value_payload, &d, sizeof(d));
                    break;
                }
                case mora::Value::Kind::FormID:
                    row.value_payload = static_cast<uint64_t>(value.as_formid());
                    break;
                case mora::Value::Kind::Bool:
                    row.value_payload = value.as_bool() ? 1u : 0u;
                    break;
                case mora::Value::Kind::String: {
                    auto sv = pool.get(value.as_string());
                    row.value_payload = static_cast<uint64_t>(sp.intern(sv));
                    break;
                }
                case mora::Value::Kind::Keyword: {
                    auto sv = pool.get(value.as_keyword());
                    row.value_payload = static_cast<uint64_t>(sp.intern(sv));
                    break;
                }
                default:
                    // Var / List unsupported; skip silently.
                    continue;
            }
            rows.push_back(row);
        }
    }

    // Emit the file: header + row array + string pool.
    SnapshotHeader hdr{};
    hdr.magic            = kSnapshotMagic;
    hdr.version          = kSnapshotVersion;
    hdr.num_rows         = static_cast<uint32_t>(rows.size());
    hdr.string_pool_size = static_cast<uint32_t>(sp.bytes().size());

    std::error_code ec;
    std::filesystem::create_directories(out_path.parent_path(), ec);
    // create_directories may fail if the parent is empty; ignore ec here.
    // The subsequent ofstream open will surface any real problem.

    std::ofstream ofs(out_path, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        diags.error("runtime-snapshot-open",
            std::string("runtime_snapshot: cannot open '") + out_path.string() +
                "' for writing",
            mora::SourceSpan{}, "");
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));
    if (!rows.empty()) {
        ofs.write(reinterpret_cast<const char*>(rows.data()),
                  static_cast<std::streamsize>(rows.size() * sizeof(SnapshotRow)));
    }
    if (!sp.bytes().empty()) {
        ofs.write(reinterpret_cast<const char*>(sp.bytes().data()),
                  static_cast<std::streamsize>(sp.bytes().size()));
    }
    return static_cast<bool>(ofs);
}

// ── read_snapshot ─────────────────────────────────────────────────────
std::optional<LoadedSnapshot>
read_snapshot(const std::filesystem::path& path,
               mora::DiagBag&               diags)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        diags.error("runtime-snapshot-open",
            std::string("runtime_snapshot: cannot open '") + path.string() +
                "' for reading",
            mora::SourceSpan{}, "");
        return std::nullopt;
    }

    SnapshotHeader hdr{};
    ifs.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!ifs || hdr.magic != kSnapshotMagic || hdr.version != kSnapshotVersion) {
        diags.error("runtime-snapshot-bad-header",
            std::string("runtime_snapshot: bad header at '") + path.string() + "'",
            mora::SourceSpan{}, "");
        return std::nullopt;
    }

    LoadedSnapshot snap;
    snap.rows.resize(hdr.num_rows);
    if (hdr.num_rows > 0) {
        ifs.read(reinterpret_cast<char*>(snap.rows.data()),
                 static_cast<std::streamsize>(hdr.num_rows * sizeof(SnapshotRow)));
        if (!ifs) {
            diags.error("runtime-snapshot-truncated-rows",
                "runtime_snapshot: truncated row block",
                mora::SourceSpan{}, "");
            return std::nullopt;
        }
    }
    snap.string_pool.resize(hdr.string_pool_size);
    if (hdr.string_pool_size > 0) {
        ifs.read(reinterpret_cast<char*>(snap.string_pool.data()),
                 static_cast<std::streamsize>(hdr.string_pool_size));
        if (!ifs) {
            diags.error("runtime-snapshot-truncated-pool",
                "runtime_snapshot: truncated string pool",
                mora::SourceSpan{}, "");
            return std::nullopt;
        }
    }
    return snap;
}

// ── apply_snapshot ────────────────────────────────────────────────────
size_t apply_snapshot(const LoadedSnapshot& snap,
                       GameAPI&              api,
                       mora::StringPool&     pool)
{
    size_t applied = 0;
    for (const auto& row : snap.rows) {
        auto field_sv = snap.get_string(row.field_string_offset);
        if (field_sv.empty()) continue;

        // Decode value.
        mora::Value v;
        switch (static_cast<ValueKind>(row.value_kind)) {
            case ValueKind::Int:
                v = mora::Value::make_int(
                    static_cast<int64_t>(row.value_payload));
                break;
            case ValueKind::Float: {
                double d = 0;
                std::memcpy(&d, &row.value_payload, sizeof(d));
                v = mora::Value::make_float(d);
                break;
            }
            case ValueKind::FormID:
                v = mora::Value::make_formid(
                    static_cast<uint32_t>(row.value_payload));
                break;
            case ValueKind::Bool:
                v = mora::Value::make_bool(row.value_payload != 0);
                break;
            case ValueKind::String: {
                auto sv = snap.get_string(static_cast<uint32_t>(row.value_payload));
                v = mora::Value::make_string(pool.intern(std::string(sv)));
                break;
            }
            case ValueKind::Keyword: {
                auto sv = snap.get_string(static_cast<uint32_t>(row.value_payload));
                v = mora::Value::make_keyword(pool.intern(std::string(sv)));
                break;
            }
            default:
                continue;
        }

        // Dispatch.
        switch (static_cast<Op>(row.op)) {
            case Op::Set:      api.set     (row.target_formid, field_sv, v); break;
            case Op::Add:      api.add     (row.target_formid, field_sv, v); break;
            case Op::Remove:   api.remove  (row.target_formid, field_sv, v); break;
            case Op::Multiply: api.multiply(row.target_formid, field_sv, v); break;
            default: continue;
        }
        ++applied;
    }
    return applied;
}

} // namespace mora_skyrim_runtime
