//! KID INI file discovery + line parsing.
//!
//! See `docs/src/kid-ini-grammar.md` for the full grammar.

use std::path::{Path, PathBuf};

use tracing::warn;

use crate::TraitParseError;
use crate::filter::parse_filter_field;
use crate::reference::Reference;
use crate::rule::{ExclusiveGroup, FilterBuckets, KidRule, RecordType, SourceLocation, Traits};
use crate::traits_armor::ArmorTraits;
use crate::traits_weapon::WeaponTraits;

/// Output of a single KID INI parse: rules + exclusive groups.
#[derive(Debug, Default, Clone)]
pub struct ParsedIni {
    pub rules: Vec<KidRule>,
    pub exclusive_groups: Vec<ExclusiveGroup>,
}

#[derive(Debug, thiserror::Error)]
pub enum IniError {
    #[error("io: {0}")]
    Io(#[from] std::io::Error),
    #[error("trait parse: {0}")]
    Trait(#[from] TraitParseError),
    #[error("{file}:{line}: missing type field")]
    MissingType { file: String, line: usize },
}

/// Discover all `*_KID.ini` files in a directory (non-recursive).
pub fn discover_kid_ini_files(data_dir: &Path) -> std::io::Result<Vec<PathBuf>> {
    let mut out = Vec::new();
    for entry in std::fs::read_dir(data_dir)? {
        let entry = entry?;
        let path = entry.path();
        if !path.is_file() {
            continue;
        }
        let Some(name) = path.file_name().and_then(|n| n.to_str()) else {
            continue;
        };
        let lower = name.to_ascii_lowercase();
        if lower.ends_with(".ini") && lower.contains("_kid") {
            out.push(path);
        }
    }
    out.sort();
    Ok(out)
}

/// Parse all rules in a KID INI file.
pub fn parse_file(path: &Path) -> Result<ParsedIni, IniError> {
    let content = std::fs::read_to_string(path)?;
    let file_name = path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("<unknown>")
        .to_string();
    Ok(parse_ini_content(&content, &file_name))
}

/// Parse INI content (no I/O). Exposed for unit tests.
pub fn parse_ini_content(content: &str, file_name: &str) -> ParsedIni {
    let mut out = ParsedIni::default();
    for (idx, raw) in content.lines().enumerate() {
        let line_number = idx + 1;
        let line = raw.trim();
        if line.is_empty()
            || line.starts_with(';')
            || line.starts_with('#')
            || line.starts_with('[')
        {
            continue;
        }
        let Some(eq_pos) = line.find('=') else {
            continue;
        };
        let key = line[..eq_pos].trim();
        let value = line[eq_pos + 1..].trim();

        // ExclusiveGroup: "Name|member1,member2,..."
        if key.eq_ignore_ascii_case("ExclusiveGroup") {
            match parse_exclusive_group(value, file_name, line_number) {
                Ok(group) => out.exclusive_groups.push(group),
                Err(e) => warn!("{file_name}:{line_number}: skipped exclusive group: {e}"),
            }
            continue;
        }

        match parse_rule_line(key, value, file_name, line_number) {
            Ok(rule) => out.rules.push(rule),
            Err(e) => {
                warn!("{file_name}:{line_number}: skipped rule: {e}");
            }
        }
    }
    out
}

/// Parse an `ExclusiveGroup = Name|kw1,kw2,...` value.
fn parse_exclusive_group(
    value: &str,
    file_name: &str,
    line_number: usize,
) -> Result<ExclusiveGroup, IniError> {
    let fields: Vec<&str> = value.split('|').collect();
    let name = fields.first().map(|s| s.trim()).unwrap_or("").to_string();
    if name.is_empty() {
        return Err(IniError::MissingType {
            file: file_name.to_string(),
            line: line_number,
        });
    }
    let members_str = fields.get(1).copied().unwrap_or("");
    let members: Vec<crate::reference::Reference> = members_str
        .split(',')
        .map(|s| s.trim())
        .filter(|s| !s.is_empty())
        .map(|s| {
            // KID allows a '-' prefix for NOT-in-group; M3 treats them as
            // regular members with a debug log.
            if let Some(rest) = s.strip_prefix('-') {
                tracing::debug!(
                    "{file_name}:{line_number}: ExclusiveGroup member '-{rest}' — NOT semantics deferred, treated as regular member"
                );
                crate::reference::Reference::parse(rest)
            } else {
                crate::reference::Reference::parse(s)
            }
        })
        .collect();
    Ok(ExclusiveGroup {
        name,
        members,
        source: SourceLocation {
            file: file_name.to_string(),
            line_number,
        },
    })
}

fn parse_rule_line(
    key: &str,
    value: &str,
    file_name: &str,
    line_number: usize,
) -> Result<KidRule, IniError> {
    let keyword = Reference::parse(key);
    let fields: Vec<&str> = value.split('|').collect();
    // Field 0: Type (required)
    let type_str = fields.first().map(|s| s.trim()).unwrap_or("");
    if type_str.is_empty() {
        return Err(IniError::MissingType {
            file: file_name.to_string(),
            line: line_number,
        });
    }
    let record_type = RecordType::parse(type_str);

    // Field 1: Filters (optional)
    let filters = match fields.get(1) {
        Some(s) if !is_absent(s) => parse_filter_field(s.trim()),
        _ => FilterBuckets::default(),
    };

    // Field 2: Traits (optional, type-specific)
    let traits = match fields.get(2) {
        Some(s) if !is_absent(s) => match &record_type {
            RecordType::Weapon => Traits::Weapon(WeaponTraits::parse(s.trim())?),
            RecordType::Armor => Traits::Armor(ArmorTraits::parse(s.trim())?),
            RecordType::Other(_) => Traits::None,
        },
        _ => Traits::None,
    };

    // Field 3: Chance (optional, default 100)
    let chance = match fields.get(3) {
        Some(s) if !is_absent(s) => {
            let v: f32 = s.trim().parse().unwrap_or(100.0);
            v.clamp(0.0, 100.0).round() as u8
        }
        _ => 100,
    };

    Ok(KidRule {
        keyword,
        record_type,
        filters,
        traits,
        chance,
        source: SourceLocation {
            file: file_name.to_string(),
            line_number,
        },
    })
}

fn is_absent(s: &str) -> bool {
    let t = s.trim();
    t.is_empty() || t.eq_ignore_ascii_case("NONE")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_simple_weapon_rule() {
        let content = "WeapMaterialIron = Weapon\n";
        let parsed = parse_ini_content(content, "test.ini");
        let rules = &parsed.rules;
        assert_eq!(rules.len(), 1);
        assert!(matches!(rules[0].record_type, RecordType::Weapon));
        assert!(matches!(rules[0].keyword, Reference::EditorId(ref s) if s == "WeapMaterialIron"));
        assert_eq!(rules[0].chance, 100);
    }

    #[test]
    fn skips_comments_blank_sections() {
        let content = "; a comment\n# another\n\n[IgnoreMe]\nWeapMaterialSteel = Weapon\n";
        let parsed = parse_ini_content(content, "test.ini");
        let rules = &parsed.rules;
        assert_eq!(rules.len(), 1);
    }

    #[test]
    fn parses_exclusive_group() {
        let content = "ExclusiveGroup = Materials|WeapMaterialIron,WeapMaterialSteel\n";
        let parsed = parse_ini_content(content, "test.ini");
        assert_eq!(parsed.rules.len(), 0);
        assert_eq!(parsed.exclusive_groups.len(), 1);
        let g = &parsed.exclusive_groups[0];
        assert_eq!(g.name, "Materials");
        assert_eq!(g.members.len(), 2);
    }

    #[test]
    fn skips_exclusive_group_from_rules() {
        let content = "ExclusiveGroup = Materials|A,B,C\nWeapMaterialIron = Weapon\n";
        let parsed = parse_ini_content(content, "test.ini");
        assert_eq!(parsed.rules.len(), 1);
        assert_eq!(parsed.exclusive_groups.len(), 1);
    }

    #[test]
    fn parses_chance() {
        let content = "WeapTypeBow = Weapon|||75\n";
        let parsed = parse_ini_content(content, "test.ini");
        let rules = &parsed.rules;
        assert_eq!(rules.len(), 1);
        assert_eq!(rules[0].chance, 75);
    }

    #[test]
    fn parses_armor_traits() {
        let content = "ArmorMaterialIron = Armor||HEAVY,-E\n";
        let parsed = parse_ini_content(content, "test.ini");
        let rules = &parsed.rules;
        assert_eq!(rules.len(), 1);
        match &rules[0].traits {
            Traits::Armor(at) => {
                assert_eq!(at.armor_types.len(), 1);
                assert_eq!(at.require_enchanted, Some(false));
            }
            other => panic!("expected Armor traits, got {other:?}"),
        }
    }

    #[test]
    fn parses_weapon_with_filter_and_chance() {
        let content = "WeapTypeBow = Weapon|-E|Bow|50\n";
        let parsed = parse_ini_content(content, "test.ini");
        let rules = &parsed.rules;
        assert_eq!(rules.len(), 1);
        assert_eq!(rules[0].chance, 50);
        assert_eq!(rules[0].filters.not.len(), 1);
    }

    #[test]
    fn missing_type_is_skipped_with_warning() {
        let content = "NoType = \n";
        let parsed = parse_ini_content(content, "test.ini");
        assert_eq!(parsed.rules.len(), 0);
    }

    #[test]
    fn other_record_type_preserved() {
        let content = "AlchPoison = Potion|||-F\n";
        let parsed = parse_ini_content(content, "test.ini");
        let rules = &parsed.rules;
        assert_eq!(rules.len(), 1);
        assert!(matches!(rules[0].record_type, RecordType::Other(ref s) if s == "Potion"));
        // Traits for non-Weapon/Armor types are set to None.
        assert!(matches!(rules[0].traits, Traits::None));
    }
}
