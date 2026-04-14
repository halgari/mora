set_project("mora")
set_version("0.1.0")

set_languages("c++20")
set_warnings("all", "error")

-- ══════════════════════════════════════════════════════════════
-- Linux: CLI compiler + tests
-- ══════════════════════════════════════════════════════════════
if not is_plat("windows") then

add_requires("gtest")
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
    add_packages("zlib", "fmt")
    -- Regenerate src/model/relations_seed.cpp from data/relations/*.yaml
    -- when any YAML source is newer than the generated file.
    before_build(function (target)
        import("core.project.project")
        local out = path.join(os.projectdir(), "src/model/relations_seed.cpp")
        local out_mtime = os.exists(out) and os.mtime(out) or 0
        local stale = false
        for _, yaml in ipairs(os.files(path.join(os.projectdir(), "data/relations/*.yaml"))) do
            if os.mtime(yaml) > out_mtime then
                stale = true
                break
            end
        end
        if stale then
            print("regenerating src/model/relations_seed.cpp from data/relations/*.yaml")
            os.vrunv("python3", {path.join(os.projectdir(), "tools/gen_relations.py")})
        end
    end)
target_end()

-- CLI executable
target("mora")
    set_kind("binary")
    add_files("src/main.cpp")
    add_deps("mora_lib")
target_end()

-- Tests
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
