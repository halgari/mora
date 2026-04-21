//! `cargo xtask <cmd>` — dev-workflow orchestration.

use anyhow::Result;
use clap::{Parser, Subcommand};

mod capture_kid_goldens;

#[derive(Parser)]
#[command(name = "xtask", about = "Mora dev-workflow orchestration")]
struct Cli {
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Capture KID ground-truth dumps for one or more scenarios by
    /// running the real KID SKSE plugin against the scenario's INI in
    /// a real Skyrim under Proton, then extracting the harness's JSONL
    /// output into `tests/golden-data/expected/<scenario>/`.
    CaptureKidGoldens(capture_kid_goldens::Args),
}

fn main() -> Result<()> {
    let cli = Cli::parse();
    match cli.command {
        Command::CaptureKidGoldens(args) => capture_kid_goldens::run(args),
    }
}
