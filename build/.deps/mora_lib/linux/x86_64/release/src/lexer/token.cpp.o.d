{
    files = {
        "src/lexer/token.cpp"
    },
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
    depfiles = "token.o: src/lexer/token.cpp include/mora/lexer/token.h  include/mora/core/source_location.h include/mora/core/string_pool.h\
"
}