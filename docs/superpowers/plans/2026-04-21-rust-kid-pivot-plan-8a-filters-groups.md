# Rust + KID Pivot — Plan 8a: Activate ALL / ANY filters + ExclusiveGroup

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Activate three KID features currently parsed but ignored by the evaluator/distributor: `ALL` (`+` prefix) filters, `ANY` (`*` prefix) substring filters, and `ExclusiveGroup` mutual-exclusion semantics.

**Architecture:** Pure mora-kid work — no mora-esp binary-format changes, no new subrecord parsers. The AST already carries these filters (Plan 6); we activate their evaluation + add state tracking for ExclusiveGroups. DistributorStats gains a `rejected_by_exclusive_group` counter. The distributor pre-builds two lookup maps at setup time (keyword FormId → editor-ID string; keyword FormId → list of group indices) so per-item evaluation is O(1) + small constants.

**Tech Stack:** Rust 1.90. No new workspace deps.

**Reference spec:** `docs/superpowers/specs/2026-04-20-rust-kid-pivot-design.md`

**Scope discipline:**
- **ALL + ANY + ExclusiveGroup only.** Trait predicates (DNAM / BOD2 / EITM / CNAM) require mora-esp record-layout extensions — that's Plan 8b.
- **ANY substring match** against item editor-ID + its keyword editor-IDs. **Not yet implemented:** display name (FULL subrecord) or model path (`.nif` matches) — both need mora-esp extensions. Document as M3-scope.
- **ExclusiveGroup member `-` prefix** (NOT-in-group) — parsed as a regular member with a debug log. KID's semantics here are subtle and rarely used; deferred.
- **No mora-esp, mora-core, or mora-cli changes.** Entirely mora-kid internal.

---

## File Structure

**Modified:**
- `crates/mora-core/src/distributor.rs` — add `rejected_by_exclusive_group` to `DistributorStats`
- `crates/mora-kid/src/rule.rs` — add `ExclusiveGroup` struct; track groups alongside rules
- `crates/mora-kid/src/ini.rs` — parse `ExclusiveGroup = Name|kw,kw,...` lines into the new struct
- `crates/mora-kid/src/filter.rs` — activate ALL + ANY evaluation
- `crates/mora-kid/src/distributor.rs` — pre-build lookup maps; enforce ExclusiveGroup per (form, group)

**Created:**
- `crates/mora-kid/tests/filter_activation.rs` — integration tests for ALL/ANY/Exclusive
- `docs/src/kid-ini-grammar.md` — updated "Current Mora coverage" section

---

## Phase A — DistributorStats field + AST extension (Tasks 1-2)

### Task 1: Extend `DistributorStats` with `rejected_by_exclusive_group`

**Files:**
- Modify: `crates/mora-core/src/distributor.rs`

- [ ] **Step 1: Add the field**

```bash
cd /home/tbaldrid/oss/mora
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-core/src/distributor.rs")
text = p.read_text()
old = """#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct DistributorStats {
    pub rules_evaluated: u64,
    pub candidates_considered: u64,
    pub patches_emitted: u64,
    pub rejected_by_chance: u64,
    pub rejected_by_filter: u64,
}"""
new = """#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct DistributorStats {
    pub rules_evaluated: u64,
    pub candidates_considered: u64,
    pub patches_emitted: u64,
    pub rejected_by_chance: u64,
    pub rejected_by_filter: u64,
    pub rejected_by_exclusive_group: u64,
}"""
assert old in text, "expected DistributorStats declaration not found"
text = text.replace(old, new)

# Update AddAssign impl
old_add = """impl std::ops::AddAssign for DistributorStats {
    fn add_assign(&mut self, rhs: Self) {
        self.rules_evaluated += rhs.rules_evaluated;
        self.candidates_considered += rhs.candidates_considered;
        self.patches_emitted += rhs.patches_emitted;
        self.rejected_by_chance += rhs.rejected_by_chance;
        self.rejected_by_filter += rhs.rejected_by_filter;
    }
}"""
new_add = """impl std::ops::AddAssign for DistributorStats {
    fn add_assign(&mut self, rhs: Self) {
        self.rules_evaluated += rhs.rules_evaluated;
        self.candidates_considered += rhs.candidates_considered;
        self.patches_emitted += rhs.patches_emitted;
        self.rejected_by_chance += rhs.rejected_by_chance;
        self.rejected_by_filter += rhs.rejected_by_filter;
        self.rejected_by_exclusive_group += rhs.rejected_by_exclusive_group;
    }
}"""
assert old_add in text, "expected AddAssign impl not found"
text = text.replace(old_add, new_add)

p.write_text(text)
PY
```

- [ ] **Step 2: Update the add_assign test**

The existing `stats_add_assign` test uses struct-literal init for both operands. Update to include the new field.

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-core/src/distributor.rs")
text = p.read_text()
# Add a "rejected_by_exclusive_group: N" field to each struct literal in the test.
old = """        let mut a = DistributorStats {
            rules_evaluated: 1,
            candidates_considered: 2,
            patches_emitted: 3,
            rejected_by_chance: 4,
            rejected_by_filter: 5,
        };
        let b = DistributorStats {
            rules_evaluated: 10,
            candidates_considered: 20,
            patches_emitted: 30,
            rejected_by_chance: 40,
            rejected_by_filter: 50,
        };
        a += b;
        assert_eq!(
            a,
            DistributorStats {
                rules_evaluated: 11,
                candidates_considered: 22,
                patches_emitted: 33,
                rejected_by_chance: 44,
                rejected_by_filter: 55,
            }
        );"""
new = """        let mut a = DistributorStats {
            rules_evaluated: 1,
            candidates_considered: 2,
            patches_emitted: 3,
            rejected_by_chance: 4,
            rejected_by_filter: 5,
            rejected_by_exclusive_group: 6,
        };
        let b = DistributorStats {
            rules_evaluated: 10,
            candidates_considered: 20,
            patches_emitted: 30,
            rejected_by_chance: 40,
            rejected_by_filter: 50,
            rejected_by_exclusive_group: 60,
        };
        a += b;
        assert_eq!(
            a,
            DistributorStats {
                rules_evaluated: 11,
                candidates_considered: 22,
                patches_emitted: 33,
                rejected_by_chance: 44,
                rejected_by_filter: 55,
                rejected_by_exclusive_group: 66,
            }
        );"""
assert old in text, "stats_add_assign literal not found"
text = text.replace(old, new)
p.write_text(text)
PY
```

- [ ] **Step 3: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-core --lib distributor::tests
cargo xwin check --target x86_64-pc-windows-msvc --workspace
git add crates/mora-core/src/distributor.rs
git commit -m "mora-core: DistributorStats gains rejected_by_exclusive_group

Sixth counter on DistributorStats tracking skipped emissions due
to ExclusiveGroup enforcement. AddAssign impl + existing test
updated. Plan 8a's distributor populates this counter."
```

---

### Task 2: Add `ExclusiveGroup` struct to `rule.rs`

**Files:**
- Modify: `crates/mora-kid/src/rule.rs`

- [ ] **Step 1: Append ExclusiveGroup struct**

```bash
cd /home/tbaldrid/oss/mora
cat >> crates/mora-kid/src/rule.rs <<'EOF'

/// A parsed `ExclusiveGroup = Name|kw1,kw2,...` line.
///
/// Group members are kept as raw `Reference`s (unresolved); the
/// distributor resolves them against the EspWorld at setup time.
#[derive(Debug, Clone)]
pub struct ExclusiveGroup {
    pub name: String,
    pub members: Vec<Reference>,
    pub source: SourceLocation,
}
EOF
```

Need to also make `Reference` importable within `rule.rs` if it isn't already. Check the existing `use` at the top of rule.rs — should already have `use crate::reference::Reference;`.

- [ ] **Step 2: Parser output — add `Vec<ExclusiveGroup>` alongside `Vec<KidRule>`**

Rather than bundling into a single `ParsedIni` struct (which would propagate through more code paths), the ini parser will add a new return type. Replace the current `parse_ini_content(content, file_name) -> Vec<KidRule>` with a new signature in Task 4 that returns both. For now, leave the parser alone — this task just introduces the type.

- [ ] **Step 3: Verify + commit**

```bash
source $HOME/.cargo/env
cargo check --package mora-kid
git add crates/mora-kid/src/rule.rs
git commit -m "mora-kid: ExclusiveGroup AST type

Holds name + unresolved member references + source location.
Plan 8a Task 4 extends the INI parser to produce these alongside
KidRules; Task 6 resolves + enforces in the distributor."
```

---

## Phase B — ALL filter activation (Task 3)

### Task 3: Activate ALL bucket evaluation in `filter.rs`

**Files:**
- Modify: `crates/mora-kid/src/filter.rs`

- [ ] **Step 1: Replace the current `evaluate` with ALL-activated version**

```bash
cd /home/tbaldrid/oss/mora
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-kid/src/filter.rs")
text = p.read_text()
old = """    // NOT: if any matches, fail.
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

    true
}"""
new = """    // ALL: each group is a '+'-joined ref list. Every ref across every
    // group must match (KID stores ALL as a flat \"every must match\" list;
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
                && edid.to_ascii_lowercase().contains(&substring.to_ascii_lowercase())
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
}"""
assert old in text, "expected evaluate() tail not found"
text = text.replace(old, new)
p.write_text(text)
PY
```

- [ ] **Step 2: Update `FilterBuckets::has_unsupported`**

ALL and ANY are now supported. The method can be kept as-is for documentation but should always return `false` now. Or remove. Simplest: remove the method and its callers.

```bash
python3 - <<'PY'
from pathlib import Path

# Remove has_unsupported from rule.rs
p = Path("crates/mora-kid/src/rule.rs")
text = p.read_text()
old = """    pub fn has_unsupported(&self) -> bool {
        !self.all.is_empty() || !self.any.is_empty()
    }
"""
if old in text:
    text = text.replace(old, "")
    p.write_text(text)

# Remove the distributor's has_unsupported() call + the debug log.
p = Path("crates/mora-kid/src/distributor.rs")
text = p.read_text()
old = """            if rule.filters.has_unsupported() {
                debug!(
                    "{}:{}: rule has unsupported ALL/ANY filters; evaluator treats as pass",
                    rule.source.file, rule.source.line_number
                );
            }
"""
if old in text:
    text = text.replace(old, "")
    p.write_text(text)
PY
```

- [ ] **Step 3: Add unit tests for ALL bucket**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-kid/src/filter.rs")
text = p.read_text()
# Replace the existing #[cfg(test)] block with an expanded one.
old_tests = """#[cfg(test)]
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
}"""
new_tests = """#[cfg(test)]
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
}"""
assert old_tests in text, "expected existing tests block not found"
text = text.replace(old_tests, new_tests)
p.write_text(text)
PY
```

Full semantic tests for ALL and ANY evaluation come in Task 7's integration tests — they require a real `EspWorld` to test `ref_matches_item` + the kw_edid_map.

- [ ] **Step 4: Verify + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-kid --lib filter::tests
cargo xwin check --package mora-kid --target x86_64-pc-windows-msvc
git add crates/mora-kid/src/filter.rs crates/mora-kid/src/rule.rs crates/mora-kid/src/distributor.rs
git commit -m "mora-kid: activate ALL filter + expose evaluate_with_any hook

evaluate() now enforces the ALL bucket (every + sub-group, every ref
within, must match) before MATCH/NOT checks. New evaluate_with_any()
layers ANY substring check on top — requires item_editor_id +
kw_edid_map which only the distributor pre-builds. Unit test for
parser grouping; semantic tests land in tests/filter_activation.rs."
```

---

## Phase C — ExclusiveGroup parsing (Task 4)

### Task 4: Extend INI parser to return `(Vec<KidRule>, Vec<ExclusiveGroup>)`

**Files:**
- Modify: `crates/mora-kid/src/ini.rs`

- [ ] **Step 1: Add a typed return struct + update parser**

Keep `parse_ini_content` / `parse_file` returning just `Vec<KidRule>` for backward compat (pipeline::compile and other callers may still use them). Add new `parse_ini_content_full` / `parse_file_full` that return both.

Actually — simpler: change the signature and update pipeline + CLI callers. Fewer public APIs to maintain.

```bash
cd /home/tbaldrid/oss/mora
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-kid/src/ini.rs")
text = p.read_text()

# Insert a ParsedIni struct after IniError.
marker = """#[derive(Debug, thiserror::Error)]
pub enum IniError {"""
insertion = """/// Output of a single KID INI parse: rules + exclusive groups.
#[derive(Debug, Default, Clone)]
pub struct ParsedIni {
    pub rules: Vec<KidRule>,
    pub exclusive_groups: Vec<ExclusiveGroup>,
}

"""
if "struct ParsedIni" not in text:
    text = text.replace(marker, insertion + marker, 1)

# Import ExclusiveGroup
old_use = "use crate::rule::{FilterBuckets, KidRule, RecordType, SourceLocation, Traits};"
new_use = "use crate::rule::{ExclusiveGroup, FilterBuckets, KidRule, RecordType, SourceLocation, Traits};"
text = text.replace(old_use, new_use)

# Change parse_file signature
old = """pub fn parse_file(path: &Path) -> Result<Vec<KidRule>, IniError> {
    let content = std::fs::read_to_string(path)?;
    let file_name = path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("<unknown>")
        .to_string();
    Ok(parse_ini_content(&content, &file_name))
}"""
new = """pub fn parse_file(path: &Path) -> Result<ParsedIni, IniError> {
    let content = std::fs::read_to_string(path)?;
    let file_name = path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or("<unknown>")
        .to_string();
    Ok(parse_ini_content(&content, &file_name))
}"""
assert old in text, "parse_file signature not found"
text = text.replace(old, new)

# Change parse_ini_content signature + body
old = """pub fn parse_ini_content(content: &str, file_name: &str) -> Vec<KidRule> {
    let mut rules = Vec::new();
    for (idx, raw) in content.lines().enumerate() {
        let line_number = idx + 1;
        let line = raw.trim();
        if line.is_empty() || line.starts_with(';') || line.starts_with('#') || line.starts_with('[')
        {
            continue;
        }
        let Some(eq_pos) = line.find('=') else {
            continue;
        };
        let key = line[..eq_pos].trim();
        let value = line[eq_pos + 1..].trim();
        // KID reserves `ExclusiveGroup` — M3 ignores these lines.
        if key.eq_ignore_ascii_case("ExclusiveGroup") {
            continue;
        }
        match parse_rule_line(key, value, file_name, line_number) {
            Ok(rule) => rules.push(rule),
            Err(e) => {
                warn!("{file_name}:{line_number}: skipped rule: {e}");
            }
        }
    }
    rules
}"""
new = """pub fn parse_ini_content(content: &str, file_name: &str) -> ParsedIni {
    let mut out = ParsedIni::default();
    for (idx, raw) in content.lines().enumerate() {
        let line_number = idx + 1;
        let line = raw.trim();
        if line.is_empty() || line.starts_with(';') || line.starts_with('#') || line.starts_with('[')
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
}"""
assert old in text, "parse_ini_content body not found"
text = text.replace(old, new)

p.write_text(text)
PY
```

- [ ] **Step 2: Fix downstream callers**

`pipeline::compile` and `mora-cli::compile` both call `ini::parse_file`. Update to extract `.rules` + collect `.exclusive_groups`.

```bash
python3 - <<'PY'
from pathlib import Path

# pipeline.rs
p = Path("crates/mora-kid/src/pipeline.rs")
text = p.read_text()
old = """    let mut all_rules = Vec::new();
    for p in &ini_paths {
        let rules = ini::parse_file(p)?;
        all_rules.extend(rules);
    }

    let distributor = KidDistributor::new(all_rules);"""
new = """    let mut all_rules = Vec::new();
    let mut all_groups = Vec::new();
    for p in &ini_paths {
        let parsed = ini::parse_file(p)?;
        all_rules.extend(parsed.rules);
        all_groups.extend(parsed.exclusive_groups);
    }

    let distributor = KidDistributor::new(all_rules).with_exclusive_groups(all_groups);"""
assert old in text, "pipeline parse loop not found"
text = text.replace(old, new)
p.write_text(text)

# mora-cli/compile.rs
p = Path("crates/mora-cli/src/compile.rs")
text = p.read_text()
old = """    let mut all_rules = Vec::new();
    for p in &ini_paths {
        match ini::parse_file(p) {
            Ok(rules) => all_rules.extend(rules),
            Err(e) => {
                warn!("{}: {}", p.display(), e);
            }
        }
    }
    info!("Parsed {} rules", all_rules.len());

    // Run distributor.
    let distributor = KidDistributor::new(all_rules);"""
new = """    let mut all_rules = Vec::new();
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
    let distributor = KidDistributor::new(all_rules).with_exclusive_groups(all_groups);"""
assert old in text, "mora-cli compile loop not found"
text = text.replace(old, new)
p.write_text(text)
PY
```

Note: `with_exclusive_groups` is a builder method we'll add in Task 6. Until then, the build will fail — that's OK, it's a multi-task patch. Task 5 will add the method.

- [ ] **Step 3: Add a parser test for ExclusiveGroup**

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-kid/src/ini.rs")
text = p.read_text()

# Append a new test to the existing tests module.
marker = """    #[test]
    fn other_record_type_preserved() {"""
# Insert before this test — put new test above it.
insertion = """    #[test]
    fn parses_exclusive_group() {
        let content = "ExclusiveGroup = Materials|WeapMaterialIron,WeapMaterialSteel\\n";
        let parsed = parse_ini_content(content, "test.ini");
        assert_eq!(parsed.rules.len(), 0);
        assert_eq!(parsed.exclusive_groups.len(), 1);
        let g = &parsed.exclusive_groups[0];
        assert_eq!(g.name, "Materials");
        assert_eq!(g.members.len(), 2);
    }

"""
assert marker in text, "marker not found"
text = text.replace(marker, insertion + marker)

# Update the existing tests that assert on the old return type — now they
# need to use parsed.rules instead of a Vec<KidRule>.
for old, new in [
        ("""        let rules = parse_ini_content(content, "test.ini");
        assert_eq!(rules.len(), 1);
        assert!(matches!(rules[0].record_type, RecordType::Weapon));""",
         """        let parsed = parse_ini_content(content, "test.ini");
        let rules = &parsed.rules;
        assert_eq!(rules.len(), 1);
        assert!(matches!(rules[0].record_type, RecordType::Weapon));"""),
]:
    text = text.replace(old, new)

p.write_text(text)
PY
```

After this, there are likely still test-compile errors because the existing `parses_simple_weapon_rule`/`parses_chance`/etc. tests still use the old `let rules = parse_ini_content(...)` pattern. Fix them all:

```bash
python3 - <<'PY'
from pathlib import Path
p = Path("crates/mora-kid/src/ini.rs")
text = p.read_text()
# Crude but effective: replace "let rules = parse_ini_content(" with
# "let parsed = parse_ini_content(" wherever it appears inside the tests,
# then add a following "let rules = &parsed.rules;" line on the next line.
import re
text = re.sub(
    r"let rules = parse_ini_content\((.*?)\);",
    r"let parsed = parse_ini_content(\1);\n        let rules = &parsed.rules;",
    text,
)
p.write_text(text)
PY
```

Verify no stale refs:

```bash
grep -n 'let rules = parse_ini_content' crates/mora-kid/src/ini.rs  # expect: zero matches
grep -n 'let parsed = parse_ini_content' crates/mora-kid/src/ini.rs  # expect: matches everywhere
```

- [ ] **Step 4: Verify (expect failures in distributor — Task 5 fixes)**

```bash
source $HOME/.cargo/env
cargo check --package mora-kid --lib 2>&1 | tail -10
```

Expect: `mora-kid` compiles (ini.rs changes + parser test). `pipeline.rs` and `mora-cli/compile.rs` still fail because they call `with_exclusive_groups` which doesn't exist yet.

Commit here anyway — the multi-task patch is intentional. Build will be green after Task 5.

Actually, cleanest is to leave all 3 files uncommitted until Task 5 commits the distributor change together. So:

```bash
git status  # should show ini.rs + pipeline.rs + mora-cli/compile.rs all modified
# NO COMMIT yet
```

- [ ] **Step 5: Verify ini.rs unit tests pass even without full build**

Since cargo check on the whole crate fails due to pipeline.rs, verify the ini.rs changes pass their own tests in isolation:

```bash
# Skip for now — will verify holistically after Task 5.
# If you want to test ini.rs alone:
cargo test --package mora-kid --lib ini 2>&1 | tail -5
```

Note: this may still fail because tests compile with the full lib. If compilation fails, proceed to Task 5.

---

## Phase D — Distributor enforcement (Task 5)

### Task 5: Enforce ExclusiveGroup + pre-build kw_edid_map; call evaluate_with_any

**Files:**
- Modify: `crates/mora-kid/src/distributor.rs`

- [ ] **Step 1: Add `with_exclusive_groups` builder + plumb through to `lower`**

Replace the `KidDistributor` + its `lower` impl with an updated version that: accepts exclusive groups, builds `kw_edid_map` once, tracks per-form applied-group state, calls `evaluate_with_any`.

Read the existing file first, then replace it wholesale:

```bash
source $HOME/.cargo/env
# Show current size for sanity
wc -l crates/mora-kid/src/distributor.rs
```

Then write the new version:

```bash
cat > crates/mora-kid/src/distributor.rs <<'EOF'
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

        // Iterate weapons.
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
EOF
```

- [ ] **Step 2: Verify full workspace compiles**

```bash
source $HOME/.cargo/env
cargo check --workspace --all-targets
cargo test --workspace --lib 2>&1 | grep -E "^test result" | awk '{c+=$4} END {print "TOTAL:", c}'
cargo xwin check --target x86_64-pc-windows-msvc --workspace
```

Expected: everything green. Total tests stays ~175 (we've added a parser test but no other test changes yet — integration tests come in Task 6).

- [ ] **Step 3: Commit (Tasks 4 + 5 together)**

```bash
git add crates/mora-kid/src/ini.rs crates/mora-kid/src/rule.rs crates/mora-kid/src/filter.rs crates/mora-kid/src/distributor.rs crates/mora-kid/src/pipeline.rs crates/mora-cli/src/compile.rs crates/mora-core/src/distributor.rs
git commit -m "mora-kid: ExclusiveGroup parsing + enforcement + ALL/ANY activation

INI parser now returns ParsedIni { rules, exclusive_groups } so
ExclusiveGroup lines are captured instead of silently skipped.
KidDistributor gains with_exclusive_groups() builder + per-form
applied-group tracking — when a keyword is in one or more exclusive
groups and another member of those groups has already been applied
to the form, the emission is skipped and stats.rejected_by_exclusive_group
increments.

Filter evaluator: ALL (+) bucket activation + new evaluate_with_any
that layers ANY (*) substring match against item editor-ID + its
keyword editor-IDs. Distributor pre-builds kw_edid_map once per run
so per-candidate checks are O(candidates * substrings * keywords)
with constant-time lookups.

Downstream callers (pipeline::compile + mora-cli compile.rs) updated
for the new ini::parse_file signature."
```

---

## Phase E — Integration tests (Task 6)

### Task 6: Integration tests for ALL / ANY / ExclusiveGroup

**Files:**
- Create: `crates/mora-kid/tests/filter_activation.rs`

- [ ] **Step 1: Write the test file**

```bash
cat > crates/mora-kid/tests/filter_activation.rs <<'EOF'
//! Integration tests for Plan 8a: activated ALL + ANY + ExclusiveGroup.

use std::io::Write;

use mora_core::{DeterministicChance, Distributor, FormId, PatchSink};
use mora_esp::{EspPlugin, EspWorld};
use mora_kid::distributor::KidDistributor;
use mora_kid::ini::parse_ini_content;

fn write_tmp(name: &str, bytes: &[u8]) -> std::path::PathBuf {
    let dir = std::env::temp_dir().join(format!("mora-kid-plan8a-{}", std::process::id()));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join(name);
    std::fs::File::create(&path).unwrap().write_all(bytes).unwrap();
    path
}

// Lifted from Plan 6's distribute.rs — minimal plugin builder.
fn build_plugin(
    keywords: &[(&str, u32)],
    weapons: &[(u32, &str, Vec<u32>)],
    armors: &[(u32, &str, Vec<u32>)],
) -> Vec<u8> {
    fn sub(sig: &[u8; 4], data: &[u8]) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(sig);
        v.extend_from_slice(&(data.len() as u16).to_le_bytes());
        v.extend_from_slice(data);
        v
    }
    fn rec(sig: &[u8; 4], form_id: u32, body: &[u8]) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(sig);
        v.extend_from_slice(&(body.len() as u32).to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes());
        v.extend_from_slice(&form_id.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes());
        v.extend_from_slice(&44u16.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(body);
        v
    }
    fn group(label: &[u8; 4], contents: &[u8]) -> Vec<u8> {
        let mut v = Vec::new();
        v.extend_from_slice(b"GRUP");
        v.extend_from_slice(&((24 + contents.len()) as u32).to_le_bytes());
        v.extend_from_slice(label);
        v.extend_from_slice(&0u32.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(&0u16.to_le_bytes());
        v.extend_from_slice(&0u32.to_le_bytes());
        v.extend_from_slice(contents);
        v
    }
    fn nul(s: &str) -> Vec<u8> {
        let mut v = s.as_bytes().to_vec();
        v.push(0);
        v
    }
    fn kwda(ids: &[u32]) -> Vec<u8> {
        let mut v = Vec::with_capacity(ids.len() * 4);
        for &id in ids {
            v.extend_from_slice(&id.to_le_bytes());
        }
        v
    }

    let mut tes4_body = Vec::new();
    tes4_body.extend_from_slice(b"HEDR");
    tes4_body.extend_from_slice(&12u16.to_le_bytes());
    tes4_body.extend_from_slice(&1.7f32.to_bits().to_le_bytes());
    tes4_body.extend_from_slice(&0u32.to_le_bytes());
    tes4_body.extend_from_slice(&0x800u32.to_le_bytes());

    let mut out = Vec::new();
    out.extend_from_slice(b"TES4");
    out.extend_from_slice(&(tes4_body.len() as u32).to_le_bytes());
    out.extend_from_slice(&1u32.to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&0u32.to_le_bytes());
    out.extend_from_slice(&44u16.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    out.extend_from_slice(&tes4_body);

    if !keywords.is_empty() {
        let mut contents = Vec::new();
        for (edid, form_id) in keywords {
            let body = sub(b"EDID", &nul(edid));
            contents.extend_from_slice(&rec(b"KYWD", *form_id, &body));
        }
        out.extend_from_slice(&group(b"KYWD", &contents));
    }
    if !weapons.is_empty() {
        let mut contents = Vec::new();
        for (form_id, edid, kwids) in weapons {
            let mut body = sub(b"EDID", &nul(edid));
            body.extend_from_slice(&sub(b"KWDA", &kwda(kwids)));
            contents.extend_from_slice(&rec(b"WEAP", *form_id, &body));
        }
        out.extend_from_slice(&group(b"WEAP", &contents));
    }
    if !armors.is_empty() {
        let mut contents = Vec::new();
        for (form_id, edid, kwids) in armors {
            let mut body = sub(b"EDID", &nul(edid));
            body.extend_from_slice(&sub(b"KWDA", &kwda(kwids)));
            contents.extend_from_slice(&rec(b"ARMO", *form_id, &body));
        }
        out.extend_from_slice(&group(b"ARMO", &contents));
    }

    out
}

fn open_world(suffix: &str, bytes: Vec<u8>) -> EspWorld {
    let path = write_tmp(&format!("{suffix}.esm"), &bytes);
    let plugin = EspPlugin::open(&path).unwrap();
    let plugins_txt = path
        .parent()
        .unwrap()
        .join(format!("plugins-{suffix}.txt"));
    std::fs::write(&plugins_txt, format!("*{}\n", plugin.filename)).unwrap();
    EspWorld::open(path.parent().unwrap(), &plugins_txt).unwrap()
}

fn run(world: &EspWorld, ini: &str) -> mora_core::PatchFile {
    let parsed = parse_ini_content(ini, "test.ini");
    let dist = KidDistributor::new(parsed.rules).with_exclusive_groups(parsed.exclusive_groups);
    let chance = DeterministicChance::kid_compatible();
    let mut sink = PatchSink::new();
    dist.lower(world, &chance, &mut sink).unwrap();
    sink.finalize()
}

#[test]
fn all_filter_requires_every_ref_to_match() {
    // Plugin: 2 weapons, one with KW_A (and nothing else), one with KW_A + KW_B.
    let bytes = build_plugin(
        &[
            ("KW_A", 0x0001_1000),
            ("KW_B", 0x0001_1001),
            ("Target", 0x0001_1002),
        ],
        &[
            (0x0001_2000, "OnlyA", vec![0x0001_1000]),
            (0x0001_2001, "AandB", vec![0x0001_1000, 0x0001_1001]),
        ],
        &[],
    );
    let world = open_world("all", bytes);
    // Rule: add Target keyword to weapons with KW_A AND KW_B
    let file = run(&world, "Target = Weapon|KW_A+KW_B\n");
    // Only AandB should be targeted.
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_2001));
}

#[test]
fn any_filter_substring_matches_editor_id() {
    let bytes = build_plugin(
        &[("Target", 0x0001_1100)],
        &[
            (0x0001_2100, "IronSword", vec![]),
            (0x0001_2101, "SteelAxe", vec![]),
        ],
        &[],
    );
    let world = open_world("anyed", bytes);
    let file = run(&world, "Target = Weapon|*Iron\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_2100));
}

#[test]
fn any_filter_substring_matches_keyword_editor_id() {
    let bytes = build_plugin(
        &[
            ("WeapMaterialIron", 0x0001_1200),
            ("Target", 0x0001_1201),
        ],
        &[
            (0x0001_2200, "Weapon1", vec![0x0001_1200]),
            (0x0001_2201, "Weapon2", vec![]),
        ],
        &[],
    );
    let world = open_world("anykw", bytes);
    // *material substring should match WeapMaterialIron keyword edid on Weapon1
    let file = run(&world, "Target = Weapon|*material\n");
    assert_eq!(file.patches.len(), 1);
    assert_eq!(file.patches[0].target(), FormId(0x0001_2200));
}

#[test]
fn exclusive_group_prevents_second_application() {
    let bytes = build_plugin(
        &[
            ("KwA", 0x0001_1300),
            ("KwB", 0x0001_1301),
        ],
        &[(0x0001_2300, "Sword", vec![])],
        &[],
    );
    let world = open_world("excl", bytes);
    // Two rules, both distributing to the same weapon. ExclusiveGroup
    // puts both keywords in one group → only the first should apply.
    let ini = "ExclusiveGroup = Mats|KwA,KwB\nKwA = Weapon\nKwB = Weapon\n";
    let file = run(&world, ini);
    assert_eq!(file.patches.len(), 1, "exclusive group should limit to 1 patch, got {:?}", file.patches);
    // Either KwA or KwB wins — both are valid (iteration order in
    // the hash map isn't strictly defined). Just assert the set.
    let kw = match file.patches[0] {
        mora_core::Patch::AddKeyword { keyword, .. } => keyword,
    };
    assert!(
        kw == FormId(0x0001_1300) || kw == FormId(0x0001_1301),
        "unexpected winning keyword {kw:?}"
    );
}

#[test]
fn exclusive_group_independent_per_form() {
    // Two weapons, each gets its own independent "first keyword wins" treatment.
    let bytes = build_plugin(
        &[
            ("KwA", 0x0001_1400),
            ("KwB", 0x0001_1401),
        ],
        &[
            (0x0001_2400, "Sword1", vec![]),
            (0x0001_2401, "Sword2", vec![]),
        ],
        &[],
    );
    let world = open_world("exclmulti", bytes);
    let ini = "ExclusiveGroup = Mats|KwA,KwB\nKwA = Weapon\nKwB = Weapon\n";
    let file = run(&world, ini);
    // Each weapon gets exactly 1 keyword.
    assert_eq!(file.patches.len(), 2);
}
EOF
```

- [ ] **Step 2: Run + commit**

```bash
source $HOME/.cargo/env
cargo test --package mora-kid --test filter_activation
cargo xwin check --package mora-kid --target x86_64-pc-windows-msvc --tests
git add crates/mora-kid/tests/filter_activation.rs
git commit -m "mora-kid: integration tests for ALL / ANY / ExclusiveGroup

5 tests against synthetic ESPs:
  - all_filter_requires_every_ref_to_match
  - any_filter_substring_matches_editor_id
  - any_filter_substring_matches_keyword_editor_id
  - exclusive_group_prevents_second_application
  - exclusive_group_independent_per_form
Exercises Plan 8a's filter activations end-to-end."
```

---

## Phase F — Final verification (Task 7)

### Task 7: Full clean verify + push + PR

**Files:** none modified.

- [ ] **Step 1: Clean verification**

```bash
source $HOME/.cargo/env
cargo clean
cargo check --workspace --all-targets
cargo test --workspace --all-targets -- --test-threads=1 2>&1 | grep -E "^test result" | awk '{c+=$4} END {print "TOTAL:", c}'
cargo fmt --check
cargo clippy --workspace --all-targets -- -D warnings
cargo xwin check --target x86_64-pc-windows-msvc --workspace
```

Expected: all green. Test count: 174 (Plan 7) + 1 (parses_exclusive_group) + 1 (all_bucket_records_groups) + 5 (filter_activation.rs) = ~181.

- [ ] **Step 2: Push + PR**

```bash
git push -u origin m3-plan8a-full-filters
gh pr create --base master --head m3-plan8a-full-filters \
    --title "Rust + KID pivot — Plan 8a: activate ALL / ANY / ExclusiveGroup" \
    --body "$(cat <<'PRBODY'
## Summary

Activates three KID features that Plan 6 parsed but left dormant in the
evaluator/distributor:

- **ALL filter bucket (\`+\` prefix)** — every ref across every \`+\`-joined group must match. The AST already captured these; this PR activates the evaluator branch.
- **ANY filter bucket (\`*\` prefix)** — case-insensitive substring match against the item's editor-ID and its keyword editor-IDs. New \`evaluate_with_any()\` extends \`evaluate()\` with the kw_edid_map context that only the distributor can build.
- **ExclusiveGroup** — INI parser now returns \`ParsedIni { rules, exclusive_groups }\` so \`ExclusiveGroup = Name|kw1,kw2,...\` lines are captured. Distributor tracks per-form applied-group state; skips emitting a keyword if another member of its exclusive group has already been applied to the same form. \`DistributorStats\` gains \`rejected_by_exclusive_group\`.

Downstream callers (pipeline::compile + mora-cli compile.rs) updated for the new \`ini::parse_file\` signature.

## Test plan

- [x] \`cargo test --workspace\` — ~181 tests pass (+7 new)
- [x] \`cargo clippy --all-targets -- -D warnings\` clean
- [x] \`cargo fmt --check\` clean
- [x] \`cargo xwin check --target x86_64-pc-windows-msvc --workspace\` clean
- [x] 5 new integration tests exercise ALL / ANY-editor-id / ANY-keyword / ExclusiveGroup / per-form ExclusiveGroup

## Scope discipline

- **No mora-esp or mora-core binary-format changes** beyond the single new field on DistributorStats.
- **ANY does not yet match display name (FULL) or model path (.nif)** — both need mora-esp extensions. Documented as M3-scope.
- **ExclusiveGroup member \`-\` prefix** (NOT-in-group) parsed as regular member with debug log — KID's NOT semantics here are subtle and rarely used.

## What still blocks full KID parity

- **Trait predicates** (anim type, armor type, AR / damage / weight ranges, body slots, -E, -T) — parsed into the AST, evaluator log-and-skips. Requires mora-esp to expose DNAM / BOD2 / EITM / CNAM subrecord fields on WeaponRecord / ArmorRecord. That's **Plan 8b**.
- After 8b, Weapon + Armor rules hit the full KID filter grammar, and we can move to M4 (golden-test harness against real KID).
PRBODY
)"
```

- [ ] **Step 3: Watch CI + hand off**

---

## Completion criteria

- [ ] ~7 new tests pass across mora-kid.
- [ ] `cargo clippy -D warnings` clean.
- [ ] `DistributorStats` prints 6 counters including the new group rejection.
- [ ] PR merged to `master`.

## Next plan

**Plan 8b: trait predicates** — extend mora-esp's WeaponRecord + ArmorRecord with DNAM / BOD2 / EITM / CNAM subrecord parsing (anim type, armor type, enchantment, template, damage/weight/AR ranges, body slots). Activate trait evaluation in mora-kid so `Weapon|||OneHandSword,-E|50`-style rules work against real data.
