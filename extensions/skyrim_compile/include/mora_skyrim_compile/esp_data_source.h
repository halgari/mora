#pragma once

#include "mora/ext/data_source.h"

#include <string>
#include <vector>

namespace mora_skyrim_compile {

// Data source that loads facts from Skyrim ESP/ESM/ESL plugin files.
// Orchestrates: build load order (data-dir walk or plugins.txt), mmap
// each plugin, build runtime index map, build override filter, emit
// plugin-level facts, then parallel-extract per-record facts.
//
// provides() enumerates every relation in the Skyrim default schema
// whose ESP source spec is non-empty — i.e. every relation that ESP
// extraction can populate. Names are returned pool-agnostically (as
// std::string); ExtensionContext::load_required() interns them into
// the caller's StringPool at dispatch time.
//
// load() builds a fresh SchemaRegistry bound to ctx.pool on every
// invocation, so the registry's name ids match the caller's pool.
class SkyrimEspDataSource : public mora::ext::DataSource {
public:
    SkyrimEspDataSource();
    ~SkyrimEspDataSource() override;

    std::string_view             name()     const override;
    std::span<const std::string> provides() const override;
    void                         load(mora::ext::LoadCtx& ctx,
                                       mora::FactDB& out) override;

private:
    std::vector<std::string> provides_;   // populated in the ctor
};

} // namespace mora_skyrim_compile
