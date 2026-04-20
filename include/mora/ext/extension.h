#pragma once

#include "mora/ast/ast.h"
#include "mora/core/source_location.h"
#include "mora/core/string_pool.h"
#include "mora/core/type.h"
#include "mora/diag/diagnostic.h"
#include "mora/ext/data_source.h"
#include "mora/ext/relation_schema.h"
#include "mora/ext/sink.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <unordered_map>

namespace mora {
class FactDB;
}

namespace mora::ext {

// Reader-tag expansion context. Extensions register a ReaderFn under a
// tag name via ExtensionContext::register_reader; the expansion pass
// (src/sema/reader_expansion.cpp) invokes it for each
// `#<tag> "<payload>"` literal it encounters, splicing the returned
// Expr in place. Readers have access to loaded plugin/EditorID data.
struct ReaderContext {
    mora::StringPool&                                            pool;
    mora::DiagBag&                                               diags;
    const std::unordered_map<std::string, uint32_t>*             editor_ids;            // may be null
    const std::unordered_map<std::string, uint32_t>*             plugin_runtime_index;  // may be null
};

using ReaderFn = std::function<mora::Expr(
    ReaderContext&          ctx,
    std::string_view        payload,
    const mora::SourceSpan& span)>;

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

    // Register a Sink. Takes ownership.
    void register_sink(std::unique_ptr<Sink> sink);

    // Register a nominal type tag (e.g. "FormID") layered over a physical
    // type. Returns a stable singleton pointer. Idempotent: registering
    // the same name twice returns the same pointer.
    const Type* register_nominal_type(std::string_view name,
                                       const Type* physical,
                                       Value::Kind kind_hint);

    // Back-compat overload: defaults the kind hint to physical->kind_hint().
    const Type* register_nominal_type(std::string_view name,
                                       const Type* physical) {
        return register_nominal_type(name, physical, physical->kind_hint());
    }

    // Register a RelationSchema. Takes ownership of the copy — caller
    // may let the argument go out of scope after the call. Names must
    // be unique; registering a duplicate name overwrites the prior
    // entry (consistent with how other registry hash-maps in this
    // codebase behave) and is considered a configuration error for
    // callers to avoid.
    void register_relation(RelationSchema schema);

    // Read-only view of all registered relation schemas in registration
    // order. Safe to call after all extensions have registered.
    std::span<const RelationSchema> schemas() const;

    // Look up a schema by name. Returns nullptr if no relation by that
    // name is registered.
    const RelationSchema* find_schema(std::string_view name) const;

    // Read-only view of all registered data sources, in registration order.
    std::span<const std::unique_ptr<DataSource>> data_sources() const;

    // Read-only view of all registered sinks, in registration order.
    std::span<const std::unique_ptr<Sink>> sinks() const;

    // Convenience driver: invoke every registered DataSource whose
    // provides() intersects ctx.needed_relations, in registration order.
    // Each source writes into `out`. Returns the number of sources
    // actually invoked.
    std::size_t load_required(LoadCtx& ctx, FactDB& out) const;

    // Register a reader-tag handler. `tag` matches the `#<tag>` spelling
    // (without the '#'). Readers run during the reader-expansion pass
    // between ESP-load and name-resolution. Duplicate tags overwrite.
    void register_reader(std::string_view tag, ReaderFn fn);

    // Look up a registered reader. Returns nullptr if no tag by that
    // name is registered.
    const ReaderFn* find_reader(std::string_view tag) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mora::ext
