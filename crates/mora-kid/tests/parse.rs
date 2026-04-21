//! Integration tests for KID INI parsing.

use mora_kid::ini::parse_ini_content;
use mora_kid::rule::{RecordType, Traits};

#[test]
fn parses_real_world_style_kid_ini() {
    let content = r"; A real-world-ish KID INI file
# Another comment

[ignored section]

; Simple weapon rule
WeapMaterialIron = Weapon

; Armor with traits + chance
ArmorMaterialSteel = Armor||HEAVY|50

; Weapon with filter and chance
WeapTypeBow = Weapon|-E|Bow|75

; Unsupported record type (parsed but distributor skips)
AlchPoison = Potion||-F

; Exclusive group — skipped
ExclusiveGroup = MaterialGroup|WeapMaterialIron,WeapMaterialSteel

; Missing type — skipped with warning
NoTypeHere =
";

    let parsed = parse_ini_content(content, "test.ini");
    let rules = &parsed.rules;
    assert_eq!(rules.len(), 4); // the 4 well-formed rules
    assert_eq!(parsed.exclusive_groups.len(), 1);

    assert!(matches!(rules[0].record_type, RecordType::Weapon));
    assert_eq!(rules[0].chance, 100);

    assert!(matches!(rules[1].record_type, RecordType::Armor));
    assert_eq!(rules[1].chance, 50);
    assert!(matches!(rules[1].traits, Traits::Armor(_)));

    assert!(matches!(rules[2].record_type, RecordType::Weapon));
    assert_eq!(rules[2].chance, 75);
    assert_eq!(rules[2].filters.not.len(), 1);

    assert!(matches!(rules[3].record_type, RecordType::Other(ref s) if s == "Potion"));
}
