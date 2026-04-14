#!/usr/bin/env python3
"""Generate docs/src/relations.md from data/relations/**/*.yaml.

Produces a structured relation reference: one section per namespace, one
subsection per relation, showing signature, derived verb set, source, and
the relation's docs string. The YAML is the single source of truth for
both the compiler (via tools/gen_relations.py) and this document.
"""
import re
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("PyYAML required: pip install pyyaml", file=sys.stderr)
    sys.exit(1)

ROOT = Path(__file__).parent.parent
RELATIONS_DIR = ROOT / "data" / "relations"
OUT_FILE = ROOT / "docs" / "src" / "relations.md"

NAMESPACE_ORDER = ["form", "ref", "player", "world", "event"]

NAMESPACE_BLURB = {
    "form":   "Static record data extracted from ESP/ESM files at compile time.",
    "ref":    "Dynamic data about placed references (live world instances).",
    "player": "Player-specific state and effects.",
    "world":  "Global game state (time, weather, difficulty).",
    "event":  "Edge-triggered inputs. Only legal in `on` rules.",
}

# Verbs implied by each type constructor. Must mirror kCtorTable in
# include/mora/model/relation_types.h.
CTOR_VERBS = {
    "scalar":    ["set"],
    "countable": ["set", "add", "sub"],
    "list":      ["add", "remove"],
    "const":     [],
    "predicate": [],
}

TYPE_RE = re.compile(r'^(scalar|countable|list|const|predicate)(?:<(\w+)>)?$')

def parse_type(s):
    m = TYPE_RE.match(s)
    if not m:
        raise ValueError(f"bad type: {s!r}")
    return m.group(1), (m.group(2) or None)

def format_type(ctor, elem):
    return f"`{ctor}<{elem}>`" if elem else f"`{ctor}`"

def format_args(args):
    """'F: FormRef, KW: FormRef'."""
    return ", ".join(f"{a['name']}: {a['type']}" for a in args)

def format_verbs(ctor):
    verbs = CTOR_VERBS.get(ctor, [])
    if not verbs:
        return "*(read-only; body-position only)*"
    return ", ".join(f"`{v}`" for v in verbs)

def format_source(rel):
    src = rel.get("source", "static")
    esp = rel.get("esp")
    hook = rel.get("hook")
    apply_h = rel.get("apply_handler")
    retract_h = rel.get("retract_handler")

    lines = []
    if src == "static":
        lines.append("Extracted from ESP data at compile time.")
        if esp:
            rt = esp.get("record_type", "")
            sr = esp.get("subrecord", "")
            extract = esp.get("extract")
            bits = []
            if rt: bits.append(f"record `{rt}`")
            if sr: bits.append(f"subrecord `{sr}`")
            if bits:
                lines.append("From " + ", ".join(bits) + ".")
            if extract:
                detail = f"Extraction: `{extract}`"
                if "offset" in esp: detail += f", offset `{esp['offset']}`"
                if "element_size" in esp: detail += f", element size `{esp['element_size']}`"
                if "bit" in esp: detail += f", bit `{esp['bit']}`"
                if "read_as" in esp: detail += f", read as `{esp['read_as']}`"
                detail += "."
                lines.append(detail)
    elif src == "handler":
        lines.append("Runtime handler-dispatched.")
        if apply_h: lines.append(f"Apply handler: `{apply_h}`.")
        if retract_h: lines.append(f"Retract handler: `{retract_h}`.")
    elif src == "hook":
        lines.append("Maintained via an SKSE hook.")
        if hook: lines.append(f"Hook: `{hook.get('name','?')}` ({hook.get('kind','edge')}).")
    elif src == "event":
        lines.append("Edge-triggered SKSE event.")
        if hook: lines.append(f"Hook: `{hook.get('name','?')}`.")
    elif src == "memory":
        lines.append("Read from in-memory form data.")
    return lines

def emit_relation(ns, name, rel, out):
    ctor, elem = parse_type(rel["type"])
    args = rel.get("args", [])
    sig = f"`{ns}/{name}({format_args(args)})`"
    out.append(f"### {sig}")
    out.append("")
    out.append(f"**Type:** {format_type(ctor, elem)}  ")
    out.append(f"**Verbs:** {format_verbs(ctor)}")
    out.append("")

    docs = rel.get("docs", "").strip()
    if docs:
        out.append(docs)
        out.append("")

    for line in format_source(rel):
        out.append(line)
    out.append("")

def main():
    files = sorted(RELATIONS_DIR.rglob("*.yaml"))
    if not files:
        print(f"No YAML files in {RELATIONS_DIR}", file=sys.stderr)
        sys.exit(1)

    by_ns = {}
    for f in files:
        with open(f) as fh:
            data = yaml.safe_load(fh) or {}
        ns = data.get("namespace")
        if not ns:
            raise ValueError(f"{f}: missing 'namespace' key")
        for rname, rel in (data.get("relations") or {}).items():
            by_ns.setdefault(ns, []).append((f, rname, rel))

    def ns_rank(ns):
        return (NAMESPACE_ORDER.index(ns) if ns in NAMESPACE_ORDER else 999, ns)

    lines = [
        "# Relation Reference",
        "",
        "> **Auto-generated** from `data/relations/**/*.yaml`.",
        "> Regenerate with `python3 tools/gen_docs.py`.",
        "> Do not edit this file by hand — changes will be overwritten.",
        "",
        "Relations are the core vocabulary of Mora rules. Each is declared under",
        "a namespace (`form/`, `ref/`, `player/`, `world/`, `event/`) and carries a",
        "type whose constructor determines which verbs are legal in head position.",
        "",
        "## Verb rules",
        "",
        "| Type constructor | Legal verbs | Example |",
        "|------------------|-------------|---------|",
        "| `scalar<T>`       | `set` | `=> set form/name(F, \"Nazeem\")` |",
        "| `countable<T>`    | `set`, `add`, `sub` | `=> add player/gold(P, 100)` |",
        "| `list<T>`         | `add`, `remove` | `=> add form/keyword(W, @Enchanted)` |",
        "| `const<T>`        | *(none — read-only)* | used only in body position |",
        "| `predicate`       | *(none — unary existence)* | `form/npc(F)` |",
        "",
        "## Namespaces",
        "",
    ]
    for ns in sorted(by_ns.keys(), key=ns_rank):
        lines.append(f"- [`{ns}/*`](#{ns}) — {NAMESPACE_BLURB.get(ns, '')}")
    lines.append("")
    lines.append("---")
    lines.append("")

    for ns in sorted(by_ns.keys(), key=ns_rank):
        lines.append(f'<a id="{ns}"></a>')
        lines.append(f"## `{ns}/*`")
        lines.append("")
        lines.append(NAMESPACE_BLURB.get(ns, ""))
        lines.append("")
        last_file = None
        for src_file, rname, rel in by_ns[ns]:
            if src_file != last_file:
                rel_path = src_file.relative_to(ROOT)
                lines.append(f"*Source: `{rel_path}`*")
                lines.append("")
                last_file = src_file
            emit_relation(ns, rname, rel, lines)
        lines.append("---")
        lines.append("")

    OUT_FILE.write_text("\n".join(lines))
    total = sum(len(v) for v in by_ns.values())
    print(f"wrote {OUT_FILE} ({len(files)} YAML files, {total} relations documented)")

if __name__ == "__main__":
    main()
