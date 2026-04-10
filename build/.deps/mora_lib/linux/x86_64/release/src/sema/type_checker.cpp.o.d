{
    depfiles = "type_checker.o: src/sema/type_checker.cpp  include/mora/sema/type_checker.h include/mora/ast/ast.h  include/mora/ast/types.h include/mora/core/source_location.h  include/mora/core/string_pool.h include/mora/sema/name_resolver.h  include/mora/diag/diagnostic.h\
",
    depfiles_format = "gcc",
    files = {
        "src/sema/type_checker.cpp"
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