#pragma once
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace mora::rt {

using TupleU32 = std::vector<uint32_t>;

struct Delta {
    TupleU32 tuple;
    int8_t   diff = 0;
};

class DeltaQueue {
public:
    void push(uint32_t node_id, Delta d) { q_.push_back({node_id, std::move(d)}); }
    std::pair<uint32_t, Delta> pop() {
        auto v = std::move(q_.front()); q_.pop_front(); return v;
    }
    bool empty() const { return q_.empty(); }
    size_t size() const { return q_.size(); }
private:
    std::deque<std::pair<uint32_t, Delta>> q_;
};

} // namespace mora::rt
