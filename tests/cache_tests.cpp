#include "CacheSimulator.hpp"
#include "TraceReader.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

CacheConfig dm128() {
    CacheConfig cfg;
    cfg.capacity_bytes = 128;
    cfg.block_size_bytes = 64;
    cfg.associativity = 1;
    cfg.replacement = ReplacementPolicy::LRU;
    return cfg;
}

CacheConfig sa_one_set() {
    // 128 bytes, 64-byte blocks, 2-way: 2 lines, 1 set.
    CacheConfig cfg;
    cfg.capacity_bytes = 128;
    cfg.block_size_bytes = 64;
    cfg.associativity = 2;
    cfg.replacement = ReplacementPolicy::LRU;
    return cfg;
}

void access_all(CacheSimulator& sim, const std::vector<std::uint64_t>& addresses) {
    for (std::uint64_t address : addresses) {
        sim.access(address);
    }
}

}  // namespace

TEST(ConfigValidation, RejectsZeroCapacity) {
    CacheConfig cfg = dm128();
    cfg.capacity_bytes = 0;
    EXPECT_THROW(validate_cache_config(cfg), CacheConfigError);
}

TEST(ConfigValidation, RejectsZeroBlockSize) {
    CacheConfig cfg = dm128();
    cfg.block_size_bytes = 0;
    EXPECT_THROW(validate_cache_config(cfg), CacheConfigError);
}

TEST(ConfigValidation, RejectsZeroAssociativity) {
    CacheConfig cfg = dm128();
    cfg.associativity = 0;
    EXPECT_THROW(validate_cache_config(cfg), CacheConfigError);
}

TEST(ConfigValidation, RejectsCapacityNotDivisibleByBlockSize) {
    CacheConfig cfg;
    cfg.capacity_bytes = 64;
    cfg.block_size_bytes = 128;  // both powers of two, but 64 % 128 != 0
    cfg.associativity = 1;
    EXPECT_THROW(validate_cache_config(cfg), CacheConfigError);
}

TEST(ConfigValidation, RejectsNonPowerOfTwoCapacity) {
    CacheConfig cfg;
    cfg.capacity_bytes = 192;
    cfg.block_size_bytes = 64;
    cfg.associativity = 1;
    EXPECT_THROW(validate_cache_config(cfg), CacheConfigError);
}

TEST(ConfigValidation, RejectsAssociativityGreaterThanLineCount) {
    CacheConfig cfg = dm128();
    cfg.associativity = 4;  // only 2 lines
    EXPECT_THROW(validate_cache_config(cfg), CacheConfigError);
}

TEST(ConfigValidation, RejectsAssociativityThatDoesNotDivideLines) {
    CacheConfig cfg;
    cfg.capacity_bytes = 256;
    cfg.block_size_bytes = 64;  // 4 lines
    cfg.associativity = 3;
    EXPECT_THROW(validate_cache_config(cfg), CacheConfigError);
}

TEST(ConfigValidation, AcceptsDirectMappedSetAssociativeAndFullyAssociative) {
    CacheConfig dm = dm128();
    EXPECT_EQ(num_cache_lines(dm), 2u);
    EXPECT_EQ(num_sets(dm), 2u);

    CacheConfig sa = sa_one_set();
    EXPECT_EQ(num_cache_lines(sa), 2u);
    EXPECT_EQ(num_sets(sa), 1u);

    CacheConfig fa;
    fa.capacity_bytes = 256;
    fa.block_size_bytes = 64;
    fa.associativity = 4;  // one set, all lines
    EXPECT_EQ(num_sets(fa), 1u);
    EXPECT_NO_THROW(CacheSimulator{fa});
}

TEST(AddressDecode, SpecExampleAddress256) {
    CacheConfig cfg;
    cfg.capacity_bytes = 1024;
    cfg.block_size_bytes = 64;
    cfg.associativity = 2;
    const AddressParts parts = decode_address(cfg, 256);
    EXPECT_EQ(parts.block_number, 4u);
    EXPECT_EQ(parts.set_index, 4u);
    EXPECT_EQ(parts.tag, 0u);
    EXPECT_EQ(parts.offset, 0u);
}

TEST(AddressDecode, OffsetDoesNotChangeBlockTagOrSet) {
    CacheConfig cfg;
    cfg.capacity_bytes = 1024;
    cfg.block_size_bytes = 64;
    cfg.associativity = 2;
    const AddressParts a = decode_address(cfg, 256);
    const AddressParts b = decode_address(cfg, 256 + 63);
    EXPECT_EQ(a.block_number, b.block_number);
    EXPECT_EQ(a.set_index, b.set_index);
    EXPECT_EQ(a.tag, b.tag);
    EXPECT_EQ(b.offset, 63u);
}

TEST(AddressDecode, DistinctSetsAndTags) {
    CacheConfig cfg = dm128();  // 2 sets
    const AddressParts same_set_diff_tag = decode_address(cfg, 0x80);  // block 2 -> set 0
    const AddressParts other_set = decode_address(cfg, 0x40);          // block 1 -> set 1
    const AddressParts base = decode_address(cfg, 0x0);                // block 0 -> set 0
    EXPECT_EQ(base.set_index, same_set_diff_tag.set_index);
    EXPECT_NE(base.tag, same_set_diff_tag.tag);
    EXPECT_NE(base.set_index, other_set.set_index);
}

TEST(DirectMapped, RepeatedAddressIsMissThenHit) {
    CacheSimulator sim(dm128());
    EXPECT_FALSE(sim.access(0x0));
    EXPECT_TRUE(sim.access(0x0));
    EXPECT_EQ(sim.stats().accesses, 2u);
    EXPECT_EQ(sim.stats().hits, 1u);
    EXPECT_EQ(sim.stats().misses, 1u);
}

TEST(DirectMapped, DistinctBlocksAreBothMisses) {
    CacheSimulator sim(dm128());
    EXPECT_FALSE(sim.access(0x0));
    EXPECT_FALSE(sim.access(0x40));
    EXPECT_EQ(sim.stats().hits, 0u);
    EXPECT_EQ(sim.stats().misses, 2u);
}

TEST(DirectMapped, ConflictingBlocksEvictEachOther) {
    CacheSimulator sim(dm128());
    EXPECT_FALSE(sim.access(0x0));
    EXPECT_FALSE(sim.access(0x80));
    EXPECT_FALSE(sim.access(0x0));
    EXPECT_EQ(sim.stats().hits, 0u);
    EXPECT_EQ(sim.stats().misses, 3u);
}

TEST(DirectMapped, AddressesInSameBlockHit) {
    CacheSimulator sim(dm128());
    EXPECT_FALSE(sim.access(0x0));
    EXPECT_TRUE(sim.access(0x3F));
    EXPECT_EQ(sim.stats().hits, 1u);
    EXPECT_EQ(sim.stats().misses, 1u);
}

TEST(DirectMapped, SingleLineCache) {
    CacheConfig cfg;
    cfg.capacity_bytes = 64;
    cfg.block_size_bytes = 64;
    cfg.associativity = 1;
    CacheSimulator sim(cfg);
    EXPECT_EQ(sim.set_count(), 1u);
    EXPECT_FALSE(sim.access(0x0));
    EXPECT_TRUE(sim.access(0x20));
    EXPECT_FALSE(sim.access(0x40));  // different block, only one line
    EXPECT_EQ(sim.stats().hits, 1u);
    EXPECT_EQ(sim.stats().misses, 2u);
}

TEST(EmptyTrace, ZeroAccessesHaveSafeRates) {
    CacheSimulator sim(dm128());
    EXPECT_EQ(sim.stats().accesses, 0u);
    EXPECT_DOUBLE_EQ(sim.stats().hit_rate(), 0.0);
    EXPECT_DOUBLE_EQ(sim.stats().miss_rate(), 0.0);
}

TEST(Replacement, LruKeepsRecentlyUsedLine) {
    CacheConfig cfg = sa_one_set();
    cfg.replacement = ReplacementPolicy::LRU;
    CacheSimulator sim(cfg);
    // A=0x0, B=0x40, reuse A, then C=0x80 should evict B.
    access_all(sim, {0x0, 0x40, 0x0, 0x80});
    EXPECT_TRUE(sim.access(0x0));   // A retained
    EXPECT_FALSE(sim.access(0x40)); // B was the LRU victim
}

TEST(Replacement, FifoEvictsOldestInsertedLine) {
    CacheConfig cfg = sa_one_set();
    cfg.replacement = ReplacementPolicy::FIFO;
    CacheSimulator sim(cfg);
    // Same trace: reuse of A does not change FIFO order, so C evicts A.
    // Probe B first; a later miss on A would otherwise evict B as the oldest remaining line.
    access_all(sim, {0x0, 0x40, 0x0, 0x80});
    EXPECT_TRUE(sim.access(0x40));  // B still resident
    EXPECT_FALSE(sim.access(0x0));  // A was the oldest insertion
}

TEST(MappingModes, FullyAssociativeAvoidsDirectMappedConflict) {
    CacheConfig fa;
    fa.capacity_bytes = 128;
    fa.block_size_bytes = 64;
    fa.associativity = 2;
    fa.replacement = ReplacementPolicy::LRU;
    CacheSimulator sim(fa);
    EXPECT_FALSE(sim.access(0x0));
    EXPECT_FALSE(sim.access(0x80));
    EXPECT_TRUE(sim.access(0x0));  // both blocks fit in the single 2-way set
    EXPECT_EQ(sim.stats().hits, 1u);
    EXPECT_EQ(sim.stats().misses, 2u);
}

TEST(TraceReader, ParsesHexAddressesAndSkipsBlanksAndComments) {
    std::istringstream in("0x00000000\n\n# comment\n0x40\n");
    const auto addrs = read_trace(in);
    ASSERT_EQ(addrs.size(), 2u);
    EXPECT_EQ(addrs[0].address, 0u);
    EXPECT_EQ(addrs[0].kind, AccessKind::Read);
    EXPECT_EQ(addrs[1].address, 0x40u);
}

TEST(TraceReader, AcceptsReadWritePrefix) {
    std::istringstream in("R 0x00000000\nW 0x00000040\n");
    const auto addrs = read_trace(in);
    ASSERT_EQ(addrs.size(), 2u);
    EXPECT_EQ(addrs[0].kind, AccessKind::Read);
    EXPECT_EQ(addrs[0].address, 0u);
    EXPECT_EQ(addrs[1].kind, AccessKind::Write);
    EXPECT_EQ(addrs[1].address, 0x40u);
}

TEST(TraceReader, HandlesMissingFinalNewline) {
    std::istringstream in("0x10");
    const auto addrs = read_trace(in);
    ASSERT_EQ(addrs.size(), 1u);
    EXPECT_EQ(addrs[0].address, 0x10u);
    EXPECT_EQ(addrs[0].kind, AccessKind::Read);
}

TEST(TraceReader, RejectsMalformedHex) {
    std::istringstream in("0xGG\n");
    EXPECT_THROW(read_trace(in), TraceParseError);
}

TEST(TraceReader, RejectsNegativeAddress) {
    std::istringstream in("-1\n");
    EXPECT_THROW(read_trace(in), TraceParseError);
}

TEST(TraceReader, RejectsDecimalWithoutPrefix) {
    std::istringstream in("16\n");
    EXPECT_THROW(read_trace(in), TraceParseError);
}

TEST(TraceReader, RejectsEmptyHex) {
    std::istringstream in("0x\n");
    EXPECT_THROW(read_trace(in), TraceParseError);
}

TEST(ReplacementPolicyParse, AcceptsLruAndFifo) {
    EXPECT_EQ(parse_replacement_policy("lru"), ReplacementPolicy::LRU);
    EXPECT_EQ(parse_replacement_policy("FIFO"), ReplacementPolicy::FIFO);
    EXPECT_THROW(parse_replacement_policy("random"), CacheConfigError);
}

TEST(WritePolicyParse, AcceptsThroughAndBack) {
    EXPECT_EQ(parse_write_policy("wt"), WritePolicy::WriteThrough);
    EXPECT_EQ(parse_write_policy("write-back"), WritePolicy::WriteBack);
    EXPECT_THROW(parse_write_policy("write-around"), CacheConfigError);
    EXPECT_EQ(parse_allocate_policy("WA"), AllocatePolicy::WriteAllocate);
    EXPECT_EQ(parse_allocate_policy("nwa"), AllocatePolicy::NoWriteAllocate);
}

TEST(WritePolicies, WriteAllocateInstallsOnWriteMiss) {
    CacheConfig cfg = dm128();
    cfg.allocate_policy = AllocatePolicy::WriteAllocate;
    CacheSimulator sim(cfg);
    EXPECT_FALSE(sim.access(AccessKind::Write, 0x0));
    EXPECT_TRUE(sim.access(AccessKind::Read, 0x0));
    EXPECT_EQ(sim.stats().misses, 1u);
    EXPECT_EQ(sim.stats().hits, 1u);
    EXPECT_EQ(sim.stats().memory_reads, 1u);
}

TEST(WritePolicies, NoWriteAllocateDoesNotInstallOnWriteMiss) {
    CacheConfig cfg = dm128();
    cfg.allocate_policy = AllocatePolicy::NoWriteAllocate;
    CacheSimulator sim(cfg);
    EXPECT_FALSE(sim.access(AccessKind::Write, 0x0));
    EXPECT_FALSE(sim.access(AccessKind::Read, 0x0));
    EXPECT_EQ(sim.stats().misses, 2u);
    EXPECT_EQ(sim.stats().hits, 0u);
    EXPECT_EQ(sim.stats().memory_writes, 1u);  // the NWA store
    EXPECT_EQ(sim.stats().memory_reads, 1u);   // the later read miss fetch
}

TEST(WritePolicies, WriteThroughStoresOnHitWithoutDirtyEviction) {
    CacheConfig cfg = dm128();
    cfg.write_policy = WritePolicy::WriteThrough;
    CacheSimulator sim(cfg);
    EXPECT_FALSE(sim.access(AccessKind::Write, 0x0));
    EXPECT_TRUE(sim.access(AccessKind::Write, 0x0));
    EXPECT_FALSE(sim.access(AccessKind::Read, 0x80));  // conflict eviction
    EXPECT_EQ(sim.stats().writebacks, 0u);
    EXPECT_EQ(sim.stats().memory_writes, 2u);  // miss allocate WT + hit WT
}

TEST(WritePolicies, WriteBackSetsDirtyAndWritebackOnConflict) {
    CacheConfig cfg = dm128();
    cfg.write_policy = WritePolicy::WriteBack;
    CacheSimulator sim(cfg);
    EXPECT_FALSE(sim.access(AccessKind::Write, 0x0));  // allocate, mark dirty
    EXPECT_EQ(sim.stats().memory_writes, 0u);
    EXPECT_FALSE(sim.access(AccessKind::Read, 0x80));  // evict dirty block 0
    EXPECT_EQ(sim.stats().writebacks, 1u);
    EXPECT_EQ(sim.stats().memory_writes, 1u);
    EXPECT_EQ(sim.stats().memory_reads, 2u);
}

TEST(WritePolicies, WriteBackHitDoesNotWriteMemoryUntilEviction) {
    CacheConfig cfg = dm128();
    cfg.write_policy = WritePolicy::WriteBack;
    CacheSimulator sim(cfg);
    EXPECT_FALSE(sim.access(AccessKind::Read, 0x0));
    EXPECT_TRUE(sim.access(AccessKind::Write, 0x0));
    EXPECT_EQ(sim.stats().memory_writes, 0u);
    EXPECT_EQ(sim.stats().hits, 1u);
}
