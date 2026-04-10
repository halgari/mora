{
    files = {
        "src/ast/placeholder.cpp"
    },
    depfiles_format = "gcc",
    depfiles = "placeholder.o: src/ast/placeholder.cpp\
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