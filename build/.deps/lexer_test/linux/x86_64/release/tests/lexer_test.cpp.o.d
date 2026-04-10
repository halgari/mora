{
    depfiles = "lexer_test.o: tests/lexer_test.cpp include/mora/lexer/lexer.h  include/mora/lexer/token.h include/mora/core/source_location.h  include/mora/core/string_pool.h include/mora/diag/diagnostic.h\
",
    files = {
        "tests/lexer_test.cpp"
    },
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
    }
}