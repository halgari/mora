{
    files = {
        "src/sema/name_resolver.cpp"
    },
    depfiles = "name_resolver.o: src/sema/name_resolver.cpp  include/mora/sema/name_resolver.h include/mora/ast/ast.h  include/mora/ast/types.h include/mora/core/source_location.h  include/mora/core/string_pool.h include/mora/diag/diagnostic.h\
",
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
    depfiles_format = "gcc"
}