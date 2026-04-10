#include "mora/esp/esp_reader.h"
#include "mora/esp/subrecord_reader.h"
#include "mora/esp/record_types.h"
#include <cstring>
#include <algorithm>

namespace mora {

EspReader::EspReader(StringPool& pool, DiagBag& diags, const SchemaRegistry& schema)
    : pool_(pool), diags_(diags), schema_(schema) {}

void EspReader::set_needed_relations(const std::unordered_set<uint32_t>& relation_name_indexes) {
    needed_relations_ = relation_name_indexes;
    filter_active_ = !needed_relations_.empty();
}

bool EspReader::is_relation_needed(StringId name) const {
    if (!filter_active_) return true;
    return needed_relations_.count(name.index) > 0;
}

void EspReader::read_plugin(const std::filesystem::path& path, FactDB& db) {
    MmapFile file(path.string());
    std::string filename = path.filename().string();
    PluginInfo info = build_plugin_index(file, filename);

    for (auto& [type, records] : info.by_type) {
        auto all_schemas = schema_.schemas_for_record(type);
        if (all_schemas.empty()) continue;

        // Filter to only needed schemas (lazy loading)
        std::vector<const RelationSchema*> schemas;
        for (auto* s : all_schemas) {
            if (is_relation_needed(s->name)) {
                schemas.push_back(s);
            }
        }
        // Always include editor_id for symbol resolution
        auto edid_name = pool_.intern("editor_id");
        for (auto* s : all_schemas) {
            if (s->name == edid_name && !is_relation_needed(s->name)) {
                schemas.push_back(s);
                break;
            }
        }
        if (schemas.empty()) continue;

        for (auto& loc : records) {
            extract_record_facts(file, info, loc, schemas, db);
        }
    }

    current_load_index_++;
}

void EspReader::read_load_order(const std::vector<std::filesystem::path>& plugins, FactDB& db) {
    current_load_index_ = 0;
    for (auto& path : plugins) {
        read_plugin(path, db);
    }
}

const std::unordered_map<std::string, uint32_t>& EspReader::editor_id_map() const {
    return editor_ids_;
}

uint32_t EspReader::resolve_symbol(const std::string& editor_id) const {
    auto it = editor_ids_.find(editor_id);
    if (it == editor_ids_.end()) return 0;
    return it->second;
}

uint32_t EspReader::make_global_formid(uint32_t local_id, const PluginInfo& info) {
    (void)info;
    return (current_load_index_ << 24) | (local_id & 0x00FFFFFF);
}

void EspReader::extract_record_facts(const MmapFile& file, const PluginInfo& info,
                                      const RecordLocation& loc,
                                      const std::vector<const RelationSchema*>& schemas,
                                      FactDB& db) {
    auto data = file.span(loc.offset + sizeof(RawRecordHeader), loc.data_size);
    SubrecordReader reader(data, loc.flags);

    uint32_t global_fid = make_global_formid(loc.form_id, info);
    records_processed_++;

    // Extract EDID for the editor_ids_ map
    {
        auto edid_data = reader.find("EDID");
        if (!edid_data.empty()) {
            // Null-terminated string
            std::string edid(reinterpret_cast<const char*>(edid_data.data()));
            if (!edid.empty()) {
                editor_ids_[edid] = global_fid;
            }
        }
        reader.reset();
    }

    bool is_localized = info.is_localized();

    for (auto* schema : schemas) {
        for (auto& src : schema->esp_sources) {
            // Only process sources matching this record's type
            std::string record_type(read_record_header(file.span().data() + loc.offset)->type.as_sv());
            if (src.record_type != record_type) continue;

            switch (src.kind) {
            case EspSource::Kind::Existence: {
                Tuple t;
                t.push_back(Value::make_formid(global_fid));
                db.add_fact(schema->name, std::move(t));
                facts_generated_++;
                break;
            }

            case EspSource::Kind::Subrecord: {
                reader.reset();
                auto sub_data = reader.find(src.subrecord_tag.c_str());
                if (sub_data.empty()) break;

                Value val = read_esp_value(sub_data, 0, src.read_type, is_localized);
                Tuple t;
                t.push_back(Value::make_formid(global_fid));
                t.push_back(std::move(val));
                db.add_fact(schema->name, std::move(t));
                facts_generated_++;
                break;
            }

            case EspSource::Kind::PackedField: {
                reader.reset();
                auto sub_data = reader.find(src.subrecord_tag.c_str());
                if (sub_data.empty()) break;

                // Check bounds: need enough data for the read at the given offset
                size_t needed = src.offset;
                switch (src.read_type) {
                    case ReadType::Int8: case ReadType::UInt8: needed += 1; break;
                    case ReadType::Int16: case ReadType::UInt16: needed += 2; break;
                    case ReadType::Int32: case ReadType::UInt32:
                    case ReadType::FormID: case ReadType::Float32:
                    case ReadType::LString: needed += 4; break;
                    case ReadType::ZString: needed += 1; break; // at least 1 byte
                }
                if (sub_data.size() < needed) break;

                Value val = read_esp_value(sub_data, src.offset, src.read_type, is_localized);
                Tuple t;
                t.push_back(Value::make_formid(global_fid));
                t.push_back(std::move(val));
                db.add_fact(schema->name, std::move(t));
                facts_generated_++;
                break;
            }

            case EspSource::Kind::ArrayField: {
                reader.reset();
                auto sub_data = reader.find(src.subrecord_tag.c_str());
                if (sub_data.empty() || src.element_size == 0) break;

                size_t count = sub_data.size() / src.element_size;
                for (size_t i = 0; i < count; i++) {
                    size_t elem_offset = i * src.element_size;
                    if (elem_offset + src.element_size > sub_data.size()) break;

                    // For FormID array elements, resolve local -> global
                    if (src.read_type == ReadType::FormID && src.element_size >= 4) {
                        uint32_t local_id = 0;
                        std::memcpy(&local_id, sub_data.data() + elem_offset, 4);
                        uint32_t gfid = make_global_formid(local_id, info);

                        Tuple t;
                        t.push_back(Value::make_formid(global_fid));
                        t.push_back(Value::make_formid(gfid));
                        db.add_fact(schema->name, std::move(t));
                        facts_generated_++;
                    } else {
                        Value val = read_esp_value(sub_data, elem_offset, src.read_type, is_localized);
                        Tuple t;
                        t.push_back(Value::make_formid(global_fid));
                        t.push_back(std::move(val));
                        db.add_fact(schema->name, std::move(t));
                        facts_generated_++;
                    }
                }
                break;
            }

            case EspSource::Kind::ListField: {
                reader.reset();
                auto all_subs = reader.find_all(src.subrecord_tag.c_str());
                for (auto& sub_data : all_subs) {
                    if (sub_data.empty()) continue;

                    if (src.read_type == ReadType::FormID) {
                        if (sub_data.size() < src.offset + 4) continue;
                        uint32_t local_id = 0;
                        std::memcpy(&local_id, sub_data.data() + src.offset, 4);
                        uint32_t gfid = make_global_formid(local_id, info);

                        Tuple t;
                        t.push_back(Value::make_formid(global_fid));
                        t.push_back(Value::make_formid(gfid));
                        db.add_fact(schema->name, std::move(t));
                        facts_generated_++;
                    } else {
                        size_t needed = src.offset + 1;
                        if (sub_data.size() < needed) continue;
                        Value val = read_esp_value(sub_data, src.offset, src.read_type, is_localized);
                        Tuple t;
                        t.push_back(Value::make_formid(global_fid));
                        t.push_back(std::move(val));
                        db.add_fact(schema->name, std::move(t));
                        facts_generated_++;
                    }
                }
                break;
            }
            }
        }
    }
}

Value EspReader::read_esp_value(std::span<const uint8_t> data, size_t offset,
                                 ReadType read_type, bool localized) {
    switch (read_type) {
    case ReadType::FormID: {
        if (offset + 4 > data.size()) return Value::make_formid(0);
        uint32_t val = 0;
        std::memcpy(&val, data.data() + offset, 4);
        return Value::make_formid(val);
    }
    case ReadType::Int8: {
        if (offset + 1 > data.size()) return Value::make_int(0);
        int8_t val = 0;
        std::memcpy(&val, data.data() + offset, 1);
        return Value::make_int(val);
    }
    case ReadType::Int16: {
        if (offset + 2 > data.size()) return Value::make_int(0);
        int16_t val = 0;
        std::memcpy(&val, data.data() + offset, 2);
        return Value::make_int(val);
    }
    case ReadType::Int32: {
        if (offset + 4 > data.size()) return Value::make_int(0);
        int32_t val = 0;
        std::memcpy(&val, data.data() + offset, 4);
        return Value::make_int(val);
    }
    case ReadType::UInt8: {
        if (offset + 1 > data.size()) return Value::make_int(0);
        uint8_t val = 0;
        std::memcpy(&val, data.data() + offset, 1);
        return Value::make_int(val);
    }
    case ReadType::UInt16: {
        if (offset + 2 > data.size()) return Value::make_int(0);
        uint16_t val = 0;
        std::memcpy(&val, data.data() + offset, 2);
        return Value::make_int(val);
    }
    case ReadType::UInt32: {
        if (offset + 4 > data.size()) return Value::make_int(0);
        uint32_t val = 0;
        std::memcpy(&val, data.data() + offset, 4);
        return Value::make_int(static_cast<int64_t>(val));
    }
    case ReadType::Float32: {
        if (offset + 4 > data.size()) return Value::make_float(0.0);
        float val = 0;
        std::memcpy(&val, data.data() + offset, 4);
        return Value::make_float(static_cast<double>(val));
    }
    case ReadType::ZString: {
        if (offset >= data.size()) return Value::make_string(pool_.intern(""));
        const char* str = reinterpret_cast<const char*>(data.data() + offset);
        size_t max_len = data.size() - offset;
        size_t len = strnlen(str, max_len);
        return Value::make_string(pool_.intern(std::string_view(str, len)));
    }
    case ReadType::LString: {
        if (localized) {
            // Localized: 4-byte string ID. Store as int for now.
            if (offset + 4 > data.size()) return Value::make_int(0);
            uint32_t string_id = 0;
            std::memcpy(&string_id, data.data() + offset, 4);
            return Value::make_int(static_cast<int64_t>(string_id));
        } else {
            // Not localized: treat as ZString
            if (offset >= data.size()) return Value::make_string(pool_.intern(""));
            const char* str = reinterpret_cast<const char*>(data.data() + offset);
            size_t max_len = data.size() - offset;
            size_t len = strnlen(str, max_len);
            return Value::make_string(pool_.intern(std::string_view(str, len)));
        }
    }
    }
    return Value::make_int(0);
}

} // namespace mora
