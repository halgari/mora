#include "mora/ext/extension.h"

#include <fmt/core.h>
#include <unordered_map>

namespace mora::ext {

struct ExtensionContext::Impl {
    std::vector<std::unique_ptr<DataSource>> sources;
};

ExtensionContext::ExtensionContext()  : impl_(std::make_unique<Impl>()) {}
ExtensionContext::~ExtensionContext() = default;

void ExtensionContext::register_data_source(std::unique_ptr<DataSource> src) {
    impl_->sources.push_back(std::move(src));
}

std::span<const std::unique_ptr<DataSource>>
ExtensionContext::data_sources() const {
    return impl_->sources;
}

std::size_t ExtensionContext::load_required(LoadCtx& ctx, FactDB& out) const {
    // First pass: find the set of sources whose provides() intersects
    // needed_relations, and detect collisions where two or more sources
    // claim the same relation. Spec: collisions produce a diagnostic
    // (--prefer-source disambiguation lands later); load() isn't
    // invoked if any collision was reported.
    std::vector<DataSource*> matching;
    std::unordered_map<uint32_t, DataSource*> owner;
    for (auto& src : impl_->sources) {
        bool touches_needed = false;
        for (uint32_t rel : src->provides()) {
            if (!ctx.needed_relations.contains(rel)) continue;
            touches_needed = true;
            auto [it, inserted] = owner.try_emplace(rel, src.get());
            if (!inserted && it->second != src.get()) {
                ctx.diags.error("data-source-conflict",
                    fmt::format("relation (id {}) is provided by both "
                                "data sources '{}' and '{}'; disambiguate "
                                "with --prefer-source",
                                rel, it->second->name(), src->name()),
                    mora::SourceSpan{}, "");
            }
        }
        if (touches_needed) matching.push_back(src.get());
    }

    if (ctx.diags.has_errors()) return 0;

    for (auto* src : matching) {
        src->load(ctx, out);
    }
    return matching.size();
}

} // namespace mora::ext
