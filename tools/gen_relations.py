#!/usr/bin/env python3
"""Generate src/model/relations_seed.cpp from data/relations/*.yaml.

Relation metadata lives in YAML as the source of truth. This script emits
the constexpr C++ table consumed by the compiler.
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
OUT_FILE = ROOT / "src" / "model" / "relations_seed.cpp"

TYPE_RE = re.compile(r'^(scalar|countable|list|const|predicate)(?:<(\w+)>)?$')

def parse_type(s):
    """Parse 'list<FormRef>' -> ('List', 'FormRef'); 'predicate' -> ('Predicate', 'Int')."""
    m = TYPE_RE.match(s)
    if not m:
        raise ValueError(f"bad type: {s!r}")
    ctor = m.group(1)
    elem = m.group(2) or "Int"
    ctor_map = {'scalar':'Scalar','countable':'Countable','list':'List','const':'Const','predicate':'Predicate'}
    return ctor_map[ctor], elem

def fmt_args(args):
    parts = []
    for a in args:
        parts.append(f'{{ElemType::{a["type"]}, "{a["name"]}"}}')
    return "{" + ", ".join(parts) + "}"

def fmt_source(rel):
    src = rel.get("source", "static")
    m = {"static":"Static","handler":"Handler","hook":"Hook","event":"Event","memory":"MemoryRead"}
    return f"RelationSourceKind::{m[src]}"

EXTRACT_MAP = {
    "existence": "Existence",
    "subrecord": "Subrecord",
    "packed_field": "PackedField",
    "array_field": "ArrayField",
    "list_field": "ListField",
    "bit_test": "BitTest",
}

READ_MAP = {
    "int8":    "Int8",    "int16":  "Int16",  "int32":  "Int32",
    "uint8":   "UInt8",   "uint16": "UInt16", "uint32": "UInt32",
    "float32": "Float32", "formid": "FormID",
    "zstring": "ZString", "lstring": "LString",
}

def fmt_esp(esp):
    parts = [
        f'.record_type = "{esp.get("record_type", "")}"',
        f'.subrecord = "{esp.get("subrecord", "")}"',
    ]
    if "extract" in esp:
        extract = esp["extract"]
        if extract not in EXTRACT_MAP:
            raise ValueError(f"bad extract kind: {extract!r}")
        parts.append(f'.extract = EspExtract::{EXTRACT_MAP[extract]}')
    if "offset" in esp:
        parts.append(f'.offset = {esp["offset"]}')
    if "element_size" in esp:
        parts.append(f'.element_size = {esp["element_size"]}')
    if "bit" in esp:
        parts.append(f'.bit = {esp["bit"]}')
    if "read_as" in esp:
        rt = esp["read_as"]
        if rt not in READ_MAP:
            raise ValueError(f"bad read_as: {rt!r}")
        parts.append(f'.read_as = EspReadType::{READ_MAP[rt]}')
    return "{" + ", ".join(parts) + "}"

def fmt_hook(h):
    name = h["name"]
    kind = h.get("kind", "edge").capitalize()
    return f'{{.hook_name = "{name}", .kind = HookSpec::Kind::{kind}}}'

def emit_entry(namespace, rname, rel, out):
    ctor, elem = parse_type(rel["type"])
    args = rel.get("args", [])
    out.append(f"    {{")
    out.append(f'        .namespace_ = "{namespace}",')
    out.append(f'        .name = "{rname}",')
    if args:
        out.append(f'        .args = {fmt_args(args)},')
    out.append(f'        .arg_count = {len(args)},')
    out.append(f'        .type = {{TypeCtor::{ctor}, ElemType::{elem}}},')
    out.append(f'        .source = {fmt_source(rel)},')
    if "esp" in rel:
        out.append(f'        .esp_source = {fmt_esp(rel["esp"])},')
    if "hook" in rel:
        out.append(f'        .hook = {fmt_hook(rel["hook"])},')
    if "apply_handler" in rel:
        out.append(f'        .apply_handler = HandlerId::{rel["apply_handler"]},')
    if "retract_handler" in rel:
        out.append(f'        .retract_handler = HandlerId::{rel["retract_handler"]},')
    docs = rel.get("docs", "").replace('"', '\\"')
    out.append(f'        .docs = "{docs}",')
    out.append(f"    }},")

def main():
    # Deterministic order: form, ref, player, world, event (matches prior hand-authored order)
    PREFERRED_ORDER = ["form", "ref", "player", "world", "event"]
    files = list(RELATIONS_DIR.glob("*.yaml"))
    if not files:
        print(f"No YAML files in {RELATIONS_DIR}", file=sys.stderr)
        sys.exit(1)

    def ns_key(p):
        name = p.stem
        return (PREFERRED_ORDER.index(name) if name in PREFERRED_ORDER else 999, name)
    files.sort(key=ns_key)

    lines = [
        "// GENERATED FILE — DO NOT EDIT BY HAND.",
        "// Source: data/relations/*.yaml",
        "// Regenerate with: python3 tools/gen_relations.py",
        "",
        '#include "mora/model/relations.h"',
        '#include "mora/model/validate.h"',
        "",
        "namespace mora::model {",
        "",
        "constexpr RelationEntry kRelations[] = {",
    ]
    total = 0
    for f in files:
        with open(f) as fh:
            data = yaml.safe_load(fh)
        namespace = data["namespace"]
        lines.append(f"    // ── {namespace}/* ──")
        for rname, rel in data.get("relations", {}).items():
            emit_entry(namespace, rname, rel, lines)
            total += 1
    lines.append("};")
    lines.append("")
    lines.append("const size_t kRelationCount = sizeof(kRelations) / sizeof(kRelations[0]);")
    lines.append("")
    lines.append("static_assert(validate_all(kRelations, sizeof(kRelations) / sizeof(kRelations[0])),")
    lines.append('              "kRelations fails validation — see helper checks");')
    lines.append("")
    lines.append("} // namespace mora::model")
    lines.append("")

    OUT_FILE.write_text("\n".join(lines))
    print(f"wrote {OUT_FILE} ({len(files)} YAML files, {total} relations emitted)")

if __name__ == "__main__":
    main()
