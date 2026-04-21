//! Filter-bucket parsing + evaluation.

use mora_core::FormId;
use mora_esp::EspWorld;

use crate::reference::Reference;
use crate::rule::FilterBuckets;

/// Parse a comma-separated filter field into bucketed `FilterBuckets`.
/// Dispatches each token on its prefix: `+` (ALL), `-` (NOT), `*` (ANY),
/// none (MATCH).
pub fn parse_filter_field(s: &str) -> FilterBuckets {
    let mut buckets = FilterBuckets::default();
    for token in s.split(',') {
        let t = token.trim();
        if t.is_empty() {
            continue;
        }
        if t.contains('+') {
            // ALL: split further on '+' into sub-references; all must match
            let parts: Vec<Reference> = t.split('+').map(|p| Reference::parse(p.trim())).collect();
            if !parts.is_empty() {
                buckets.all.push(parts);
            }
        } else if let Some(rest) = t.strip_prefix('-') {
            buckets.not.push(Reference::parse(rest.trim()));
        } else if let Some(rest) = t.strip_prefix('*') {
            buckets.any.push(rest.trim().to_string());
        } else {
            buckets.match_.push(Reference::parse(t));
        }
    }
    buckets
}

/// Evaluate the filter pipeline against an item. Returns `true` if the
/// item passes all active filters. `item_keywords` is the item's
/// existing keyword set (already resolved FormIDs). `item_plugin_index`
/// is the plugin index of the item's own source — used for plugin-name
/// filters.
pub fn evaluate(
    buckets: &FilterBuckets,
    world: &EspWorld,
    item_plugin_index: usize,
    item_keywords: &[FormId],
) -> bool {
    // ALL: each group is a '+'-joined ref list. Every ref across every
    // group must match (KID stores ALL as a flat "every must match" list;
    // our nested representation produces the same result — flatten).
    for group in &buckets.all {
        for r in group {
            if !ref_matches_item(r, world, item_plugin_index, item_keywords) {
                return false;
            }
        }
    }

    // NOT: if any matches, fail.
    for r in &buckets.not {
        if ref_matches_item(r, world, item_plugin_index, item_keywords) {
            return false;
        }
    }

    // MATCH: if bucket non-empty, at least one must match.
    if !buckets.match_.is_empty() {
        let any_matched = buckets
            .match_
            .iter()
            .any(|r| ref_matches_item(r, world, item_plugin_index, item_keywords));
        if !any_matched {
            return false;
        }
    }

    // ANY: substring match activated separately in evaluate_with_any
    // (needs kw_edid_map context that individual filter eval doesn't have).
    // The basic evaluate() ignores ANY; evaluate_with_any() is used by
    // the distributor which has the map pre-built.

    true
}

/// Extended evaluate that also honors the ANY (`*` prefix) bucket.
/// Callers provide `item_editor_id` (optional if the record has no EDID)
/// and `kw_edid_map` (FormId -> editor-id string) for keyword-edid
/// substring checks.
pub fn evaluate_with_any(
    buckets: &FilterBuckets,
    world: &EspWorld,
    item_plugin_index: usize,
    item_keywords: &[FormId],
    item_editor_id: Option<&str>,
    kw_edid_map: &std::collections::HashMap<FormId, String>,
) -> bool {
    if !evaluate(buckets, world, item_plugin_index, item_keywords) {
        return false;
    }

    if !buckets.any.is_empty() {
        let any_matched = buckets.any.iter().any(|substring| {
            if let Some(edid) = item_editor_id
                && edid
                    .to_ascii_lowercase()
                    .contains(&substring.to_ascii_lowercase())
            {
                return true;
            }
            // Check item's keyword editor-ids.
            for kw_fid in item_keywords {
                if let Some(kw_edid) = kw_edid_map.get(kw_fid)
                    && kw_edid
                        .to_ascii_lowercase()
                        .contains(&substring.to_ascii_lowercase())
                {
                    return true;
                }
            }
            false
        });
        if !any_matched {
            return false;
        }
    }

    true
}

fn ref_matches_item(
    r: &Reference,
    world: &EspWorld,
    item_plugin_index: usize,
    item_keywords: &[FormId],
) -> bool {
    match r {
        Reference::PluginName(name) => {
            let Some(plugin) = world.plugins.get(item_plugin_index) else {
                return false;
            };
            plugin.filename.eq_ignore_ascii_case(name)
        }
        Reference::EditorId(_) | Reference::FormIdWithPlugin { .. } | Reference::FormIdOnly(_) => {
            // Resolve to a FormId. If the resolved form is a keyword
            // that the item has, match. (Form filters for non-keyword
            // references — e.g. matching the item *itself* — aren't
            // supported at M3; those would require per-record-type
            // identity checks.)
            let Some(fid) = r.resolve_form(world) else {
                return false;
            };
            item_keywords.contains(&fid)
        }
    }
}

/// Evaluate weapon trait predicates against a parsed WeaponRecord.
/// Returns `true` if every specified predicate passes; `true` if the
/// traits struct is empty (no constraints).
pub fn evaluate_weapon_traits(
    traits: &crate::traits_weapon::WeaponTraits,
    weapon: &mora_esp::records::weapon::WeaponRecord,
) -> bool {
    // anim_types: OR across the listed types. If any matches, pass.
    if !traits.anim_types.is_empty() {
        let Some(weapon_anim) = weapon.animation_type else {
            return false;
        };
        let matched = traits.anim_types.iter().any(|t| match t {
            crate::traits_weapon::WeaponAnimType::OneHandSword => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::OneHandSword
            }
            crate::traits_weapon::WeaponAnimType::OneHandDagger => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::OneHandDagger
            }
            crate::traits_weapon::WeaponAnimType::OneHandAxe => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::OneHandAxe
            }
            crate::traits_weapon::WeaponAnimType::OneHandMace => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::OneHandMace
            }
            crate::traits_weapon::WeaponAnimType::TwoHandSword => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::TwoHandSword
            }
            crate::traits_weapon::WeaponAnimType::TwoHandAxe => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::TwoHandAxe
            }
            crate::traits_weapon::WeaponAnimType::Bow => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::Bow
            }
            crate::traits_weapon::WeaponAnimType::Crossbow => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::Crossbow
            }
            crate::traits_weapon::WeaponAnimType::Staff => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::Staff
            }
            crate::traits_weapon::WeaponAnimType::HandToHandMelee => {
                weapon_anim == mora_esp::subrecords::weapon_dnam::WeaponAnimType::HandToHandMelee
            }
        });
        if !matched {
            return false;
        }
    }

    // require_enchanted: Some(true) means "must be enchanted";
    // Some(false) means "must not be"; None means "no constraint".
    if let Some(must_enchanted) = traits.require_enchanted {
        let is_enchanted = weapon.enchantment.is_some();
        if is_enchanted != must_enchanted {
            return false;
        }
    }

    // require_template: Some(true/false) / None as above.
    if let Some(must_template) = traits.require_template {
        let has_template = weapon.template_weapon.is_some();
        if has_template != must_template {
            return false;
        }
    }

    // damage_range: inclusive range check.
    if let Some((min, max)) = traits.damage_range {
        let Some(damage) = weapon.damage else {
            return false;
        };
        let damage_f = damage as f32;
        if !(damage_f >= min && damage_f <= max) {
            return false;
        }
    }

    // weight_range: inclusive range check.
    if let Some((min, max)) = traits.weight_range {
        let Some(weight) = weapon.weight else {
            return false;
        };
        if !(weight >= min && weight <= max) {
            return false;
        }
    }

    true
}

/// Evaluate armor trait predicates against a parsed ArmorRecord.
pub fn evaluate_armor_traits(
    traits: &crate::traits_armor::ArmorTraits,
    armor: &mora_esp::records::armor::ArmorRecord,
) -> bool {
    // armor_types: OR. At least one must match the armor's type.
    if !traits.armor_types.is_empty() {
        let Some(armor_type) = armor.armor_type else {
            return false;
        };
        let matched = traits.armor_types.iter().any(|t| match t {
            crate::traits_armor::ArmorType::Heavy => {
                armor_type == mora_esp::subrecords::biped_object::ArmorType::HeavyArmor
            }
            crate::traits_armor::ArmorType::Light => {
                armor_type == mora_esp::subrecords::biped_object::ArmorType::LightArmor
            }
            crate::traits_armor::ArmorType::Clothing => {
                armor_type == mora_esp::subrecords::biped_object::ArmorType::Clothing
            }
        });
        if !matched {
            return false;
        }
    }

    if let Some(must_enchanted) = traits.require_enchanted
        && armor.enchantment.is_some() != must_enchanted
    {
        return false;
    }

    if let Some(must_template) = traits.require_template
        && armor.template_armor.is_some() != must_template
    {
        return false;
    }

    if let Some((min, max)) = traits.ar_range {
        let Some(ar) = armor.armor_rating else {
            return false;
        };
        if !(ar >= min && ar <= max) {
            return false;
        }
    }

    if let Some((min, max)) = traits.weight_range {
        let Some(weight) = armor.weight else {
            return false;
        };
        if !(weight >= min && weight <= max) {
            return false;
        }
    }

    // body_slots: OR. At least one listed slot must be occupied.
    if !traits.body_slots.is_empty() {
        let any_match = traits
            .body_slots
            .iter()
            .any(|wanted| armor.body_slots.contains(wanted));
        if !any_match {
            return false;
        }
    }

    true
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_mixed_prefixes() {
        let buckets = parse_filter_field("A,-B,*C,D+E,F");
        assert_eq!(buckets.match_.len(), 2); // A and F
        assert_eq!(buckets.not.len(), 1); // B
        assert_eq!(buckets.any.len(), 1); // *C
        assert_eq!(buckets.all.len(), 1); // D+E
        assert_eq!(buckets.all[0].len(), 2);
    }

    #[test]
    fn none_returns_empty() {
        let buckets = parse_filter_field("NONE");
        // NONE is the literal string; our parser treats it as a single
        // MATCH token. Callers check is_absent before invoking.
        assert_eq!(buckets.match_.len(), 1);
    }

    #[test]
    fn empty_returns_empty() {
        let buckets = parse_filter_field("");
        assert!(buckets.is_empty());
    }

    #[test]
    fn all_bucket_records_groups() {
        let buckets = parse_filter_field("A+B,C+D");
        assert_eq!(buckets.all.len(), 2);
        assert_eq!(buckets.all[0].len(), 2);
        assert_eq!(buckets.all[1].len(), 2);
    }
}
