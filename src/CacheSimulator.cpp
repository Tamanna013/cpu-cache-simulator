#include "CacheSimulator.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace {

bool is_power_of_two(std::size_t value) {
    return value > 0 && (value & (value - 1)) == 0;
}

std::string to_upper_copy(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return text;
}

}  // namespace

void validate_cache_config(const CacheConfig& config) {
    if (config.capacity_bytes == 0) {
        throw CacheConfigError("capacity must be greater than zero");
    }
    if (config.block_size_bytes == 0) {
        throw CacheConfigError("block size must be greater than zero");
    }
    if (config.associativity == 0) {
        throw CacheConfigError("associativity must be greater than zero");
    }
    if (!is_power_of_two(config.capacity_bytes)) {
        throw CacheConfigError("capacity must be a power of two");
    }
    if (!is_power_of_two(config.block_size_bytes)) {
        throw CacheConfigError("block size must be a power of two");
    }
    if (config.capacity_bytes % config.block_size_bytes != 0) {
        throw CacheConfigError("capacity must be divisible by block size");
    }

    const std::size_t lines = config.capacity_bytes / config.block_size_bytes;
    if (config.associativity > lines) {
        throw CacheConfigError("associativity cannot exceed the number of cache lines");
    }
    if (lines % config.associativity != 0) {
        throw CacheConfigError(
            "number of cache lines must be divisible by associativity "
            "(sets = capacity / (block size * associativity) must be an integer)");
    }
}

std::size_t num_cache_lines(const CacheConfig& config) {
    validate_cache_config(config);
    return config.capacity_bytes / config.block_size_bytes;
}

std::size_t num_sets(const CacheConfig& config) {
    validate_cache_config(config);
    return (config.capacity_bytes / config.block_size_bytes) / config.associativity;
}

AddressParts decode_address(const CacheConfig& config, std::uint64_t address) {
    validate_cache_config(config);
    AddressParts parts;
    parts.block_number = address / config.block_size_bytes;
    parts.offset = address % config.block_size_bytes;
    const std::size_t sets = num_sets(config);
    parts.set_index = parts.block_number % sets;
    parts.tag = parts.block_number / sets;
    return parts;
}

ReplacementPolicy parse_replacement_policy(const std::string& text) {
    const std::string upper = to_upper_copy(text);
    if (upper == "LRU") {
        return ReplacementPolicy::LRU;
    }
    if (upper == "FIFO") {
        return ReplacementPolicy::FIFO;
    }
    throw CacheConfigError("replacement policy must be LRU or FIFO");
}

std::string replacement_policy_name(ReplacementPolicy policy) {
    return policy == ReplacementPolicy::LRU ? "LRU" : "FIFO";
}

WritePolicy parse_write_policy(const std::string& text) {
    const std::string upper = to_upper_copy(text);
    if (upper == "WT" || upper == "WRITE-THROUGH" || upper == "WRITETHROUGH") {
        return WritePolicy::WriteThrough;
    }
    if (upper == "WB" || upper == "WRITE-BACK" || upper == "WRITEBACK") {
        return WritePolicy::WriteBack;
    }
    throw CacheConfigError("write policy must be WT (write-through) or WB (write-back)");
}

AllocatePolicy parse_allocate_policy(const std::string& text) {
    const std::string upper = to_upper_copy(text);
    if (upper == "WA" || upper == "WRITE-ALLOCATE" || upper == "WRITEALLOCATE") {
        return AllocatePolicy::WriteAllocate;
    }
    if (upper == "NWA" || upper == "NO-WRITE-ALLOCATE" || upper == "NOWRITEALLOCATE") {
        return AllocatePolicy::NoWriteAllocate;
    }
    throw CacheConfigError("allocate policy must be WA (write-allocate) or NWA (no-write-allocate)");
}

std::string write_policy_name(WritePolicy policy) {
    return policy == WritePolicy::WriteThrough ? "write-through" : "write-back";
}

std::string allocate_policy_name(AllocatePolicy policy) {
    return policy == AllocatePolicy::WriteAllocate ? "write-allocate" : "no-write-allocate";
}

CacheSimulator::CacheSimulator(CacheConfig config) : config_(config) {
    validate_cache_config(config_);
    sets_.assign(num_sets(config_), std::vector<CacheLine>(config_.associativity));
}

bool CacheSimulator::access(std::uint64_t address) {
    return access(AccessKind::Read, address);
}

bool CacheSimulator::access(AccessKind kind, std::uint64_t address) {
    ++stats_.accesses;
    ++tick_;
    if (kind == AccessKind::Write) {
        ++stats_.writes;
    } else {
        ++stats_.reads;
    }

    const AddressParts parts = decode_address(config_, address);
    std::vector<CacheLine>& set = sets_.at(static_cast<std::size_t>(parts.set_index));

    for (CacheLine& line : set) {
        if (line.valid && line.tag == parts.tag) {
            ++stats_.hits;
            line.last_used = tick_;
            if (kind == AccessKind::Write) {
                if (config_.write_policy == WritePolicy::WriteThrough) {
                    ++stats_.memory_writes;
                    line.dirty = false;
                } else {
                    line.dirty = true;
                }
            }
            return true;
        }
    }

    ++stats_.misses;

    if (kind == AccessKind::Write &&
        config_.allocate_policy == AllocatePolicy::NoWriteAllocate) {
        ++stats_.memory_writes;
        return false;
    }

    auto invalid = std::find_if(set.begin(), set.end(),
                                [](const CacheLine& line) { return !line.valid; });
    CacheLine& slot = (invalid != set.end()) ? *invalid : choose_victim(set);
    writeback_if_dirty(slot);

    const bool dirty = (kind == AccessKind::Write &&
                        config_.write_policy == WritePolicy::WriteBack);
    fill_line(slot, parts.tag, dirty);
    ++stats_.memory_reads;
    if (kind == AccessKind::Write && config_.write_policy == WritePolicy::WriteThrough) {
        ++stats_.memory_writes;
    }
    return false;
}

CacheLine& CacheSimulator::choose_victim(std::vector<CacheLine>& set) {
    auto victim = set.begin();
    for (auto it = set.begin(); it != set.end(); ++it) {
        if (config_.replacement == ReplacementPolicy::LRU) {
            if (it->last_used < victim->last_used) {
                victim = it;
            }
        } else {
            if (it->inserted_at < victim->inserted_at) {
                victim = it;
            }
        }
    }
    return *victim;
}

void CacheSimulator::writeback_if_dirty(CacheLine& line) {
    if (line.valid && line.dirty && config_.write_policy == WritePolicy::WriteBack) {
        ++stats_.writebacks;
        ++stats_.memory_writes;
    }
}

void CacheSimulator::fill_line(CacheLine& line, std::uint64_t tag, bool dirty) {
    line.valid = true;
    line.tag = tag;
    line.last_used = tick_;
    line.inserted_at = tick_;
    line.dirty = dirty;
}
