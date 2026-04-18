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
              "src/cli/*.cpp", "src/eval/*.cpp", "src/ext/*.cpp",
              "src/emit/*.cpp",
              "src/data/chunk_pool.cpp", "src/data/columnar_relation.cpp",
              "src/data/indexed_relation.cpp", "src/data/schema_registry.cpp",
              "src/data/value.cpp",
              "src/model/*.cpp",
              "src/lsp/*.cpp", "src/lsp/handlers/*.cpp")
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

-- ══════════════════════════════════════════════════════════════
-- Extensions (foundation stubs — no-op until Plan 3)
-- ══════════════════════════════════════════════════════════════
includes("extensions/parquet/xmake.lua")
includes("extensions/skyrim_compile/xmake.lua")
includes("extensions/synthetic/xmake.lua")

-- CLI executable
target("mora")
    set_kind("binary")
    add_files("src/main.cpp")
    add_deps("mora_lib", "mora_skyrim_compile")
    -- CLI11 is header-only; bring in its multi-file include tree.
    -- Submodule pinned at v2.6.2 under extern/CLI11.
    add_includedirs("extern/CLI11/include")
    -- Keep the compile-time banner in sync with xmake's version —
    -- the previous hardcoded "0.1.0" string drifted over four
    -- bumps. Projected into main.cpp via MORA_VERSION.
    on_load(function (target)
        import("core.project.project")
        target:add("defines", "MORA_VERSION=\"" .. project.version() .. "\"")
    end)
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
        add_includedirs("tests")
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
        add_includedirs("tests")
        add_deps("mora_lib")
        add_packages("gtest")
        add_syslinks("gtest_main")
        add_tests(name)
    target_end()
end

end -- not windows
