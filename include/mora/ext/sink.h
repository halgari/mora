#pragma once

#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

#include <string>
#include <string_view>

namespace mora {
class FactDB;
}

namespace mora::ext {

class ExtensionContext;  // fwd decl — full definition in extension.h

// Runtime context handed to Sink::emit. Caller configures `config` from
// a CLI flag like `--sink parquet.snapshot=./out` — the sink receives
// the right-hand side ("./out") as `config`. Sinks parse the string as
// they see fit.
//
// `extension` is an optional pointer back to the ExtensionContext the
// sink was registered into. Sinks that need to query registered
// RelationSchemas (e.g. to filter by is_output) use this. Callers MUST
// populate it for full functionality; for simple single-sink unit tests
// that don't need schema introspection, leaving it null is fine.
struct EmitCtx {
    StringPool& pool;
    DiagBag&    diags;

    // Per-invocation config string (from `--sink <name>=<config>`).
    std::string config;

    // Optional access to the host ExtensionContext. Populated by the
    // CLI driver before invoking each sink. Lifetime: the pointed-to
    // ExtensionContext must outlive the emit() invocation; sinks must
    // not retain this pointer past the call.
    const ExtensionContext* extension = nullptr;
};

// A Sink consumes a FactDB after evaluation and writes its content
// somewhere. Sinks are registered with the ExtensionContext at
// extension registration time and invoked by the CLI (or other
// drivers) after evaluation.
class Sink {
public:
    virtual ~Sink() = default;

    // Stable identifier (e.g. "parquet.snapshot"). Matches the name
    // used on the CLI's --sink flag.
    virtual std::string_view name() const = 0;

    // Do the work: write `db` out. Errors report through ctx.diags;
    // returning doesn't imply success — check diags.
    virtual void emit(EmitCtx& ctx, const FactDB& db) = 0;
};

} // namespace mora::ext
