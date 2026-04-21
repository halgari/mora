//! Tracing subscriber setup for user-facing output.

use tracing_subscriber::EnvFilter;
use tracing_subscriber::fmt::format::FmtSpan;

/// Initialize tracing. `verbose=true` enables `debug` level; else `info`.
/// Respects `RUST_LOG` when set (overrides our default filter).
pub fn init(verbose: bool) {
    let default_filter = if verbose { "debug" } else { "info" };
    let env_filter = EnvFilter::try_from_default_env()
        .unwrap_or_else(|_| EnvFilter::new(default_filter));

    tracing_subscriber::fmt()
        .with_env_filter(env_filter)
        .with_target(false)
        .with_span_events(FmtSpan::NONE)
        .without_time()
        .with_writer(std::io::stderr)
        .init();
}
