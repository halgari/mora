{
    depfiles = "renderer.o: src/diag/renderer.cpp include/mora/diag/renderer.h  include/mora/diag/diagnostic.h include/mora/core/source_location.h  include/mora/cli/terminal.h\
",
    files = {
        "src/diag/renderer.cpp"
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