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

-- Static library with all compiler sources
target("mora_lib")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp",
              "src/data/*.cpp", "src/esp/*.cpp", "src/import/*.cpp",
              "src/codegen/*.cpp", "src/rt/*.cpp")
    add_packages("zlib")
    add_cxflags("-fno-exceptions", {force = true})  -- LLVM headers require this
    add_syslinks("LLVM-22", "lldCOFF", "lldCommon")
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

    -- Shared Mora core code (subset — no ESP parser, no CLI, no lexer/parser)
    add_files("src/core/*.cpp", "src/data/*.cpp", "src/eval/*.cpp", "src/emit/*.cpp")
    add_files("src/runtime/*.cpp")
    add_includedirs("include", {public = true})

    -- CommonLibSSE (powerof3)
    local commonlib = path.join(os.getenv("HOME") or "", "oss/CommonLibSSE/include")
    if os.isdir(commonlib) then
        add_includedirs(commonlib)
    end

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
