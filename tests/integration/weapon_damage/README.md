# Case: weapon_damage

**Invariant.** `form/weapon(W) => set form/damage(W, 99)` applies to every
Skyrim.esm weapon form (formid high byte = 0x00) — no exclusions, no
misses within that plugin.

Exercises: scalar<Int> patching end-to-end through ESP load → static
patch compilation → runtime patch application → harness readback.

## Why Skyrim.esm only?

At the time this test was added, compile-time and runtime load-order
indices agree only for Skyrim.esm; CC/DLC weapons (formids
`0x01xxxxxx`+) show a shift. That's a separate bug — this test scopes
its assertion to the set that's actually deterministic.

## Why not Nazeem?

The original design called for `form/npc(N) => set form/name(N, "Nazeem")`
but `mora compile` currently drops string-valued patches at the
PatchSet → PatchBuffer conversion (`src/main.cpp`, the emit switch has no
`Kind::String` case even though `PatchValueType::StringIndex = 3` is
reserved in the binary format). Once string-patch emission lands, add a
`tests/integration/nazeem/` case alongside this one.
