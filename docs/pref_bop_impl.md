# Best-Offset Prefetcher Implementation Summary

## Overview

This branch adds a Best-Offset Prefetcher (BOP) implementation to Scarab under `src/prefetcher/`. The implementation follows the main mechanism from Pierre Michaud's "Best-Offset Hardware Prefetching": it dynamically learns a useful positive prefetch offset, issues at most one prefetch per eligible access, and uses a recent-request table to favor offsets whose prefetches would have been timely.

The implementation is integrated into Scarab's existing prefetch framework rather than adding a separate prefetch path.


## Files Added

- `src/prefetcher/pref_bop.c`
  - Core BOP implementation.
  - Contains RR table logic, inflight prefetch tracking, offset scoring, learning-phase control, throttling, and prefetch issue logic.

- `src/prefetcher/pref_bop.h`
  - BOP data structures and framework hook declarations.

- `src/prefetcher/pref_bop.param.def`
  - Runtime-tunable BOP parameters.

- `src/prefetcher/pref_bop.param.h`
  - Parameter extern declarations generated from `pref_bop.param.def`.

- `src/prefetcher/pref_bop.md`
  - This implementation summary.

## Files Modified

- `src/prefetcher/pref_table.def`
  - Registers `"bop"` in the global prefetcher table.

- `src/prefetcher/pref_common.c`
  - Includes `pref_bop.h` so BOP hooks are declared before `pref_table.def` expands.

- `src/param_files.def`
  - Includes `prefetcher/pref_bop.param.def` so BOP parameters are visible to the simulator parameter system.

- `src/dcache_stage.c`
  - Calls the BOP fill-completion hook when a data prefetch fills the dcache through `dcache_fill_process_cacheline()`.

- `src/memory/memory.c`
  - Calls the BOP fill-completion hook when a BOP data prefetch fills L1 or MLC through the memory hierarchy fill paths.

## Algorithm

On each eligible cache access, BOP performs two independent actions:

1. Learning:
   - Select one candidate offset from the offset list.
   - Test whether `X - candidate_offset` exists in the RR table.
   - If it hits, increment that candidate offset's score.
   - Advance to the next candidate offset.

2. Prefetching:
   - If prefetching is currently enabled, issue one prefetch to `X + D`, where `D` is the current learned best offset.
   - Do not prefetch across the configured page boundary.
   - Track the issued prefetch in an inflight table so the RR table can be updated only after the prefetch actually fills.

## Offset List

By default, BOP uses positive offsets up to `PREF_BOP_MAX_OFFSET`.

When `PREF_BOP_SMALL_PRIME_OFFSETS` is enabled, the list contains only offsets whose prime factors are limited to 2, 3, and 5. With the default maximum offset of 256, this matches the style described in the BOP paper:

```text
1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 16, ...
```

This gives denser coverage for small offsets while still allowing larger prefetch distances.

### Data structures

| Field | Type | Description |
|---|---|---|
| `Pref_BOP.offsets` | `int*` | Array of candidate offsets, length `num_offsets` |
| `Pref_BOP.scores` | `uns*` | Per-offset score array, parallel to `offsets[]` |
| `Pref_BOP.num_offsets` | `uns` | Number of entries in `offsets[]` / `scores[]` |
| `Pref_BOP.offset_index` | `uns` | Index of the candidate currently under evaluation |
| `Pref_BOP.rounds` | `uns` | Number of complete passes over the offset list in the current phase |
| `Pref_BOP.best_offset` | `int` | Offset with the highest score seen so far this phase |
| `Pref_BOP.best_score` | `uns` | Highest score seen so far this phase |
| `Pref_BOP.current_offset` | `int` | The offset actually used for issuing prefetches (updated only at phase end); `0` means disabled |
| `Pref_BOP.prefetch_enabled` | `Flag` | Shorthand: `current_offset != 0` |

### Functions

- **`pref_bop_build_offset_list()`** — called once from `pref_bop_init_core()`. Iterates `1..PREF_BOP_MAX_OFFSET`, optionally filters by `pref_bop_is_small_prime_offset()`, and fills `bop_hwp->offsets[]`. Returns `num_offsets`.

- **`pref_bop_is_small_prime_offset()`** — returns `TRUE` if the only prime factors of `offset` are 2, 3, and 5 (i.e. trial-divides by each until the remainder is 1).

- **`pref_bop_test_offset()`** — called on every eligible access from `pref_bop_access()`. Tests one candidate at `offsets[offset_index]`: looks up `line_index - offset` in the RR table; on a hit, increments `scores[offset_index]` and updates `best_score`/`best_offset` if this score is the new maximum. Advances `offset_index`; wraps at `num_offsets` and increments `rounds`. Calls `pref_bop_finish_learning_phase()` when `best_score >= PREF_BOP_SCORE_MAX` or `rounds >= PREF_BOP_ROUND_MAX`.

- **`pref_bop_finish_learning_phase()`** — commits the learned offset to `current_offset` (or sets it to `0` if `best_score <= PREF_BOP_BAD_SCORE`), updates `prefetch_enabled`, then resets `scores[]`, `rounds`, `offset_index`, `best_score`, and `best_offset` for the next phase.

## RR Table

The RR table records base cache-line addresses for completed BOP prefetches.

For an access to line `X` and current offset `D`, BOP issues:

```text
prefetch X + D
```

Here:

```text
base_line = X
pref_line = X + D
```

The RR table stores `base_line`, not `pref_line`.

During learning, candidate offset `d` is considered timely for current line `X` if `X - d` is found in the RR table. This means that a previous prefetch based at `X - d` completed before the current access to `X`, so using offset `d` would likely have been timely.

### Data structures

| Field | Type | Description |
|---|---|---|
| `Pref_BOP.rr_table` | `BOP_RR_Entry*` | Direct-mapped array of `PREF_BOP_RR_ENTRIES` entries |
| `BOP_RR_Entry.tag` | `Addr` | Stored address tag (full `line_index` or low `PREF_BOP_RR_TAG_BITS` bits) |
| `BOP_RR_Entry.valid` | `Flag` | Whether this slot holds a live entry |

The table is direct-mapped: both reads and writes use the same index function.

**Index function** (`pref_bop_rr_index()`):
```c
hash = line_index ^ (line_index >> 6) ^ (line_index >> 13)
index = hash % PREF_BOP_RR_ENTRIES
```

**Tag function** (`pref_bop_rr_tag()`): returns `line_index` when `PREF_BOP_RR_TAG_BITS == 0` (default), otherwise masks to the low `PREF_BOP_RR_TAG_BITS` bits.

### Functions

- **`pref_bop_rr_lookup(bop_hwp, line_index)`** — checks whether `line_index` is present: computes index and tag, returns `TRUE` only if `valid` and `tag` match. Called from `pref_bop_test_offset()`.

- **`pref_bop_rr_insert(bop_hwp, line_index)`** — writes `tag` and sets `valid = TRUE` at the computed index, overwriting any prior occupant (no eviction policy needed for a direct-mapped table). Called from `pref_bop_note_prefetch_fill()` after a successful inflight lookup.

## Inflight Prefetch Table

The inflight table maps:

```text
pref_line -> base_line
```

This is necessary because when a prefetch returns, Scarab knows which line filled, but the fill path does not otherwise know which original access caused that prefetch.

Example:

```text
Access X
Current offset D
Issue prefetch X + D
Record inflight[X + D] = X
```

When `X + D` fills, BOP looks up `X + D` in the inflight table and inserts `X` into the RR table.

This is more faithful to the paper than updating RR when the prefetch is merely queued or sent. RR is updated only after prefetch completion/fill.

### Data structures

| Field | Type | Description |
|---|---|---|
| `Pref_BOP.inflight_table` | `BOP_Inflight_Entry*` | Direct-mapped array of `PREF_BOP_INFLIGHT_ENTRIES` entries |
| `BOP_Inflight_Entry.pref_line` | `Addr` | The prefetched line address (key) |
| `BOP_Inflight_Entry.base_line` | `Addr` | The triggering access address (value, inserted into RR on fill) |
| `BOP_Inflight_Entry.valid` | `Flag` | Whether this slot is occupied |

**Index function** (`pref_bop_inflight_index()`):
```c
hash = line_index ^ (line_index >> 7)
index = hash % PREF_BOP_INFLIGHT_ENTRIES
```

### Functions

- **`pref_bop_inflight_insert(bop_hwp, pref_line, base_line)`** — writes `pref_line`, `base_line`, and `valid = TRUE` at the computed index. Called from `pref_bop_access()` immediately after a prefetch is accepted into the request queue (`pref_addto_ul1req_queue_set()` or `pref_addto_umlc_req_queue()` returns `TRUE`).

- **`pref_bop_inflight_remove(bop_hwp, pref_line, &base_line)`** — looks up `pref_line` at the computed index; if `valid` and `pref_line` matches, writes the stored `base_line` into `*base_line`, clears `valid`, and returns `TRUE`. Returns `FALSE` on a miss (slot invalid or `pref_line` mismatch, indicating a collision eviction). Called from `pref_bop_note_prefetch_fill()`.

### Fill completion hook

`pref_bop_note_prefetch_fill(proc_id, lineAddr, prefetcher_id, type)` is the entry point called by `dcache_stage.c` (UL1 fills) and `memory.c` (MLC fills) whenever a BOP-issued prefetch line arrives in the cache. It:

1. Identifies the correct `Pref_BOP` instance for the given `type`.
2. Calls `pref_bop_inflight_remove()` to recover `base_line` from the inflight table.
3. On success, calls `pref_bop_rr_insert(bop_hwp, base_line)` to record that this base address had a completed prefetch — making it available for future learning lookups.

## Learning Phase

Each learning phase consists of rounds over the offset list.

- One candidate offset is tested per eligible access.
- A round ends after all offsets have been tested once.
- A phase ends when either:
  - the best score reaches `PREF_BOP_SCORE_MAX`, or
  - the number of rounds reaches `PREF_BOP_ROUND_MAX`.

At the end of the phase:

- The offset with the highest score becomes the current prefetch offset.
- If the best score is not greater than `PREF_BOP_BAD_SCORE`, prefetching is disabled.
- Learning state is reset for the next phase.

Learning continues even when prefetching is disabled, so BOP can re-enable prefetching if the workload behavior changes.

### State fields in `Pref_BOP`

| Field | Updated in | Meaning |
|---|---|---|
| `offset_index` | `pref_bop_test_offset()` | Which offset is being evaluated this access; wraps at `num_offsets` |
| `rounds` | `pref_bop_test_offset()` | Incremented each time `offset_index` wraps |
| `best_score` | `pref_bop_test_offset()` | Running maximum score seen this phase |
| `best_offset` | `pref_bop_test_offset()` | Offset that achieved `best_score` |
| `current_offset` | `pref_bop_finish_learning_phase()` | Committed offset used for prefetch issue; `0` = disabled |
| `prefetch_enabled` | `pref_bop_finish_learning_phase()` | `current_offset != 0` |
| `scores[]` | `pref_bop_test_offset()` / `pref_bop_finish_learning_phase()` | Incremented on RR hit; zeroed at phase end via `memset` |

### Phase transition logic (in `pref_bop_test_offset()`)

```
offset_index++
if offset_index == num_offsets:
    offset_index = 0
    rounds++
    if best_score >= PREF_BOP_SCORE_MAX or rounds >= PREF_BOP_ROUND_MAX:
        pref_bop_finish_learning_phase()
```

### Phase commit logic (in `pref_bop_finish_learning_phase()`)

```
current_offset = (best_score > PREF_BOP_BAD_SCORE) ? best_offset : 0
prefetch_enabled = (current_offset != 0)
memset(scores, 0, ...)
rounds = 0; offset_index = 0; best_score = 0; best_offset = offsets[0]
```

## Prefetch Issue Policy

This implementation is degree-1:

```text
one eligible access -> at most one prefetch
```

For line `X` and current best offset `D`, BOP issues:

```text
X + D
```

It does not issue `X + 2D`, `X + 3D`, or multiple top-scoring offsets. This matches the standard BOP design in the paper and avoids unnecessary bandwidth pressure and cache pollution.

## Cache-Level Integration

The BOP paper targets L2 prefetching: triggered on L2 miss, fetching from LLC/memory into L2. In Scarab's naming, L2 is called UL1 (Unified Level 1), and there is an optional mid-level cache (MLC) between L2 and LLC called UMLC.

BOP is wired into both levels:

- `UL1` (= L2, matches the paper)
  - Uses `pref_bop_ul1_miss()` and `pref_bop_ul1_prefhit()`.
  - Issues through `pref_addto_ul1req_queue_set()`.
  - Updates RR after fill through dcache/L1 fill paths.
  - Controlled by `PREF_UL1_ON` (default `TRUE`).

- `UMLC` (= MLC, optional extra level between L2 and LLC)
  - Uses `pref_bop_umlc_miss()` and `pref_bop_umlc_prefhit()`.
  - Issues through `pref_addto_umlc_req_queue()`.
  - Updates RR after MLC fill.
  - Controlled by `PREF_UMLC_ON` (default `FALSE`).


## BOP Prefetcher Integration into Scarab

BOP is registered in `pref_table.def` as a normal Scarab hardware prefetcher, which means `pref_common.c` drives its lifecycle automatically:

```
simulator start
    → pref_init()                    # pref_common.c, iterates pref_table[]
        → pref_bop_init()            # allocates Pref_BOP per core per level
cache miss/prefetch hit
    → pref_ul1_miss() / pref_ul1_pref_hit()   # pref_common.c
        → pref_bop_ul1_miss() / pref_bop_ul1_prefhit()
```

The per-access and fill-completion behavior is described in the Per-access Call Chain section below.

## Parameters

BOP is controlled through `pref_bop.param.def`.

Important parameters:

| Parameter                      | Default | Meaning                                               |
| ------------------------------ | ------: | ----------------------------------------------------- |
| `pref_framework_on`            | `FALSE` | Enables the prefetch framework globally.              |
| `pref_ul1_on`                  |  `TRUE` | Enables UL1 (L2) level prefetching.                  |
| `pref_umlc_on`                 | `FALSE` | Enables UMLC (MLC) level prefetching.                |
| `pref_bop_on`                  | `FALSE` | Enables BOP.                                          |
| `debug_pref_bop`               | `FALSE` | Enables BOP debug logging.                            |
| `pref_bop_rr_entries`          |   `256` | Number of direct-mapped RR entries.                   |
| `pref_bop_rr_tag_bits`         |     `0` | RR tag width; `0` means full line address tag.        |
| `pref_bop_inflight_entries`    |  `1024` | Number of inflight prefetch tracking entries.         |
| `pref_bop_score_max`           |    `31` | Maximum score before ending a phase.                  |
| `pref_bop_round_max`           |   `100` | Maximum rounds per learning phase.                    |
| `pref_bop_bad_score`           |     `1` | Threshold for disabling prefetching.                  |
| `pref_bop_max_offset`          |   `256` | Maximum candidate offset.                             |
| `pref_bop_small_prime_offsets` |  `TRUE` | Use offsets with only 2/3/5 prime factors.            |
| `pref_bop_start_offset`        |     `1` | Initial offset before learning converges.             |
| `pref_bop_page_bytes`          |  `4096` | Page boundary used to suppress cross-page prefetches. |
| `pref_bop_prefetch_on_miss`    |  `TRUE` | Issue prefetches on demand misses.                    |
| `pref_bop_prefetch_on_prefhit` |  `TRUE` | Issue prefetches on useful prefetch hits.             |

Example configuration:

```bash
--pref_framework_on 1 \
--pref_ul1_on 1 \
--pref_bop_on 1 \
--pref_stream_on 0 \
--pref_stride_on 0 \
--pref_stridepc_on 0 \
--pref_ghb_on 0 \
--pref_2dc_on 0 \
--pref_markov_on 0 \
--pref_phase_on 0
```

## Correctness Considerations

The implementation intentionally updates the RR table on prefetch fill, not on prefetch enqueue or send.

This matters because:

- Enqueue only means the prefetch was placed into a software/model queue.
- Send only means the request left the queue and entered the memory path.
- Fill means the line actually returned and was inserted into the cache.

The BOP paper's RR semantics depend on completed prefetches, because the goal is to identify offsets that would have produced timely prefetches. Fill-time RR updates are therefore the closest match to the intended hardware behavior.

## Current Limitations

- No BOP-specific stats have been added yet.
  - Existing global prefetch stats still work.
  - For deeper validation, add BOP-specific counters such as RR updates, score hits, phase completions, learned offsets, and page-boundary drops.

- The inflight table is direct-mapped.
  - Collisions may lose some base-line mappings.
  - This is acceptable for a first implementation but can be improved with associativity if needed.

- The RR table is direct-mapped.
  - This follows the simple implementation described in the paper.

- Negative offsets are not implemented.
  - The paper evaluates only positive offsets for its main design.

- Degree is fixed at one.
  - This matches the standard BOP design.
  - More aggressive variants could add a tunable degree, but that would no longer be the baseline BOP behavior.

## Recommended Validation Plan

Build validation:

- Confirm Scarab builds successfully from the `bop-prefetcher` branch on k8s.
- Confirm the generated binary exists.
- Confirm `pref_bop_on` is accepted as a runtime parameter.

Behavior validation with debug/stat instrumentation:

- Verify one candidate offset is tested per eligible access.
- Verify scores increment only on RR hits.
- Verify RR inserts happen at fill time.
- Verify phase transitions select the highest-scoring offset.
- Verify `best_score <= pref_bop_bad_score` disables prefetch issue while learning continues.
- Verify page-boundary checks suppress cross-page prefetches.

Microbenchmark validation:

- Sequential stream:
  - Expected behavior: learn a small positive offset and issue `X + D`.

- Fixed stride stream:
  - Expected behavior: prefer offsets related to the line stride or its multiples.

- Random access:
  - Expected behavior: low scores and throttled/off prefetching.

- Page-boundary stream:
  - Expected behavior: no prefetch crossing the configured page boundary.

Experiment validation:

- Compare no-prefetch, existing prefetchers, and BOP.
- Inspect `pref.stat.0.csv`, `memory.stat.0.csv`, IPC, useful prefetches, late prefetches, and DRAM traffic.
- Watch for abnormal memory traffic increases, which would indicate overly aggressive or incorrect prefetch issue behavior.

## Top-level State

All per-core BOP state is held in a single global:

```c
BOP_Prefetchers bop_prefetchers;   // pref_bop.c, file scope
```

`BOP_Prefetchers` contains two arrays (one per cache level):

```c
Pref_BOP* bop_hwp_core_ul1;   // [NUM_CORES], allocated if PREF_UL1_ON
Pref_BOP* bop_hwp_core_umlc;  // [NUM_CORES], allocated if PREF_UMLC_ON
```

Each `Pref_BOP` is independent per core and per level — they maintain separate RR tables, inflight tables, offset scores, and learning state.

`bop_hwp_id` (file-scope `uns8`) stores the prefetcher's numeric id assigned by the framework at init time; it is used in `pref_bop_note_prefetch_fill()` to filter fill events to only those caused by BOP.

## Per-access Call Chain

```
cache miss / prefetch hit
    → pref_bop_ul1_miss() / pref_bop_ul1_prefhit()
        → pref_bop_access()
            → pref_bop_test_offset()          # learning: test one candidate
                → pref_bop_rr_lookup()        # check RR for (line - offset)
                → (on hit) scores[i]++, update best_score/best_offset
                → (on round wrap) pref_bop_finish_learning_phase()
            → pref_addto_ul1req_queue_set()   # issue prefetch X + current_offset
            → pref_bop_inflight_insert()      # record pref_line -> base_line

prefetch line fills in cache
    → pref_bop_note_prefetch_fill()           # called from dcache_stage.c / memory.c
        → pref_bop_inflight_remove()          # recover base_line
        → pref_bop_rr_insert()               # insert base_line into RR table
```

## Relevant Commits

- `da75d0e` - Add best-offset prefetcher
- `4ef89d0` - Update BOP RR table on prefetch fill
- `a26a0f8` - Declare BOP hooks before prefetcher table
