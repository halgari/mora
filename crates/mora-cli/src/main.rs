//! `mora` CLI entry point.

mod cli;
mod compile;
mod logging;

use clap::Parser;

use crate::cli::{Cli, Commands};

fn main() {
    let args = Cli::parse();
    let verbose = match &args.command {
        Commands::Compile(c) => c.verbose,
    };
    logging::init(verbose);

    let result = match args.command {
        Commands::Compile(c) => compile::run(c),
    };

    match result {
        Ok(()) => std::process::exit(0),
        Err(e) => {
            // Print error chain to stderr.
            eprintln!("error: {e}");
            let mut source = e.source();
            while let Some(s) = source {
                eprintln!("  caused by: {s}");
                source = s.source();
            }
            std::process::exit(1);
        }
    }
}
