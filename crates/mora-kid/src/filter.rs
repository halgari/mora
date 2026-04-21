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
