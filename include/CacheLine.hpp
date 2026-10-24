#pragma once

#include <cstdint>

struct CacheLine {
    bool valid = false;
    std::uint64_t tag = 0;
    std::uint64_t last_used = 0;    // LRU: updated on every hit and insert
    std::uint64_t inserted_at = 0;  // FIFO: updated only on insert
    bool dirty = false;             // write-back: true after a store until eviction
};
