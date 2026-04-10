{
    depfiles = "diagnostic_test.o: tests/diagnostic_test.cpp  include/mora/diag/diagnostic.h include/mora/core/source_location.h  include/mora/diag/renderer.h include/mora/cli/terminal.h\
",
    files = {
        "tests/diagnostic_test.cpp"
    },
    values = {
        "/usr/bin/g++",
        {
            "-m64",
            "-Wall",
            "-Werror",
            "-std=c++20",
            "-Iinclude",
            "-DGTEST_HAS_PTHREAD=1"
        }
    },
    depfiles_format = "gcc"
}