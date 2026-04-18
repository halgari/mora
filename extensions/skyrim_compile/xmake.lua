target("mora_skyrim_compile")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_includedirs("../../include", {public = false})
    add_files("src/register.cpp",
              "src/plugin_facts.cpp",
              "src/esp_data_source.cpp",
              "src/esp/*.cpp")
    if is_plat("windows") then
        add_deps("zlib")
        add_packages("fmt", "nlohmann_json", {public = true})
    else
        add_packages("zlib", "fmt", "nlohmann_json", {public = true})
    end
target_end()

-- Tests for the skyrim_compile extension. Matches the root xmake.lua's
-- test-discovery pattern so behavior is uniform across core + extensions.
if not is_plat("windows") then
    for _, testfile in ipairs(os.files("tests/*_test.cpp")) do
        local name = path.basename(testfile)
        target(name)
            set_kind("binary")
            set_default(false)
            add_files(testfile)
            add_includedirs("tests")
            add_deps("mora_skyrim_compile", "mora_lib")
            add_packages("gtest")
            add_syslinks("gtest_main")
            add_tests(name)
        target_end()
    end

    for _, testfile in ipairs(os.files("tests/**/test_*.cpp")) do
        local name = path.basename(testfile)
        target(name)
            set_kind("binary")
            set_default(false)
            add_files(testfile)
            add_includedirs("tests")
            add_deps("mora_skyrim_compile", "mora_lib")
            add_packages("gtest")
            add_syslinks("gtest_main")
            add_tests(name)
        target_end()
    end
end
