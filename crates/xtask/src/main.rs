//! `cargo xtask <cmd>` — dev-workflow orchestration.
//!
//! Commands are added as milestones require them:
//!   - M4: `capture-kid-goldens`
//!   - M5: `stage-runner-image`
//!   - later: `build-windows-dlls` and similar
//!
//! For now this is a stub that prints available commands (none yet).

use anyhow::Result;

fn main() -> Result<()> {
    eprintln!("xtask: no commands implemented yet (M0 stub)");
    std::process::exit(0);
}
