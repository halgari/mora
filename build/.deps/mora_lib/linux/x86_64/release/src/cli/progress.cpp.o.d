{
    files = {
        "src/cli/progress.cpp"
    },
    depfiles_format = "gcc",
    depfiles = "progress.o: src/cli/progress.cpp include/mora/cli/progress.h  include/mora/cli/terminal.h\
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