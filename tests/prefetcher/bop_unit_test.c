#define NO_ASSERT
#define NO_DEBUG
#define NO_STAT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/globals/global_types.h"
#include "../../src/prefetcher/pref_common.h"

uns NUM_CORES = 1;
uns DCACHE_LINE_SIZE = 64;

Flag PREF_UL1_ON = TRUE;
Flag PREF_UMLC_ON = FALSE;

Flag PREF_BOP_ON = TRUE;
Flag DEBUG_PREF_BOP = FALSE;
uns PREF_BOP_RR_ENTRIES = 64;
uns PREF_BOP_RR_TAG_BITS = 0;
uns PREF_BOP_INFLIGHT_ENTRIES = 64;
uns PREF_BOP_SCORE_MAX = 2;
uns PREF_BOP_ROUND_MAX = 2;
uns PREF_BOP_BAD_SCORE = 1;
uns PREF_BOP_MAX_OFFSET = 4;
Flag PREF_BOP_SMALL_PRIME_OFFSETS = FALSE;
int PREF_BOP_START_OFFSET = 1;
uns64 PREF_BOP_PAGE_BYTES = 256;
Flag PREF_BOP_PREFETCH_ON_MISS = TRUE;
Flag PREF_BOP_PREFETCH_ON_PREFHIT = TRUE;

static uns ul1_queue_adds;
static Addr last_ul1_pref_index;
static uns last_ul1_distance;

Flag pref_addto_ul1req_queue_set(uns8 proc_id, Addr line_index, uns8 prefetcher_id, uns distance, Addr loadPC,
                                 uns32 global_hist, Flag bw) {
  (void)proc_id;
  (void)prefetcher_id;
  (void)loadPC;
  (void)global_hist;
  (void)bw;
  ul1_queue_adds++;
  last_ul1_pref_index = line_index;
  last_ul1_distance = distance;
  return TRUE;
}

Flag pref_addto_umlc_req_queue(uns8 proc_id, Addr line_index, uns8 prefetcher_id) {
  (void)proc_id;
  (void)line_index;
  (void)prefetcher_id;
  return TRUE;
}

#include "../../src/prefetcher/pref_bop.c"

#define CHECK(cond, msg)               \
  do {                                 \
    if (!(cond)) {                     \
      fprintf(stderr, "FAIL: %s\n", msg); \
      return 1;                        \
    }                                  \
  } while (0)

static HWP make_test_hwp(HWP_Info* info) {
  memset(info, 0, sizeof(*info));
  info->id = 7;
  HWP hwp = {0};
  hwp.hwp_info = info;
  return hwp;
}

static Pref_BOP* fresh_bop(void) {
  HWP_Info* info = (HWP_Info*)calloc(1, sizeof(HWP_Info));
  HWP hwp = make_test_hwp(info);
  memset(&bop_prefetchers, 0, sizeof(bop_prefetchers));
  ul1_queue_adds = 0;
  last_ul1_pref_index = 0;
  last_ul1_distance = 0;
  pref_bop_init(&hwp);
  return &bop_prefetchers.bop_hwp_core_ul1[0];
}

static int test_degree_one_issue_and_inflight_insert(void) {
  Pref_BOP* bop = fresh_bop();
  pref_bop_ul1_miss(0, 10 * DCACHE_LINE_SIZE, 0, 0);
  CHECK(ul1_queue_adds == 1, "one eligible access should enqueue exactly one prefetch");
  CHECK(last_ul1_pref_index == 11, "initial offset 1 should prefetch X+1");
  CHECK(last_ul1_distance == 1, "queued distance should equal current offset");
  CHECK(pref_bop_inflight_remove(bop, 11, &(Addr){0}), "issued prefetch should create an inflight pref_line->base_line entry");
  return 0;
}

static int test_rr_updates_only_on_fill(void) {
  Pref_BOP* bop = fresh_bop();
  pref_bop_ul1_miss(0, 10 * DCACHE_LINE_SIZE, 0, 0);
  CHECK(!pref_bop_rr_lookup(bop, 10), "RR must not update when the prefetch is only queued");
  pref_bop_note_prefetch_fill(0, 11 * DCACHE_LINE_SIZE, 7, UL1);
  CHECK(pref_bop_rr_lookup(bop, 10), "RR should update with base_line only after prefetch fill");
  return 0;
}

static int test_rr_hit_increments_score(void) {
  Pref_BOP* bop = fresh_bop();
  pref_bop_rr_insert(bop, 100);
  pref_bop_test_offset(bop, 102);
  CHECK(bop->scores[0] == 0, "offset 1 should not score when X-1 is absent");
  pref_bop_test_offset(bop, 102);
  CHECK(bop->scores[1] == 1, "offset 2 should score when X-2 is present in RR");
  CHECK(bop->best_offset == 2 && bop->best_score == 1, "best offset/score should track the highest scoring candidate");
  return 0;
}

static int test_phase_selects_highest_scoring_offset(void) {
  Pref_BOP* bop = fresh_bop();
  pref_bop_rr_insert(bop, 100);
  pref_bop_test_offset(bop, 102);
  pref_bop_test_offset(bop, 102);
  pref_bop_test_offset(bop, 200);
  pref_bop_test_offset(bop, 200);
  pref_bop_test_offset(bop, 102);
  pref_bop_test_offset(bop, 102);
  pref_bop_test_offset(bop, 200);
  pref_bop_test_offset(bop, 200);
  CHECK(bop->current_offset == 2, "phase should select offset 2 after it reaches score max");
  CHECK(bop->prefetch_enabled, "prefetch should stay enabled when best_score is above bad_score");
  CHECK(bop->best_score == 0 && bop->offset_index == 0 && bop->rounds == 0, "learning state should reset after phase completion");
  return 0;
}

static int test_bad_score_disables_prefetch_but_learning_continues(void) {
  Pref_BOP* bop = fresh_bop();
  for (uns i = 0; i < PREF_BOP_MAX_OFFSET * PREF_BOP_ROUND_MAX; i++)
    pref_bop_test_offset(bop, 1000 + i);
  CHECK(!bop->prefetch_enabled, "prefetch should turn off when best score is not greater than bad_score");
  CHECK(bop->current_offset == 0, "current offset should be zero when throttled off");
  pref_bop_test_offset(bop, 2000);
  CHECK(bop->offset_index == 1, "learning should continue after prefetch is disabled");
  return 0;
}

static int test_page_crossing_drops_prefetch(void) {
  fresh_bop();
  pref_bop_ul1_miss(0, 3 * DCACHE_LINE_SIZE, 0, 0);
  CHECK(ul1_queue_adds == 0, "prefetch crossing the configured page boundary should be dropped");
  return 0;
}

int main(void) {
  struct {
    const char* name;
    int (*fn)(void);
  } tests[] = {
      {"degree_one_issue_and_inflight_insert", test_degree_one_issue_and_inflight_insert},
      {"rr_updates_only_on_fill", test_rr_updates_only_on_fill},
      {"rr_hit_increments_score", test_rr_hit_increments_score},
      {"phase_selects_highest_scoring_offset", test_phase_selects_highest_scoring_offset},
      {"bad_score_disables_prefetch_but_learning_continues", test_bad_score_disables_prefetch_but_learning_continues},
      {"page_crossing_drops_prefetch", test_page_crossing_drops_prefetch},
  };

  for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
    if (tests[i].fn())
      return 1;
    printf("PASS: %s\n", tests[i].name);
  }

  printf("All BOP unit tests passed.\n");
  return 0;
}
