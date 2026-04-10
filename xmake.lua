set_project("mora")
set_version("0.1.0")

set_languages("c++20")
set_warnings("all", "error")

add_requires("gtest")

-- Static library with all compiler sources (tests and exe both link this)
target("mora_lib")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_files("src/core/*.cpp", "src/lexer/*.cpp", "src/ast/*.cpp",
              "src/parser/*.cpp", "src/sema/*.cpp", "src/diag/*.cpp",
              "src/cli/*.cpp")
target_end()

-- Main executable
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
