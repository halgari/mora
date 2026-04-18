target("mora_skyrim_runtime")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_includedirs("../../include", {public = false})
    add_files("src/game_api.cpp", "src/runtime.cpp", "src/register.cpp")
    add_packages("arrow", {public = false})
    add_deps("mora_parquet")
    if is_plat("windows") then
        add_packages("fmt", "nlohmann_json", {public = true})
    else
        add_packages("fmt", "nlohmann_json", {public = true})
    end
target_end()

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
