{
    depfiles = "terminal.o: src/cli/terminal.cpp include/mora/cli/terminal.h\
",
    files = {
        "src/cli/terminal.cpp"
    },
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
    depfiles_format = "gcc"
}