#include "mora/data/plugin_facts.h"
#include "mora/core/string_utils.h"
#include "mora/data/value.h"
#include "mora/eval/fact_db.h"

namespace mora {

namespace {

std::string extension_lower(const std::string& filename) {
    auto dot = filename.find_last_of('.');
    if (dot == std::string::npos) return {};
    return to_lower(filename.substr(dot));
}

Value plugin_key(StringPool& pool, const std::string& filename) {
    return Value::make_string(pool.intern(to_lower(filename)));
}

} // namespace

void populate_plugin_facts(FactDB& db, StringPool& pool,
                            const LoadOrder& lo,
                            const std::vector<PluginInfo>& infos,
                            const RuntimeIndexMap& rtmap) {
    (void)lo; // currently derived entirely from infos + rtmap

    StringId rel_exists     = pool.intern("plugin_exists");
    StringId rel_load_index = pool.intern("plugin_load_index");
    StringId rel_is_master  = pool.intern("plugin_is_master");
    StringId rel_is_light   = pool.intern("plugin_is_light");
    StringId rel_master_of  = pool.intern("plugin_master_of");
    StringId rel_version    = pool.intern("plugin_version");
    StringId rel_extension  = pool.intern("plugin_extension");

    for (auto& info : infos) {
        Value p = plugin_key(pool, info.filename);
        std::string lowered = to_lower(info.filename);

        db.add_fact(rel_exists, {p});

        auto idx_it = rtmap.index.find(lowered);
        if (idx_it != rtmap.index.end()) {
            db.add_fact(rel_load_index,
                        {p, Value::make_int(static_cast<int64_t>(idx_it->second))});
        }

        if (info.is_esm()) db.add_fact(rel_is_master, {p});
        if (info.is_esl()) db.add_fact(rel_is_light,  {p});

        db.add_fact(rel_version,
                    {p, Value::make_float(static_cast<double>(info.version))});

        db.add_fact(rel_extension,
                    {p, Value::make_string(pool.intern(extension_lower(info.filename)))});

        for (auto& master : info.masters) {
            db.add_fact(rel_master_of,
                        {p, plugin_key(pool, master)});
        }
    }
}

} // namespace mora
