# Case: weapon_damage

**Invariant.** `form/weapon(W) => set form/damage(W, 99)` applies to every
weapon form in the loaded load order — no exclusions, no misses.

Exercises: scalar<Int> patching end-to-end through ESP load → static
patch compilation → runtime patch application → harness readback.

## Why not Nazeem?

The original design called for `form/npc(N) => set form/name(N, "Nazeem")`
but `mora compile` currently drops string-valued patches at the
PatchSet → PatchBuffer conversion (`src/main.cpp`, the emit switch has no
`Kind::String` case even though `PatchValueType::StringIndex = 3` is
reserved in the binary format). Once string-patch emission lands, add a
`tests/integration/nazeem/` case alongside this one.
