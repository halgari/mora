//! `plugins.txt` parser.
//!
//! Standard Bethesda plugins.txt format:
//!   - One plugin filename per line
//!   - Lines starting with `#` are comments
//!   - Blank lines ignored
//!   - Lines starting with `*` mark ACTIVE plugins
//!   - Filenames are case-insensitive (matched against Data/ directory)
//!   - CRLF or LF line endings

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PluginEntry {
    pub name: String,
    pub active: bool,
}

/// Parse a plugins.txt buffer into a list of entries, preserving
/// file order.
pub fn parse(content: &str) -> Vec<PluginEntry> {
    let mut entries = Vec::new();
    for line in content.lines() {
        let line = line.trim_end_matches('\r').trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let (active, name) = if let Some(rest) = line.strip_prefix('*') {
            (true, rest.trim())
        } else {
            (false, line)
        };
        if name.is_empty() {
            continue;
        }
        entries.push(PluginEntry {
            name: name.to_string(),
            active,
        });
    }
    entries
}

/// Filter to only active entries.
pub fn active_plugins(entries: &[PluginEntry]) -> Vec<&PluginEntry> {
    entries.iter().filter(|e| e.active).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_simple() {
        let s = "# comment\n*Skyrim.esm\n*MyMod.esp\nInactive.esp\n";
        let e = parse(s);
        assert_eq!(e.len(), 3);
        assert_eq!(e[0].name, "Skyrim.esm");
        assert!(e[0].active);
        assert_eq!(e[1].name, "MyMod.esp");
        assert!(e[1].active);
        assert_eq!(e[2].name, "Inactive.esp");
        assert!(!e[2].active);
    }

    #[test]
    fn handles_crlf_line_endings() {
        let s = "*A.esm\r\n*B.esp\r\n";
        let e = parse(s);
        assert_eq!(e.len(), 2);
        assert_eq!(e[0].name, "A.esm");
        assert_eq!(e[1].name, "B.esp");
    }

    #[test]
    fn skips_blank_lines_and_comments() {
        let s = "\n# foo\n\n*A.esp\n  # bar\n";
        let e = parse(s);
        assert_eq!(e.len(), 1);
    }

    #[test]
    fn active_plugins_filter() {
        let entries = parse("*A.esp\nB.esp\n*C.esp\n");
        let active = active_plugins(&entries);
        assert_eq!(active.len(), 2);
        assert_eq!(active[0].name, "A.esp");
        assert_eq!(active[1].name, "C.esp");
    }
}
