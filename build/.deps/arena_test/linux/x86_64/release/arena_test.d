{
    files = {
        "build/linux/x86_64/release/libmora_lib.a",
        "build/.objs/arena_test/linux/x86_64/release/tests/arena_test.cpp.o"
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