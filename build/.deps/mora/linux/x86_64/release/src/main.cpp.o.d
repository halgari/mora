{
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
    depfiles = "main.o: src/main.cpp include/mora/lexer/lexer.h  include/mora/lexer/token.h include/mora/core/source_location.h  include/mora/core/string_pool.h include/mora/diag/diagnostic.h  include/mora/parser/parser.h include/mora/ast/ast.h  include/mora/ast/types.h include/mora/sema/name_resolver.h  include/mora/sema/type_checker.h include/mora/diag/renderer.h  include/mora/cli/terminal.h include/mora/cli/progress.h\
",
    files = {
        "src/main.cpp"
    },
    depfiles_format = "gcc"
}