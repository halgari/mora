{
    depfiles = "parser.o: src/parser/parser.cpp include/mora/parser/parser.h  include/mora/ast/ast.h include/mora/ast/types.h  include/mora/core/source_location.h include/mora/core/string_pool.h  include/mora/lexer/lexer.h include/mora/lexer/token.h  include/mora/diag/diagnostic.h\
",
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
    files = {
        "src/parser/parser.cpp"
    }
}