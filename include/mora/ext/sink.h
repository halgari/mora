#pragma once

#include "mora/core/string_pool.h"
#include "mora/diag/diagnostic.h"

#include <string>
#include <string_view>

namespace mora {
class FactDB;
}

namespace mora::ext {

// Runtime context handed to Sink::emit. Caller configures `config` from
// a CLI flag like `--sink parquet.snapshot=./out` — the sink receives
// the right-hand side ("./out") as `config`. Sinks parse the string as
// they see fit.
struct EmitCtx {
    StringPool& pool;
    DiagBag&    diags;

    // Per-invocation config string (from `--sink <name>=<config>`).
    std::string config;
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
