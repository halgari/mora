{
    values = {
        "/usr/bin/g++",
        {
            "-m64",
            "-Wall",
            "-Werror",
            "-std=c++20",
            "-Iinclude"
        }
    },
    depfiles = "string_pool.o: src/core/string_pool.cpp include/mora/core/string_pool.h\
",
    depfiles_format = "gcc",
    files = {
        "src/core/string_pool.cpp"
    }
}