//! `mora compile` subcommand — end-to-end patch compilation.

use std::time::Instant;

use anyhow::{Context, Result};
use mora_core::{DeterministicChance, Distributor, PatchFile, PatchSink};
use mora_esp::EspWorld;
use mora_esp::load_order_hash::compute_load_order_hash;
use mora_kid::distributor::KidDistributor;
use mora_kid::ini;
use tracing::{info, warn};

use crate::cli::CompileArgs;

pub fn run(args: CompileArgs) -> Result<()> {
    let started = Instant::now();

    // Validate paths exist.
    anyhow::ensure!(
        args.data_dir.is_dir(),
        "--data-dir does not exist or is not a directory: {}",
        args.data_dir.display()
    );
    anyhow::ensure!(
        args.plugins_txt.is_file(),
        "--plugins-txt does not exist: {}",
        args.plugins_txt.display()
    );

    info!("Mora v{}", env!("CARGO_PKG_VERSION"));

    // Open the EspWorld.
    let world =
        EspWorld::open(&args.data_dir, &args.plugins_txt).context("failed to open EspWorld")?;
    info!("Loaded plugins.txt: {} active plugins", world.plugins.len());

    // Compute load-order hash.
    let load_hash = compute_load_order_hash(&world.plugins);
    info!("Load-order hash: 0x{load_hash:016x}");

    // Discover + parse KID INIs.
    let ini_paths =
        ini::discover_kid_ini_files(&args.data_dir).context("failed to scan for _KID.ini files")?;
    info!("Discovered {} _KID.ini files", ini_paths.len());

    let mut all_rules = Vec::new();
    let mut all_groups = Vec::new();
    for p in &ini_paths {
        match ini::parse_file(p) {
            Ok(parsed) => {
                all_rules.extend(parsed.rules);
                all_groups.extend(parsed.exclusive_groups);
            }
            Err(e) => {
                warn!("{}: {}", p.display(), e);
            }
        }
    }
    info!(
        "Parsed {} rules, {} exclusive groups",
        all_rules.len(),
        all_groups.len()
    );

    // Run distributor.
    let distributor = KidDistributor::new(all_rules).with_exclusive_groups(all_groups);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    sink.set_load_order_hash(load_hash);
    let stats = distributor
        .lower(&world, &chance, &mut sink)
        .context("distributor failed")?;

    info!("Rules evaluated:        {}", stats.rules_evaluated);
    info!("Candidates considered:  {}", stats.candidates_considered);
    info!("Patches emitted:        {}", stats.patches_emitted);
    info!("Rejected by filter:     {}", stats.rejected_by_filter);
    info!("Rejected by chance:     {}", stats.rejected_by_chance);
    info!(
        "Rejected by excl-group: {}",
        stats.rejected_by_exclusive_group
    );

    let file: PatchFile = sink.finalize();

    // Write (or skip on --dry-run).
    if args.dry_run {
        info!(
            "Dry run: would have written {} bytes to {}",
            file.to_bytes().unwrap_or_default().len(),
            args.resolve_output().display()
        );
    } else {
        let output_path = args.resolve_output();
        if let Some(parent) = output_path.parent() {
            std::fs::create_dir_all(parent)
                .with_context(|| format!("failed to create output dir: {}", parent.display()))?;
        }
        let bytes = file.to_bytes().context("postcard serialize failed")?;
        std::fs::write(&output_path, &bytes)
            .with_context(|| format!("failed to write {}", output_path.display()))?;
        info!(
            "Wrote mora_patches.bin: {} bytes → {}",
            bytes.len(),
            output_path.display()
        );
    }

    info!("Total: {}ms", started.elapsed().as_millis());
    Ok(())
}
