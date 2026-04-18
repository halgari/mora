#include "mora/ext/extension.h"

#include <fmt/core.h>
#include <unordered_map>

namespace mora::ext {

struct ExtensionContext::Impl {
    std::vector<std::unique_ptr<DataSource>> sources;
    std::vector<std::unique_ptr<Sink>>       sinks;
    std::vector<RelationSchema>              schemas;
    std::unordered_map<std::string, std::size_t> schema_by_name;
};

ExtensionContext::ExtensionContext()  : impl_(std::make_unique<Impl>()) {}
ExtensionContext::~ExtensionContext() = default;

const Type* ExtensionContext::register_nominal_type(std::string_view name,
                                                    const Type* physical) {
    return TypeRegistry::instance().register_nominal(name, physical);
}

void ExtensionContext::register_data_source(std::unique_ptr<DataSource> src) {
    impl_->sources.push_back(std::move(src));
}

std::span<const std::unique_ptr<DataSource>>
ExtensionContext::data_sources() const {
    return impl_->sources;
}

void ExtensionContext::register_sink(std::unique_ptr<Sink> sink) {
    impl_->sinks.push_back(std::move(sink));
}

std::span<const std::unique_ptr<Sink>>
ExtensionContext::sinks() const {
    return impl_->sinks;
}

void ExtensionContext::register_relation(RelationSchema schema) {
    auto name = schema.name;
    auto& byn = impl_->schema_by_name;
    auto it = byn.find(name);
    if (it != byn.end()) {
        impl_->schemas[it->second] = std::move(schema);
    } else {
        byn[name] = impl_->schemas.size();
        impl_->schemas.push_back(std::move(schema));
    }
}

std::span<const RelationSchema>
ExtensionContext::schemas() const {
    return impl_->schemas;
}

const RelationSchema*
ExtensionContext::find_schema(std::string_view name) const {
    auto it = impl_->schema_by_name.find(std::string(name));
    if (it == impl_->schema_by_name.end()) return nullptr;
    return &impl_->schemas[it->second];
}

std::size_t ExtensionContext::load_required(LoadCtx& ctx, FactDB& out) const {
    const auto errors_before = ctx.diags.error_count();

    // First pass: find the set of sources whose provides() intersects
    // needed_relations, and detect collisions where two or more sources
    // claim the same relation. Each DataSource returns relation NAMES
    // (pool-agnostic strings); we intern them into ctx.pool at dispatch
    // time so the resulting ids match the pool that sema used to build
    // needed_relations. Spec: collisions produce a diagnostic
    // (--prefer-source disambiguation lands later); load() isn't
    // invoked if any collision was reported.
    std::vector<DataSource*> matching;
    std::unordered_map<uint32_t, DataSource*> owner;
    for (auto& src : impl_->sources) {
        bool touches_needed = false;
        for (const auto& rel_name : src->provides()) {
            auto rel = ctx.pool.intern(rel_name).index;
            if (!ctx.needed_relations.contains(rel)) continue;
            touches_needed = true;
            auto [it, inserted] = owner.try_emplace(rel, src.get());
            if (!inserted && it->second != src.get()) {
                ctx.diags.error("data-source-conflict",
                    fmt::format("relation '{}' is provided by both "
                                "data sources '{}' and '{}'; disambiguate "
                                "with --prefer-source",
                                rel_name, it->second->name(), src->name()),
                    mora::SourceSpan{}, "");
            }
        }
        if (touches_needed) matching.push_back(src.get());
    }

    if (ctx.diags.error_count() > errors_before) return 0;

    for (auto* src : matching) {
        src->load(ctx, out);
    }
    return matching.size();
}

} // namespace mora::ext
