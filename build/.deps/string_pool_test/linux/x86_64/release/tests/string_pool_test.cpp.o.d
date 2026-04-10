{
    values = {
        "/usr/bin/g++",
        {
            "-m64",
            "-Wall",
            "-Werror",
            "-std=c++20",
            "-Iinclude",
            "-DGTEST_HAS_PTHREAD=1"
        }
    },
    depfiles = "string_pool_test.o: tests/string_pool_test.cpp  include/mora/core/string_pool.h\
",
    depfiles_format = "gcc",
    files = {
        "tests/string_pool_test.cpp"
    }
}