{
    files = {
        "build/linux/x86_64/release/libmora_lib.a",
        "build/.objs/mora/linux/x86_64/release/src/main.cpp.o"
    },
    values = {
        "/usr/bin/g++",
        {
            "-m64",
            "-Lbuild/linux/x86_64/release",
            "-lmora_lib"
        }
    }
}