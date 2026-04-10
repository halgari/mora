{
    values = {
        "/usr/bin/g++",
        {
            "-m64",
            "-Lbuild/linux/x86_64/release",
            "-lgtest",
            "-lmora_lib",
            "-lgtest_main"
        }
    },
    files = {
        "build/linux/x86_64/release/libmora_lib.a",
        "build/.objs/name_resolver_test/linux/x86_64/release/tests/name_resolver_test.cpp.o"
    }
}