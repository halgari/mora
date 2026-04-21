//! Command-line argument definitions via clap derive.

use std::path::PathBuf;

use clap::{Parser, Subcommand};

#[derive(Parser, Debug)]
#[command(name = "mora", version, about = "Mora — KID-compatible keyword distributor compiler")]
pub struct Cli {
    #[command(subcommand)]
    pub command: Commands,
}

#[derive(Subcommand, Debug)]
pub enum Commands {
    /// Compile KID INIs against the active plugins into mora_patches.bin.
    Compile(CompileArgs),
}

#[derive(Debug, clap::Args)]
pub struct CompileArgs {
    /// Skyrim's Data/ directory. Contains *.esm, *.esp, and *_KID.ini files.
    #[arg(long, value_name = "PATH")]
    pub data_dir: PathBuf,

    /// Path to plugins.txt defining active load order.
    #[arg(long, value_name = "PATH")]
    pub plugins_txt: PathBuf,

    /// Output path for mora_patches.bin. Default: <data-dir>/SKSE/Plugins/mora_patches.bin.
    #[arg(long, value_name = "PATH")]
    pub output: Option<PathBuf>,

    /// Run the full pipeline but don't write the output file.
    #[arg(long)]
    pub dry_run: bool,

    /// Enable debug-level logging.
    #[arg(long, short)]
    pub verbose: bool,
}

impl CompileArgs {
    /// Resolve the output path, defaulting to <data_dir>/SKSE/Plugins/mora_patches.bin.
    pub fn resolve_output(&self) -> PathBuf {
        self.output
            .clone()
            .unwrap_or_else(|| self.data_dir.join("SKSE").join("Plugins").join("mora_patches.bin"))
    }
}
