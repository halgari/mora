{
    files = {
        "tests/name_resolver_test.cpp"
    },
    depfiles = "name_resolver_test.o: tests/name_resolver_test.cpp  include/mora/sema/name_resolver.h include/mora/ast/ast.h  include/mora/ast/types.h include/mora/core/source_location.h  include/mora/core/string_pool.h include/mora/diag/diagnostic.h  include/mora/parser/parser.h include/mora/lexer/lexer.h  include/mora/lexer/token.h\
",
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
    },
    depfiles_format = "gcc"
}