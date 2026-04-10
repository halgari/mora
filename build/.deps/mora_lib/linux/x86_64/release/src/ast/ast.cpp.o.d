{
    files = {
        "src/ast/ast.cpp"
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
    depfiles = "ast.o: src/ast/ast.cpp include/mora/ast/ast.h include/mora/ast/types.h  include/mora/core/source_location.h include/mora/core/string_pool.h\
"
}