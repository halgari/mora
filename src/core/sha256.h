#pragma once
#include <cstdint>
#include <cstddef>

namespace mora::sha256 {

struct Ctx {
    uint8_t  data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
};

void init(Ctx& c);
void update(Ctx& c, const uint8_t* data, size_t len);
void finish(Ctx& c, uint8_t out[32]);

} // namespace mora::sha256
