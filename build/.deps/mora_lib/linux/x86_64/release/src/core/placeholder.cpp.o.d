{
    files = {
        "src/core/placeholder.cpp"
    },
    depfiles_format = "gcc",
    depfiles = "placeholder.o: src/core/placeholder.cpp\
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