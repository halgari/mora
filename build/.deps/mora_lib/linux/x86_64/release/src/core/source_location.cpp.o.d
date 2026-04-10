{
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
    },
    files = {
        "src/core/source_location.cpp"
    },
    depfiles = "source_location.o: src/core/source_location.cpp  include/mora/core/source_location.h\
"
}