# Benchmark Results

Run on the development machine (Apple Silicon, AppleClang 17, Release build,
local SSD with a warm OS page cache). Run via:

```
cmake --build build -j4
./build/benchmarks/bench_seqscan_vs_index
./build/benchmarks/bench_buffer_pool
./build/benchmarks/bench_replication_lag
```

## 1. IndexScan vs. SeqScan (optimizer's core decision)

`bench_seqscan_vs_index.cpp` inserts N rows into `bench(id INT PRIMARY KEY,
payload VARCHAR)` and times 200 point lookups two ways: an equality
predicate on `id` (PK -> IndexScan) vs. the logically equivalent lookup on
`payload`, which has no index and forces a full SeqScan.

| rows (N) | IndexScan (us/lookup) | SeqScan (us/lookup) | Speedup |
|---------:|-----------------------:|----------------------:|--------:|
|    1,000 |                 145.56 |                1069.55 |    7.3x |
|    5,000 |                 225.87 |                5483.62 |   24.3x |
|   20,000 |                 273.51 |               21985.30 |   80.4x |

**Analysis.** IndexScan latency grows ~O(log N) (273us at 20,000 rows vs.
145us at 1,000 - a 20x increase in N costs less than 2x in latency).
SeqScan grows linearly with N as expected, since every lookup walks the
entire heap file. The gap (7x -> 80x) is exactly why the optimizer in
[optimizer.cpp](../src/query/optimizer.cpp) prefers IndexScan whenever an
equality predicate on the primary key is available.

## 2. Buffer pool size vs. repeated full-scan latency

`bench_buffer_pool.cpp` runs 20 full scans of a ~50-page table (4,000
rows) with pool sizes of 4, 64, and 1024 frames.

| Pool frames | Total time (20 scans) |
|------------:|-----------------------:|
|           4 |               157.98 ms |
|          64 |               132.92 ms |
|        1024 |               138.49 ms |

**Analysis.** The gap is real but modest here because `DiskManager`'s
reads/writes go through `fstream` to a file that's almost entirely warm
in the OS page cache on this hardware - so even a buffer-pool "miss"
rarely costs an actual physical disk seek. The architectural effect
(pin/unpin + LRU eviction, frames recycled under pressure) is exercised
correctly regardless - in fact, a real pin-leak bug in the B+Tree's
split path (see commit `fix(index): leaf-split path leaked a buffer pool
pin per split`) was only caught because a 20,000-row insert workload
exhausted a 256-frame pool and `FetchPage()` started returning `nullptr`.
On slower/networked storage the 4-frame case would show a much larger gap.

## 3. Replication lag and throughput (Track D)

`bench_replication_lag.cpp` runs 500 `INSERT`s on the primary, broadcasts
each over TCP, and measures how long after the primary's last write the
replica has applied everything.

```
Primary applied 500 writes in 51.42ms (9723.2 writes/sec)
Replica caught up 1.26ms after the primary's last write (replication lag)
```

**Analysis.** Replication lag is sub-2ms on localhost - the replica's
background apply thread parses and executes each broadcast statement
about as fast as it arrives. Lag would grow with network latency between
primary and replica (this is logical/statement-based replication over
TCP, not physical WAL shipping - see the Replication section of the main
README for that design tradeoff).

## Limitations of this benchmark suite

- All three benchmarks run on a single machine with a warm OS cache;
  absolute numbers are illustrative, not a substitute for profiling on
  target hardware.
- `HeapFile::InsertTuple` re-walks the page chain from the first page on
  every insert looking for free space, which is O(pages) per insert - the
  IndexScan benchmark's insert phase (not timed above) is the slowest part
  of the run for exactly this reason. This is a known scalability
  limitation, documented in the main README.
