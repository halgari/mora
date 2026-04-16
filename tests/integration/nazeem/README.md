# Case: nazeem

**Invariant.** `form/npc(NPC) => set form/name(NPC, "Nazeem")` applies to
every NPC form in the loaded load order — no exclusions, no misses.

Exercises: name-field patching end-to-end through ESP load → static
patch compilation → runtime patch application → harness readback.
