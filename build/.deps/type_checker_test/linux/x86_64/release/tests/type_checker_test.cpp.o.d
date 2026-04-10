{
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
    },
    depfiles = "type_checker_test.o: tests/type_checker_test.cpp  include/mora/sema/type_checker.h include/mora/ast/ast.h  include/mora/ast/types.h include/mora/core/source_location.h  include/mora/core/string_pool.h include/mora/sema/name_resolver.h  include/mora/diag/diagnostic.h include/mora/parser/parser.h  include/mora/lexer/lexer.h include/mora/lexer/token.h\
",
    files = {
        "tests/type_checker_test.cpp"
    }
}