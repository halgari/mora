#pragma once
#include "mora/ast/ast.h"
#include "mora/core/string_pool.h"
#include "mora/dag/graph.h"
#include <string>
#include <vector>

namespace mora::dag {

struct CompileResult {
    bool success = false;
    std::vector<std::string> errors;
};

CompileResult compile_dynamic_rules(const Module& m, StringPool& pool, DagGraph& g);

} // namespace mora::dag
