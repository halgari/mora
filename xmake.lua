set_project("mora")
set_version("0.1.0")

set_languages("c++20")
set_warnings("all", "error")

if is_plat("windows") then
    -- fmt (base.h) static_asserts unless this is set on MSVC, and it
    -- must apply to every TU that includes a fmt header.
    add_cxflags("/utf-8", {force = true})
    -- Quiet MSVC's C4996 "unsafe" CRT deprecations (getenv, etc.).
    -- We build with /WX, so leaving them hot turns warnings into errors.
    add_defines("_CRT_SECURE_NO_WARNINGS")
end

-- ══════════════════════════════════════════════════════════════
-- CLI compiler (Linux + Windows)
-- ══════════════════════════════════════════════════════════════
add_requires("zlib")
add_requires("fmt")

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
              "src/model/*.cpp", "src/dag/*.cpp")
    -- Runtime files that touch CommonLibSSE-NG guard their bodies on
    -- MORA_WITH_COMMONLIB. The CLI doesn't link CommonLib, so leave
    -- that undefined here — guarded blocks compile as empty stubs.
    add_packages("zlib", "fmt", {public = true})
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
-- Windows: SKSE Runtime DLL (cross-compile via clang-cl)
-- Build with: xmake f -p windows --cc=clang-cl --cxx=clang-cl --ld=lld-link --ar=llvm-lib
--             xmake build mora_runtime
-- ══════════════════════════════════════════════════════════════
if is_plat("windows") then

target("mora_runtime")
    set_kind("shared")
    set_filename("MoraRuntime.dll")
    set_languages("c++23")

    -- Shared Mora core code (subset — no ESP parser, no CLI, no lexer/parser)
    add_files("src/core/*.cpp", "src/data/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp")
    add_files("src/rt/*.cpp", "src/rt/handlers/*.cpp")
    add_files("src/dag/*.cpp", "src/model/*.cpp")
    add_includedirs("include", {public = true})

    -- CommonLibSSE-NG (alandtse/CommonLibVR ng branch)
    add_includedirs("extern/CommonLibSSE-NG/include")
    add_defines("SKYRIMSE", "SKSE_SUPPORT_XBYAK=0")

    -- Windows SDK via xwin
    local xwin = path.join(os.getenv("HOME") or "", ".xwin")
    if os.isdir(xwin) then
        add_sysincludedirs(path.join(xwin, "crt/include"))
        add_sysincludedirs(path.join(xwin, "sdk/include/ucrt"))
        add_sysincludedirs(path.join(xwin, "sdk/include/um"))
        add_sysincludedirs(path.join(xwin, "sdk/include/shared"))
        add_linkdirs(path.join(xwin, "crt/lib/x86_64"))
        add_linkdirs(path.join(xwin, "sdk/lib/um/x86_64"))
        add_linkdirs(path.join(xwin, "sdk/lib/ucrt/x86_64"))
    end

    add_cxflags("/EHsc", "/permissive-", {force = true})
    add_defines("WIN32", "_WINDOWS", "NOMINMAX")
target_end()

end -- windows
