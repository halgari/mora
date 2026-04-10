{
    files = {
        "src/main.cpp"
    },
    depfiles_format = "gcc",
    depfiles = "main.o: src/main.cpp\
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