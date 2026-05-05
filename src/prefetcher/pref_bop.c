#include "prefetcher/pref_bop.h"

#include <stdlib.h>
#include <string.h>

#include "globals/assert.h"
#include "globals/global_defs.h"
#include "globals/global_types.h"
#include "globals/global_vars.h"

#include "debug/debug.param.h"
#include "debug/debug_macros.h"

#include "core.param.h"
#include "memory/memory.param.h"
#include "prefetcher/pref.param.h"
#include "prefetcher/pref_bop.param.h"

#define DEBUG(proc_id, args...) _DEBUG(proc_id, DEBUG_PREF_BOP, ##args)

BOP_Prefetchers bop_prefetchers;

static uns8 bop_hwp_id;

static void pref_bop_init_core(HWP* hwp, Pref_BOP* bop_hwp_core, CacheLevel type);
static void pref_bop_access(Pref_BOP* bop_hwp, uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist,
                            Flag is_prefhit);
static Flag pref_bop_same_page(Addr line_index_a, Addr line_index_b);
static Flag pref_bop_rr_lookup(Pref_BOP* bop_hwp, Addr line_index);
static void pref_bop_rr_insert(Pref_BOP* bop_hwp, Addr line_index);
static void pref_bop_inflight_insert(Pref_BOP* bop_hwp, Addr pref_line, Addr base_line);
static Flag pref_bop_inflight_remove(Pref_BOP* bop_hwp, Addr pref_line, Addr* base_line);
static void pref_bop_test_offset(Pref_BOP* bop_hwp, Addr line_index);
static void pref_bop_finish_learning_phase(Pref_BOP* bop_hwp);
static uns pref_bop_build_offset_list(Pref_BOP* bop_hwp);
static Flag pref_bop_is_small_prime_offset(uns offset);
static Addr pref_bop_rr_tag(Addr line_index);
static uns pref_bop_rr_index(Addr line_index, uns entries);
static uns pref_bop_inflight_index(Addr line_index, uns entries);

void pref_bop_init(HWP* hwp) {
  if (!PREF_BOP_ON)
    return;

  ASSERTM(0, PREF_BOP_RR_ENTRIES > 0, "BOP RR table must have at least one entry\n");
  ASSERTM(0, PREF_BOP_INFLIGHT_ENTRIES > 0, "BOP inflight table must have at least one entry\n");
  ASSERTM(0, PREF_BOP_SCORE_MAX > 0, "BOP score max must be greater than zero\n");
  ASSERTM(0, PREF_BOP_ROUND_MAX > 0, "BOP round max must be greater than zero\n");
  ASSERTM(0, PREF_BOP_MAX_OFFSET > 0, "BOP max offset must be greater than zero\n");
  ASSERTM(0, PREF_BOP_PAGE_BYTES == 0 || PREF_BOP_PAGE_BYTES >= DCACHE_LINE_SIZE,
          "BOP page size must be zero or at least one cache line\n");

  hwp->hwp_info->enabled = TRUE;
  bop_hwp_id = hwp->hwp_info->id;

  if (PREF_UL1_ON) {
    bop_prefetchers.bop_hwp_core_ul1 = (Pref_BOP*)calloc(NUM_CORES, sizeof(Pref_BOP));
    pref_bop_init_core(hwp, bop_prefetchers.bop_hwp_core_ul1, UL1);
  }
  if (PREF_UMLC_ON) {
    bop_prefetchers.bop_hwp_core_umlc = (Pref_BOP*)calloc(NUM_CORES, sizeof(Pref_BOP));
    pref_bop_init_core(hwp, bop_prefetchers.bop_hwp_core_umlc, UMLC);
  }
}

static void pref_bop_init_core(HWP* hwp, Pref_BOP* bop_hwp_core, CacheLevel type) {
  for (uns8 proc_id = 0; proc_id < NUM_CORES; proc_id++) {
    Pref_BOP* bop_hwp = &bop_hwp_core[proc_id];
    bop_hwp->hwp_info = hwp->hwp_info;
    bop_hwp->type = type;
    bop_hwp->rr_table = (BOP_RR_Entry*)calloc(PREF_BOP_RR_ENTRIES, sizeof(BOP_RR_Entry));
    bop_hwp->inflight_table = (BOP_Inflight_Entry*)calloc(PREF_BOP_INFLIGHT_ENTRIES, sizeof(BOP_Inflight_Entry));
    bop_hwp->offsets = (int*)calloc(PREF_BOP_MAX_OFFSET, sizeof(int));
    bop_hwp->num_offsets = pref_bop_build_offset_list(bop_hwp);
    ASSERTM(proc_id, bop_hwp->num_offsets > 0, "BOP offset list is empty\n");
    bop_hwp->scores = (uns*)calloc(bop_hwp->num_offsets, sizeof(uns));

    bop_hwp->offset_index = 0;
    bop_hwp->rounds = 0;
    bop_hwp->best_offset = bop_hwp->offsets[0];
    bop_hwp->best_score = 0;
    bop_hwp->current_offset = PREF_BOP_START_OFFSET ? PREF_BOP_START_OFFSET : bop_hwp->offsets[0];
    bop_hwp->prefetch_enabled = TRUE;
  }
}

void pref_bop_ul1_miss(uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist) {
  if (bop_prefetchers.bop_hwp_core_ul1)
    pref_bop_access(&bop_prefetchers.bop_hwp_core_ul1[proc_id], proc_id, lineAddr, loadPC, global_hist, FALSE);
}

void pref_bop_ul1_prefhit(uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist) {
  if (bop_prefetchers.bop_hwp_core_ul1)
    pref_bop_access(&bop_prefetchers.bop_hwp_core_ul1[proc_id], proc_id, lineAddr, loadPC, global_hist, TRUE);
}

void pref_bop_umlc_miss(uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist) {
  if (bop_prefetchers.bop_hwp_core_umlc)
    pref_bop_access(&bop_prefetchers.bop_hwp_core_umlc[proc_id], proc_id, lineAddr, loadPC, global_hist, FALSE);
}

void pref_bop_umlc_prefhit(uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist) {
  if (bop_prefetchers.bop_hwp_core_umlc)
    pref_bop_access(&bop_prefetchers.bop_hwp_core_umlc[proc_id], proc_id, lineAddr, loadPC, global_hist, TRUE);
}

static void pref_bop_access(Pref_BOP* bop_hwp, uns8 proc_id, Addr lineAddr, Addr loadPC, uns32 global_hist,
                            Flag is_prefhit) {
  Addr line_index = lineAddr >> LOG2(DCACHE_LINE_SIZE);

  pref_bop_test_offset(bop_hwp, line_index);

  if ((is_prefhit && !PREF_BOP_PREFETCH_ON_PREFHIT) || (!is_prefhit && !PREF_BOP_PREFETCH_ON_MISS))
    return;
  if (!bop_hwp->prefetch_enabled || bop_hwp->current_offset == 0)
    return;

  Addr pref_index = line_index + bop_hwp->current_offset;
  if (!pref_bop_same_page(line_index, pref_index))
    return;

  Flag added = FALSE;
  if (bop_hwp->type == UMLC) {
    added = pref_addto_umlc_req_queue(proc_id, pref_index, bop_hwp->hwp_info->id);
    if (added)
      pref_bop_rr_insert(bop_hwp, line_index);
  } else {
    added = pref_addto_ul1req_queue_set(proc_id, pref_index, bop_hwp->hwp_info->id, bop_hwp->current_offset, loadPC,
                                        global_hist, FALSE);
    if (added)
      pref_bop_inflight_insert(bop_hwp, pref_index, line_index);
  }

  DEBUG(proc_id, "line:%llx pref:%llx off:%d enabled:%d added:%d\n", line_index, pref_index, bop_hwp->current_offset,
        bop_hwp->prefetch_enabled, added);
}

void pref_bop_note_prefetch_sent(uns8 proc_id, Addr lineAddr, uns8 prefetcher_id) {
  if (!PREF_BOP_ON || prefetcher_id != bop_hwp_id || !bop_prefetchers.bop_hwp_core_ul1)
    return;

  Pref_BOP* bop_hwp = &bop_prefetchers.bop_hwp_core_ul1[proc_id];
  Addr pref_line = lineAddr >> LOG2(DCACHE_LINE_SIZE);
  Addr base_line = 0;

  if (pref_bop_inflight_remove(bop_hwp, pref_line, &base_line))
    pref_bop_rr_insert(bop_hwp, base_line);
}

static void pref_bop_test_offset(Pref_BOP* bop_hwp, Addr line_index) {
  int offset = bop_hwp->offsets[bop_hwp->offset_index];
  if (offset > 0 && line_index >= (Addr)offset && pref_bop_same_page(line_index - offset, line_index) &&
      pref_bop_rr_lookup(bop_hwp, line_index - offset)) {
    if (bop_hwp->scores[bop_hwp->offset_index] < PREF_BOP_SCORE_MAX)
      bop_hwp->scores[bop_hwp->offset_index]++;

    if (bop_hwp->scores[bop_hwp->offset_index] > bop_hwp->best_score) {
      bop_hwp->best_score = bop_hwp->scores[bop_hwp->offset_index];
      bop_hwp->best_offset = offset;
    }
  }

  bop_hwp->offset_index++;
  if (bop_hwp->offset_index == bop_hwp->num_offsets) {
    bop_hwp->offset_index = 0;
    bop_hwp->rounds++;
    if (bop_hwp->best_score >= PREF_BOP_SCORE_MAX || bop_hwp->rounds >= PREF_BOP_ROUND_MAX)
      pref_bop_finish_learning_phase(bop_hwp);
  }
}

static void pref_bop_finish_learning_phase(Pref_BOP* bop_hwp) {
  bop_hwp->current_offset = (bop_hwp->best_score > PREF_BOP_BAD_SCORE) ? bop_hwp->best_offset : 0;
  bop_hwp->prefetch_enabled = bop_hwp->current_offset != 0;

  memset(bop_hwp->scores, 0, sizeof(uns) * bop_hwp->num_offsets);
  bop_hwp->rounds = 0;
  bop_hwp->offset_index = 0;
  bop_hwp->best_score = 0;
  bop_hwp->best_offset = bop_hwp->offsets[0];
}

static uns pref_bop_build_offset_list(Pref_BOP* bop_hwp) {
  uns count = 0;
  for (uns offset = 1; offset <= PREF_BOP_MAX_OFFSET; offset++) {
    if (PREF_BOP_SMALL_PRIME_OFFSETS && !pref_bop_is_small_prime_offset(offset))
      continue;
    bop_hwp->offsets[count++] = (int)offset;
  }
  return count;
}

static Flag pref_bop_is_small_prime_offset(uns offset) {
  while (offset % 2 == 0)
    offset /= 2;
  while (offset % 3 == 0)
    offset /= 3;
  while (offset % 5 == 0)
    offset /= 5;
  return offset == 1;
}

static Flag pref_bop_same_page(Addr line_index_a, Addr line_index_b) {
  if (PREF_BOP_PAGE_BYTES == 0)
    return TRUE;

  Addr lines_per_page = PREF_BOP_PAGE_BYTES >> LOG2(DCACHE_LINE_SIZE);
  if (lines_per_page == 0)
    return TRUE;
  return (line_index_a / lines_per_page) == (line_index_b / lines_per_page);
}

static Flag pref_bop_rr_lookup(Pref_BOP* bop_hwp, Addr line_index) {
  uns index = pref_bop_rr_index(line_index, PREF_BOP_RR_ENTRIES);
  return bop_hwp->rr_table[index].valid && bop_hwp->rr_table[index].tag == pref_bop_rr_tag(line_index);
}

static void pref_bop_rr_insert(Pref_BOP* bop_hwp, Addr line_index) {
  uns index = pref_bop_rr_index(line_index, PREF_BOP_RR_ENTRIES);
  bop_hwp->rr_table[index].tag = pref_bop_rr_tag(line_index);
  bop_hwp->rr_table[index].valid = TRUE;
}

static void pref_bop_inflight_insert(Pref_BOP* bop_hwp, Addr pref_line, Addr base_line) {
  uns index = pref_bop_inflight_index(pref_line, PREF_BOP_INFLIGHT_ENTRIES);
  bop_hwp->inflight_table[index].pref_line = pref_line;
  bop_hwp->inflight_table[index].base_line = base_line;
  bop_hwp->inflight_table[index].valid = TRUE;
}

static Flag pref_bop_inflight_remove(Pref_BOP* bop_hwp, Addr pref_line, Addr* base_line) {
  uns index = pref_bop_inflight_index(pref_line, PREF_BOP_INFLIGHT_ENTRIES);
  if (!bop_hwp->inflight_table[index].valid || bop_hwp->inflight_table[index].pref_line != pref_line)
    return FALSE;

  *base_line = bop_hwp->inflight_table[index].base_line;
  bop_hwp->inflight_table[index].valid = FALSE;
  return TRUE;
}

static Addr pref_bop_rr_tag(Addr line_index) {
  if (PREF_BOP_RR_TAG_BITS == 0 || PREF_BOP_RR_TAG_BITS >= sizeof(Addr) * 8)
    return line_index;
  return line_index & ((((Addr)1) << PREF_BOP_RR_TAG_BITS) - 1);
}

static uns pref_bop_rr_index(Addr line_index, uns entries) {
  Addr hash = line_index ^ (line_index >> 6) ^ (line_index >> 13);
  return hash % entries;
}

static uns pref_bop_inflight_index(Addr line_index, uns entries) {
  Addr hash = line_index ^ (line_index >> 7);
  return hash % entries;
}
