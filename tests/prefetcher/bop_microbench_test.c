#define NO_ASSERT
#define NO_DEBUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/globals/global_types.h"
#include "../../src/prefetcher/pref_common.h"
#include "../../src/statistics.h"

uns NUM_CORES = 1;
uns DCACHE_LINE_SIZE = 64;

Flag PREF_UL1_ON = TRUE;
Flag PREF_UMLC_ON = FALSE;

Flag PREF_BOP_ON = TRUE;
Flag DEBUG_PREF_BOP = FALSE;
uns PREF_BOP_RR_ENTRIES = 256;
uns PREF_BOP_RR_TAG_BITS = 0;
uns PREF_BOP_INFLIGHT_ENTRIES = 256;
uns PREF_BOP_SCORE_MAX = 8;
uns PREF_BOP_ROUND_MAX = 8;
uns PREF_BOP_BAD_SCORE = 1;
uns PREF_BOP_MAX_OFFSET = 4;
Flag PREF_BOP_SMALL_PRIME_OFFSETS = FALSE;
int PREF_BOP_START_OFFSET = 1;
uns64 PREF_BOP_PAGE_BYTES = 0;
Flag PREF_BOP_PREFETCH_ON_MISS = TRUE;
Flag PREF_BOP_PREFETCH_ON_PREFHIT = TRUE;

static Stat core0_stats[NUM_GLOBAL_STATS];
static Stat* stat_array[1] = {core0_stats};
Stat** global_stat_array = stat_array;

static Addr pending_pref_lines[4096];
static uns pending_pref_count;

Flag pref_addto_ul1req_queue_set(uns8 proc_id, Addr line_index, uns8 prefetcher_id, uns distance, Addr loadPC,
                                 uns32 global_hist, Flag bw) {
  (void)proc_id;
  (void)prefetcher_id;
  (void)distance;
  (void)loadPC;
  (void)global_hist;
  (void)bw;

  if (pending_pref_count >= sizeof(pending_pref_lines) / sizeof(pending_pref_lines[0])) {
    fprintf(stderr, "FAIL: pending prefetch queue overflow\n");
    exit(1);
  }
  pending_pref_lines[pending_pref_count++] = line_index;
  return TRUE;
}

Flag pref_addto_umlc_req_queue(uns8 proc_id, Addr line_index, uns8 prefetcher_id) {
  (void)proc_id;
  (void)line_index;
  (void)prefetcher_id;
  return TRUE;
}

#include "../../src/prefetcher/pref_bop.c"

#define CHECK(cond, msg)                  \
  do {                                    \
    if (!(cond)) {                        \
      fprintf(stderr, "FAIL: %s\n", msg); \
      return 1;                           \
    }                                     \
  } while (0)

static Counter stat_count(Stat_Enum stat) {
  return core0_stats[stat].count;
}

static void reset_bop_params(void) {
  PREF_BOP_RR_ENTRIES = 256;
  PREF_BOP_RR_TAG_BITS = 0;
  PREF_BOP_INFLIGHT_ENTRIES = 256;
  PREF_BOP_SCORE_MAX = 8;
  PREF_BOP_ROUND_MAX = 8;
  PREF_BOP_BAD_SCORE = 1;
  PREF_BOP_MAX_OFFSET = 4;
  PREF_BOP_SMALL_PRIME_OFFSETS = FALSE;
  PREF_BOP_START_OFFSET = 1;
  PREF_BOP_PAGE_BYTES = 0;
  PREF_BOP_PREFETCH_ON_MISS = TRUE;
  PREF_BOP_PREFETCH_ON_PREFHIT = TRUE;
}

static void fresh_bop(void) {
  static HWP_Info info;
  static HWP hwp;

  memset(core0_stats, 0, sizeof(core0_stats));
  memset(&info, 0, sizeof(info));
  memset(&hwp, 0, sizeof(hwp));
  memset(&bop_prefetchers, 0, sizeof(bop_prefetchers));
  pending_pref_count = 0;

  info.id = 7;
  hwp.hwp_info = &info;
  pref_bop_init(&hwp);
}

static void drain_prefetch_fills(void) {
  for (uns i = 0; i < pending_pref_count; i++)
    pref_bop_note_prefetch_fill(0, pending_pref_lines[i] * DCACHE_LINE_SIZE, 7, UL1);
  pending_pref_count = 0;
}

static void touch_line(Addr line_index, Flag fill_prefetches) {
  pref_bop_ul1_miss(0, line_index * DCACHE_LINE_SIZE, 0, 0);
  if (fill_prefetches)
    drain_prefetch_fills();
}

static int test_sequential_stream(void) {
  reset_bop_params();
  fresh_bop();

  for (Addr line = 1024; line < 1024 + 512; line++)
    touch_line(line, TRUE);

  CHECK(stat_count(BOP_PREF_ISSUED) > 0, "sequential stream should issue prefetches");
  CHECK(stat_count(BOP_RR_FILL_UPDATES) > 0, "sequential stream should update RR on prefetch fill");
  CHECK(stat_count(BOP_RR_HIT_SCORE_INC) > 0, "sequential stream should get RR score hits");
  CHECK(stat_count(BOP_PHASE_PREF_ON) > 0, "sequential stream should eventually select an ON phase");

  printf("sequential: issued=%llu rr_fill=%llu rr_hit=%llu phase_on=%llu phase_off=%llu\n",
         stat_count(BOP_PREF_ISSUED), stat_count(BOP_RR_FILL_UPDATES), stat_count(BOP_RR_HIT_SCORE_INC),
         stat_count(BOP_PHASE_PREF_ON), stat_count(BOP_PHASE_PREF_OFF));
  return 0;
}

static int test_randomish_stream(void) {
  reset_bop_params();
  PREF_BOP_SCORE_MAX = 16;
  PREF_BOP_ROUND_MAX = 4;
  fresh_bop();

  Addr state = 0x12345;
  for (uns i = 0; i < 512; i++) {
    state = (state * 1103515245 + 12345) & 0x7fffffff;
    touch_line(4096 + ((state >> 6) * 17), TRUE);
  }

  CHECK(stat_count(BOP_PHASE_PREF_OFF) > 0, "random-ish stream should complete at least one OFF phase");
  CHECK(stat_count(BOP_PHASE_PREF_ON) == 0, "random-ish stream should not select an ON phase with this pattern");
  CHECK(stat_count(BOP_RR_HIT_SCORE_INC) <= stat_count(BOP_PHASE_COMPLETE),
        "random-ish stream should have very low score-hit density");
  CHECK(stat_count(BOP_PREF_ISSUED) <= PREF_BOP_MAX_OFFSET * PREF_BOP_ROUND_MAX,
        "random-ish stream should stop issuing after the first low-score phase");

  printf("randomish: issued=%llu rr_fill=%llu rr_hit=%llu phase_on=%llu phase_off=%llu\n",
         stat_count(BOP_PREF_ISSUED), stat_count(BOP_RR_FILL_UPDATES), stat_count(BOP_RR_HIT_SCORE_INC),
         stat_count(BOP_PHASE_PREF_ON), stat_count(BOP_PHASE_PREF_OFF));
  return 0;
}

static int test_page_boundary(void) {
  reset_bop_params();
  PREF_BOP_PAGE_BYTES = 4 * 64;
  fresh_bop();

  for (Addr line = 3; line < 3 + 4 * 32; line += 4)
    touch_line(line, TRUE);

  CHECK(stat_count(BOP_PREF_DROPPED_PAGE) > 0, "page-boundary accesses should drop page-crossing prefetches");
  CHECK(stat_count(BOP_PREF_ISSUED) == 0, "page-boundary accesses should not enqueue dropped prefetches");

  printf("page_boundary: dropped_page=%llu issued=%llu rr_fill=%llu\n",
         stat_count(BOP_PREF_DROPPED_PAGE), stat_count(BOP_PREF_ISSUED), stat_count(BOP_RR_FILL_UPDATES));
  return 0;
}

int main(void) {
  struct {
    const char* name;
    int (*fn)(void);
  } tests[] = {
      {"sequential_stream", test_sequential_stream},
      {"randomish_stream", test_randomish_stream},
      {"page_boundary", test_page_boundary},
  };

  for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    if (tests[i].fn())
      return 1;
    printf("PASS: %s\n", tests[i].name);
  }

  printf("All BOP microbench tests passed.\n");
  return 0;
}
