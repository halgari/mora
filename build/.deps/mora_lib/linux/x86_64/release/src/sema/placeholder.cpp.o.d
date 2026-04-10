{
    files = {
        "src/sema/placeholder.cpp"
    },
    depfiles_format = "gcc",
    depfiles = "placeholder.o: src/sema/placeholder.cpp\
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