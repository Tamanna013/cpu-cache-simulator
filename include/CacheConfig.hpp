#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

enum class ReplacementPolicy { LRU, FIFO };
enum class AccessKind { Read, Write };
enum class WritePolicy { WriteThrough, WriteBack };
enum class AllocatePolicy { WriteAllocate, NoWriteAllocate };

struct CacheConfig {
    std::size_t capacity_bytes = 0;
    std::size_t block_size_bytes = 0;
    std::size_t associativity = 0;
    ReplacementPolicy replacement = ReplacementPolicy::LRU;
    WritePolicy write_policy = WritePolicy::WriteThrough;
    AllocatePolicy allocate_policy = AllocatePolicy::WriteAllocate;
};

struct CacheStats {
    std::uint64_t accesses = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t reads = 0;
    std::uint64_t writes = 0;
    std::uint64_t memory_reads = 0;   // block fetches from main memory
    std::uint64_t memory_writes = 0;  // write-through stores + write-back evictions
    std::uint64_t writebacks = 0;     // dirty lines written on eviction

    double hit_rate() const {
        if (accesses == 0) {
            return 0.0;
        }
        return static_cast<double>(hits) / static_cast<double>(accesses);
    }

    double miss_rate() const {
        if (accesses == 0) {
            return 0.0;
        }
        return static_cast<double>(misses) / static_cast<double>(accesses);
    }
};

struct AddressParts {
    std::uint64_t block_number = 0;
    std::uint64_t set_index = 0;
    std::uint64_t tag = 0;
    std::uint64_t offset = 0;
};

class CacheConfigError : public std::invalid_argument {
public:
    explicit CacheConfigError(const std::string& message)
        : std::invalid_argument(message) {}
};

void validate_cache_config(const CacheConfig& config);
std::size_t num_cache_lines(const CacheConfig& config);
std::size_t num_sets(const CacheConfig& config);
AddressParts decode_address(const CacheConfig& config, std::uint64_t address);

ReplacementPolicy parse_replacement_policy(const std::string& text);
WritePolicy parse_write_policy(const std::string& text);
AllocatePolicy parse_allocate_policy(const std::string& text);

std::string replacement_policy_name(ReplacementPolicy policy);
std::string write_policy_name(WritePolicy policy);
std::string allocate_policy_name(AllocatePolicy policy);
