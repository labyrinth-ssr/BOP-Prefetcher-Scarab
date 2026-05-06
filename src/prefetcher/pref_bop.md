# Best-Offset Prefetcher Implementation Summary

## Overview

This branch adds a Best-Offset Prefetcher (BOP) implementation to Scarab under
`src/prefetcher/`. The implementation follows the main mechanism from Pierre
Michaud's "Best-Offset Hardware Prefetching": it dynamically learns a useful
positive prefetch offset, issues at most one prefetch per eligible access, and
uses a recent-request table to favor offsets whose prefetches would have been
timely.

The implementation is integrated into Scarab's existing prefetch framework
rather than adding a separate prefetch path.

## Files Added

- `src/prefetcher/pref_bop.c`
  - Core BOP implementation.
  - Contains RR table logic, inflight prefetch tracking, offset scoring,
    learning-phase control, throttling, and prefetch issue logic.

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
  - Includes `pref_bop.h` so BOP hooks are declared before `pref_table.def`
    expands.

- `src/param_files.def`
  - Includes `prefetcher/pref_bop.param.def` so BOP parameters are visible to
    the simulator parameter system.

- `src/dcache_stage.c`
  - Calls the BOP fill-completion hook when a data prefetch fills the dcache
    through `dcache_fill_process_cacheline()`.

- `src/memory/memory.c`
  - Calls the BOP fill-completion hook when a BOP data prefetch fills L1 or MLC
    through the memory hierarchy fill paths.

## Algorithm

On each eligible cache access, BOP performs two independent actions:

1. Learning:
   - Select one candidate offset from the offset list.
   - Test whether `X - candidate_offset` exists in the RR table.
   - If it hits, increment that candidate offset's score.
   - Advance to the next candidate offset.

2. Prefetching:
   - If prefetching is currently enabled, issue one prefetch to `X + D`, where
     `D` is the current learned best offset.
   - Do not prefetch across the configured page boundary.
   - Track the issued prefetch in an inflight table so the RR table can be
     updated only after the prefetch actually fills.

## Offset List

By default, BOP uses positive offsets up to `PREF_BOP_MAX_OFFSET`.

When `PREF_BOP_SMALL_PRIME_OFFSETS` is enabled, the list contains only offsets
whose prime factors are limited to 2, 3, and 5. With the default maximum offset
of 256, this matches the style described in the BOP paper:

```text
1, 2, 3, 4, 5, 6, 8, 9, 10, 12, 15, 16, ...
```

This gives denser coverage for small offsets while still allowing larger
prefetch distances.

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

During learning, candidate offset `d` is considered timely for current line `X`
if:

```text
X - d
```

is found in the RR table. This means that a previous prefetch based at `X - d`
completed before the current access to `X`, so using offset `d` would likely
have been timely.

## Inflight Prefetch Table

The inflight table maps:

```text
pref_line -> base_line
```

This is necessary because when a prefetch returns, Scarab knows which line
filled, but the fill path does not otherwise know which original access caused
that prefetch.

Example:

```text
Access X
Current offset D
Issue prefetch X + D
Record inflight[X + D] = X
```

When `X + D` fills, BOP looks up `X + D` in the inflight table and inserts `X`
into the RR table.

This is more faithful to the paper than updating RR when the prefetch is merely
queued or sent. RR is updated only after prefetch completion/fill.

## Learning Phase

Each learning phase consists of rounds over the offset list.

- One candidate offset is tested per eligible access.
- A round ends after all offsets have been tested once.
- A phase ends when either:
  - the best score reaches `PREF_BOP_SCORE_MAX`, or
  - the number of rounds reaches `PREF_BOP_ROUND_MAX`.

At the end of the phase:

- The offset with the highest score becomes the current prefetch offset.
- If the best score is not greater than `PREF_BOP_BAD_SCORE`, prefetching is
  disabled.
- Learning state is reset for the next phase.

Learning continues even when prefetching is disabled, so BOP can re-enable
prefetching if the workload behavior changes.

## Prefetch Issue Policy

This implementation is degree-1:

```text
one eligible access -> at most one prefetch
```

For line `X` and current best offset `D`, BOP issues:

```text
X + D
```

It does not issue `X + 2D`, `X + 3D`, or multiple top-scoring offsets. This
matches the standard BOP design in the paper and avoids unnecessary bandwidth
pressure and cache pollution.

## Cache-Level Integration

BOP is wired into both existing Scarab prefetch framework levels:

- `UL1`
  - Uses `pref_bop_ul1_miss()` and `pref_bop_ul1_prefhit()`.
  - Issues through `pref_addto_ul1req_queue_set()`.
  - Updates RR after data prefetch fill through dcache/L1 fill paths.

- `UMLC`
  - Uses `pref_bop_umlc_miss()` and `pref_bop_umlc_prefhit()`.
  - Issues through `pref_addto_umlc_req_queue()`.
  - Updates RR after MLC fill.

The active path depends on Scarab runtime parameters such as `PREF_UL1_ON` and
`PREF_UMLC_ON`.

## Parameters

BOP is controlled through `pref_bop.param.def`.

Important parameters:

| Parameter | Default | Meaning |
| --- | ---: | --- |
| `pref_bop_on` | `FALSE` | Enables BOP. |
| `debug_pref_bop` | `FALSE` | Enables BOP debug logging. |
| `pref_bop_rr_entries` | `256` | Number of direct-mapped RR entries. |
| `pref_bop_rr_tag_bits` | `0` | RR tag width; `0` means full line address tag. |
| `pref_bop_inflight_entries` | `1024` | Number of inflight prefetch tracking entries. |
| `pref_bop_score_max` | `31` | Maximum score before ending a phase. |
| `pref_bop_round_max` | `100` | Maximum rounds per learning phase. |
| `pref_bop_bad_score` | `1` | Threshold for disabling prefetching. |
| `pref_bop_max_offset` | `256` | Maximum candidate offset. |
| `pref_bop_small_prime_offsets` | `TRUE` | Use offsets with only 2/3/5 prime factors. |
| `pref_bop_start_offset` | `1` | Initial offset before learning converges. |
| `pref_bop_page_bytes` | `4096` | Page boundary used to suppress cross-page prefetches. |
| `pref_bop_prefetch_on_miss` | `TRUE` | Issue prefetches on demand misses. |
| `pref_bop_prefetch_on_prefhit` | `TRUE` | Issue prefetches on useful prefetch hits. |

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

The implementation intentionally updates the RR table on prefetch fill, not on
prefetch enqueue or send.

This matters because:

- Enqueue only means the prefetch was placed into a software/model queue.
- Send only means the request left the queue and entered the memory path.
- Fill means the line actually returned and was inserted into the cache.

The BOP paper's RR semantics depend on completed prefetches, because the goal is
to identify offsets that would have produced timely prefetches. Fill-time RR
updates are therefore the closest match to the intended hardware behavior.

## Current Limitations

- No BOP-specific stats have been added yet.
  - Existing global prefetch stats still work.
  - For deeper validation, add BOP-specific counters such as RR updates, score
    hits, phase completions, learned offsets, and page-boundary drops.

- The inflight table is direct-mapped.
  - Collisions may lose some base-line mappings.
  - This is acceptable for a first implementation but can be improved with
    associativity if needed.

- The RR table is direct-mapped.
  - This follows the simple implementation described in the paper.

- Negative offsets are not implemented.
  - The paper evaluates only positive offsets for its main design.

- Degree is fixed at one.
  - This matches the standard BOP design.
  - More aggressive variants could add a tunable degree, but that would no
    longer be the baseline BOP behavior.

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
- Verify `best_score <= pref_bop_bad_score` disables prefetch issue while
  learning continues.
- Verify page-boundary checks suppress cross-page prefetches.

Microbenchmark validation:

- Sequential stream:
  - Expected behavior: learn a small positive offset and issue `X + D`.

- Fixed stride stream:
  - Expected behavior: prefer offsets related to the line stride or its
    multiples.

- Random access:
  - Expected behavior: low scores and throttled/off prefetching.

- Page-boundary stream:
  - Expected behavior: no prefetch crossing the configured page boundary.

Experiment validation:

- Compare no-prefetch, existing prefetchers, and BOP.
- Inspect `pref.stat.0.csv`, `memory.stat.0.csv`, IPC, useful prefetches, late
  prefetches, and DRAM traffic.
- Watch for abnormal memory traffic increases, which would indicate overly
  aggressive or incorrect prefetch issue behavior.

## Relevant Commits

- `da75d0e` - Add best-offset prefetcher
- `4ef89d0` - Update BOP RR table on prefetch fill
- `a26a0f8` - Declare BOP hooks before prefetcher table
