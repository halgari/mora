# Relation definitions

These YAML files are the **source of truth** for the builtin relation catalog
(`form/*`, `ref/*`, `player/*`, `world/*`, `event/*`).

## Workflow

1. Edit a YAML file in this directory.
2. Run the generator (xmake does this automatically before a build when any
   YAML file is newer than the generated output):

   ```
   python3 tools/gen_relations.py
   ```

3. The generator writes `src/model/relations_seed.cpp`, which is committed
   to the repo for PR reviewability.

## Schema

```yaml
namespace: <ns>          # required
relations:
  <name>:
    type: <ctor>[<Elem>] # required. Ctor: scalar|countable|list|const|predicate
                         # Elem (omitted for predicate): Int|Float|String|FormRef|Keyword|RefId
    args:                # optional for predicate
      - {name: N, type: FormRef}
      - ...
    source: static|handler|hook|event|memory
    esp:     {record_type: NPC_, subrecord: KWDA}   # source=static
    hook:    {name: OnX, kind: edge|state}          # source=hook|event
    apply_handler:   HandlerId                       # source=handler
    retract_handler: HandlerId                       # source=handler (optional)
    docs: "..."
```

## Constructor → verbs

| ctor       | writable | legal verbs       |
|------------|----------|-------------------|
| scalar     | yes      | set               |
| countable  | yes      | set, add, sub     |
| list       | yes      | add, remove       |
| const      | no       | (read-only)       |
| predicate  | no       | (read-only)       |
