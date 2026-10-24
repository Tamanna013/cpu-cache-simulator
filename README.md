# CPU Cache Simulator

A command-line C++17 simulator of a single-level CPU cache. It reads a trace of
byte addresses, applies a configurable mapping, replacement, and write policy,
and reports hit/miss statistics plus modeled memory traffic.

This is a memory-hierarchy teaching tool, not a cycle-accurate CPU model. Each
trace line is **one lookup of the cache block that contains that byte address**.

Beginners: start with [docs/beginner-walkthrough.md](docs/beginner-walkthrough.md)
(how the project was built, with every source file explained). Design notes:
[docs/design.md](docs/design.md).

## Quick start

Requires CMake 3.16+, a C++17 compiler, and network access on the first
configure so GoogleTest can be fetched.

```bash
cmake -S . -B build
cmake --build build
```

On Windows with Visual Studio generators, the binary is typically
`build\Debug\cache_sim.exe` (or `build\Release\cache_sim.exe`). On
Ninja/Makefiles it is `build/cache_sim`.

```bash
./build/cache_sim \
  --capacity 1024 \
  --block-size 64 \
  --associativity 2 \
  --replacement LRU \
  --trace traces/repeated.trace
```

Example report (values depend on the trace and configuration):

```
Cache configuration:
  Capacity:       1024 bytes
  Block size:     64 bytes
  Associativity:  2-way
  Replacement:    LRU
  Write policy:   write-through
  Allocate:       write-allocate

Accesses:         6
  Reads:          6
  Writes:         0
Hits:             5
Misses:           1
Memory reads:     1
Memory writes:    0
Writebacks:       0
Hit rate:         83.33%
Miss rate:        16.67%
```

Run unit tests:

```bash
cmake --build build --target cache_tests
ctest --test-dir build --output-on-failure
```

If `ctest` does not pick up the binary on your generator, run `cache_tests`
directly from the build directory (on this machine: `build\Release\cache_tests.exe`).

## Command-line interface

| Flag | Meaning |
|------|---------|
| `--capacity <bytes>` | Total cache size. Must be a positive power of two. |
| `--block-size <bytes>` | Block/line size. Must be a positive power of two that divides capacity. |
| `--associativity <ways>` | Lines per set. `1` is direct-mapped; `capacity/block-size` is fully associative. |
| `--replacement LRU\|FIFO` | Victim selection when a set is full. |
| `--write-policy WT\|WB` | Write-through (default) or write-back. |
| `--allocate WA\|NWA` | Write-allocate (default) or no-write-allocate. |
| `--trace <file>` | Path to a trace file. |
| `--help` | Print usage. |

Invalid configurations print an error to stderr and exit with status 1. They
do not crash.

## Trace format

Hexadecimal byte addresses, one per line (treated as **reads**):

```
0x00000000
0x00000004
0x00000040
```

Read/write prefixes select the operation used by write policies:

```
R 0x00000000
W 0x00000040
```

- Addresses must use a `0x` prefix.
- Blank lines and `#` comments are skipped.
- A file without a trailing newline is valid.
- Negative values and malformed hex are errors.

Bundled traces:

| File | What it shows |
|------|----------------|
| `traces/repeated.trace` | Spatial/temporal reuse inside one block |
| `traces/sequential.trace` | Cold misses walking consecutive blocks |
| `traces/conflict.trace` | Direct-mapped conflict on blocks 0 and 2 |
| `traces/lru_vs_fifo.trace` | Same trace, different victims under LRU vs FIFO |
| `traces/writes.trace` | Mixed R/W for write-through vs write-back |

Compare LRU and FIFO on the designed conflict:

```bash
./build/cache_sim --capacity 128 --block-size 64 --associativity 2 --replacement LRU --trace traces/lru_vs_fifo.trace
./build/cache_sim --capacity 128 --block-size 64 --associativity 2 --replacement FIFO --trace traces/lru_vs_fifo.trace
```

On a 2-way 128-byte cache those four accesses are `A, B, A, C`. LRU then
retains `A` and evicts `B`; FIFO retains `B` and evicts `A`.

Compare write policies:

```bash
./build/cache_sim --capacity 128 --block-size 64 --associativity 1 --replacement LRU --write-policy WT --allocate WA --trace traces/writes.trace
./build/cache_sim --capacity 128 --block-size 64 --associativity 1 --replacement LRU --write-policy WB --allocate WA --trace traces/writes.trace
./build/cache_sim --capacity 128 --block-size 64 --associativity 1 --replacement LRU --write-policy WT --allocate NWA --trace traces/writes.trace
```

## Design overview

See [docs/design.md](docs/design.md) for formulas, data structures, replacement,
and write-policy rules.

Summary:

- `CacheConfig` / `validate_cache_config` reject illegal sizes.
- `decode_address` computes block number, set index, tag, and offset.
- `CacheSimulator` stores `sets[set_index][way]`.
- LRU uses a monotonic access clock (`last_used`).
- FIFO uses insertion time (`inserted_at`), which does not change on hits.
- Write-through updates memory on every store; write-back sets a dirty bit and
  writes memory only when a dirty line is evicted.
- Write-allocate installs a block on a write miss; no-write-allocate does not.
- `TraceReader` is independent of cache policy.

Mapping modes from the same code:

- Direct-mapped: `--associativity 1`
- Set-associative: `--associativity N` with more than one set
- Fully associative: `--associativity` equal to the number of lines

## Assumptions

- Byte-addressed memory; offsets within a block do not create extra lookups.
- Capacity and block size are powers of two (bit-field decoding).
- Number of lines must be divisible by associativity.
- Empty traces report 0 accesses and 0.00% hit/miss rates (no division by zero).
- A bare address in a trace is a read. `R`/`W` select the access kind.
- Memory-read/write counters count **events** (block fetches and store/writeback
  events), not bytes transferred.

## Known limitations

- Compulsory, capacity, and conflict misses are not classified in the report.
- No timing, miss penalties, multi-level hierarchy, or coherence.
- Associativity values that do not divide the line count are rejected even if
  a hardware designer might pad the cache.
- Write-back no-write-allocate write misses are modeled as a single memory
  write without installing the block (a teaching simplification).

## Project layout

```
cpu-cache-simulator/
├── CMakeLists.txt
├── README.md
├── include/          # public headers
├── src/              # simulator, trace reader, CLI
├── tests/            # GoogleTest cases
├── traces/           # example address traces
└── docs/
    ├── design.md
    └── beginner-walkthrough.md
```
