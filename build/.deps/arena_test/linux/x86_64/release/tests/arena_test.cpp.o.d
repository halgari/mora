{
    depfiles_format = "gcc",
    files = {
        "tests/arena_test.cpp"
    },
    depfiles = "arena_test.o: tests/arena_test.cpp include/mora/core/arena.h\
",
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
    }
}