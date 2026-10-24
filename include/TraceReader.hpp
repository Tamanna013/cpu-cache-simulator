#pragma once

#include "CacheConfig.hpp"

#include <cstdint>
#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

class TraceParseError : public std::invalid_argument {
public:
    explicit TraceParseError(const std::string& message)
        : std::invalid_argument(message) {}
};

struct MemoryAccess {
    AccessKind kind = AccessKind::Read;
    std::uint64_t address = 0;
};

// One access per listed byte address. A bare address is a read.
// "R addr" and "W addr" select read vs write for write-policy simulation.
std::uint64_t parse_trace_address(const std::string& token);
std::vector<MemoryAccess> read_trace(std::istream& in);
std::vector<MemoryAccess> read_trace_file(const std::string& path);
