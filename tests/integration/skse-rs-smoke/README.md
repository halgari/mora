# skse-rs-smoke

End-to-end smoke test for the `skse-rs` Rust SKSE framework.

**Invariant:** after Skyrim reaches main menu, `SkseRsSmoke.log` must
contain, in order:

1. `Hello from skse-rs` (plugin loaded + log opened)
2. `SKSE runtime: 0x<hex>` (SKSEInterface read successfully)
3. `Address Library loaded` (version-lib bin parsed)
4. `kDataLoaded received` (messaging callback fired)
5. `Iron Sword lookup: 0x00012EB7 -> <ptr>` (TESForm::lookup_by_id hit)
6. `WeapMaterialIron lookup: 0x0001E718 -> <ptr>`
7. `AddKeyword result: <outcome>` (add_keyword returned Ok)
8. `verify readback: num_keywords = <n>`
9. `smoke OK`

All skse-rs subsystems are exercised: the full ABI layer,
logging, address library parsing, relocation resolution, messaging
listener registration, form lookup through the `allForms` hash map,
and keyword-array mutation via Skyrim's MemoryManager.

Run the case locally via `run-skyrim-test.sh` or on the self-hosted
runners. CI gate is enabled by the presence of `rust-ready.marker`
in this directory.
