#include "mora/eval/binding_chunk.h"

#include <stdexcept>

namespace mora {

BindingChunk::BindingChunk(std::vector<StringId>    var_names,
                            std::vector<const Type*> col_types)
    : names_(std::move(var_names))
{
    if (names_.size() != col_types.size()) {
        throw std::runtime_error(
            "BindingChunk: var_names and col_types arity mismatch");
    }
    columns_.reserve(col_types.size());
    for (size_t i = 0; i < col_types.size(); ++i) {
        columns_.push_back(std::make_unique<Column>(col_types[i]));
        by_name_.emplace(names_[i].index, i);
    }
}

size_t BindingChunk::row_count() const {
    return columns_.empty() ? 0 : columns_.front()->row_count();
}

int BindingChunk::index_of(StringId var_name) const {
    auto it = by_name_.find(var_name.index);
    if (it == by_name_.end()) return -1;
    return static_cast<int>(it->second);
}

void BindingChunk::append_row(const std::vector<Value>& row) {
    if (row.size() != columns_.size()) {
        throw std::runtime_error(
            "BindingChunk::append_row: row arity mismatch");
    }
    for (size_t i = 0; i < row.size(); ++i) {
        columns_[i]->append(row[i]);
    }
}

Value BindingChunk::cell(size_t row, size_t col) const {
    return columns_[col]->at(row);
}

} // namespace mora
