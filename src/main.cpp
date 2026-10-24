#include "CacheSimulator.hpp"
#include "TraceReader.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage(std::ostream& out) {
    out << "Usage: cache_sim --capacity <bytes> --block-size <bytes> --associativity <ways>\n"
        << "                 --replacement <LRU|FIFO> --trace <file>\n"
        << "                 [--write-policy WT|WB] [--allocate WA|NWA]\n"
        << "\n"
        << "Options:\n"
        << "  --capacity <n>        Total cache capacity in bytes (power of two)\n"
        << "  --block-size <n>      Cache block size in bytes (power of two)\n"
        << "  --associativity <n>   Ways per set (1 = direct-mapped; lines = fully associative)\n"
        << "  --replacement <p>     LRU or FIFO\n"
        << "  --write-policy <p>    WT (write-through, default) or WB (write-back)\n"
        << "  --allocate <p>        WA (write-allocate, default) or NWA (no-write-allocate)\n"
        << "  --trace <path>        Trace file of hexadecimal byte addresses\n"
        << "  --help                Show this message\n";
}

bool consume_flag(const std::vector<std::string>& args, std::size_t& i, const std::string& name,
                  std::string& value) {
    if (args[i] != name) {
        return false;
    }
    if (i + 1 >= args.size()) {
        throw CacheConfigError("missing value for " + name);
    }
    value = args[++i];
    return true;
}

std::size_t parse_size_flag(const std::string& name, const std::string& text) {
    if (text.empty() || text[0] == '-' || text[0] == '+') {
        throw CacheConfigError(name + " must be a positive integer");
    }
    try {
        std::size_t idx = 0;
        const unsigned long long value = std::stoull(text, &idx, 10);
        if (idx != text.size() || value == 0) {
            throw CacheConfigError(name + " must be a positive integer");
        }
        return static_cast<std::size_t>(value);
    } catch (const std::exception&) {
        throw CacheConfigError(name + " must be a positive integer");
    }
}

void print_report(const CacheSimulator& sim) {
    const CacheConfig& cfg = sim.config();
    const CacheStats& stats = sim.stats();

    std::cout << "Cache configuration:\n";
    std::cout << "  Capacity:       " << cfg.capacity_bytes << " bytes\n";
    std::cout << "  Block size:     " << cfg.block_size_bytes << " bytes\n";
    if (cfg.associativity == 1) {
        std::cout << "  Associativity:  direct-mapped\n";
    } else if (sim.set_count() == 1) {
        std::cout << "  Associativity:  fully associative (" << cfg.associativity << "-way)\n";
    } else {
        std::cout << "  Associativity:  " << cfg.associativity << "-way\n";
    }
    std::cout << "  Replacement:    " << replacement_policy_name(cfg.replacement) << "\n";
    std::cout << "  Write policy:   " << write_policy_name(cfg.write_policy) << "\n";
    std::cout << "  Allocate:       " << allocate_policy_name(cfg.allocate_policy) << "\n\n";

    std::cout << "Accesses:         " << stats.accesses << "\n";
    std::cout << "  Reads:          " << stats.reads << "\n";
    std::cout << "  Writes:         " << stats.writes << "\n";
    std::cout << "Hits:             " << stats.hits << "\n";
    std::cout << "Misses:           " << stats.misses << "\n";
    std::cout << "Memory reads:     " << stats.memory_reads << "\n";
    std::cout << "Memory writes:    " << stats.memory_writes << "\n";
    std::cout << "Writebacks:       " << stats.writebacks << "\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Hit rate:         " << (stats.hit_rate() * 100.0) << "%\n";
    std::cout << "Miss rate:        " << (stats.miss_rate() * 100.0) << "%\n";
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);
        if (args.empty()) {
            print_usage(std::cerr);
            return 1;
        }

        CacheConfig config;
        std::string trace_path;
        bool have_capacity = false;
        bool have_block = false;
        bool have_assoc = false;
        bool have_repl = false;

        for (std::size_t i = 0; i < args.size(); ++i) {
            if (args[i] == "--help" || args[i] == "-h") {
                print_usage(std::cout);
                return 0;
            }

            std::string value;
            if (consume_flag(args, i, "--capacity", value)) {
                config.capacity_bytes = parse_size_flag("capacity", value);
                have_capacity = true;
            } else if (consume_flag(args, i, "--block-size", value)) {
                config.block_size_bytes = parse_size_flag("block size", value);
                have_block = true;
            } else if (consume_flag(args, i, "--associativity", value)) {
                config.associativity = parse_size_flag("associativity", value);
                have_assoc = true;
            } else if (consume_flag(args, i, "--replacement", value)) {
                config.replacement = parse_replacement_policy(value);
                have_repl = true;
            } else if (consume_flag(args, i, "--write-policy", value)) {
                config.write_policy = parse_write_policy(value);
            } else if (consume_flag(args, i, "--allocate", value)) {
                config.allocate_policy = parse_allocate_policy(value);
            } else if (consume_flag(args, i, "--trace", value)) {
                trace_path = value;
            } else {
                throw CacheConfigError("unknown argument: " + args[i]);
            }
        }

        if (!have_capacity || !have_block || !have_assoc || !have_repl || trace_path.empty()) {
            throw CacheConfigError(
                "required flags: --capacity --block-size --associativity --replacement --trace");
        }

        CacheSimulator sim(config);
        const std::vector<MemoryAccess> accesses = read_trace_file(trace_path);
        for (const MemoryAccess& access : accesses) {
            sim.access(access.kind, access.address);
        }
        print_report(sim);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
