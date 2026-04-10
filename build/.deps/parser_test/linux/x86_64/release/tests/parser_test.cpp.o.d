{
    files = {
        "tests/parser_test.cpp"
    },
    depfiles = "parser_test.o: tests/parser_test.cpp include/mora/parser/parser.h  include/mora/ast/ast.h include/mora/ast/types.h  include/mora/core/source_location.h include/mora/core/string_pool.h  include/mora/lexer/lexer.h include/mora/lexer/token.h  include/mora/diag/diagnostic.h\
",
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