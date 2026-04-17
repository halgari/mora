# Case: nazeem

**Invariant.** `form/npc(N) => set form/name(N, "Nazeem")` applies to every
Skyrim.esm NPC form (formid high byte = 0x00) — the TESFullName component
on every base actor ends up as the string `Nazeem`.

Exercises: scalar<String> patching end-to-end through ESP load → static
patch compilation → StringTable emission → runtime apply_name via
BSFixedString. Regression test for #4 (PatchSet → PatchBuffer conversion
previously dropped String-valued patches).

## Why Skyrim.esm only?

Same reason as `weapon_damage`: compile-time and runtime load-order
indices only agree for Skyrim.esm right now. CC/DLC NPCs (formid high
byte ≠ 0x00) show a shift that this case doesn't try to cover.
