{
    depfiles_format = "gcc",
    files = {
        "src/core/arena.cpp"
    },
    depfiles = "arena.o: src/core/arena.cpp include/mora/core/arena.h\
",
    values = {
        "/usr/bin/g++",
        {
            "-m64",
            "-Wall",
            "-Werror",
            "-std=c++20",
            "-Iinclude"
        }
    }
}