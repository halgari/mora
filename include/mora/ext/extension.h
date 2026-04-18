#pragma once

#include "mora/ext/data_source.h"

#include <memory>
#include <span>

namespace mora {
class FactDB;
}

namespace mora::ext {

// ExtensionContext is the handle an extension receives during
// registration. Extensions call register_* to contribute types,
// relations, data sources, sinks, predicates, and LSP providers.
// In Plan 2 only DataSource registration is implemented; additional
// registration methods land in later plans.
//
// Thread safety: registration (register_*) and load_required() are
// NOT thread-safe relative to each other. Callers must complete all
// registrations before invoking load_required, and must not invoke
// load_required concurrently. Individual DataSource::load()
// implementations MAY use internal parallelism (the Skyrim ESP
// source in a later plan will do this).
class ExtensionContext {
public:
    ExtensionContext();
    ~ExtensionContext();

    ExtensionContext(const ExtensionContext&) = delete;
    ExtensionContext& operator=(const ExtensionContext&) = delete;

    // Register a DataSource. Takes ownership.
    void register_data_source(std::unique_ptr<DataSource> src);

    // Read-only view of all registered data sources, in registration order.
    std::span<const std::unique_ptr<DataSource>> data_sources() const;

    // Convenience driver: invoke every registered DataSource whose
    // provides() intersects ctx.needed_relations, in registration order.
    // Each source writes into `out`. Returns the number of sources
    // actually invoked.
    std::size_t load_required(LoadCtx& ctx, FactDB& out) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mora::ext
