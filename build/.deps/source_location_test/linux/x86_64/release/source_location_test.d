{
    files = {
        "build/linux/x86_64/release/libmora_lib.a",
        "build/.objs/source_location_test/linux/x86_64/release/tests/source_location_test.cpp.o"
    },
    values = {
        "/usr/bin/g++",
        {
            "-m64",
            "-Lbuild/linux/x86_64/release",
            "-lgtest",
            "-lmora_lib",
            "-lgtest_main"
        }
    }
}