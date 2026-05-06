Here is the complete summary of findings:

---
1. How prefetchers define and increment stat counters

Stat macros (defined in /Users/shuransun/graduate/cse220/labs/scarab-check/src/statistics.h, lines 96-135):

- STAT_EVENT(proc_id, stat) -- increments global_stat_array[proc_id][stat].count by 1
- STAT_EVENT_ALL(stat) -- increments count for all cores
- INC_STAT_EVENT(proc_id, stat, inc) -- increments count by inc
- INC_STAT_EVENT_ALL(stat, inc) -- increments count by inc for all cores
- INC_STAT_VALUE(proc_id, stat, inc) -- increments the .value field (for ratio-type stats)
- GET_STAT_EVENT(proc_id, stat) -- reads the current count

Usage examples in pref_stream.c (/Users/shuransun/graduate/cse220/labs/scarab-check/src/prefetcher/pref_stream.c):
- STAT_EVENT(0, HIT_TRAIN_STREAM); (line 203)
- STAT_EVENT_ALL(MISS_TRAIN_STREAM); (line 263)
- STAT_EVENT(proc_id, CORE_STREAM_TRAIN_CREATE); (line 404)
- INC_STAT_EVENT(proc_id, CORE_CUM_STREAM_LENGTH_0 + MIN2(len/10, 10), len); (line 426)

Usage in pref_common.c (/Users/shuransun/graduate/cse220/labs/scarab-check/src/prefetcher/pref_common.c):
- STAT_EVENT(0, PREF_DL0REQ_QUEUE_HIT_BY_DEMAND); (line 467)
- STAT_EVENT_ALL(PREF_DL0REQ_QUEUE_FULL); (line 534)

pref_stride.c has NO STAT_EVENT calls (empty result).

pref_bop.c also has NO STAT_EVENT calls currently.

---
2. Stat declaration/registration mechanism

Stats are declared using DEF_STAT in .stat.def files with the format:

DEF_STAT( Name, Type, Ratio )

Type can be: COUNT, PER_CYCLE, PER_INST, DIST, RATIO.

Ratio is typically NO_RATIO unless it's a RATIO type.

The .stat.def files are centrally included from:
- /Users/shuransun/graduate/cse220/labs/scarab-check/src/stat_files.def

This file includes:
- "prefetcher/stream.stat.def"
- "prefetcher/l2l1pref.stat.def"
- "prefetcher/pref.stat.def"
- (plus fetch.stat.def, bp/bp.stat.def, memory/memory.stat.def, core.stat.def, inst.stat.def, power/power.stat.def)

Existing prefetcher .stat.def files:
- /Users/shuransun/graduate/cse220/labs/scarab-check/src/prefetcher/pref.stat.def
- /Users/shuransun/graduate/cse220/labs/scarab-check/src/prefetcher/stream.stat.def
- /Users/shuransun/graduate/cse220/labs/scarab-check/src/prefetcher/l2l1pref.stat.def

There is NO pref_bop.stat.def file. To add BOP-specific stats, you would need to:
1. Create a pref_bop.stat.def file with DEF_STAT(...) entries
2. Add #include "prefetcher/pref_bop.stat.def" to stat_files.def
3. Use STAT_EVENT(proc_id, YOUR_STAT_NAME) in pref_bop.c

---
3. BOP param files

Both exist:
- /Users/shuransun/graduate/cse220/labs/scarab-check/src/prefetcher/pref_bop.param.def -- defines 13 parameters including PREF_BOP_ON, DEBUG_PREF_BOP,
PREF_BOP_RR_ENTRIES, PREF_BOP_SCORE_MAX, PREF_BOP_ROUND_MAX, PREF_BOP_MAX_OFFSET, etc.
- /Users/shuransun/graduate/cse220/labs/scarab-check/src/prefetcher/pref_bop.param.h -- the header wrapper that externs the parameters via the X-macro
pattern.

---
4. DEBUG_PREF_BOP

DEBUG_PREF_BOP is already fully defined and wired up:

- Declared in /Users/shuransun/graduate/cse220/labs/scarab-check/src/prefetcher/pref_bop.param.def (line 4):
DEF_PARAM(debug_pref_bop, DEBUG_PREF_BOP, Flag, Flag, FALSE, )
- Used in /Users/shuransun/graduate/cse220/labs/scarab-check/src/prefetcher/pref_bop.c (line 19):
#define DEBUG(proc_id, args...) _DEBUG(proc_id, DEBUG_PREF_BOP, ##args)

It does NOT need to be added to debug.param.def -- it lives in the BOP-specific param file, which is the same pattern other prefetchers can use. Note
that the generic DEBUG_PREF flag is separately in /Users/shuransun/graduate/cse220/labs/scarab-check/src/debug/debug.param.def (line 126), but
DEBUG_PREF_BOP is BOP-specific and already properly set up. It defaults to FALSE and can be enabled at runtime.


