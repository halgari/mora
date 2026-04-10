{
    depfiles = "diagnostic.o: src/diag/diagnostic.cpp include/mora/diag/diagnostic.h  include/mora/core/source_location.h\
",
    files = {
        "src/diag/diagnostic.cpp"
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