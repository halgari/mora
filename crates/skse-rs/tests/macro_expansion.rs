//! Compile-only test for the `declare_plugin!` macro expansion.
//!
//! We can't usefully *run* the expanded plugin exports — they assume
//! a live SKSE host. But we can verify the macro expands without
//! compile errors, which proves the generated C-ABI items are
//! well-formed.

// The stub plugin itself. No `on_load` body — we won't run it.
struct CompileOnlyPlugin;

impl skse_rs::SksePlugin for CompileOnlyPlugin {
    const NAME: &'static str = "CompileOnlyPlugin";
    const VERSION: skse_rs::PluginVersion = skse_rs::PluginVersion {
        major: 0,
        minor: 1,
        patch: 0,
        build: 0,
    };
    unsafe fn on_load(_skse: &'static skse_rs::ffi::SKSEInterface) -> skse_rs::LoadOutcome {
        Ok(())
    }
}

skse_rs::declare_plugin!(CompileOnlyPlugin);

#[test]
fn macro_expands_successfully() {
    // The generated SKSEPlugin_Version is a `pub static` at the test
    // file's module scope — accessible directly.
    let name_bytes = &SKSEPlugin_Version.plugin_name;
    // First 17 bytes should be "CompileOnlyPlugin"
    assert_eq!(&name_bytes[..17], b"CompileOnlyPlugin");
    // Byte 17 should be NUL
    assert_eq!(name_bytes[17], 0);
    // data_version should be 1
    assert_eq!(SKSEPlugin_Version.data_version, 1);
}
