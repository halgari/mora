{
    depfiles = "types.o: src/ast/types.cpp include/mora/ast/types.h\
",
    files = {
        "src/ast/types.cpp"
    },
    depfiles_format = "gcc",
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