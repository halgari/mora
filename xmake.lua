set_project("mora")
set_version("0.3.0")

set_languages("c++20")
-- Linux GCC + macOS Clang honor our "error" setting cleanly. clang-cl
-- in MSVC-compat mode is stricter than MSVC proper for a few warnings
-- (unused private fields, unused-but-set vars, missing field inits)
-- that don't indicate real bugs — with /WX those become hard errors
-- on Windows but pass on Linux CI. Relax the /WX on windows only.
if is_plat("windows") then
    set_warnings("all")
else
    set_warnings("all", "error")
end

-- ══════════════════════════════════════════════════════════════
-- Custom toolchain: clang-cl + lld-link + llvm-lib targeting
-- x86_64-pc-windows-msvc using the xwin-provided Windows SDK /
-- CRT at $XWIN_PATH (default: $HOME/.xwin). Lets us cross-compile
-- Windows binaries from Linux without wine or MSVC.
--
-- Activate with:
--   xmake f -p windows -a x64 --toolchain=xwin-clang-cl
-- ══════════════════════════════════════════════════════════════
toolchain("xwin-clang-cl")
    set_kind("standalone")
    set_description("clang-cl + lld-link + llvm-lib via xwin sysroot")
    -- Match the builtin clang-cl toolchain's runtime set so xmake's
    -- release/debug modes resolve /MD /MT correctly without a VS install.
    set_runtimes("MT", "MTd", "MD", "MDd")

    set_toolset("cc",  "clang-cl")
    set_toolset("cxx", "clang-cl")
    set_toolset("ld",  "lld-link")
    set_toolset("sh",  "lld-link")
    -- lld-link doubles as a static-archiver via `-lib`; xmake's `link.lua`
    -- driver emits that flag for `targetkind == "static"`. llvm-lib would
    -- be the direct lib.exe drop-in but xmake has no matching tool driver
    -- for it.
    set_toolset("ar",  "lld-link")

    on_check(function (toolchain)
        -- Avoid xmake's find_tool driver — it doesn't know about these
        -- LLVM binaries by default. A plain `which` is enough.
        return os.iorunv("which", {"clang-cl"}) ~= nil and
               os.iorunv("which", {"lld-link"}) ~= nil and
               os.iorunv("which", {"llvm-lib"}) ~= nil
    end)

    on_load(function (toolchain)
        local xwin = os.getenv("XWIN_PATH") or (os.getenv("HOME") .. "/.xwin")
        -- Pass via -imsvc rather than `sysincludedirs` (which xmake's
        -- cl.lua translates to `-external:I`). clang-cl in MSVC-compat
        -- mode needs -imsvc for the xwin sysroot so its own resource-dir
        -- intrinsic headers (xmmintrin.h et al.) stay ahead of the MSVC
        -- copies in the include search order. With -external:I, the MSVC
        -- versions win and emit extern declarations like
        -- `extern _mm_set_ps1` without inline bodies, leaving unresolved
        -- symbols at link time.
        --
        -- The flag and path must be fused (no separator) because xmake
        -- de-duplicates identical tokens in its command-line assembly;
        -- passing `-imsvc` as a standalone token leaves only the first
        -- copy and makes the subsequent paths read as source-file args.
        local imsvc_flags = {}
        for _, d in ipairs({"crt/include", "sdk/include/ucrt",
                            "sdk/include/um", "sdk/include/shared"}) do
            table.insert(imsvc_flags, "-imsvc" .. path.join(xwin, d))
        end
        toolchain:add("cxflags", table.unpack(imsvc_flags))
        toolchain:add("cflags",  table.unpack(imsvc_flags))
        toolchain:add("linkdirs",
            path.join(xwin, "crt/lib/x86_64"),
            path.join(xwin, "sdk/lib/um/x86_64"),
            path.join(xwin, "sdk/lib/ucrt/x86_64"))
        toolchain:add("cxflags", "--target=x86_64-pc-windows-msvc", "/EHsc")
        toolchain:add("cflags",  "--target=x86_64-pc-windows-msvc")
        toolchain:add("ldflags", "/machine:x64")
        toolchain:add("shflags", "/machine:x64")
    end)
toolchain_end()

if is_plat("windows") then
    -- fmt (base.h) static_asserts unless this is set on MSVC, and it
    -- must apply to every TU that includes a fmt header.
    add_cxflags("/utf-8", {force = true})
    -- Quiet MSVC's C4996 "unsafe" CRT deprecations (getenv, etc.).
    -- We build with /WX, so leaving them hot turns warnings into errors.
    add_defines("_CRT_SECURE_NO_WARNINGS")

    -- Statically link the entire MSVC CRT (libcmt.lib + libcpmt.lib +
    -- libvcruntime.lib + libucrt.lib) into every Windows target. Results
    -- in binaries that have zero dependency on msvcp140.dll /
    -- vcruntime140.dll / api-ms-win-crt-*.dll — they import only
    -- kernel32.dll and the Windows SDK DLLs they use directly. Also
    -- sidesteps the stale-msvcp140-in-Wine-prefix compatibility trap
    -- (see docs/src/cross-compile-windows.md, trap #3) entirely:
    -- with the CRT bundled, no external msvcp140 lookup happens.
    set_runtimes("MT")

    -- Disable mode.release's default strip=all. xmake's `link.lua` turns
    -- strip=all into `/opt:ref /opt:icf` via `nf_strip`, which lld-link
    -- rejects when invoked in `-lib` archive mode. The flags apply to
    -- every link in the build — including xrepo packages (zlib et al.)
    -- that pull in `mode.release` and end up archiving with the leaked
    -- linker flags. We don't need strip for a release build anyway; lld
    -- already omits debug info without /DEBUG.
    set_policy("build.release.strip", false)
end

-- ══════════════════════════════════════════════════════════════
-- CLI compiler (Linux + Windows)
-- ══════════════════════════════════════════════════════════════
-- When cross-compiling to windows via xwin-clang-cl, every xmake package
-- (zlib/fmt/nlohmann_json/spdlog/directxmath/directxtk) also needs to
-- build under the same toolchain — otherwise xmake tries to detect MSVC
-- and fails. Apply it globally at requires-time.
if is_plat("windows") then
    set_toolchains("xwin-clang-cl")
    add_requireconfs("*", {configs = {toolchains = "xwin-clang-cl"}})
    -- fmt's CMake build expects a real Visual Studio install to drive
    -- its "Visual Studio" generator. Skip it — header-only is fine for
    -- how the Mora CLI uses fmt, and it removes one full CMake build
    -- from the critical path.
    add_requires("fmt", {configs = {header_only = true}})
else
    add_requires("fmt")
end

-- zlib is vendored in extern/zlib on Windows (built as a local static-lib
-- target below). On Linux we still use xmake's packaged version — the
-- xrepo zlib's archive step ends up with leaked /opt:ref /opt:icf flags
-- under our static-CRT + lld-link setup, which lld-link rejects in -lib
-- mode. Vendoring sidesteps the xmake package builder's linker config.
if is_plat("windows") then
    -- satisfied by the in-tree `zlib` target
else
    add_requires("zlib")
end
add_requires("nlohmann_json")

-- Vendored zlib (extern/zlib submodule, v1.3.1). Only included on
-- windows to avoid the xrepo-zlib + static-CRT + lld-link collision
-- (see "zlib" add_requires block above for details). On linux the
-- xmake-packaged zlib still wins.
if is_plat("windows") then
    target("zlib")
        set_kind("static")
        add_files("extern/zlib/*.c|example.c|minigzip.c|gzappend.c|gzjoin.c|gzlog.c")
        add_includedirs("extern/zlib", {public = true})
        add_defines("_CRT_SECURE_NO_DEPRECATE", "_CRT_NONSTDC_NO_DEPRECATE")
        set_warnings("none")
    target_end()
end

-- Static library with all compiler sources
target("mora_lib")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp",
              "src/data/*.cpp", "src/esp/*.cpp",
              "src/codegen/*.cpp", "src/rt/*.cpp", "src/rt/handlers/*.cpp",
              "src/harness/*.cpp",
              "src/model/*.cpp", "src/dag/*.cpp",
              "src/lsp/*.cpp", "src/lsp/handlers/*.cpp")
    -- Runtime files that touch CommonLibSSE-NG guard their bodies on
    -- MORA_WITH_COMMONLIB. The CLI doesn't link CommonLib, so leave
    -- that undefined here — guarded blocks compile as empty stubs.
    if is_plat("windows") then
        add_deps("zlib")
        add_packages("fmt", "nlohmann_json", {public = true})
    else
        add_packages("zlib", "fmt", "nlohmann_json", {public = true})
    end
    -- Regenerate src/model/relations_seed.cpp and docs/src/relations.md from
    -- data/relations/**/*.yaml whenever any source YAML is newer than its
    -- generated output.
    before_build(function (target)
        import("core.project.project")
        local yamls = os.files(path.join(os.projectdir(), "data/relations/**/*.yaml"))
        local function newest_yaml_mtime()
            local m = 0
            for _, y in ipairs(yamls) do
                if os.mtime(y) > m then m = os.mtime(y) end
            end
            return m
        end
        local newest = newest_yaml_mtime()
        local py = is_host("windows") and "python" or "python3"
        local targets = {
            {out = "src/model/relations_seed.cpp", gen = "tools/gen_relations.py"},
            {out = "docs/src/relations.md",        gen = "tools/gen_docs.py"},
        }
        for _, t in ipairs(targets) do
            local out_path = path.join(os.projectdir(), t.out)
            local out_mtime = os.exists(out_path) and os.mtime(out_path) or 0
            if newest > out_mtime then
                print("regenerating " .. t.out .. " from data/relations/**/*.yaml")
                os.vrunv(py, {path.join(os.projectdir(), t.gen)})
            end
        end
    end)
target_end()

-- CLI executable
target("mora")
    set_kind("binary")
    add_files("src/main.cpp")
    add_deps("mora_lib")
target_end()

-- ══════════════════════════════════════════════════════════════
-- Tests (Linux only — rely on POSIX-specific gtest setup)
-- ══════════════════════════════════════════════════════════════
if not is_plat("windows") then

add_requires("gtest")

for _, testfile in ipairs(os.files("tests/*_test.cpp")) do
    local name = path.basename(testfile)
    target(name)
        set_kind("binary")
        set_default(false)
        add_files(testfile)
        add_deps("mora_lib")
        add_packages("gtest")
        add_syslinks("gtest_main")
        add_tests(name)
    target_end()
end

-- Tests under subdirectories (tests/<group>/test_*.cpp)
for _, testfile in ipairs(os.files("tests/**/test_*.cpp")) do
    local name = path.basename(testfile)
    target(name)
        set_kind("binary")
        set_default(false)
        add_files(testfile)
        add_deps("mora_lib")
        add_packages("gtest")
        add_syslinks("gtest_main")
        add_tests(name)
    target_end()
end

end -- not windows

-- ══════════════════════════════════════════════════════════════
-- Windows: SKSE Runtime DLL + test harness (cross-compile)
--
-- We can't `includes("extern/CommonLibSSE-NG")` to reuse its xmake
-- target: the upstream pulls spdlog/directxmath/directxtk via the
-- xmake package manager, and those packages' on_install uses
-- `package.tools.cmake` which hardcodes the "Visual Studio"
-- generator on windows (xmake/.../modules/package/tools/cmake.lua
-- line 754-756 — no hook to override from the consumer side). That
-- generator needs a real MSVC install, which defeats our goal of a
-- wine-free cross-compile from Linux.
--
-- Instead: compile CommonLibSSE-NG's sources in-tree with our own
-- target. We bring our own spdlog/fmt via `extern/spdlog-shim`
-- (symlinks to system-installed /usr/include/{spdlog,fmt} on dev
-- boxes; materialized from upstream clones in CI). DirectXMath.h
-- comes from the xwin Windows SDK; DirectXTK's SimpleMath is
-- replaced by a POD shim in `extern/simplemath-shim` since Mora
-- never actually calls its methods.
-- ══════════════════════════════════════════════════════════════
if is_plat("windows") then

target("commonlibsse_ng")
    set_kind("static")
    set_languages("c++23")
    -- Required for SSE intrinsics (_mm_set_ps1, _mm_shuffle_ps, etc.)
    -- to inline rather than emit undefined external references — the
    -- upstream release build assumes /O2 semantics, and without any
    -- optimization clang-cl falls back to expecting CRT-provided bodies
    -- for the SSE shims that don't exist.
    set_optimize("fastest")

    add_files("extern/CommonLibSSE-NG/src/**.cpp")

    -- These flow to every target that add_deps("commonlibsse_ng")
    add_includedirs("extern/CommonLibSSE-NG/include",
                    "extern/spdlog-shim",
                    "extern/simplemath-shim",
                    {public = true})

    add_defines("ENABLE_SKYRIM_SE=1", "ENABLE_SKYRIM_AE=1",
                "SPDLOG_COMPILED_LIB", "SPDLOG_FMT_EXTERNAL",
                {public = true})
    add_defines("WIN32", "_WINDOWS", "NOMINMAX")

    -- Force-include the PCH header rather than /Yu'ing a precompiled
    -- binary. clang-cl + /Yu truncates some header bodies inside the
    -- PCH — in particular, SSE intrinsic inline definitions get lost,
    -- leaving unresolved externs like _mm_set_ps1 at link time. /FI
    -- re-parses the header per TU (slower, but a clean 14s full build
    -- is already fast enough) and keeps all inlines visible.
    add_cxflags("/FISKSE/Impl/PCH.h", {force = true})

    -- Upstream code triggers a mountain of warnings under clang-cl.
    -- We're treating the SKSE submodule as third-party binary-input:
    -- turn all warnings off for this TU only (our own code below
    -- still inherits project-level /WX).
    add_cxflags("/w",
                "/bigobj",
                "/utf-8",
                "/permissive-",
                "/Zc:preprocessor",
                {force = true})

    add_syslinks("advapi32", "bcrypt", "d3d11", "d3dcompiler",
                 "dbghelp", "dxgi", "ole32", "shell32", "user32",
                 "version")
target_end()

-- spdlog compiled-library TUs from the shim. Splitting these out
-- keeps the heavy PCH off their compile and makes the intent clear:
-- these two files exist solely to satisfy SPDLOG_COMPILED_LIB's
-- one-definition-rule needs.
target("spdlog_rt")
    set_kind("static")
    set_languages("c++23")
    add_files("extern/spdlog-shim/spdlog.cpp",
              "extern/spdlog-shim/fmt.cpp")
    add_includedirs("extern/spdlog-shim", {public = true})
    add_defines("SPDLOG_COMPILED_LIB", "SPDLOG_FMT_EXTERNAL",
                {public = true})
    add_cxflags("/w", "/utf-8", {force = true})
target_end()

target("mora_runtime")
    set_kind("shared")
    set_filename("MoraRuntime.dll")
    set_languages("c++23")
    add_deps("commonlibsse_ng", "spdlog_rt")

    -- mora_runtime TUs transitively include CommonLibSSE-NG headers,
    -- which trip a number of clang-cl warnings (noreturn returning,
    -- tautological compare, etc.) that upstream knowingly builds with
    -- suppressed. Our project default is /W3 /WX — relax it for this
    -- target so we don't fight third-party idioms.
    set_warnings("none")

    -- Shared Mora core code (subset — no ESP parser, no CLI, no lexer/parser).
    -- Exclusions, matching the legacy build_rt_lib.sh:
    --   * form_model_verify.cpp: compile-time offset sanity check using
    --     constexpr reinterpret_cast, which the standard disallows.
    --   * diag/renderer.cpp: pulls in CLI/terminal-rendering code the DLL
    --     doesn't need.
    add_files("src/core/*.cpp", "src/data/*.cpp",
              "src/diag/*.cpp|renderer.cpp",
              "src/eval/*.cpp", "src/emit/*.cpp")
    add_files("src/rt/*.cpp|form_model_verify.cpp", "src/rt/handlers/*.cpp")
    add_files("src/dag/*.cpp", "src/model/*.cpp")
    add_includedirs("include", {public = true})

    add_defines("SKYRIMSE", "MORA_WITH_COMMONLIB")
    add_defines("WIN32", "_WINDOWS", "NOMINMAX")
    -- /FI force-includes CommonLib's PCH into every TU — upstream's
    -- headers assume the PCH has already pulled in <bit>/<cstdint>/etc.
    -- /Zc:preprocessor is MSVC-only; clang-cl's preprocessor is already
    -- standard-conforming.
    add_cxflags("/utf-8", "/FISKSE/Impl/PCH.h", {force = true})

    -- SKSE plugin entry points are exported via a .def file: the
    -- 0x350-byte SKSEPlugin_Version struct + SKSEPlugin_Load + DllMain.
    -- /wholearchive isn't needed because mora_runtime compiles its
    -- sources directly into the DLL (no separate static trampoline).
    --
    -- /ENTRY:_DllMainCRTStartup is load-bearing: our `plugin_entry.cpp`
    -- defines a user-DllMain, and lld-link's default behavior under
    -- clang-cl is to use that as the DLL entry point directly. That
    -- skips the vcruntime CRT init chain (_initterm), so `.CRT$XCU`
    -- static constructors never run — which leaves
    -- CommonLibSSE-NG's `static inline std::mutex REL::Module::_initLock`
    -- with a zero-initialized internal SRWLock pointer. The first
    -- call to `Module::get()` then AVs at 0xC0000005 inside MSVCP140's
    -- mutex lock code, trying to dereference that null pointer.
    -- Pinning the entry point to the CRT wrapper routes startup through
    -- _initterm → every static ctor → our DllMain.
    add_shflags("/def:" .. path.join(os.projectdir(), "scripts/MoraRuntime.def"),
                -- /ENTRY:_DllMainCRTStartup routes DLL load through the
                -- vcruntime CRT wrapper, which runs C++ static
                -- constructors (the `_initterm(__xc_a, __xc_z)` walk
                -- over the `.CRT$XCU` function-pointer array) before
                -- calling our user DllMain. Without it, lld-link would
                -- pick our own `DllMain` as the entry point and skip
                -- CRT init entirely.
                "/ENTRY:_DllMainCRTStartup",
                {force = true})
target_end()

-- ─────────────────────────────────────────────────────────────
-- MoraTestHarness.dll — standalone SKSE plugin for integration
-- testing (weapon/NPC dumpers + TCP listener). Uses its own
-- set of CommonLib hooks, independent from mora_runtime.
-- ─────────────────────────────────────────────────────────────
target("mora_test_harness")
    set_kind("shared")
    set_filename("MoraTestHarness.dll")
    set_languages("c++23")
    add_deps("commonlibsse_ng", "spdlog_rt")
    set_warnings("none")

    add_files("src/harness/*.cpp",
              "src/codegen/address_library.cpp",
              -- Harness handlers call into mora::rt form-iteration
              -- helpers (for_each_form_of_type, lookup_form_by_id),
              -- which live in form_ops.cpp. Compile it in directly
              -- rather than linking the mora_runtime DLL — the harness
              -- is a standalone SKSE plugin with its own lifecycle.
              "src/rt/form_ops.cpp")
    add_includedirs("include")

    add_defines("SKYRIMSE", "MORA_WITH_COMMONLIB")
    add_defines("WIN32", "_WINDOWS", "NOMINMAX")
    add_cxflags("/utf-8", "/FISKSE/Impl/PCH.h", {force = true})

    -- tcp_listener.cpp uses Winsock
    add_syslinks("ws2_32")
target_end()

end -- windows
