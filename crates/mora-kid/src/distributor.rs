//! KidDistributor — impl of `mora_core::Distributor<EspWorld>`.
//!
//! Scans Weapon + Armor records in the world, evaluates each rule's
//! filter pipeline against the record, runs the deterministic chance
//! roll, emits `Patch::AddKeyword` patches to the sink.

use mora_core::{DeterministicChance, Distributor, DistributorStats, FormId, Patch, PatchSink};
use mora_esp::EspWorld;
use tracing::{debug, warn};

use crate::filter;
use crate::rule::{KidRule, RecordType, Traits};

#[derive(Debug, thiserror::Error)]
pub enum KidError {
    #[error("keyword reference did not resolve: {0:?}")]
    UnresolvedKeyword(crate::reference::Reference),
}

/// KidDistributor — consumes a list of parsed rules + an EspWorld,
/// emits patches.
pub struct KidDistributor {
    pub rules: Vec<KidRule>,
}

impl KidDistributor {
    pub fn new(rules: Vec<KidRule>) -> Self {
        KidDistributor { rules }
    }
}

impl Distributor<EspWorld> for KidDistributor {
    type Error = KidError;

    fn name(&self) -> &'static str {
        "kid"
    }

    fn lower(
        &self,
        world: &EspWorld,
        chance: &DeterministicChance,
        sink: &mut PatchSink,
    ) -> Result<DistributorStats, Self::Error> {
        let mut stats = DistributorStats::default();
        stats.rules_evaluated = self.rules.len() as u64;

        // Pre-resolve each rule's keyword FormId + editor-ID string.
        struct ResolvedRule<'a> {
            rule: &'a KidRule,
            keyword_form_id: FormId,
            keyword_editor_id: String,
        }
        let mut resolved: Vec<ResolvedRule<'_>> = Vec::new();
        for rule in &self.rules {
            let Some(fid) = rule.keyword.resolve_form(world) else {
                warn!(
                    "{}:{}: keyword {:?} did not resolve — rule skipped",
                    rule.source.file, rule.source.line_number, rule.keyword
                );
                continue;
            };
            // Resolve the editor-id for chance seeding. If the rule key
            // was already an EditorId, use that string directly; else
            // look it up via the world.
            let edid = match &rule.keyword {
                crate::reference::Reference::EditorId(s) => s.clone(),
                _ => {
                    // Find the editor-id of this keyword FormId.
                    let mut out = None;
                    for entry in world.keywords() {
                        if let Ok((entry_fid, kw)) = entry
                            && entry_fid == fid
                            && let Some(edid) = kw.editor_id.clone()
                        {
                            out = Some(edid);
                            break;
                        }
                    }
                    match out {
                        Some(s) => s,
                        None => {
                            warn!(
                                "{}:{}: keyword {:?} resolved to {fid} but editor-id not found",
                                rule.source.file, rule.source.line_number, rule.keyword
                            );
                            continue;
                        }
                    }
                }
            };
            if rule.filters.has_unsupported() {
                debug!(
                    "{}:{}: rule has unsupported ALL/ANY filters; evaluator treats as pass",
                    rule.source.file, rule.source.line_number
                );
            }
            resolved.push(ResolvedRule {
                rule,
                keyword_form_id: fid,
                keyword_editor_id: edid,
            });
        }

        // Iterate weapons via world.records(WEAP) so we get the exact
        // source plugin_index — world.weapons() drops it, and
        // reconstructing it from the resolved FormId's high byte is
        // unreliable for ESL plugins (which all share 0xFE).
        for wr in world.records(mora_esp::signature::WEAP) {
            let form_id = wr.resolved_form_id;
            let plugin_index = wr.plugin_index;
            let weapon = match mora_esp::records::weapon::parse(&wr.record, plugin_index, world) {
                Ok(w) => w,
                Err(_) => continue,
            };
            stats.candidates_considered += 1;

            for rr in &resolved {
                if !matches!(rr.rule.record_type, RecordType::Weapon) {
                    continue;
                }
                if !filter::evaluate(&rr.rule.filters, world, plugin_index, &weapon.keywords) {
                    stats.rejected_by_filter += 1;
                    continue;
                }
                // M3: trait evaluation log-and-skip (Weapon trait predicates
                // require DNAM/OBND subrecord data not yet on WeaponRecord).
                if let Traits::Weapon(wt) = &rr.rule.traits
                    && !wt.anim_types.is_empty()
                {
                    debug!(
                        "{}:{}: weapon trait predicates not yet evaluated (WeaponRecord lacks animType) — treating as pass",
                        rr.rule.source.file, rr.rule.source.line_number
                    );
                }
                // Chance roll.
                if !chance.passes(&rr.keyword_editor_id, form_id, rr.rule.chance) {
                    stats.rejected_by_chance += 1;
                    continue;
                }
                sink.push(Patch::AddKeyword {
                    target: form_id,
                    keyword: rr.keyword_form_id,
                });
                stats.patches_emitted += 1;
            }
        }

        // Iterate armors (same structure).
        for wr in world.records(mora_esp::signature::ARMO) {
            let form_id = wr.resolved_form_id;
            let plugin_index = wr.plugin_index;
            let armor = match mora_esp::records::armor::parse(&wr.record, plugin_index, world) {
                Ok(a) => a,
                Err(_) => continue,
            };
            stats.candidates_considered += 1;
            for rr in &resolved {
                if !matches!(rr.rule.record_type, RecordType::Armor) {
                    continue;
                }
                if !filter::evaluate(&rr.rule.filters, world, plugin_index, &armor.keywords) {
                    stats.rejected_by_filter += 1;
                    continue;
                }
                if let Traits::Armor(at) = &rr.rule.traits
                    && !at.armor_types.is_empty()
                {
                    debug!(
                        "{}:{}: armor trait predicates not yet evaluated (ArmorRecord lacks armorType) — treating as pass",
                        rr.rule.source.file, rr.rule.source.line_number
                    );
                }
                if !chance.passes(&rr.keyword_editor_id, form_id, rr.rule.chance) {
                    stats.rejected_by_chance += 1;
                    continue;
                }
                sink.push(Patch::AddKeyword {
                    target: form_id,
                    keyword: rr.keyword_form_id,
                });
                stats.patches_emitted += 1;
            }
        }

        // Warn for Other record types.
        for rr in &resolved {
            if let RecordType::Other(ref t) = rr.rule.record_type {
                warn!(
                    "{}:{}: record type {:?} not supported at M3 (Weapon+Armor only)",
                    rr.rule.source.file, rr.rule.source.line_number, t
                );
            }
        }

        Ok(stats)
    }
}
