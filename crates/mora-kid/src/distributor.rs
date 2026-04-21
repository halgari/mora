//! KidDistributor — impl of `mora_core::Distributor<EspWorld>`.
//!
//! Scans Weapon + Armor records in the world, evaluates each rule's
//! filter pipeline against the record, runs the deterministic chance
//! roll, emits `Patch::AddKeyword` patches to the sink.

use std::collections::{HashMap, HashSet};

use mora_core::{DeterministicChance, Distributor, DistributorStats, FormId, Patch, PatchSink};
use mora_esp::EspWorld;
use tracing::{debug, warn};

use crate::filter;
use crate::rule::{ExclusiveGroup, KidRule, RecordType, Traits};

#[derive(Debug, thiserror::Error)]
pub enum KidError {
    #[error("keyword reference did not resolve: {0:?}")]
    UnresolvedKeyword(crate::reference::Reference),
}

/// KidDistributor — consumes a list of parsed rules + optional
/// exclusive groups + an EspWorld, emits patches.
pub struct KidDistributor {
    pub rules: Vec<KidRule>,
    pub exclusive_groups: Vec<ExclusiveGroup>,
}

impl KidDistributor {
    pub fn new(rules: Vec<KidRule>) -> Self {
        KidDistributor {
            rules,
            exclusive_groups: Vec::new(),
        }
    }

    pub fn with_exclusive_groups(mut self, groups: Vec<ExclusiveGroup>) -> Self {
        self.exclusive_groups = groups;
        self
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
        let mut stats = DistributorStats {
            rules_evaluated: self.rules.len() as u64,
            ..Default::default()
        };

        // Pre-build keyword FormId -> editor-id map for chance seeding and
        // ANY-filter substring checks.
        let mut kw_edid_map: HashMap<FormId, String> = HashMap::new();
        for entry in world.keywords() {
            if let Ok((fid, kw)) = entry
                && let Some(edid) = kw.editor_id
            {
                kw_edid_map.insert(fid, edid);
            }
        }

        // Resolve exclusive groups: each group's members -> FormIds.
        // Build kw_fid -> Vec<group_idx> lookup.
        let mut kw_to_groups: HashMap<FormId, Vec<usize>> = HashMap::new();
        for (idx, group) in self.exclusive_groups.iter().enumerate() {
            for member in &group.members {
                if let Some(fid) = member.resolve_form(world) {
                    kw_to_groups.entry(fid).or_default().push(idx);
                } else {
                    debug!(
                        "{}:{}: exclusive-group '{}' member {:?} did not resolve — skipped",
                        group.source.file, group.source.line_number, group.name, member
                    );
                }
            }
        }

        // Per-form applied-group tracker. Keyed by target form; value is
        // the set of group indices that already have a keyword applied.
        let mut applied_groups: HashMap<FormId, HashSet<usize>> = HashMap::new();

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
            let edid = match &rule.keyword {
                crate::reference::Reference::EditorId(s) => s.clone(),
                _ => match kw_edid_map.get(&fid) {
                    Some(s) => s.clone(),
                    None => {
                        warn!(
                            "{}:{}: keyword {:?} resolved to {fid} but editor-id not found",
                            rule.source.file, rule.source.line_number, rule.keyword
                        );
                        continue;
                    }
                },
            };
            resolved.push(ResolvedRule {
                rule,
                keyword_form_id: fid,
                keyword_editor_id: edid,
            });
        }

        // Iterate weapons via world.records(WEAP) so we get the exact
        // source plugin_index.
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
                if !filter::evaluate_with_any(
                    &rr.rule.filters,
                    world,
                    plugin_index,
                    &weapon.keywords,
                    weapon.editor_id.as_deref(),
                    &kw_edid_map,
                ) {
                    stats.rejected_by_filter += 1;
                    continue;
                }
                if let Traits::Weapon(wt) = &rr.rule.traits
                    && !wt.anim_types.is_empty()
                {
                    debug!(
                        "{}:{}: weapon trait predicates not yet evaluated (WeaponRecord lacks animType) — treating as pass",
                        rr.rule.source.file, rr.rule.source.line_number
                    );
                }
                // ExclusiveGroup check.
                if let Some(groups) = kw_to_groups.get(&rr.keyword_form_id)
                    && let Some(applied_set) = applied_groups.get(&form_id)
                    && groups.iter().any(|g| applied_set.contains(g))
                {
                    stats.rejected_by_exclusive_group += 1;
                    continue;
                }
                // Chance roll.
                if !chance.passes(&rr.keyword_editor_id, form_id, rr.rule.chance) {
                    stats.rejected_by_chance += 1;
                    continue;
                }
                // Emit + record applied-group.
                sink.push(Patch::AddKeyword {
                    target: form_id,
                    keyword: rr.keyword_form_id,
                });
                stats.patches_emitted += 1;
                if let Some(groups) = kw_to_groups.get(&rr.keyword_form_id) {
                    let set = applied_groups.entry(form_id).or_default();
                    for g in groups {
                        set.insert(*g);
                    }
                }
            }
        }

        // Iterate armors (mirror of the weapon loop).
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
                if !filter::evaluate_with_any(
                    &rr.rule.filters,
                    world,
                    plugin_index,
                    &armor.keywords,
                    armor.editor_id.as_deref(),
                    &kw_edid_map,
                ) {
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
                if let Some(groups) = kw_to_groups.get(&rr.keyword_form_id)
                    && let Some(applied_set) = applied_groups.get(&form_id)
                    && groups.iter().any(|g| applied_set.contains(g))
                {
                    stats.rejected_by_exclusive_group += 1;
                    continue;
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
                if let Some(groups) = kw_to_groups.get(&rr.keyword_form_id) {
                    let set = applied_groups.entry(form_id).or_default();
                    for g in groups {
                        set.insert(*g);
                    }
                }
            }
        }

        // Warn for Other record types.
        for rr in &resolved {
            if let RecordType::Other(t) = &rr.rule.record_type {
                warn!(
                    "{}:{}: record type {:?} not supported at M3 (Weapon+Armor only)",
                    rr.rule.source.file, rr.rule.source.line_number, t
                );
            }
        }

        Ok(stats)
    }
}
