{
    depfiles_format = "gcc",
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
    files = {
        "tests/source_location_test.cpp"
    },
    depfiles = "source_location_test.o: tests/source_location_test.cpp  include/mora/core/source_location.h\
"
}