{
    depfiles = "lexer.o: src/lexer/lexer.cpp include/mora/lexer/lexer.h  include/mora/lexer/token.h include/mora/core/source_location.h  include/mora/core/string_pool.h include/mora/diag/diagnostic.h\
",
    depfiles_format = "gcc",
    files = {
        "src/lexer/lexer.cpp"
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
    }
}