#pragma once

#include "CacheConfig.hpp"
#include "CacheLine.hpp"

#include <cstdint>
#include <vector>

class CacheSimulator {
public:
    explicit CacheSimulator(CacheConfig config);

    // One lookup of the block containing address. Address-only traces are reads.
    bool access(std::uint64_t address);
    bool access(AccessKind kind, std::uint64_t address);

    const CacheConfig& config() const { return config_; }
    const CacheStats& stats() const { return stats_; }
    std::size_t set_count() const { return sets_.size(); }
    std::size_t ways() const { return config_.associativity; }

private:
    CacheLine& choose_victim(std::vector<CacheLine>& set);
    void writeback_if_dirty(CacheLine& line);
    void fill_line(CacheLine& line, std::uint64_t tag, bool dirty);

    CacheConfig config_;
    CacheStats stats_;
    std::vector<std::vector<CacheLine>> sets_;
    std::uint64_t tick_ = 0;
};
