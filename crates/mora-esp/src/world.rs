//! `EspWorld` — indexed view across all active plugins.
//!
//! Iterates records by signature (e.g. all WEAP records from every
//! plugin in the load order). Handles top-level group scanning and
//! master-index FormID remapping.

use std::path::Path;

use mora_core::FormId;

use crate::group::{GROUP_HEADER_SIZE, read_group};
use crate::load_order::{LoadOrder, build as build_load_order, remap_form_id};
use crate::plugin::{EspPlugin, EspPluginError};
use crate::plugins_txt;
use crate::reader::ReadError;
use crate::record::{Record, read_record};
use crate::records::{armor, weapon};
use crate::signature::{ARMO, Signature, WEAP};

#[derive(Debug, thiserror::Error)]
pub enum WorldError {
    #[error("plugin: {0}")]
    Plugin(#[from] EspPluginError),
    #[error("read: {0}")]
    Read(#[from] ReadError),
    #[error("io: {0}")]
    Io(#[from] std::io::Error),
}

/// A record yielded by `EspWorld::records`. Includes the owning
/// plugin index for downstream consumers that want to know provenance.
pub struct WorldRecord<'a> {
    pub plugin_index: usize,
    pub resolved_form_id: FormId,
    pub record: Record<'a>,
}

pub struct EspWorld {
    pub plugins: Vec<EspPlugin>,
    pub load_order: LoadOrder,
}

impl EspWorld {
    /// Open all plugins referenced by an already-parsed plugins.txt.
    pub fn open<P: AsRef<Path>>(data_dir: P, plugins_txt_path: &Path) -> Result<Self, WorldError> {
        let data_dir = data_dir.as_ref();
        let content = std::fs::read_to_string(plugins_txt_path)?;
        let entries = plugins_txt::parse(&content);
        let active_names: Vec<&str> = entries
            .iter()
            .filter(|e| e.active)
            .map(|e| e.name.as_str())
            .collect();

        // Check which implicit plugins exist on disk.
        let implicit_present: Vec<&str> = crate::load_order::IMPLICIT_PLUGINS
            .iter()
            .copied()
            .filter(|n| data_dir.join(n).exists())
            .collect();

        // Open each plugin so we can check the ESL flag.
        let mut plugins = Vec::new();
        for name in implicit_present.iter().chain(active_names.iter()) {
            let path = data_dir.join(name);
            if !path.exists() {
                continue; // plugins.txt may list stale entries
            }
            let plugin = EspPlugin::open(&path)?;
            plugins.push(plugin);
        }

        let is_esl = |name: &str| -> bool {
            plugins
                .iter()
                .find(|p| p.filename.eq_ignore_ascii_case(name))
                .map(|p| p.is_esl())
                .unwrap_or(false)
        };
        let user_refs: Vec<&str> = active_names.clone();
        let load_order = build_load_order(&implicit_present, &user_refs, &is_esl);

        Ok(EspWorld {
            plugins,
            load_order,
        })
    }

    /// Resolve a plugin-local FormID (high byte = index into the
    /// plugin's master list, or `== len(masters)` for self-reference)
    /// to a fully-qualified runtime `FormId`. Returns `None` if the
    /// referenced plugin is not in the active load order.
    ///
    /// `plugin_index` is the index into `self.plugins` of the source
    /// plugin that contains the local FormID.
    pub fn resolve_in_plugin(&self, plugin_index: usize, raw_local: u32) -> Option<FormId> {
        let plugin = self.plugins.get(plugin_index)?;
        remap_form_id(
            raw_local,
            &plugin.header.masters,
            &plugin.filename,
            &self.load_order,
        )
        .map(FormId)
    }

    /// Iterate records of the given signature across all active
    /// plugins, in load order. Record FormIDs are resolved against
    /// the active load order; keyword / reference FormIDs inside
    /// the record bodies are NOT — the typed record accessors
    /// (`WorldRecord -> WeaponRecord` via `weapons()`, etc.) handle
    /// that.
    pub fn records(&self, sig: Signature) -> impl Iterator<Item = WorldRecord<'_>> + '_ {
        self.plugins.iter().enumerate().flat_map(move |(idx, p)| {
            scan_top_level_group(p, sig).map(move |r| {
                let resolved =
                    remap_form_id(r.form_id, &p.header.masters, &p.filename, &self.load_order)
                        .unwrap_or(r.form_id);
                WorldRecord {
                    plugin_index: idx,
                    resolved_form_id: FormId(resolved),
                    record: r,
                }
            })
        })
    }

    /// Iterate all weapons, parsed and resolved. Each item is
    /// `(record_form_id, parsed_weapon)`. Keywords inside
    /// `WeaponRecord::keywords` are already resolved `FormId`s.
    pub fn weapons(
        &self,
    ) -> impl Iterator<Item = Result<(FormId, weapon::WeaponRecord), weapon::WeaponError>> + '_
    {
        self.records(WEAP).map(move |wr| {
            let parsed = weapon::parse(&wr.record, wr.plugin_index, self)?;
            Ok((wr.resolved_form_id, parsed))
        })
    }

    /// Iterate all armors, parsed and resolved.
    pub fn armors(
        &self,
    ) -> impl Iterator<Item = Result<(FormId, armor::ArmorRecord), armor::ArmorError>> + '_ {
        self.records(ARMO).map(move |wr| {
            let parsed = armor::parse(&wr.record, wr.plugin_index, self)?;
            Ok((wr.resolved_form_id, parsed))
        })
    }
}

fn scan_top_level_group<'a>(
    plugin: &'a EspPlugin,
    target: Signature,
) -> Box<dyn Iterator<Item = Record<'a>> + 'a> {
    let body = plugin.body();
    // Walk top-level groups looking for the target.
    let mut offset = 0usize;
    let mut found_contents: Option<&'a [u8]> = None;
    while offset < body.len() {
        let (group, next) = match read_group(body, offset) {
            Ok(x) => x,
            Err(_) => break,
        };
        if group.is_top_level() && group.label_signature() == target {
            found_contents = Some(group.contents);
            break;
        }
        offset = next;
    }

    let Some(contents) = found_contents else {
        return Box::new(std::iter::empty());
    };

    Box::new(GroupRecordIter {
        bytes: contents,
        offset: 0,
    })
}

struct GroupRecordIter<'a> {
    bytes: &'a [u8],
    offset: usize,
}

impl<'a> Iterator for GroupRecordIter<'a> {
    type Item = Record<'a>;
    fn next(&mut self) -> Option<Record<'a>> {
        while self.offset + 4 <= self.bytes.len() {
            // A nested group might appear inside a top-level group
            // (e.g. CELL contains children). Skip past it.
            if &self.bytes[self.offset..self.offset + 4] == b"GRUP" {
                if self.offset + 8 > self.bytes.len() {
                    return None;
                }
                let size = u32::from_le_bytes(
                    self.bytes[self.offset + 4..self.offset + 8]
                        .try_into()
                        .unwrap(),
                ) as usize;
                self.offset += size.max(GROUP_HEADER_SIZE);
                continue;
            }
            match read_record(self.bytes, self.offset) {
                Ok((rec, next)) => {
                    self.offset = next;
                    return Some(rec);
                }
                Err(_) => return None,
            }
        }
        None
    }
}

#[cfg(test)]
mod tests {
    // Full EspWorld integration tests live in tests/esp_format.rs
    // (Task 21); here we just confirm the struct compiles.
    #[test]
    fn compiles() {}
}
