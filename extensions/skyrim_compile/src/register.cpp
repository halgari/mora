#include "mora_skyrim_compile/register.h"
#include "mora_skyrim_compile/esp_data_source.h"
#include "mora_skyrim_compile/types.h"
#include "mora/core/string_pool.h"
#include "mora/data/schema_registry.h"
#include "mora/ext/extension.h"
#include "mora/ext/relation_schema.h"

#include <memory>
#include <string>
#include <unordered_set>

namespace mora_skyrim_compile {

namespace {

// Convert the core, StringPool-bound mora::RelationSchema into the
// pool-agnostic mora::ext::RelationSchema that ExtensionContext stores.
// EspSource vectors stay in the core SchemaRegistry; the ext side gets
// column type pointers (const Type* singletons) for downstream consumers.
mora::ext::RelationSchema to_ext_schema(const mora::RelationSchema& core,
                                         const mora::StringPool& pool) {
    mora::ext::RelationSchema out;
    out.name = pool.get(core.name);
    out.columns.reserve(core.column_types.size());
    std::unordered_set<size_t> idx(core.indexed_columns.begin(),
                                    core.indexed_columns.end());
    for (size_t i = 0; i < core.column_types.size(); ++i) {
        mora::ext::ColumnSpec c;
        c.name    = "col" + std::to_string(i);  // positional; real names land with Plan 5+
        c.indexed = idx.contains(i);
        c.type    = core.column_types[i];  // const Type* singleton
        out.columns.push_back(std::move(c));
    }
    return out;
}

} // namespace

void register_skyrim(mora::ext::ExtensionContext& ctx) {
    // Register Skyrim nominal types first so schema-building code can
    // resolve type names via mora::types::get().
    register_all_nominal_types(ctx);

    ctx.register_data_source(std::make_unique<SkyrimEspDataSource>());

    // Bridge: enumerate the default Skyrim schemas via a throwaway
    // SchemaRegistry bound to a throwaway StringPool, then mirror each
    // into the ExtensionContext so sinks + future consumers can query
    // them pool-agnostically.
    mora::StringPool bridge_pool;
    mora::SchemaRegistry bridge(bridge_pool);
    bridge.register_defaults();
    for (const auto* core : bridge.all_schemas()) {
        ctx.register_relation(to_ext_schema(*core, bridge_pool));
    }

    // Effect relations — populated by the evaluator in a later plan.
    // For Plan 4 they exist as schemas only; their FactDB slots stay
    // empty, and the parquet sink's output-only filter emits an empty
    // parquet file for each. Once the evaluator starts producing
    // effect facts (spec step 11), these files will carry the data
    // that mora_patches.bin carries today.
    for (std::string_view effect : {"skyrim/set", "skyrim/add", "skyrim/remove", "skyrim/multiply"}) {
        ctx.register_relation(mora::ext::RelationSchema{
            .name      = std::string(effect),
            .columns   = {
                {"entity", /*indexed*/ true},
                {"field",  /*indexed*/ false},
                {"value",  /*indexed*/ false},
            },
            .is_output = true,
        });
    }
}

} // namespace mora_skyrim_compile
