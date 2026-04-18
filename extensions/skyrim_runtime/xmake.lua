-- Linux-only static lib: the full skyrim_runtime extension surface
-- (parquet reader + snapshot writer + GameAPI). The DLL side below
-- relinks only the files it needs (snapshot reader + GameAPI) without
-- the parquet dependency.
if not is_plat("windows") then
    target("mora_skyrim_runtime")
        set_kind("static")
        add_includedirs("include", {public = true})
        add_includedirs("../../include", {public = false})
        add_files("src/game_api.cpp", "src/runtime.cpp",
                  "src/runtime_snapshot.cpp", "src/runtime_snapshot_sink.cpp",
                  "src/register.cpp")
        add_packages("arrow", {public = false})
        add_deps("mora_parquet")
        add_packages("fmt", "nlohmann_json", {public = true})
    target_end()
end

-- Tests for the skyrim_runtime extension. Mirrors the parquet extension's
-- test-discovery pattern.
if not is_plat("windows") then
    for _, testfile in ipairs(os.files("tests/test_*.cpp")) do
        local name = path.basename(testfile)
        target(name)
            set_kind("binary")
            set_default(false)
            add_files(testfile)
            add_includedirs("tests")
            add_deps("mora_skyrim_runtime", "mora_parquet", "mora_lib")
            add_packages("gtest", "arrow")
            add_syslinks("gtest_main")
            add_tests(name)
        target_end()
    end
end

-- ─────────────────────────────────────────────────────────────
-- MoraRuntime.dll — SKSE plugin. Loads the flat-binary snapshot at
-- Skyrim's DataLoaded event and applies every effect fact via
-- CommonLibSSE-NG. Windows-only.
-- ─────────────────────────────────────────────────────────────
if is_plat("windows") then
    target("mora_runtime")
        set_kind("shared")
        set_filename("MoraRuntime.dll")
        set_languages("c++23")
        add_deps("commonlibsse_ng", "spdlog_rt")
        set_warnings("none")

        -- The DLL links a subset of the core compiler: the StringPool +
        -- Value + FactDB + diag infrastructure are all used by the
        -- snapshot reader. No lexer/parser/sema/ESP — those are Linux-
        -- only compile-time machinery.
        add_files("../../src/core/*.cpp",
                  "../../src/data/value.cpp",
                  "../../src/data/vector.cpp",
                  "../../src/data/column.cpp",
                  "../../src/data/columnar_relation.cpp",
                  "../../src/diag/diagnostic.cpp",
                  "../../src/eval/fact_db.cpp")
        -- Snapshot reader + GameAPI interface + MockGameAPI (the mock
        -- is tiny; keep it in the DLL so potential future in-process
        -- tests can exercise it).
        add_files("src/runtime_snapshot.cpp",
                  "src/game_api.cpp",
                  "src/rt/*.cpp")
        add_includedirs("include", "../../include")

        add_defines("SKYRIMSE", "MORA_WITH_COMMONLIB")
        add_defines("WIN32", "_WINDOWS", "NOMINMAX")
        -- Force-include CommonLib's PCH — upstream headers assume the
        -- PCH has already pulled in <bit>/<cstdint>/etc.
        add_cxflags("/utf-8", "/FISKSE/Impl/PCH.h", {force = true})

        -- Export SKSEPlugin_Version/Load + DllMain via .def file.
        -- /ENTRY:_DllMainCRTStartup is load-bearing: routes DLL load
        -- through the CRT wrapper so .CRT$XCU static constructors run
        -- (otherwise CommonLib's REL::Module::_initLock stays zero-
        -- initialized and the first Module::get() call AVs).
        add_shflags("/def:" ..
                    path.join(os.projectdir(),
                              "extensions/skyrim_runtime/scripts/MoraRuntime.def"),
                    "/ENTRY:_DllMainCRTStartup",
                    {force = true})
    target_end()
end
